#include "chaoseqwrapper.h"

#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace {

struct Choice {
    const char* id;
    const char* title;
};

constexpr Choice shapeChoices[] = {
    {"bell", "Bell"},           {"lowShelf", "Low Shelf"}, {"highShelf", "High Shelf"},
    {"lowCut", "Low Cut"},      {"highCut", "High Cut"},   {"notch", "Notch"},
    {"bandPass", "Band Pass"},  {"tiltShelf", "Tilt Shelf"},
};

constexpr Choice placementChoices[] = {
    {"stereo", "Stereo"}, {"left", "Left"}, {"right", "Right"}, {"mid", "Mid"}, {"side", "Side"},
};

QVariantList toList(std::span<const Choice> choices)
{
    QVariantList list;
    for (const auto& c : choices) list.append(QVariantMap{{"id", c.id}, {"title", c.title}});
    return list;
}

QString fromView(std::string_view text)
{
    return QString::fromUtf8(text.data(), qsizetype(text.size()));
}

QVariantList toList(const std::vector<double>& values)
{
    QVariantList list;
    list.reserve(qsizetype(values.size()));
    for (double v : values) list.append(v);
    return list;
}

} // namespace

ChaosEqWrapper::ChaosEqWrapper(QObject* parent)
    : QObject(parent)
{
    m_frequencies.resize(CurvePoints);
    for (int i = 0; i < CurvePoints; ++i) m_frequencies[std::size_t(i)] = chaoseq::curveFrequency(i, CurvePoints);
    m_readBuffer.resize(16384);
    m_pollTimer.setInterval(33);
    connect(&m_pollTimer, &QTimer::timeout, this, [this] {
        const double elapsed = m_sincePoll.isValid() ? m_sincePoll.restart() / 1000.0 : 0.0;
        if (!m_sincePoll.isValid()) m_sincePoll.start();
        pollAnalyzer(elapsed);
    });
    updateCurves();
}

ChaosEqWrapper::~ChaosEqWrapper()
{
    stopListening();
}

QObject* ChaosEqWrapper::trackModel() const
{
    return m_trackModel;
}

void ChaosEqWrapper::setTrackModel(QObject* model)
{
    auto* tracks = qobject_cast<TrackListWrapper*>(model);
    if (tracks == m_trackModel) return;
    if (m_trackModel) disconnect(m_trackModel, nullptr, this, nullptr);
    m_trackModel = tracks;
    if (m_trackModel) {
        connect(m_trackModel, &TrackListWrapper::chaosEqChanged, this, [this](int id) {
            if (id == m_trackId) reload();
        });
        // Loading a project or bringing a track back replaces the settings without chaosEqChanged.
        connect(m_trackModel, &TrackListWrapper::projectChanged, this, &ChaosEqWrapper::reload);
        connect(m_trackModel, &QObject::destroyed, this, &ChaosEqWrapper::reload);
    }
    emit trackModelChanged();
    reload();
}

void ChaosEqWrapper::setTrackId(int id)
{
    if (id == m_trackId) return;
    stopListening();
    m_trackId = id;
    m_analyzer.clear();
    m_spectrum.clear();
    emit trackIdChanged();
    emit spectrumChanged();
    reload();
    updateAnalyzerTimer();
}

void ChaosEqWrapper::setEngine(QObject* engine)
{
    if (engine == m_engine) return;
    stopListening();
    m_engine = engine;
    emit engineChanged();
    updateAnalyzerTimer();
}

void ChaosEqWrapper::setAnalyzerEnabled(bool enabled)
{
    if (enabled == m_analyzerEnabled) return;
    m_analyzerEnabled = enabled;
    if (!enabled) {
        stopListening();
        m_analyzer.clear();
        m_spectrum.clear();
        emit spectrumChanged();
    }
    emit analyzerEnabledChanged();
    updateAnalyzerTimer();
}

bool ChaosEqWrapper::validTrack() const
{
    return m_trackModel && m_trackModel->chaosEq(m_trackId) != nullptr;
}

double ChaosEqWrapper::sampleRate() const
{
    const double rate = m_trackModel ? m_trackModel->project().tempo().sampleRate() : 48000.0;
    return std::isfinite(rate) && rate > 1000.0 ? rate : 48000.0;
}

void ChaosEqWrapper::reload()
{
    const auto* stored = m_trackModel ? m_trackModel->chaosEq(m_trackId) : nullptr;
    m_settings = stored ? *stored : chaoseq::Settings{};
    updateCurves();
    emit settingsChanged();
}

bool ChaosEqWrapper::commit(const chaoseq::Settings& settings)
{
    if (!validTrack()) return false;
    // setChaosEq emits chaosEqChanged, which reloads this wrapper (and any other on the track).
    return m_trackModel->setChaosEq(m_trackId, settings);
}

void ChaosEqWrapper::updateCurves()
{
    m_curve = chaoseq::curve(m_settings, sampleRate(), CurvePoints);
    m_analyzer.setSampleRate(sampleRate());
}

QVariantList ChaosEqWrapper::bands() const
{
    QVariantList list;
    for (int slot = 0; slot < chaoseq::MaxBands; ++slot) {
        const auto& band = m_settings.bands[std::size_t(slot)];
        if (!band.inUse) continue;
        list.append(QVariantMap{
            {"slot", slot},
            {"shape", fromView(chaoseq::shapeName(band.filter.shape))},
            {"slope", eqband::dbPerOctave(band.filter.slope)},
            {"frequency", band.filter.frequencyHz},
            {"gain", band.filter.gainDb},
            {"q", band.filter.q},
            {"placement", fromView(chaoseq::placementName(band.placement))},
            {"bypass", band.bypassed},
            {"solo", band.solo},
            {"usesGain", eqband::usesGain(band.filter.shape)},
            {"usesSlope", eqband::usesSlope(band.filter.shape)},
        });
    }
    return list;
}

void ChaosEqWrapper::setOutputGain(double db)
{
    if (!std::isfinite(db)) return;
    auto next = m_settings;
    next.outputGainDb = std::clamp(db, chaoseq::MinOutputDb, chaoseq::MaxOutputDb);
    if (next.outputGainDb != m_settings.outputGainDb) commit(next);
}

void ChaosEqWrapper::setAutoGain(bool enabled)
{
    if (enabled == m_settings.autoGain) return;
    auto next = m_settings;
    next.autoGain = enabled;
    commit(next);
}

QVariantList ChaosEqWrapper::curveFrequencies() const
{
    return toList(m_frequencies);
}

QVariantList ChaosEqWrapper::totalCurve() const
{
    return toList(m_curve.totalDb);
}

QVariantMap ChaosEqWrapper::bandCurves() const
{
    QVariantMap map;
    for (int slot = 0; slot < chaoseq::MaxBands; ++slot) {
        const auto& values = m_curve.bandDb[std::size_t(slot)];
        if (!values.empty()) map.insert(QString::number(slot), toList(values));
    }
    return map;
}

QVariantList ChaosEqWrapper::shapes() const
{
    return toList(shapeChoices);
}

QVariantList ChaosEqWrapper::placements() const
{
    return toList(placementChoices);
}

QVariantList ChaosEqWrapper::slopes() const
{
    return {6, 12, 24, 48};
}

int ChaosEqWrapper::addBand(double frequencyHz, double gainDb)
{
    if (!validTrack() || !std::isfinite(frequencyHz) || !std::isfinite(gainDb)) return -1;
    const int slot = chaoseq::firstFreeBand(m_settings);
    if (slot < 0) return -1;
    auto next = m_settings;
    auto& band = next.bands[std::size_t(slot)];
    band = chaoseq::Band{};
    band.inUse = true;
    band.filter.shape = eqband::Shape::Bell;
    band.filter.frequencyHz = frequencyHz;
    band.filter.gainDb = gainDb;
    band.filter.q = 1.0;
    return commit(next) ? slot : -1;
}

bool ChaosEqWrapper::removeBand(int slot)
{
    if (slot < 0 || slot >= chaoseq::MaxBands || !m_settings.bands[std::size_t(slot)].inUse) return false;
    auto next = m_settings;
    next.bands[std::size_t(slot)] = chaoseq::Band{};
    return commit(next);
}

bool ChaosEqWrapper::setBand(int slot, const QVariantMap& changes)
{
    if (slot < 0 || slot >= chaoseq::MaxBands || !m_settings.bands[std::size_t(slot)].inUse) return false;
    auto next = m_settings;
    auto& band = next.bands[std::size_t(slot)];
    const auto number = [&](const char* key, double& out) {
        if (!changes.contains(key)) return true;
        bool ok = false;
        const double value = changes.value(key).toDouble(&ok);
        if (!ok || !std::isfinite(value)) return false;
        out = value;
        return true;
    };
    if (!number("frequency", band.filter.frequencyHz) || !number("gain", band.filter.gainDb)
        || !number("q", band.filter.q))
        return false;
    if (changes.contains("shape")
        && !chaoseq::shapeFromName(changes.value("shape").toString().toStdString(), band.filter.shape))
        return false;
    if (changes.contains("placement")
        && !chaoseq::placementFromName(changes.value("placement").toString().toStdString(), band.placement))
        return false;
    if (changes.contains("slope") && !chaoseq::slopeFromDbPerOctave(changes.value("slope").toInt(), band.filter.slope))
        return false;
    if (changes.contains("bypass")) band.bypassed = changes.value("bypass").toBool();
    if (changes.contains("solo")) {
        band.solo = changes.value("solo").toBool();
        if (band.solo)
            for (int other = 0; other < chaoseq::MaxBands; ++other)
                if (other != slot) next.bands[std::size_t(other)].solo = false;
    }
    return commit(next);
}

void ChaosEqWrapper::removeAllBands()
{
    if (bandCount() == 0) return;
    auto next = m_settings;
    for (auto& band : next.bands) band = chaoseq::Band{};
    commit(next);
}

QString ChaosEqWrapper::formatFrequency(double hz) const
{
    if (!std::isfinite(hz)) return {};
    if (hz >= 1000.0) return QString::number(hz / 1000.0, 'f', hz >= 10000.0 ? 1 : 2) + " kHz";
    return QString::number(hz, 'f', hz >= 100.0 ? 0 : 1) + " Hz";
}

QString ChaosEqWrapper::formatGain(double db) const
{
    if (!std::isfinite(db)) return {};
    const double rounded = std::round(db * 10.0) / 10.0;
    return (rounded > 0.0 ? "+" : "") + QString::number(rounded == 0.0 ? 0.0 : rounded, 'f', 1) + " dB";
}

QString ChaosEqWrapper::formatQ(double q) const
{
    if (!std::isfinite(q)) return {};
    return QString::number(q, 'f', q >= 10.0 ? 1 : 2);
}

QString ChaosEqWrapper::shapeTitle(const QString& id) const
{
    for (const auto& c : shapeChoices)
        if (id == QLatin1String(c.id)) return QString::fromLatin1(c.title);
    return {};
}

void ChaosEqWrapper::updateAnalyzerTimer()
{
    const bool run = m_analyzerEnabled && m_engine && m_trackId >= 0;
    if (run && !m_pollTimer.isActive()) {
        m_sincePoll.invalidate();
        m_pollTimer.start();
    } else if (!run && m_pollTimer.isActive()) {
        m_pollTimer.stop();
    }
}

void ChaosEqWrapper::stopListening()
{
    auto* engine = qobject_cast<EngineController*>(m_engine);
    if (!engine || m_trackId < 0) return;
    for (const auto& processor : engine->chaosEqProcessors(m_trackId))
        if (processor) processor->setAnalyzerListening(false);
}

void ChaosEqWrapper::pollAnalyzer(double elapsedSeconds)
{
    auto* engine = qobject_cast<EngineController*>(m_engine);
    if (!engine || m_trackId < 0) return;
    // Both instances (monitor, playback) feed the display; usually only one has audio.
    // Drain both, show the one with more new samples.
    std::size_t best = 0;
    std::vector<float> chosen;
    for (const auto& processor : engine->chaosEqProcessors(m_trackId)) {
        if (!processor) continue;
        processor->setAnalyzerListening(true);
        const std::size_t got = processor->readAnalyzer(m_readBuffer);
        if (got > best) {
            best = got;
            chosen.assign(m_readBuffer.begin(), m_readBuffer.begin() + std::ptrdiff_t(got));
        }
    }
    if (best > 0) {
        m_analyzer.push(chosen);
        m_hadAudio = true;
    } else if (!m_hadAudio) {
        return;   // nothing ever arrived: keep the spectrum empty
    } else {
        // No new audio (stopped or silent): feed silence so the display falls away.
        const std::size_t quiet = std::size_t(std::max(0.0, elapsedSeconds) * sampleRate());
        m_analyzer.push(std::vector<float>(std::min<std::size_t>(quiet, 16384), 0.0f));
    }
    const auto& levels = m_analyzer.update(m_frequencies, elapsedSeconds);
    const bool allFloor = std::all_of(levels.begin(), levels.end(),
                                      [](double v) { return v <= SpectrumAnalyzer::FloorDb; });
    if (allFloor) {
        m_hadAudio = false;
        if (m_spectrum.isEmpty()) return;
        m_spectrum.clear();
    } else {
        m_spectrum = toList(levels);
    }
    emit spectrumChanged();
}
