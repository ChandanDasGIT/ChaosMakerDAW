#include "denoisewrapper.h"
#include "plugins/denoise/denoise.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr int CurvePoints = 160;
constexpr double FallDbPerSecond = 60.0;
const QString DenoiseType = QStringLiteral("denoise");

QVariantList toList(const std::vector<double>& values)
{
    QVariantList list;
    list.reserve(qsizetype(values.size()));
    for (const double v : values) list.append(v);
    return list;
}
}

DenoiseWrapper::DenoiseWrapper(QObject* parent) : QObject(parent)
{
    for (int i = 0; i < CurvePoints; ++i)
        m_frequencies.push_back(20.0 * std::pow(1000.0, double(i) / double(CurvePoints - 1)));   // 20 Hz .. 20 kHz
    m_timer.setInterval(33);
    connect(&m_timer, &QTimer::timeout, this, [this] { pollAnalyzer(m_timer.interval() / 1000.0); });
}

DenoiseWrapper::~DenoiseWrapper() { listen(false); }

QObject* DenoiseWrapper::trackModel() const { return m_model; }

void DenoiseWrapper::setTrackModel(QObject* model)
{
    auto* tracks = qobject_cast<TrackListWrapper*>(model);
    if (tracks == m_model) return;
    if (m_changeConnection) disconnect(m_changeConnection);
    m_model = tracks;
    if (m_model)
        m_changeConnection = connect(m_model, &TrackListWrapper::mixEffectChanged, this, [this](int id, const QString& type) {
            if (id == m_effectsId && type == DenoiseType) emit profileChanged();
        });
    emit trackModelChanged();
    emit profileChanged();
}

void DenoiseWrapper::setEffectsId(int id)
{
    if (id == m_effectsId) return;
    listen(false);
    m_effectsId = id;
    clearSpectra();
    if (!m_message.isEmpty()) { m_message.clear(); emit messageChanged(); }
    if (m_analyzerEnabled) listen(true);
    emit effectsIdChanged();
    emit profileChanged();
}

void DenoiseWrapper::setEngine(QObject* engine)
{
    if (engine == m_engine) return;
    listen(false);
    m_engine = engine;
    if (m_analyzerEnabled) listen(true);
    emit engineChanged();
}

void DenoiseWrapper::setAnalyzerEnabled(bool on)
{
    if (on == m_analyzerEnabled) return;
    m_analyzerEnabled = on;
    listen(on);
    if (on) m_timer.start();
    else { m_timer.stop(); clearSpectra(); }
    emit analyzerEnabledChanged();
}

QVariantList DenoiseWrapper::frequencies() const { return toList(m_frequencies); }

QVariantList DenoiseWrapper::profile() const
{
    if (!m_model) return {};
    const auto settings = m_model->mixEffect(m_effectsId, mixfx::Denoise);
    if (!settings || settings->profile.empty()) return {};
    return toList(Denoise::curveDb(settings->profile, m_frequencies, sampleRate()));
}

bool DenoiseWrapper::hasProfile() const
{
    if (!m_model) return false;
    const auto settings = m_model->mixEffect(m_effectsId, mixfx::Denoise);
    return settings && !settings->profile.empty();
}

bool DenoiseWrapper::learn(double startBeat, double endBeat)
{
    if (!m_model) return false;
    m_message = m_model->learnNoise(m_effectsId, startBeat, endBeat);
    emit messageChanged();
    emit profileChanged();
    return m_message.isEmpty();
}

bool DenoiseWrapper::forget()
{
    if (!m_model || !m_model->forgetNoise(m_effectsId)) return false;
    emit profileChanged();
    return true;
}

double DenoiseWrapper::sampleRate() const
{
    const double rate = m_model ? m_model->domain().project().tempo().sampleRate() : 48000.0;
    return rate > 1000.0 ? rate : 48000.0;
}

std::vector<std::shared_ptr<Denoise>> DenoiseWrapper::processors() const
{
    std::vector<std::shared_ptr<Denoise>> result;
    auto* engine = qobject_cast<EngineController*>(m_engine);
    if (!engine) return result;
    for (const bool playback : {false, true})
        if (auto denoise = std::dynamic_pointer_cast<Denoise>(engine->mixEffectProcessor(m_effectsId, DenoiseType, playback)))
            result.push_back(std::move(denoise));
    return result;
}

void DenoiseWrapper::listen(bool on)
{
    for (const auto& weak : m_listening)
        if (const auto processor = weak.lock()) processor->setAnalyzerListening(false);
    m_listening.clear();
    if (!on) return;
    for (const auto& processor : processors()) {
        processor->setAnalyzerListening(true);
        m_listening.push_back(processor);
    }
}

void DenoiseWrapper::clearSpectra()
{
    m_in = {};
    m_out = {};
    const bool had = !m_input.isEmpty() || !m_output.isEmpty();
    m_input.clear();
    m_output.clear();
    if (had) emit spectrumChanged();
}

void DenoiseWrapper::pollAnalyzer(double elapsedSeconds)
{
    // The engine replaces processors when the chain or the device changes: follow it.
    const auto current = processors();
    bool same = current.size() == m_listening.size();
    for (std::size_t i = 0; same && i < current.size(); ++i) same = m_listening[i].lock() == current[i];
    if (!same) listen(true);

    std::array<float, 4096> chunk{};
    const auto take = [&](Feed& feed, bool output) {
        for (const auto& processor : current)
            for (std::size_t got; (got = output ? processor->readOutput(chunk) : processor->readInput(chunk)) > 0;) {
                feed.history.insert(feed.history.end(), chunk.begin(), chunk.begin() + std::ptrdiff_t(got));
                feed.heard = true;
            }
        if (feed.history.size() > std::size_t(Denoise::Size))
            feed.history.erase(feed.history.begin(), feed.history.end() - Denoise::Size);
    };
    take(m_in, false);
    take(m_out, true);
    const auto input = update(m_in, elapsedSeconds), output = update(m_out, elapsedSeconds);
    if (input == m_input && output == m_output) return;
    m_input = input;
    m_output = output;
    emit spectrumChanged();
}

QVariantList DenoiseWrapper::update(Feed& feed, double elapsedSeconds)
{
    if (!feed.heard || feed.history.size() < std::size_t(Denoise::Size)) return {};
    const auto levels = Denoise::learnProfile(feed.history);
    const auto now = Denoise::curveDb(levels, m_frequencies, sampleRate());
    if (feed.shown.size() != now.size()) feed.shown = now;
    const double fall = FallDbPerSecond * std::max(0.0, elapsedSeconds);
    for (std::size_t i = 0; i < now.size(); ++i)
        feed.shown[i] = std::max(now[i], feed.shown[i] - fall);   // rises at once, falls slowly
    return toList(feed.shown);
}
