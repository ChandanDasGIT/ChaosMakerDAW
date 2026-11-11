#include "pitchmonitorwrapper.h"
#include "dsp/pitchtracker.h"
#include "midi/musickey.h"
#include "midi/notenames.h"
#include "plugins/pitchmonitor/pitchanalysis.h"
#include "plugins/pitchmonitor/pitchmonitor.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

namespace {
const QString PitchMonitorType = QStringLiteral("pitchmonitor");
}

PitchMonitorWrapper::PitchMonitorWrapper(QObject* parent) : QObject(parent)
{
    m_pollTimer.setInterval(33);
    connect(&m_pollTimer, &QTimer::timeout, this, &PitchMonitorWrapper::poll);
    m_analysisTimer.setSingleShot(true);
    m_analysisTimer.setInterval(250);                      // a drag sends many changes: wait for the last
    connect(&m_analysisTimer, &QTimer::timeout, this, &PitchMonitorWrapper::startAnalysis);
}

PitchMonitorWrapper::~PitchMonitorWrapper()
{
    listen(false);
    if (m_cancel) m_cancel->store(true);
}

QObject* PitchMonitorWrapper::trackModel() const { return m_model; }

void PitchMonitorWrapper::setTrackModel(QObject* model)
{
    auto* tracks = qobject_cast<TrackListWrapper*>(model);
    if (tracks == m_model) return;
    for (const auto& c : m_connections) disconnect(c);
    m_connections.clear();
    m_model = tracks;
    if (m_model) {
        m_connections.push_back(connect(m_model, &TrackListWrapper::clipsChanged, this, &PitchMonitorWrapper::scheduleAnalysis));
        m_connections.push_back(connect(m_model, &TrackListWrapper::projectChanged, this, &PitchMonitorWrapper::scheduleAnalysis));
        m_connections.push_back(connect(m_model, &TrackListWrapper::mixEffectChanged, this, [this](int id, const QString& type) {
            if (id != m_effectsId || type != PitchMonitorType) return;
            emit keyChanged();
            emit curveChanged();                           // the cents follow the key
            setReading(m_hasPitch, m_pitch);
        }));
    }
    m_dirty = true;
    scheduleAnalysis();
    emit trackModelChanged();
    emit keyChanged();
}

void PitchMonitorWrapper::setEffectsId(int id)
{
    if (id == m_effectsId) return;
    listen(false);
    m_effectsId = id;
    m_points.clear();
    m_lowest = 1; m_highest = 0;
    clearLive();
    setReading(false, 0.0);
    if (m_active) listen(true);
    m_dirty = true;
    scheduleAnalysis();
    emit effectsIdChanged();
    emit keyChanged();
    emit curveChanged();
}

void PitchMonitorWrapper::setEngine(QObject* engine)
{
    if (engine == m_engine) return;
    listen(false);
    m_engine = engine;
    if (m_active) listen(true);
    emit engineChanged();
}

void PitchMonitorWrapper::setActive(bool on)
{
    if (on == m_active) return;
    m_active = on;
    listen(on);
    if (on) {
        m_pollTimer.start();
        if (m_dirty) startAnalysis();
    } else {
        m_pollTimer.stop();
        setReading(false, 0.0);
    }
    emit activeChanged();
}

int PitchMonitorWrapper::keyTonic() const
{
    const auto settings = m_model ? m_model->mixEffect(m_effectsId, mixfx::PitchMonitor) : std::nullopt;
    const auto* spec = mixfx::find(mixfx::PitchMonitor);
    return settings && spec ? int(mixfx::value(*spec, *settings, "key")) : 0;
}

int PitchMonitorWrapper::scale() const
{
    const auto settings = m_model ? m_model->mixEffect(m_effectsId, mixfx::PitchMonitor) : std::nullopt;
    const auto* spec = mixfx::find(mixfx::PitchMonitor);
    return settings && spec ? int(mixfx::value(*spec, *settings, "scale")) : int(musickey::Scale::Chromatic);
}

QString PitchMonitorWrapper::keyName() const
{
    return QString::fromStdString(musickey::keyName(keyTonic(), musickey::Scale(scale())));
}

QStringList PitchMonitorWrapper::tonicNames() const
{
    QStringList names;
    for (const auto n : musickey::tonicNames()) names.append(QString::fromUtf8(n.data(), qsizetype(n.size())));
    return names;
}

QStringList PitchMonitorWrapper::scaleNames() const
{
    QStringList names;
    for (const auto n : musickey::scaleNames()) names.append(QString::fromUtf8(n.data(), qsizetype(n.size())));
    return names;
}

QString PitchMonitorWrapper::noteName() const { return m_note >= 0 ? nameOf(m_note) : QString(); }

bool PitchMonitorWrapper::inKey(int note) const { return musickey::inKey(note, keyTonic(), musickey::Scale(scale())); }

QString PitchMonitorWrapper::nameOf(int note) const { return QString::fromStdString(notenames::name(note)); }

QVariantList PitchMonitorWrapper::toList(const std::vector<Point>& points, double fromBeat, double toBeat, int maxPoints) const
{
    QVariantList out;
    if (!m_model || points.empty() || !(toBeat > fromBeat)) return out;
    const auto& tempo = m_model->domain().project().tempo();
    const std::int64_t from = tempo.sampleAt(std::max(0.0, fromBeat)), to = tempo.sampleAt(toBeat);
    auto first = std::lower_bound(points.begin(), points.end(), from, [](const Point& p, std::int64_t s) { return p.sample < s; });
    if (first != points.begin()) --first;                  // the line enters the view from the left
    auto last = std::upper_bound(first, points.end(), to, [](std::int64_t s, const Point& p) { return s < p.sample; });
    if (last != points.end()) ++last;
    const auto count = std::distance(first, last);
    const auto step = std::max<std::ptrdiff_t>(1, count / std::max(1, maxPoints));
    const int tonic = keyTonic();
    const auto key = musickey::Scale(scale());
    out.reserve(qsizetype(count / step + 8) * 3);
    for (auto it = first; it < last; it += 1) {
        const bool gap = it->midi <= 0.0f;
        // Thinned lines keep every gap, so they still break where the voice stops.
        if (!gap && step > 1 && std::distance(first, it) % step != 0) continue;
        out.append(tempo.beatAt(it->sample));
        out.append(double(it->midi));
        out.append(gap ? 0.0 : musickey::nearest(it->midi, tonic, key).cents);
    }
    return out;
}

QVariantList PitchMonitorWrapper::curve(double fromBeat, double toBeat, int maxPoints) const
{
    return toList(m_points, fromBeat, toBeat, maxPoints);
}

QVariantList PitchMonitorWrapper::live(double fromBeat, double toBeat, int maxPoints) const
{
    return toList(m_live, fromBeat, toBeat, maxPoints);
}

std::vector<std::shared_ptr<PitchMonitor>> PitchMonitorWrapper::processors(bool playback) const
{
    std::vector<std::shared_ptr<PitchMonitor>> result;
    auto* engine = qobject_cast<EngineController*>(m_engine);
    if (!engine) return result;
    if (auto fx = std::dynamic_pointer_cast<PitchMonitor>(engine->mixEffectProcessor(m_effectsId, PitchMonitorType, playback)))
        result.push_back(std::move(fx));
    return result;
}

void PitchMonitorWrapper::listen(bool on)
{
    for (const auto& weak : m_listening)
        if (const auto fx = weak.lock()) fx->setListening(false);
    m_listening.clear();
    if (!on) return;
    for (const bool playback : {false, true})
        for (const auto& fx : processors(playback)) {
            fx->setListening(true);
            m_listening.push_back(fx);
        }
}

void PitchMonitorWrapper::poll()
{
    // The engine replaces processors when the chain or the device changes: follow it.
    std::vector<std::shared_ptr<PitchMonitor>> current;
    for (const bool playback : {false, true})
        for (auto& fx : processors(playback)) current.push_back(std::move(fx));
    bool same = current.size() == m_listening.size();
    for (std::size_t i = 0; same && i < current.size(); ++i) same = m_listening[i].lock() == current[i];
    if (!same && m_active) listen(true);

    const auto liveBefore = m_live.size();
    const auto live = processors(false);
    double latest = -1.0;
    for (const auto& fx : current) {
        const bool isLive = std::find(live.begin(), live.end(), fx) != live.end();
        PitchMonitor::Reading r;
        while (fx->read(r)) {
            const bool voiced = r.hz > 0.0f;
            if (voiced) {
                latest = pitch::midiOf(r.hz);
                m_sinceVoiced.restart();
            }
            // The live line: the input as it is recorded or monitored while the song runs.
            if (!isLive || !r.playing) continue;
            if (!m_live.empty() && r.songSample < m_live.back().sample - std::int64_t(48000)) clearLive();   // a new pass
            const float midi = voiced ? float(pitch::midiOf(r.hz)) : 0.0f;
            if (midi == 0.0f && (m_live.empty() || m_live.back().midi == 0.0f)) continue;
            if (m_live.size() < MaxLivePoints) m_live.push_back({r.songSample, midi});
        }
    }
    if (latest > 0.0) setReading(true, latest);
    else if (m_hasPitch && (!m_sinceVoiced.isValid() || m_sinceVoiced.elapsed() > HoldMs)) setReading(false, 0.0);
    if (m_live.size() != liveBefore) emit liveChanged();
}

void PitchMonitorWrapper::setReading(bool has, double value)
{
    const auto nearest = has ? musickey::nearest(value, keyTonic(), musickey::Scale(scale())) : musickey::Nearest{};
    const int note = has ? nearest.note : -1;
    const double cents = has ? nearest.cents : 0.0;
    if (has == m_hasPitch && value == m_pitch && note == m_note && cents == m_cents) return;
    m_hasPitch = has;
    m_pitch = has ? value : 0.0;
    m_note = note;
    m_cents = cents;
    emit readingChanged();
}

void PitchMonitorWrapper::clearLive()
{
    if (m_live.empty()) return;
    m_live.clear();
    emit liveChanged();
}

void PitchMonitorWrapper::reanalyse()
{
    m_dirty = true;
    startAnalysis();
}

void PitchMonitorWrapper::scheduleAnalysis()
{
    m_dirty = true;
    if (m_active) m_analysisTimer.start();
}

void PitchMonitorWrapper::startAnalysis()
{
    m_analysisTimer.stop();
    m_dirty = false;
    if (m_cancel) m_cancel->store(true);
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    const auto generation = ++m_generation;
    if (!m_model) {
        m_points.clear();
        m_lowest = 1; m_highest = 0;
        emit curveChanged();
        return;
    }
    auto segments = pitchanalysis::collect(m_model->domain(), m_effectsId);
    const double rate = m_model->domain().project().tempo().sampleRate();
    if (!m_analysing) { m_analysing = true; emit analysingChanged(); }
    auto* watcher = new QFutureWatcher<std::vector<pitchanalysis::Point>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation != m_generation) return;            // a newer analysis is on its way
        const auto points = watcher->result();
        m_points.clear();
        m_points.reserve(points.size());
        m_lowest = 1; m_highest = 0;
        for (const auto& p : points) {
            m_points.push_back({p.sample, p.midi});
            if (p.midi <= 0.0f) continue;
            const int n = int(std::lround(p.midi));
            if (m_lowest > m_highest) m_lowest = m_highest = n;
            m_lowest = std::min(m_lowest, n);
            m_highest = std::max(m_highest, n);
        }
        // The take that was sung live is in the line now (an edit elsewhere leaves the live line).
        if (!m_live.empty()) {
            const auto from = m_live.front().sample, to = m_live.back().sample;
            if (std::any_of(m_points.begin(), m_points.end(), [from, to](const Point& p) {
                    return p.midi > 0.0f && p.sample >= from && p.sample <= to; }))
                clearLive();
        }
        m_analysing = false;
        emit analysingChanged();
        emit curveChanged();
    });
    watcher->setFuture(QtConcurrent::run([segments = std::move(segments), rate, cancel = m_cancel] {
        return pitchanalysis::analyse(segments, rate, cancel.get());
    }));
}
