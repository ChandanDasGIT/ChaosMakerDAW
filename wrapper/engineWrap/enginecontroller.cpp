#include "enginecontroller.h"
#include <filesystem>
#include <QThreadPool>
#include "audiofile/mp3writer.h"
#include "audiofile/wavwriter.h"
#include "render/songrender.h"
#include "engine/enginestatus.h"

#include "core/version.h"
#include "audiodevice/jackaudiodevice.h"
#include "engine/audioengine.h"
#include "engine/enginecommands.h"
#include "engine/enginesnapshot.h"
#include "settings/appsettings.h"
#include "wrapper/instruments/guitar/guitarsuiteeditorwrap/guitarsuiteeditorwrapper.h"
#include "instruments/guitar/plugins/guitarsuite/guitarsuite.h"
#include "instruments/instrumentfactory.h"
#include "instruments/drums/drumkit.h"
#include "instruments/electrodrums/electrodrums.h"
#include "instruments/harmonium/harmonium.h"
#include "instruments/piano/piano.h"
#include "instruments/chords/chordsinstrument.h"
#include "persistence/projectdocument.h"
#include "persistence/effectchainjson.h"
#include "plugins/effectchain.h"
#include "processor/trackinput.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJSEngine>
#include <QProcess>
#include <QQmlEngine>
#include <algorithm>
#include <limits>
#include <cmath>
#include <memory>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QUuid>
#include <QScopedValueRollback>

namespace {

// A new instrument for a track type, prepared before anything can run it.
std::shared_ptr<Processor> preparedInstrument(const std::string& type, double rate, int frames)
{
    auto instrument = createInstrument(type);
    if (instrument) instrument->prepare(rate, frames);
    return instrument;
}

} // namespace

EngineController::EngineController(QObject* parent)
    : EngineController(std::make_unique<JackAudioDevice>(), true, parent) {}

EngineController::EngineController(std::unique_ptr<AudioDevice> device, bool discoverSystemDevices, QObject* parent)
    : AudioEngineWrapper(std::move(device), parent)
{
    if (m_engine && m_engine->start(128)) {
        m_audioReady = true;
        for (const std::string& port : m_engine->inputNames())
            m_inputPorts.push_back(QString::fromStdString(port));
        for (const std::string& port : m_engine->outputNames())
            m_outputPorts.push_back(QString::fromStdString(port));
        if (m_inputPorts.isEmpty()) {
            setStatus(QStringLiteral("JACK is running, but no input ports were found."));
        } else {
            int preferredInput = 0;
            for (qsizetype i = 0; i < m_inputPorts.size(); ++i) {
                const QString port = m_inputPorts[i];
                if (port.contains(QStringLiteral("inst"), Qt::CaseInsensitive)
                    || port.contains(QStringLiteral("guitar"), Qt::CaseInsensitive)) {
                    preferredInput = static_cast<int>(i);
                    break;
                }
            }
            selectInput(preferredInput);
        }
        // The device connects a MIDI keyboard by itself (skipping the silent "Midi Through").
        if (discoverSystemDevices) discoverInputDevices();
    } else {
        setStatus(QStringLiteral("Audio unavailable · %1")
                      .arg(QString::fromStdString(m_engine->lastError())));
    }
    if (discoverSystemDevices) discoverOutputDevices();
    else if (m_audioReady && !m_outputPorts.isEmpty())
        m_outputConnected = m_engine->connectOutput(m_outputPorts.first().toStdString(), m_outputPorts.value(1).toStdString());

    m_guitarSuite = new GuitarSuiteModel(this);
    connect(m_guitarSuite, &GuitarSuiteModel::inputTrimDbChanged, this, [this] {
        if (!m_project) m_engine->setInputTrimDb(m_guitarSuite->inputTrimDb());
    });
    connect(&m_meterTimer, &QTimer::timeout, this, [this] {
        const auto levels = m_engine->takeMonitorLevels();
        auto update = [this](const QString& key, float peak) {
            const double db = 20.0 * std::log10(std::max(double(peak), 1e-6));
            m_monitorLevels[key] = std::max(db, m_monitorLevels.value(key, -120.0).toDouble() - 1.2);
        };
        update(QStringLiteral("rawDb"), levels.raw);
        update(QStringLiteral("preFxDb"), levels.preFx);
        update(QStringLiteral("leftDb"), levels.left);
        update(QStringLiteral("rightDb"), levels.right);
        m_monitorLevels[QStringLiteral("rawClip")] = levels.rawClip;
        m_monitorLevels[QStringLiteral("preFxClip")] = levels.preFxClip;
        m_monitorLevels[QStringLiteral("leftClip")] = levels.leftClip;
        m_monitorLevels[QStringLiteral("rightClip")] = levels.rightClip;
        emit monitorLevelsChanged();
        QVariantMap trackLevels;
        for (const auto& [id,fx] : m_trackEffects) {
            const QString key=QString::number(id);
            const auto previous=m_trackLevels.value(key).toMap();
            QVariantMap levels;
            for(int c=0;c<2;++c) for(int output=0;output<2;++output) {
                auto& meter=output ? fx.meters->output[c] : fx.meters->input[c];
                const QString channel=(output ? QStringLiteral("output") : QStringLiteral("input"))+(c==0 ? "Left" : "Right");
                const double db=20*std::log10(std::max(double(meter.takePeak()),1e-6));
                levels[channel+"Db"]=std::max(db,previous.value(channel+"Db",-120.0).toDouble()-1.2);
                levels[channel+"Clip"]=meter.clipped();
            }
            for(const auto& tap : {std::pair<const char*,PeakMeter*>{"raw",&fx.meters->raw}, {"preFx",&fx.meters->preFx}}) {
                const QString name=QString::fromLatin1(tap.first);
                const double db=20*std::log10(std::max(double(tap.second->takePeak()),1e-6));
                levels[name+"Db"]=std::max(db,previous.value(name+"Db",-120.0).toDouble()-1.2);
                levels[name+"Clip"]=tap.second->clipped();
            }
            levels["leftDb"]=levels["outputLeftDb"]; levels["rightDb"]=levels["outputRightDb"];
            levels["leftClip"]=levels["outputLeftClip"]; levels["rightClip"]=levels["outputRightClip"];
            trackLevels[key]=levels;
        }
        m_trackLevels=trackLevels;
        emit trackLevelsChanged();
        refreshAudioInfo();
    });
    m_meterTimer.start(50);
    const double rate = m_engine->sampleRate() > 1000.0 ? m_engine->sampleRate() : 48000.0;
    const int frames = m_engine->latency().captureFrames > 0 ? m_engine->latency().captureFrames : 128;
    m_guitarSuite->configure(rate, frames);
    m_guitarSuite->setPublisher([this](std::shared_ptr<GuitarSuite> chain) {
        queueGuitarSuite(std::move(chain));
    });
    connect(&m_snapshotTimer, &QTimer::timeout, this, [this] {
        if (!m_engine) return;
        m_engine->collectGarbage();
        pollTransport();
    });
    m_snapshotTimer.start(20);
    m_engine->post({EngineCommand::Type::SetStopAtEnd, stopPlaybackAtEnd() ? 1 : 0});
    m_sessionTimer.setSingleShot(true);
    m_sessionTimer.setInterval(1500);
    connect(&m_sessionTimer, &QTimer::timeout, this, &EngineController::saveSession);
    watchEffects(m_guitarSuite);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &EngineController::shutdownSession);
}

EngineController::~EngineController()
{
    m_engine->stop();
    m_recordingController.shutdown(nullptr, nullptr);
}

double EngineController::mixEffectReductionDb(int trackId, const QString& type) const
{
    const auto it = m_trackEffects.find(trackId);
    if (it == m_trackEffects.end()) return 0.0;
    float db = 0.0f;
    for (const auto* set : {&it->second.mixEffects, &it->second.playbackMixEffects})
        if (const auto effect = set->find(type.toStdString()); effect != set->end() && effect->second)
            db = std::max(db, effect->second->reductionDb());
    return db;
}

std::shared_ptr<MixEffect> EngineController::mixEffectProcessor(int trackId, const QString& type, bool playback) const
{
    const auto it = m_trackEffects.find(trackId);
    if (it == m_trackEffects.end()) return nullptr;
    const auto& set = playback ? it->second.playbackMixEffects : it->second.mixEffects;
    const auto effect = set.find(type.toStdString());
    return effect == set.end() ? nullptr : effect->second;
}

std::array<std::shared_ptr<ChaosEq>, 2> EngineController::chaosEqProcessors(int trackId) const
{
    const auto it = m_trackEffects.find(trackId);
    if (it == m_trackEffects.end()) return {};
    return {it->second.chaosEq, it->second.playbackChaosEq};
}

std::shared_ptr<Processor> EngineController::trackInstrument(int trackId) const
{
    const auto it = m_trackEffects.find(trackId);
    return it == m_trackEffects.end() ? nullptr : it->second.instrument;
}

EngineController* EngineController::instance()
{
    static auto* controller = new EngineController(QCoreApplication::instance());
    return controller;
}

QObject* EngineController::guitarSuite() const
{
    return m_guitarSuite;
}

void EngineController::queueGuitarSuite(std::shared_ptr<GuitarSuite> chain)
{
    if (m_project) return;
    m_engine->publishMonitor(SnapshotBuilder::buildMasterChainSnapshot(std::move(chain)));
}

void EngineController::watchEffects(GuitarSuiteModel* model)
{
    const auto changed = [this] { if (!m_restoring) { m_sessionTimer.start(); setProjectDirty(true); } };
    connect(model, &QAbstractItemModel::dataChanged, this, changed);
    connect(model, &GuitarSuiteModel::countChanged, this, changed);
    connect(model, &GuitarSuiteModel::inputTrimDbChanged, this, changed);
    connect(model, &GuitarSuiteModel::lastErrorChanged, this, [this, model] {
        if (!model->lastError().isEmpty()) setTransportMessage(model->lastError());
    });
    connect(model, &GuitarSuiteModel::busyChanged, this, [this, model] {
        // Async preset loaders commit after emitting busyChanged.
        QTimer::singleShot(0, model, [this] {
            if (!m_restoringPreset || effectsBusy()) return;
            m_restoringPreset = false;
            bool failed = !m_guitarSuite->lastError().isEmpty();
            for (const auto& [id, fx] : m_trackEffects) failed |= !fx.model->lastError().isEmpty();
            m_sessionSaveBlocked = failed;
            if (!failed) m_sessionTimer.start();
        });
    });
}

bool EngineController::effectsBusy() const
{
    if (m_guitarSuite->busy()) return true;
    for (const auto& [id, fx] : m_trackEffects) if (fx.model->busy()) return true;
    return false;
}

QObject* EngineController::trackEffects(int trackId)
{
    // A track, the master (G9) or a clip's effects (R2).
    if (!m_project || !m_project->project().effects(trackId)) return nullptr;
    const auto existing=m_trackEffects.find(trackId);
    if(existing!=m_trackEffects.end())return existing->second.model;
    bindTrackEffects(trackId,new GuitarSuiteModel(this));
    // A track coming back (undo of its removal) gets its pedalboard back.
    if (const auto kept = m_removedTrackEffects.find(trackId); kept != m_removedTrackEffects.end()) {
        m_trackEffects.at(trackId).model->restoreSessionData(std::string_view(kept->second));
        m_removedTrackEffects.erase(kept);
    }
    return m_trackEffects.at(trackId).model;
}

void EngineController::bindTrackEffects(int trackId, GuitarSuiteModel* model)
{
    QScopedValueRollback<bool> binding(m_bindingEffects,true);
    auto& fx=m_trackEffects[trackId];
    fx.model=model;
    fx.input = std::make_shared<TrackInput>();
    fx.playbackInput = std::make_shared<TrackInput>();
    const double rate = m_engine->sampleRate() > 1000 ? m_engine->sampleRate() : 48000;
    const int frames = std::max(128, m_engine->latency().captureFrames);
    fx.input->setTrimDb(fx.model->inputTrimDb());
    fx.input->prepare(rate, frames);
    fx.playbackInput->setTrimDb(fx.model->inputTrimDb()); fx.playbackInput->prepare(rate,frames);
    // The EQ effect; it runs only when on the track's effect list (bands set by syncTrackEq).
    fx.eq = std::make_shared<TenBandEq>(); fx.eq->prepare(rate, frames);
    fx.playbackEq = std::make_shared<TenBandEq>(); fx.playbackEq->prepare(rate, frames);
    fx.chaosEq = std::make_shared<ChaosEq>(); fx.chaosEq->prepare(rate, frames);
    fx.playbackChaosEq = std::make_shared<ChaosEq>(); fx.playbackChaosEq->prepare(rate, frames);
    fx.model->configure(rate, frames);
    if (m_project) {
        const auto* track = m_project->project().findTrack(trackId);
        if (track && track->kind == TrackKind::Instrument && !fx.instrument)
            fx.instrument = preparedInstrument(track->type, rate, frames);
    }
    connect(fx.model, &GuitarSuiteModel::inputTrimDbChanged, this, [this, trackId] {
        auto& fx = m_trackEffects.at(trackId);
        fx.input->setTrimDb(fx.model->inputTrimDb());
        fx.playbackInput->setTrimDb(fx.model->inputTrimDb());
    });
    watchEffects(fx.model);
    fx.model->setPublisher([this, trackId](std::shared_ptr<GuitarSuite> chain) {
        m_trackEffects.at(trackId).chain = std::move(chain);
        if(m_project && !m_restoring && !m_bindingEffects) { syncTrackEffects(); publishPlayback(); }
        if (!m_restoring) { m_sessionTimer.start(); setProjectDirty(true); }
    });
    fx.model->setPlaybackPublisher([this,trackId](std::shared_ptr<GuitarSuite> chain) {
        m_trackEffects.at(trackId).playbackChain=std::move(chain);
        if(!m_restoring && !m_bindingEffects)publishPlayback();
    });
    QQmlEngine::setObjectOwnership(fx.model, QQmlEngine::CppOwnership);
}

void EngineController::clearTrackEffects()
{
    m_monitoredTrack = -1;
    emit monitoredTrackChanged();
    for (auto& [id, fx] : m_trackEffects) delete fx.model;
    m_trackEffects.clear();
    m_removedTrackEffects.clear();
    m_trackLevels.clear(); emit trackLevelsChanged();
    m_engine->publishMonitor(SnapshotBuilder::buildEmptySnapshot());
}

void EngineController::syncTrackEffects()
{
    if (!m_project) return;
    const double rate = m_engine && m_engine->sampleRate() > 1000 ? m_engine->sampleRate() : 48000;
    const int frames = m_engine ? std::max(128, m_engine->latency().captureFrames) : 128;
    for (const auto& track : m_project->project().tracks()) {
        trackEffects(track->id);
        // Only a new instrument is prepared: an existing one may be playing on the audio thread.
        if (track->kind == TrackKind::Instrument) {
            auto& fx = m_trackEffects[track->id];
            if (!fx.instrument) fx.instrument = preparedInstrument(track->type, rate, frames);
        }
        // Mixing plugins (restMap G7) are made when they first go on the chain.
        std::vector<std::string> types;
        for (const auto& slot : track->effects) types.push_back(slot.type);
        SnapshotBuilder::ensureMixEffects(m_trackEffects[track->id], types, rate, frames);
    }
    {   // The master track's effects (G9), made like a track's.
        const auto& master = m_project->project().master();
        trackEffects(master.id);
        std::vector<std::string> types;
        for (const auto& slot : master.effects) types.push_back(slot.type);
        SnapshotBuilder::ensureMixEffects(m_trackEffects[master.id], types, rate, frames);
    }
    // Clip effects (restMap R2): processors only for clips that have effects.
    for (const auto& track : m_project->project().tracks())
        for (const auto& clip : track->audioClips) {
            if (!clip.fx.hasEffects()) continue;
            const int id = Project::clipEffectsId(clip.id);
            trackEffects(id);
            std::vector<std::string> types;
            for (const auto& slot : clip.fx.effects) types.push_back(slot.type);
            SnapshotBuilder::ensureMixEffects(m_trackEffects[id], types, rate, frames);
        }
    SnapshotBuilder::syncTrackEq(m_project->project(), [this](int id) {
        auto it = m_trackEffects.find(id);
        return it != m_trackEffects.end() ? &it->second : nullptr;
    });
    for (auto it = m_trackEffects.begin(); it != m_trackEffects.end();) {
        if (!m_project->project().effects(it->first)) {   // gone: a track, or a clip with effects
            if (it->second.model) m_removedTrackEffects[it->first] = it->second.model->sessionDataStd();
            delete it->second.model;
            it = m_trackEffects.erase(it);
        } else ++it;
    }

    std::vector<std::string> ports;
    ports.reserve(m_inputPorts.size());
    for (const auto& p : m_inputPorts) ports.push_back(p.toStdString());

    auto result = SnapshotBuilder::syncRouting(
        m_project->project(),
        [this](int id) {
            auto it = m_trackEffects.find(id);
            return it != m_trackEffects.end() ? &it->second : nullptr;
        },
        m_selectedInput.toStdString(),
        ports,
        m_engine->inputChannelCapacity(),
        [this](const std::string& src) { return m_engine->connectInputChannel(src); },
        [this]() { return m_engine->lastError(); });

    m_trackRoutingError = QString::fromStdString(result.error);
    m_monitoredTrack = result.monitoredTrack;
    if (result.cleanMonitorEnabled != m_monitorEnabled) {
        setCleanMonitorEnabled(result.cleanMonitorEnabled);
    }
    emit monitoredTrackChanged();
    if (!m_trackRoutingError.isEmpty()) setStatus(m_trackRoutingError);
}

EngineController* EngineController::create(QQmlEngine*, QJSEngine*)
{
    auto* controller = instance();
    QQmlEngine::setObjectOwnership(controller, QQmlEngine::CppOwnership);   // QML must not delete it
    return controller;
}

QString EngineController::version() const
{
    return QString::fromUtf8(engineVersion());
}

int EngineController::selectedBufferSize() const
{
    return m_engine ? m_engine->latency().captureFrames : 0;
}

void EngineController::setCleanMonitorEnabled(bool enabled)
{
    if (!m_engine || !m_audioReady)
        return;
    EngineCommand command;
    command.type = EngineCommand::Type::SetCleanMonitor;
    command.a = enabled ? 1 : 0;
    if (m_engine->post(command)) {
        m_monitorEnabled = enabled;
        emit monitoredTrackChanged();
        updateAudioStatus();
    }
}

bool EngineController::selectInput(int index)
{
    if (takeBusy()) return false;
    if (!m_engine || !m_audioReady || index < 0 || index >= m_inputPorts.size())
        return false;
    const QString source = m_inputPorts[index];
    m_selectedInput = source;
    m_selectedInputIndex = index;
    emit audioRoutingChanged();
    if(m_project) {
        syncTrackEffects(); publishPlayback();
        if(!monitoredTracks().empty()) { m_connectionError.clear(); updateAudioStatus(); return true; }
    }
    if (m_monitorInputOverride.isEmpty()) {
        m_inputConnected = m_engine->connectInput(source.toStdString());
        if (!m_inputConnected) {
            m_connectionError = QStringLiteral("Input connection failed · %1")
                                    .arg(QString::fromStdString(m_engine->lastError()));
            setStatus(m_connectionError);
            return false;
        }
        m_activeInput = source;
    }
    m_connectionError.clear();
    updateAudioStatus();
    return true;
}

bool EngineController::setOutputDevice(int index)
{
    if (index < 0 || index >= m_outputDeviceNames.size())
        return false;
    const QString sinkName = m_outputDeviceNames[index];
    auto* process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, index](int exitCode, QProcess::ExitStatus) {
        const QString error = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
        if (exitCode != 0) {
            m_connectionError = QStringLiteral("Could not change output · %1").arg(error);
            setStatus(m_connectionError);
            process->deleteLater();
            return;
        }
        m_connectionError.clear();
        if (m_audioReady)
            connectOutputDevicePorts(m_outputDevices.value(index));
        m_selectedOutputIndex = index;
        emit outputDeviceChanged();
        updateAudioStatus();
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
        if (process->error() == QProcess::FailedToStart) {
            m_connectionError = QStringLiteral("pactl is unavailable; cannot change the system output device.");
            setStatus(m_connectionError);
            process->deleteLater();
        }
    });
    process->start(QStringLiteral("pactl"), {QStringLiteral("set-default-sink"), sinkName});
    return true;
}

bool EngineController::setBufferSize(int frames)
{
    if (!m_engine || !m_audioReady || !bufferSizes().contains(QString::number(frames)))
        return false;
    if (!m_engine->setBufferSize(frames)) {
        m_connectionError = QStringLiteral("JACK could not change the buffer to %1 frames.").arg(frames);
        setStatus(m_connectionError);
        return false;
    }
    m_connectionError.clear();
    if (m_guitarSuite) m_guitarSuite->configure(m_engine->sampleRate(), frames);
    for (auto& [id, fx] : m_trackEffects) {
        // Replace input state rather than preparing a processor the audio thread may own.
        auto input = std::make_shared<TrackInput>();
        input->setTrimDb(fx.model->inputTrimDb());
        input->prepare(m_engine->sampleRate(), frames);
        fx.input = std::move(input);
        fx.playbackInput=std::make_shared<TrackInput>();
        fx.playbackInput->setTrimDb(fx.model->inputTrimDb()); fx.playbackInput->prepare(m_engine->sampleRate(),frames);
        fx.eq=std::make_shared<TenBandEq>(); fx.eq->prepare(m_engine->sampleRate(),frames);
        fx.playbackEq=std::make_shared<TenBandEq>(); fx.playbackEq->prepare(m_engine->sampleRate(),frames);
        fx.chaosEq=std::make_shared<ChaosEq>(); fx.chaosEq->prepare(m_engine->sampleRate(),frames);
        fx.playbackChaosEq=std::make_shared<ChaosEq>(); fx.playbackChaosEq->prepare(m_engine->sampleRate(),frames);
        SnapshotBuilder::remakeMixEffects(fx, m_engine->sampleRate(), frames);   // fresh: the old ones may be playing
        fx.model->configure(m_engine->sampleRate(), frames);
        if (fx.instrument && m_project) {
            if (const auto* track = m_project->project().findTrack(id))
                fx.instrument = preparedInstrument(track->type, m_engine->sampleRate(), frames);
        }
    }
    syncTrackEffects();
    publishPlayback();                                     // put the new processors on the audio thread
    emit bufferSizeChanged();
    updateAudioStatus();
    return true;
}

void EngineController::setCleanMonitorGain(double gain)
{
    if (!m_engine || !m_audioReady || !std::isfinite(gain))
        return;
    EngineCommand command;
    command.type = EngineCommand::Type::SetCleanMonitorGain;
    command.value = std::clamp(static_cast<float>(gain), 0.0f, 2.0f);
    m_engine->post(command);
}

void EngineController::setCleanMonitorEqBands(const QVariantList& bands)
{
    if (!m_engine || !m_audioReady)
        return;
    for (int band = 0; band < 10 && band < bands.size(); ++band)
        m_engine->setMonitorEqBand(band, static_cast<float>(bands[band].toDouble()));
}

QString EngineController::inputPortForDevice(int index) const
{
    return index >= 0 && index < m_inputDevicePorts.size() ? m_inputDevicePorts[index] : QString{};
}

void EngineController::discoverOutputDevices()
{
    auto* process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus) {
        const QByteArray json = process->readAllStandardOutput();
        const QString error = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
        process->deleteLater();
        if (exitCode != 0) {
            setStatus(QStringLiteral("Could not list system outputs · %1").arg(error));
            return;
        }

        const QJsonArray sinks = QJsonDocument::fromJson(json).array();
        QStringList names;
        QStringList descriptions;
        for (const QJsonValue& value : sinks) {
            const QJsonObject sink = value.toObject();
            const QString name = sink.value(QStringLiteral("name")).toString();
            if (name.isEmpty())
                continue;
            names.push_back(name);
            const QString description = sink.value(QStringLiteral("description")).toString();
            descriptions.push_back(description.isEmpty() ? name : description);
        }
        m_outputDeviceNames = names;
        const bool namesChanged = m_outputDevices != descriptions;
        m_outputDevices = descriptions;
        if (namesChanged)
            emit outputDevicesChanged();
        if (m_outputDeviceNames.isEmpty()) {
            setStatus(QStringLiteral("No system output devices found."));
            return;
        }

        auto* defaultProcess = new QProcess(this);
        connect(defaultProcess, &QProcess::finished, this,
                [this, defaultProcess](int, QProcess::ExitStatus) {
            const QString defaultName = QString::fromLocal8Bit(defaultProcess->readAllStandardOutput()).trimmed();
            defaultProcess->deleteLater();
            const int current = m_outputDeviceNames.indexOf(defaultName);
            setOutputDevice(current >= 0 ? current : 0);
        });
        defaultProcess->start(QStringLiteral("pactl"), {QStringLiteral("get-default-sink")});
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
        if (process->error() == QProcess::FailedToStart) {
            setStatus(QStringLiteral("pactl is unavailable; system output devices cannot be listed."));
            process->deleteLater();
        }
    });
    process->start(QStringLiteral("pactl"), {QStringLiteral("-f"), QStringLiteral("json"),
                                                QStringLiteral("list"), QStringLiteral("sinks")});
}

void EngineController::discoverInputDevices()
{
    auto* process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus) {
        const QByteArray json = process->readAllStandardOutput();
        process->deleteLater();
        if (exitCode != 0)
            return;

        const QJsonArray sources = QJsonDocument::fromJson(json).array();
        QStringList devices;
        QStringList ports;
        for (const QJsonValue& value : sources) {
            const QJsonObject source = value.toObject();
            const QString name = source.value(QStringLiteral("name")).toString();
            const QJsonValue monitorOfSink = source.value(QStringLiteral("monitor_of_sink"));
            if (name.isEmpty() || name.endsWith(QStringLiteral(".monitor"), Qt::CaseInsensitive)
                || (monitorOfSink.isDouble() && monitorOfSink.toInt(-1) >= 0))
                continue;

            const QString description = source.value(QStringLiteral("description")).toString();
            if (description.isEmpty() || description.contains(QStringLiteral("monitor of"), Qt::CaseInsensitive))
                continue;

            const QString prefix = description + QLatin1Char(':');
            QString matchingPort;
            for (const QString& port : m_inputPorts) {
                if (port.startsWith(prefix, Qt::CaseInsensitive)
                    && (matchingPort.isEmpty() || port.contains(QStringLiteral("capture"), Qt::CaseInsensitive))) {
                    matchingPort = port;
                    if (port.contains(QStringLiteral("capture"), Qt::CaseInsensitive))
                        break;
                }
            }
            if (matchingPort.isEmpty())
                continue;
            devices.push_back(description);
            ports.push_back(matchingPort);
        }
        if (devices != m_inputDevices || ports != m_inputDevicePorts) {
            m_inputDevices = devices;
            m_inputDevicePorts = ports;
            emit inputDevicesChanged();
        }
    });
    process->start(QStringLiteral("pactl"), {QStringLiteral("-f"), QStringLiteral("json"),
                                                QStringLiteral("list"), QStringLiteral("sources")});
}

void EngineController::connectOutputDevicePorts(const QString& description)
{
    const QString prefix = description + QLatin1Char(':');
    QString left;
    QString right;
    for (const QString& port : m_outputPorts) {
        if (!port.startsWith(prefix, Qt::CaseInsensitive))
            continue;
        if (port.contains(QStringLiteral("playback_FL"), Qt::CaseInsensitive)
            || port.endsWith(QStringLiteral("playback_1"), Qt::CaseInsensitive))
            left = port;
        else if (port.contains(QStringLiteral("playback_FR"), Qt::CaseInsensitive)
                 || port.endsWith(QStringLiteral("playback_2"), Qt::CaseInsensitive))
            right = port;
    }
    if (left.isEmpty()) {
        m_outputConnected = false;
        m_connectionError = QStringLiteral("System output changed, but no matching JACK playback ports were found.");
        return;
    }
    m_outputConnected = m_engine->connectOutput(left.toStdString(), right.toStdString());
    if (m_outputConnected) {
        m_selectedOutputLeft = left;
        m_selectedOutputRight = right;
    } else {
        m_connectionError = QStringLiteral("JACK output connection failed · %1")
                                .arg(QString::fromStdString(m_engine->lastError()));
    }
}

bool EngineController::setCleanMonitorInput(const QString& port)
{
    if (takeBusy()) return false;
    if (!m_engine || !m_audioReady)
        return false;
    m_monitorInputOverride = port;
    const QString source = port.isEmpty() ? m_selectedInput : port;
    if (source.isEmpty()) {
        m_inputConnected = false;
        updateAudioStatus();
        return false;
    }
    if (!m_inputPorts.contains(source)) {
        m_inputConnected = false;
        m_connectionError = QStringLiteral("Track input is unavailable · %1").arg(source);
        setStatus(m_connectionError);
        return false;
    }
    m_inputConnected = m_engine->connectInput(source.toStdString());
    if (!m_inputConnected) {
        m_connectionError = QStringLiteral("Track input connection failed · %1")
                                .arg(QString::fromStdString(m_engine->lastError()));
        setStatus(m_connectionError);
        return false;
    }
    m_activeInput = source;
    m_connectionError.clear();
    updateAudioStatus();
    return true;
}

void EngineController::updateAudioStatus()
{
    if (!m_audioReady)
        return;
    // Rate, buffer and latency are shown by the status strip's own readouts.
    refreshAudioInfo();
    if (!m_connectionError.isEmpty()) {
        setStatus(m_connectionError);
    } else if (m_monitorEnabled && m_inputConnected && m_outputConnected) {
        setStatus(QStringLiteral("Clean monitor · %1 → %2")
                      .arg(m_activeInput, m_selectedOutputLeft));
    } else if (m_monitorEnabled) {
        setStatus(QStringLiteral("Monitor armed · select a connected input and output."));
    } else if (m_inputConnected && m_outputConnected) {
        setStatus(QStringLiteral("Audio I/O connected · %1 → %2")
                      .arg(m_activeInput, m_selectedOutputLeft));
    } else {
        setStatus(QStringLiteral("JACK is running · choose an input and output port."));
    }
}

void EngineController::refreshAudioInfo()
{
    const double rate = m_audioReady && m_engine ? m_engine->sampleRate() : 0.0;
    const int frames = m_audioReady && m_engine ? m_engine->latency().captureFrames : 0;
    const std::uint64_t xruns = m_engine ? m_engine->xrunCount() : 0;
    if (rate == m_infoRate && frames == m_infoFrames && xruns == m_infoXruns) return;
    m_infoRate = rate; m_infoFrames = frames; m_infoXruns = xruns;
    emit audioInfoChanged();
}

QString EngineController::sampleRateText() const
{
    return QString::fromStdString(enginestatus::sampleRate(m_infoRate));
}

QString EngineController::bufferText() const
{
    return QString::fromStdString(enginestatus::buffer(m_infoFrames, m_infoRate));
}

QString EngineController::bufferTooltip() const
{
    const int requested = m_engine ? m_engine->latency().requestedBufferFrames : 0;
    return QString::fromStdString(enginestatus::bufferTooltip(m_infoFrames, requested, m_infoRate));
}

int EngineController::xrunCount() const
{
    return static_cast<int>(std::min<std::uint64_t>(m_infoXruns, std::numeric_limits<int>::max()));
}

QString EngineController::xrunText() const
{
    return QString::fromStdString(enginestatus::xruns(m_infoXruns));
}

QString EngineController::xrunTooltip() const
{
    return QString::fromStdString(enginestatus::xrunTooltip(m_infoXruns));
}

void EngineController::resetXruns()
{
    if (m_engine) m_engine->resetXruns();
    refreshAudioInfo();
}

void EngineController::setStatus(const QString& status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void EngineController::setCountInEnabled(bool enabled) {
    if (takeBusy() || enabled==m_countInEnabled) return;
    m_countInEnabled=enabled; emit countInSettingsChanged();
}
bool EngineController::countingIn() const { return m_engine->countInSamplesRemaining()>0; }
int EngineController::countInBeatsRemaining() const {
    const double rate=m_engine->sampleRate()>1000 ? m_engine->sampleRate() : 48000;
    const double beats=m_engine->countInSamplesRemaining()*m_bpm*m_denominator/(60*rate*4);
    return int(std::min(double(INT_MAX),std::ceil(beats)));
}

bool EngineController::postMetronome(bool enabled, double volume) {
    EngineCommand command; command.type=EngineCommand::Type::SetMetronome;
    command.a=enabled; command.value=float(volume);
    if (!m_engine->post(command)) { setTransportMessage("Audio command queue is full; try again."); return false; }
    m_metronomeEnabled=enabled; m_metronomeVolume=volume; emit metronomeChanged(); return true;
}
void EngineController::setMetronomeEnabled(bool enabled) {
    if (enabled!=m_metronomeEnabled) postMetronome(enabled,m_metronomeVolume);
}
void EngineController::setMetronomeVolume(double volume) {
    if (std::isfinite(volume) && volume>=0 && volume<=1 && volume!=m_metronomeVolume)
        postMetronome(m_metronomeEnabled,volume);
}

void EngineController::setBpm(double bpm)
{
    if (countingIn() || !m_engine || !m_engine->setTempoBpm(bpm) || m_bpm == bpm) return;
    m_bpm = bpm;
    if (m_repeating && !takeBusy()) postRepeatLoop();
    if (m_project) m_project->setTempo(bpm, m_numerator, m_denominator);
    emit bpmChanged(); emit transportChanged();
    if (!m_restoring) m_sessionTimer.start();
}

void EngineController::clearMonitorClips()
{
    if (m_engine) m_engine->clearMonitorClips();
}

void EngineController::clearTrackClips(int trackId)
{
    const auto it=m_trackEffects.find(trackId);
    if(it!=m_trackEffects.end())it->second.meters->clearClips();
}

void EngineController::setMasterVolume(double volume)
{
    if (!std::isfinite(volume)) return;
    const double clamped = std::clamp(volume, 0.0, 2.0);
    if (m_masterVolume == clamped) return;
    m_masterVolume = clamped;
    if (m_engine) m_engine->setMasterVolume(static_cast<float>(clamped));
    if (m_project && m_project->masterVolume() != clamped) {
        m_project->setMasterVolume(clamped);
    }
    emit masterVolumeChanged();
    if (!m_restoring) m_sessionTimer.start();
}

void EngineController::setMasterMuted(bool muted)
{
    if (m_masterMuted == muted) return;
    m_masterMuted = muted;
    if (m_engine) m_engine->setMasterMuted(muted);
    if (m_project && m_project->masterMuted() != muted) {
        m_project->setMasterMuted(muted);
    }
    emit masterMutedChanged();
    if (!m_restoring) m_sessionTimer.start();
}

namespace {
QString sessionPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/last-session.json");
}
}
bool EngineController::playing() const { return m_engine->playing(); }
bool EngineController::recording() const { return m_engine->recording(); }
double EngineController::positionSeconds() const {
    const double rate = m_engine->sampleRate();
    return double(m_engine->position()) / (rate > 1000 ? rate : 48000);
}
double EngineController::positionBeats() const { return positionSeconds() * m_bpm / 60; }
bool EngineController::restoreSessionOnStartup() const {
    return QSettings().value(QStringLiteral("session/restoreOnStartup"), false).toBool();
}
void EngineController::setRestoreSessionOnStartup(bool enabled) {
    QSettings settings; settings.setValue(QStringLiteral("session/restoreOnStartup"), enabled); settings.sync();
    if (settings.status() != QSettings::NoError) { setTransportMessage("Could not save startup preference."); return; }
    emit restoreSessionOnStartupChanged();
    if (enabled) saveSession();
}
bool EngineController::stopPlaybackAtEnd() const {
    return QSettings().value(QString::fromLatin1(AppSettings::KeyStopPlaybackAtEnd), true).toBool();
}
void EngineController::setStopPlaybackAtEnd(bool enabled) {
    if (enabled == stopPlaybackAtEnd()) return;
    if (!m_engine->post({EngineCommand::Type::SetStopAtEnd, enabled ? 1 : 0})) {
        setTransportMessage("Audio command queue is full; try again."); return;
    }
    QSettings settings; settings.setValue(QString::fromLatin1(AppSettings::KeyStopPlaybackAtEnd), enabled); settings.sync();
    if (settings.status() != QSettings::NoError) setTransportMessage("Could not save the stop-at-end preference.");
    emit stopPlaybackAtEndChanged();
}
bool EngineController::recordingLatencyCompensation() const {
    return QSettings().value(QString::fromLatin1(AppSettings::KeyRecordingLatencyCompensation), true).toBool();
}
void EngineController::setRecordingLatencyCompensation(bool enabled) {
    if (enabled == recordingLatencyCompensation()) return;
    QSettings settings; settings.setValue(QString::fromLatin1(AppSettings::KeyRecordingLatencyCompensation), enabled); settings.sync();
    if (settings.status() != QSettings::NoError) setTransportMessage("Could not save the latency compensation preference.");
    emit recordingLatencyChanged();
}
double EngineController::recordingExtraOffsetMs() const {
    const double ms = QSettings().value(QString::fromLatin1(AppSettings::KeyRecordingExtraOffsetMs), 0.0).toDouble();
    return std::isfinite(ms) ? std::clamp(ms, -100.0, 100.0) : 0.0;
}
void EngineController::setRecordingExtraOffsetMs(double ms) {
    if (!std::isfinite(ms)) return;
    ms = std::clamp(ms, -100.0, 100.0);
    if (ms == recordingExtraOffsetMs()) return;
    QSettings settings; settings.setValue(QString::fromLatin1(AppSettings::KeyRecordingExtraOffsetMs), ms); settings.sync();
    if (settings.status() != QSettings::NoError) setTransportMessage("Could not save the recording offset.");
    emit recordingLatencyChanged();
}
double EngineController::reportedRoundTripMs() const {
    const double rate = m_engine ? m_engine->sampleRate() : 0.0;
    return m_audioReady && rate > 0 ? m_engine->latency().roundTripFrames * 1000.0 / rate : 0.0;
}
void EngineController::setTransportMessage(const QString& message) {
    if (m_transportMessage == message) return;
    m_transportMessage = message; emit transportMessageChanged();
}
void EngineController::attachProject(TrackListWrapper* model) {
    if (!model || m_project == model) return;
    if (m_project) disconnect(m_project, nullptr, this, nullptr);
    clearTrackEffects();
    m_restoringPreset = false;
    m_sessionSaveBlocked = false;
    m_project = model;
    m_repeatStartBeat=0; m_repeatEndBeat=0;
    model->setSampleRate(m_engine->sampleRate() > 1000 ? m_engine->sampleRate() : 48000);
    m_restoring = true;
    if (restoreSessionOnStartup()) {
        const QString path = sessionPath();
        if (QFile::exists(path)) {
            persistence::ProjectFileContent content;
            std::string error;
            const double rate = m_engine->sampleRate() > 1000 ? m_engine->sampleRate() : 48000;
            if (!persistence::ProjectDocument::loadFromFile(path.toStdString(), content, error, rate)) {
                m_sessionSaveBlocked = true;
                setTransportMessage(QString::fromStdString(error.empty() ? "Invalid saved session; original file kept." : error));
            } else {
                model->replaceDocument(std::move(content.project), std::move(content.audio), std::move(content.mediaPaths));
                m_numerator = model->project().tempo().numerator(); m_denominator = model->project().tempo().denominator();
                setBpm(model->project().tempo().bpm()); emit meterChanged();
                setTransportMessage(QString::fromStdString(content.warning));
                m_sessionSaveBlocked = false;
                int row = 0;
                for (const auto& track : model->project().tracks()) {
                    auto* fx = qobject_cast<GuitarSuiteModel*>(trackEffects(track->id));
                    const auto& state = content.trackEffectsJson[row];
                    // Empty: the document dropped this pedalboard (no GuitarSuite on the track).
                    if (!state.empty() && !fx->restoreSessionData(state)) {
                        m_sessionSaveBlocked = true;
                        setTransportMessage(fx->lastError() + " · saved session kept unchanged.");
                    }
                    ++row;
                }
                if (effectsBusy()) {
                    m_restoringPreset = !m_sessionSaveBlocked;
                    m_sessionSaveBlocked = true;
                }
            }
        }
    }
    m_restoring = false;
    setMasterVolume(model->masterVolume());
    setMasterMuted(model->masterMuted());
    connect(model, &TrackListWrapper::masterVolumeChanged, this, [this] {
        if (m_project) setMasterVolume(m_project->masterVolume());
    });
    connect(model, &TrackListWrapper::masterMutedChanged, this, [this] {
        if (m_project) setMasterMuted(m_project->masterMuted());
    });
    connect(model, &TrackListWrapper::projectChanged, this, &EngineController::refreshProject);
    // Chaos EQ edits: copy the settings into the track's processors; no playback rebuild.
    connect(model, &TrackListWrapper::chaosEqChanged, this, [this](int trackId) {
        if (!m_project) return;
        const auto* settings = m_project->chaosEq(trackId);
        const auto it = m_trackEffects.find(trackId);
        if (!settings || it == m_trackEffects.end()) return;
        if (it->second.chaosEq) it->second.chaosEq->setSettings(*settings);
        if (it->second.playbackChaosEq) it->second.playbackChaosEq->setSettings(*settings);
        if (!m_restoring) { m_sessionTimer.start(); setProjectDirty(true); }
    });
    // Mixing plugin edits (restMap G7): into the track's processors; no playback rebuild.
    connect(model, &TrackListWrapper::mixEffectChanged, this, [this](int trackId, const QString& type) {
        if (!m_project) return;
        const auto settings = m_project->domain().mixEffect(trackId, type.toStdString());
        const auto it = m_trackEffects.find(trackId);
        if (!settings || it == m_trackEffects.end()) return;
        for (auto* set : {&it->second.mixEffects, &it->second.playbackMixEffects})
            if (const auto effect = set->find(type.toStdString()); effect != set->end() && effect->second)
                effect->second->apply(*settings);
        if (!m_restoring) { m_sessionTimer.start(); setProjectDirty(true); }
    });
    // Harmonium edits: straight into the track's instrument; no playback rebuild.
    connect(model, &TrackListWrapper::harmoniumChanged, this, [this](int trackId) {
        if (!m_project) return;
        const auto* settings = m_project->harmonium(trackId);
        const auto it = m_trackEffects.find(trackId);
        if (!settings || it == m_trackEffects.end()) return;
        if (it->second.instrument && it->second.instrument->typeId() == "harmonium")
            static_cast<Harmonium&>(*it->second.instrument).setSettings(*settings);
        if (!m_restoring) { m_sessionTimer.start(); setProjectDirty(true); }
    });
    // Drum kit edits: the kit rebuilds its zones in the background; no playback rebuild.
    connect(model, &TrackListWrapper::drumKitChanged, this, [this](int trackId) {
        if (!m_project) return;
        const auto* settings = m_project->drumKit(trackId);
        const auto it = m_trackEffects.find(trackId);
        if (!settings || it == m_trackEffects.end()) return;
        if (it->second.instrument && it->second.instrument->typeId() == "drumkit")
            static_cast<DrumKit&>(*it->second.instrument).setSettings(*settings);
        if (!m_restoring) { m_sessionTimer.start(); setProjectDirty(true); }
    });
    // Piano edits: the piano rebuilds its zones in the background; no playback rebuild.
    connect(model, &TrackListWrapper::pianoChanged, this, [this](int trackId) {
        if (!m_project) return;
        const auto* settings = m_project->piano(trackId);
        const auto it = m_trackEffects.find(trackId);
        if (!settings || it == m_trackEffects.end()) return;
        if (it->second.instrument && it->second.instrument->typeId() == "piano")
            static_cast<Piano&>(*it->second.instrument).setSettings(*settings);
        if (it->second.instrument && it->second.instrument->typeId() == "chords")
            static_cast<ChordsInstrument&>(*it->second.instrument).piano().setSettings(*settings);
        if (!m_restoring) { m_sessionTimer.start(); setProjectDirty(true); }
    });
    // Chords edits (strips, autoplay, volume, bass with chords): straight into the track's
    // player; no playback rebuild.
    connect(model, &TrackListWrapper::chordsChanged, this, [this](int trackId) {
        if (!m_project) return;
        const auto* settings = m_project->chords(trackId);
        const auto it = m_trackEffects.find(trackId);
        if (!settings || it == m_trackEffects.end()) return;
        if (it->second.instrument && it->second.instrument->typeId() == "chords") {
            chords::applyTo(static_cast<ChordsInstrument&>(*it->second.instrument).player(), *settings);
        }
        if (!m_restoring) { m_sessionTimer.start(); setProjectDirty(true); }
    });
    // Electronic drum edits: straight into the track's kit (atomic parameters); no rebuild.
    connect(model, &TrackListWrapper::electroDrumsChanged, this, [this](int trackId) {
        if (!m_project) return;
        const auto* settings = m_project->electroDrums(trackId);
        const auto it = m_trackEffects.find(trackId);
        if (!settings || it == m_trackEffects.end()) return;
        if (it->second.instrument && it->second.instrument->typeId() == "electrodrums")
            static_cast<ElectroDrums&>(*it->second.instrument).setSettings(*settings);
        if (!m_restoring) { m_sessionTimer.start(); setProjectDirty(true); }
    });
    refreshProject();
    setProjectDirty(model->count()>0);
}
void EngineController::refreshProject() {
    if (!m_project) return;
    syncTrackEffects();
    publishPlayback();
    if (!m_restoring) { m_sessionTimer.start(); setProjectDirty(true); }
}
double EngineController::songEndBeats() const
{
    if (!m_project) return 0.0;
    double end = 0.0;
    for (const auto& track : m_project->project().tracks())
        for (const auto& clip : track->audioClips)
            end = std::max(end, m_project->project().tempo().beatAt(clip.start + clip.length));
    return end;
}

bool EngineController::startExport(const QUrl& file, double startBeat, double endBeat, const QString& format,
                                   int mp3Kbps, double tailSeconds, bool normalise, double normalisePeakDb)
{
    if (!m_project || m_exporting || takeBusy() || recording() || !file.isLocalFile()) return false;
    if (format != QLatin1String("wav16") && format != QLatin1String("wav24") && format != QLatin1String("wav32f")
        && format != QLatin1String("mp3")) return false;
    if (!(endBeat > startBeat)) { startBeat = 0.0; endBeat = songEndBeats(); }
    if (!std::isfinite(startBeat) || !std::isfinite(endBeat) || startBeat < 0 || endBeat <= startBeat) return false;
    const auto& tempo = m_project->project().tempo();
    const double rate = tempo.sampleRate() > 1000 ? tempo.sampleRate() : 48000.0;
    songrender::Options options;
    options.sampleRate = rate;
    options.start = tempo.sampleAt(startBeat);
    options.length = tempo.sampleAt(endBeat) - options.start;
    options.tail = std::llround(std::clamp(std::isfinite(tailSeconds) ? tailSeconds : 0.0, 0.0, 10.0) * rate);
    options.bpm = m_bpm;
    options.masterVolume = m_project->project().masterVolume();
    options.masterMuted = m_project->project().masterMuted();
    if (options.length <= 0) return false;

    // The live engine stops and goes silent so the offline render can use the same processors.
    postTransport(int(EngineCommand::Type::Stop));
    syncTrackEffects();
    auto snapshot = SnapshotBuilder::buildPlaybackSnapshot(
        m_project->project(),
        [this](const std::string& poolId) { return m_project->audioFor(poolId); },
        [this](int id) -> const SnapshotBuilder::TrackEffects* {
            auto it = m_trackEffects.find(id);
            return it != m_trackEffects.end() ? &it->second : nullptr;
        },
        /*multiMonitor=*/true);
    m_engine->suspend();
    m_exporting = true;
    m_exportProgress = 0.0;
    m_exportCancel = std::make_shared<std::atomic<bool>>(false);
    emit exportChanged();
    setTransportMessage(QStringLiteral("Exporting…"));

    const auto cancel = m_exportCancel;
    const std::string path = file.toLocalFile().toStdString();
    const std::string kind = format.toStdString();
    QPointer<EngineController> self(this);
    auto job = std::make_shared<std::unique_ptr<EngineSnapshot>>(std::move(snapshot));
    QThreadPool::globalInstance()->start([self, job, options, cancel, path, kind, mp3Kbps, normalise, normalisePeakDb, rate] {
        QString message;
        bool ok = false;
        try {
            auto channels = songrender::render(std::move(*job), options, [self, cancel](double done) {
                if (self) QMetaObject::invokeMethod(self, [self, done] {
                    if (!self) return;
                    self->m_exportProgress = done * 0.9;
                    emit self->exportChanged();
                }, Qt::QueuedConnection);
                return !cancel->load();
            });
            if (cancel->load() || channels[0].empty()) {
                message = QStringLiteral("Export cancelled.");
            } else {
                if (normalise) songrender::normalise(channels, float(normalisePeakDb));
                if (kind == "mp3") {
                    audiofile::Mp3Options mp3;
                    mp3.bitrateKbps = mp3Kbps;
                    audiofile::writeMp3(path, channels, rate, mp3);
                } else {
                    audiofile::writeWav(path, channels, rate, kind == "wav16" ? audiofile::WavFormat::Int16
                                        : kind == "wav24" ? audiofile::WavFormat::Int24 : audiofile::WavFormat::Float32);
                }
                ok = true;
                message = QStringLiteral("Exported %1.").arg(QString::fromStdString(std::filesystem::path(path).filename().string()));
            }
        } catch (const std::exception& e) {
            message = QStringLiteral("Export failed: %1").arg(QString::fromStdString(e.what()));
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, ok, message] {
            if (!self) return;
            self->m_engine->resume();
            self->m_exporting = false;
            self->m_exportProgress = ok ? 1.0 : 0.0;
            emit self->exportChanged();
            self->setTransportMessage(message);
            emit self->exportFinished(ok, message);
        }, Qt::QueuedConnection);
    });
    return true;
}

void EngineController::cancelExport()
{
    if (m_exportCancel) m_exportCancel->store(true);
}

void EngineController::publishPlayback() {
    if (!m_project) return;
    auto snapshot = SnapshotBuilder::buildPlaybackSnapshot(
        m_project->project(),
        [this](const std::string& poolId) { return m_project->audioFor(poolId); },
        [this](int id) -> const SnapshotBuilder::TrackEffects* {
            auto it = m_trackEffects.find(id);
            return it != m_trackEffects.end() ? &it->second : nullptr;
        },
        /*multiMonitor=*/true);
    m_engine->publishPlayback(std::move(snapshot));
}

void EngineController::setMeter(int numerator, int denominator) {
    if (countingIn() || numerator <= 0 || denominator <= 0) return;
    m_numerator = numerator; m_denominator = denominator;
    if (m_project) m_project->setTempo(m_bpm, numerator, denominator);
    emit meterChanged(); m_sessionTimer.start();
}
bool EngineController::postTransport(int type, qint64 a, qint64 b) {
    if (!m_audioReady) { setTransportMessage("Audio is unavailable; connect JACK/PipeWire before using transport."); return false; }
    if (!m_engine->post({static_cast<EngineCommand::Type>(type), a, b})) {
        setTransportMessage("Audio command queue is full; try again."); return false;
    }
    return true;
}
void EngineController::seekBeat(double beat) {
    if (takeBusy() || !std::isfinite(beat) || beat < 0 || !m_project) return;
    const double samples = beat * 60 * m_project->project().tempo().sampleRate() / m_bpm;
    if (samples > double(INT64_MAX / 4)) return;
    postTransport(int(EngineCommand::Type::Seek), std::llround(samples));
}
bool EngineController::postRepeatLoop() {
    const double start=m_repeatEndBeat>m_repeatStartBeat ? m_repeatStartBeat : 0;
    const double end=m_repeatEndBeat>m_repeatStartBeat ? m_repeatEndBeat
        : (m_project ? std::max(m_project->contentEndBeats(),m_numerator*4.0/m_denominator) : 4);
    const double rate=m_engine->sampleRate()>1000 ? m_engine->sampleRate() : 48000;
    const double a=start*60*rate/m_bpm,b=end*60*rate/m_bpm;
    if (!std::isfinite(a) || !std::isfinite(b) || b>=double(INT64_MAX/4) || std::llround(b)<=std::llround(a)) return false;
    return postTransport(int(EngineCommand::Type::SetLoop),std::llround(a),std::llround(b));
}

bool EngineController::setRepeatRange(double startBeat, double endBeat) {
    if (takeBusy() || !std::isfinite(startBeat) || !std::isfinite(endBeat) || startBeat<0
        || (endBeat<=startBeat && (startBeat!=0 || endBeat!=0))) return false;
    const double samples=endBeat*60*(m_engine->sampleRate()>1000 ? m_engine->sampleRate() : 48000)/m_bpm;
    if (!std::isfinite(samples) || samples>=double(INT64_MAX/4)) return false;
    const double oldStart=m_repeatStartBeat,oldEnd=m_repeatEndBeat;
    m_repeatStartBeat=startBeat; m_repeatEndBeat=endBeat;
    if (m_repeating && !postRepeatLoop()) {
        m_repeatStartBeat=oldStart; m_repeatEndBeat=oldEnd; return false;
    }
    return true;
}

void EngineController::transportAction(const QString& action) {
    using Type = EngineCommand::Type;
    if (action == "stop") { postTransport(int(Type::Stop)); return; }
    if (action == "pause") { postTransport(int(Type::Pause)); return; }
    if (action == "play") { postTransport(int(Type::Play)); return; }
    if (action == "rewind") { seekBeat(0); return; }
    if (action == "forward") { seekBeat(positionBeats() + m_numerator * 4.0 / m_denominator); return; }
    if (action == "repeat") {
        if (takeBusy()) { setTransportMessage("Finish the take before changing repeat."); return; }
        if (m_repeating ? postTransport(int(Type::ClearLoop)) : postRepeatLoop()) {
            m_repeating = !m_repeating; emit transportChanged();
        }
        return;
    }
    if (action != "record") return;
    if (takeBusy()) { postTransport(int(Type::Pause)); return; }
    if (!m_audioReady || !m_inputConnected || !m_outputConnected || !m_project) {
        setTransportMessage("Recording needs a connected audio input and output."); return;
    }
    syncTrackEffects(); publishPlayback();
    if(!m_trackRoutingError.isEmpty()) { setTransportMessage(m_trackRoutingError); return; }
    std::vector<int> tracks;
    std::string eligibilityError;
    if (!RecordingController::validateEligibility(m_project->project(), m_audioReady, m_inputConnected, m_outputConnected, tracks, eligibilityError)) {
        setTransportMessage(QString::fromStdString(eligibilityError));
        return;
    }
    if(m_repeating) {
        if(!postTransport(int(Type::ClearLoop)))return;
        m_repeating=false;
    }
    const QString folder=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/Recordings";
    if(!QDir().mkpath(folder)) { setTransportMessage("Cannot create recordings folder."); return; }
    try {
        std::string msg;
        const double rate = m_engine->sampleRate() > 1000 ? m_engine->sampleRate() : 48000;
        // restMap G10: fixed for this recording, from what the device reports right now.
        m_recordingController.setLatencyCompensation(RecordingController::compensationSamples(
            recordingLatencyCompensation(), m_engine->latency().roundTripFrames, recordingExtraOffsetMs(), rate));
        bool ok = m_recordingController.startRecording(
            m_project->project(),
            folder.toStdString(),
            rate,
            m_countInEnabled,
            m_bpm,
            m_numerator,
            m_denominator,
            [this](int id) { return m_trackEffects.at(id).inputChannel; },
            [this](const EngineCommand& cmd) { return m_engine->post(cmd); },
            [this](const std::vector<int>& locked) {
                QSet<int> qlocked(locked.begin(), locked.end());
                m_project->lockRecordingTracks(qlocked);
            },
            msg);
        if (!ok) {
            setTransportMessage(QString::fromStdString(msg));
            return;
        }
        m_recordingController.setRecordStartBeat(positionBeats());
        setTransportMessage(QString::fromStdString(msg));
        emit transportChanged();
    } catch(const std::exception& error) { setTransportMessage(QString::fromUtf8(error.what())); }
}
QVariantList EngineController::monitoredTracks() const {
    QVariantList result;
    if(m_monitorEnabled && m_project)for(const auto& track:m_project->project().tracks()) {
        const auto it=m_trackEffects.find(track->id);
        if(track->hasLiveInput() && track->monitorEnabled && it!=m_trackEffects.end() && it->second.inputChannel>=0)result.push_back(track->id);
    }
    return result;
}
QVariantList EngineController::recordingWaveform(int trackId, int bins, double from, double to) const {
    QVariantList result;
    for (const auto& [low, high] : m_recordingController.livePeaks(trackId, std::clamp(bins, 0, 20000), from, to))
        result.push_back(QVariantList{low, high});
    return result;
}
QVariantList EngineController::recordingTracks() const {
    QVariantList result;
    for (int id : m_recordingController.activeRecordingTrackIds()) {
        result.push_back(id);
    }
    return result;
}
void EngineController::pollTransport() {
    if (m_recordingController.countInMessagePending() && recording()) {
        m_recordingController.clearCountInMessagePending();
        setTransportMessage(QStringLiteral("Recording %1 track(s)…").arg(m_recordingController.takeCount()));
    }
    bool done = m_recordingController.poll(
        recording(),
        m_engine->recordingStart(),
        m_project ? m_project->project().tempo() : TempoMap{},
        [this](int id, int64_t start, std::shared_ptr<const AudioData> audio, bool printed) {
            if (m_project) m_project->attachTake(id, start, audio, printed);
        },
        [this](int id, int64_t start, int64_t length, std::vector<MidiNote> notes) {
            if (m_project) m_project->attachMidiTake(id, start, length, std::move(notes));
        });
    if (done) completeTake();
    emit transportChanged();
}
void EngineController::completeTake() {
    if(!takeBusy())return;
    auto result = m_recordingController.completeTake(
        [this](int id, int64_t start, std::shared_ptr<const AudioData> audio, bool printed) {
            if (m_project) m_project->attachTake(id, start, audio, printed);
        },
        [this](int id, int64_t start, int64_t length, std::vector<MidiNote> notes) {
            if (m_project) m_project->attachMidiTake(id, start, length, std::move(notes));
        },
        [this]() {
            if (m_project) m_project->lockRecordingTrack(-1);
        });
    setTransportMessage(QString::fromStdString(result.message));
    syncTrackEffects(); publishPlayback(); emit transportChanged(); saveSession();
}

void EngineController::setProjectDirty(bool dirty) {
    if(m_projectSession.isDirty()==dirty)return;
    m_projectSession.setDirty(dirty); emit projectDirtyChanged();
}
QJsonObject EngineController::projectDocument() {
    std::vector<std::string> effects;
    for(const auto& track:m_project->project().tracks()) {
        auto* fx=qobject_cast<GuitarSuiteModel*>(trackEffects(track->id));
        effects.push_back(fx ? fx->sessionDataStd() : std::string());
    }
    const std::string jsonStr = persistence::ProjectDocument::serialize(
        m_project->project(), m_project->mediaPaths(), effects,
        persistence::ProjectDocument::projectFormat, persistence::ProjectDocument::currentVersion);
    return QJsonDocument::fromJson(QByteArray::fromStdString(jsonStr)).object();
}
bool EngineController::saveProject(const QUrl& file) {
    const QUrl target=file.isEmpty()?projectFile():file;
    if(!m_projectSession.canPerformProjectActions(takeBusy(), projectLoading(), effectsBusy()) || !m_project) {
        setTransportMessage("Finish recording or loading effects before saving a project."); return false;
    }
    if(!target.isLocalFile()) { setTransportMessage("Choose a local project file."); return false; }
    std::vector<std::string> effects;
    for(const auto& track:m_project->project().tracks()) {
        auto* fx=qobject_cast<GuitarSuiteModel*>(trackEffects(track->id));
        effects.push_back(fx ? fx->sessionDataStd() : std::string());
    }
    std::string err, warning;
    const std::string localPath = QFileInfo(target.toLocalFile()).absoluteFilePath().toStdString();
    if(!m_projectSession.saveProjectFile(localPath, m_project->project(), m_project->mediaPaths(), effects, &err, &warning)) {
        setTransportMessage("Could not save project: "+QString::fromStdString(err)); return false;
    }
    emit projectFileChanged(); emit projectDirtyChanged();
    setTransportMessage(warning.empty() ? QStringLiteral("Project saved.") : "Project saved. "+QString::fromStdString(warning));
    return true;
}
void EngineController::cancelProjectLoad() {
    for(auto* model:m_loadingEffects)delete model;
    m_loadingEffects.clear(); m_loadingProject.reset(); emit projectLoadingChanged();
}
bool EngineController::openProject(const QUrl& file) {
    if(!m_project || takeBusy() || projectLoading() || effectsBusy()) { setTransportMessage("Finish recording or loading effects before opening a project."); return false; }
    if(!file.isLocalFile()) { setTransportMessage("Choose a local project file."); return false; }
    persistence::ProjectFileContent content;
    std::string error;
    const double sampleRate = m_engine->sampleRate() > 1000 ? m_engine->sampleRate() : 48000;
    if(!persistence::ProjectDocument::loadFromFile(file.toLocalFile().toStdString(), content, error, sampleRate)) {
        setTransportMessage(QString::fromStdString(error.empty() ? "Invalid project file; current project kept." : error)); return false;
    }
    auto candidate=std::make_unique<TrackListWrapper>();
    candidate->setSampleRate(sampleRate);
    candidate->replaceDocument(std::move(content.project), std::move(content.audio), std::move(content.mediaPaths));
    m_loadingWarning = QString::fromStdString(content.warning);
    m_loadingProject=std::move(candidate); m_loadingFile=file; emit projectLoadingChanged();
    for(int row=0;row<m_loadingProject->count();++row) {
        auto* fx=new GuitarSuiteModel(this); m_loadingEffects.push_back(fx);
        fx->configure(sampleRate,std::max(128,m_engine->latency().captureFrames));
        connect(fx,&GuitarSuiteModel::busyChanged,this,[this] { QTimer::singleShot(0,this,[this]{finishProjectLoad();}); });
        const auto& state = content.trackEffectsJson[row];
        if(!state.empty() && !fx->restoreSessionData(state)) { // empty: pedalboard dropped by the document
            const auto message=fx->lastError(); cancelProjectLoad(); setTransportMessage(message+" · current project kept."); return false;
        }
    }
    finishProjectLoad(); return true;
}
void EngineController::finishProjectLoad() {
    if(!m_loadingProject)return;
    for(auto* model:m_loadingEffects)if(model->busy())return;
    for(auto* model:m_loadingEffects)if(!model->lastError().isEmpty()) {
        const auto message=model->lastError(); cancelProjectLoad(); setTransportMessage(message+" · current project kept."); return;
    }
    if(!m_project || !m_engine->post({EngineCommand::Type::Stop})) { cancelProjectLoad(); setTransportMessage("Could not switch projects; try again."); return; }
    m_restoring=true; clearTrackEffects();
    for(int row=0;row<m_loadingProject->count();++row) {
        auto* fx=m_loadingEffects[row]; disconnect(fx,nullptr,this,nullptr);
        bindTrackEffects(m_loadingProject->trackIdAt(row),fx);
    }
    m_loadingEffects.clear();
    m_project->adoptDocument(*m_loadingProject);
    m_numerator=m_project->project().tempo().numerator(); m_denominator=m_project->project().tempo().denominator();
    setBpm(m_project->project().tempo().bpm()); emit meterChanged();
    m_repeating=false; m_repeatStartBeat=0; m_repeatEndBeat=0; m_engine->post({EngineCommand::Type::ClearLoop});
    m_projectSession.setCurrentFilePath(m_loadingFile.isLocalFile() ? QFileInfo(m_loadingFile.toLocalFile()).absoluteFilePath().toStdString() : "");
    m_loadingProject.reset(); m_sessionSaveBlocked=false;
    m_restoringPreset=false; m_restoring=false; refreshProject(); setProjectDirty(false);
    emit projectFileChanged(); emit projectLoadingChanged();
    setTransportMessage(m_loadingWarning.isEmpty()?"Project opened.":m_loadingWarning);
    emit transportChanged();
}
bool EngineController::newProject() {
    if(!m_project || takeBusy() || projectLoading() || effectsBusy()) { setTransportMessage("Finish recording or loading effects before creating a project."); return false; }
    m_loadingProject=std::make_unique<TrackListWrapper>();
    m_loadingProject->setSampleRate(m_engine->sampleRate()>1000?m_engine->sampleRate():48000);
    m_loadingFile=QUrl(); m_loadingWarning.clear(); finishProjectLoad(); return !projectLoading();
}

bool EngineController::saveSession() {
    if (!m_projectSession.canSaveSession(takeBusy(), projectLoading(), m_restoring, m_sessionSaveBlocked) || !m_project || !restoreSessionOnStartup()) return false;
    m_projectSession.setSessionFilePath(sessionPath().toStdString());
    std::vector<std::string> effects;
    for (const auto& track : m_project->project().tracks()) {
        auto* fx = qobject_cast<GuitarSuiteModel*>(trackEffects(track->id));
        effects.push_back(fx ? fx->sessionDataStd() : std::string());
    }
    std::string err;
    if (!m_projectSession.saveSession(m_project->project(), m_project->mediaPaths(), effects, &err)) {
        setTransportMessage("Could not save startup session."); return false;
    }
    return true;
}
void EngineController::shutdownSession() {
    if (m_shutdown) return;
    m_shutdown = true; m_snapshotTimer.stop(); m_sessionTimer.stop(); m_meterTimer.stop();
    m_engine->stop(); // Quiesce JACK before releasing the recorder or its ring.
    if(takeBusy()) {
        m_recordingController.shutdown(
            [this](int id, int64_t start, std::shared_ptr<const AudioData> audio, bool printed) {
                if (m_project) m_project->attachTake(id, start, audio, printed);
            },
            [this](int id, int64_t start, int64_t length, std::vector<MidiNote> notes) {
                if (m_project) m_project->attachMidiTake(id, start, length, std::move(notes));
            },
            [this]() {
                if (m_project) m_project->lockRecordingTrack(-1);
            });
    }
    saveSession();
}

void EngineController::sendLiveNoteOn(int trackId, int note, int velocity) {
    if (!m_engine || note < 0 || note > 127) return;
    const int vel = std::clamp(velocity, 0, 127);
    EngineCommand cmd;
    cmd.type = EngineCommand::Type::NoteFromUi;
    cmd.a = trackId;
    cmd.b = (std::int64_t(0x90) << 16) | (std::int64_t(note & 0x7F) << 8) | vel;
    m_engine->post(cmd);
}

void EngineController::sendLiveNoteOff(int trackId, int note) {
    if (!m_engine || note < 0 || note > 127) return;
    EngineCommand cmd;
    cmd.type = EngineCommand::Type::NoteFromUi;
    cmd.a = trackId;
    cmd.b = (std::int64_t(0x80) << 16) | (std::int64_t(note & 0x7F) << 8) | 0;
    m_engine->post(cmd);
}

void EngineController::sendLiveControl(int trackId, int controller, int value) {
    if (!m_engine || controller < 0 || controller > 127) return;
    EngineCommand cmd;
    cmd.type = EngineCommand::Type::NoteFromUi;
    cmd.a = trackId;
    cmd.b = (std::int64_t(0xB0) << 16) | (std::int64_t(controller & 0x7F) << 8) | std::clamp(value, 0, 127);
    m_engine->post(cmd);
}

QStringList EngineController::midiInputPorts() const {
    QStringList result;
    if (m_engine) {
        for (const auto& port : m_engine->midiInputNames()) {
            result.push_back(QString::fromStdString(port));
        }
    }
    return result;
}

QString EngineController::connectedMidiPort() const {
    return m_engine ? QString::fromStdString(m_engine->connectedMidiInput()) : QString{};
}

void EngineController::refreshMidiInputs() {
    if (!m_engine) return;
    m_engine->refreshMidiInputs();
    emit midiInputPortsChanged();
}

bool EngineController::connectMidiInput(const QString& portName) {
    if (!m_engine) return false;
    const bool ok = m_engine->connectMidiInput(portName.toStdString());
    emit midiInputPortsChanged();
    return ok;
}

// ---------------------------------------------------------------- FX chain files
namespace {

QVariantMap chainResult(bool ok, const QString& error, const std::vector<std::string>& kept = {},
                        const std::vector<std::string>& skipped = {}, const std::string& trackType = {})
{
    QStringList keptList, skippedList;
    for (const auto& t : kept) keptList << QString::fromStdString(t);
    for (const auto& t : skipped) skippedList << QString::fromStdString(t);
    return {{QStringLiteral("ok"), ok}, {QStringLiteral("error"), error}, {QStringLiteral("kept"), keptList},
            {QStringLiteral("skipped"), skippedList}, {QStringLiteral("trackType"), QString::fromStdString(trackType)}};
}

// Reads and checks a chain file; false with `error` set.
bool readChainFile(const QUrl& file, persistence::chainjson::ChainFile& chain, QString& error)
{
    if (!file.isLocalFile()) { error = QStringLiteral("Choose a local FX chain file."); return false; }
    QFile in(file.toLocalFile());
    if (!in.open(QIODevice::ReadOnly)) { error = QStringLiteral("Could not open the FX chain file."); return false; }
    if (in.size() > qint64(persistence::chainjson::MaxFileSize)) { error = QStringLiteral("The FX chain file is too large."); return false; }
    const QByteArray bytes = in.readAll();
    const auto json = nlohmann::json::parse(bytes.constData(), bytes.constData() + bytes.size(), nullptr, false);
    std::string why;
    if (json.is_discarded() || !persistence::chainjson::fromJson(json, chain, why)) {
        error = why.empty() ? QStringLiteral("Not an FX chain file.") : QString::fromStdString(why);
        return false;
    }
    return true;
}

} // namespace

QVariantMap EngineController::previewEffectChain(int trackId, const QUrl& file)
{
    const auto* track = m_project ? m_project->project().findTrack(trackId) : nullptr;
    if (!track) return chainResult(false, QStringLiteral("Pick a track first."));
    persistence::chainjson::ChainFile chain;
    QString error;
    if (!readChainFile(file, chain, error)) return chainResult(false, error);
    persistence::chainjson::ImportPlan plan;
    std::string why;
    if (!persistence::chainjson::planImport(*track, chain, plan, why)) return chainResult(false, QString::fromStdString(why));
    return chainResult(true, {}, plan.kept, plan.skipped, chain.trackType);
}

QVariantMap EngineController::importEffectChain(int trackId, const QUrl& file)
{
    const auto* track = m_project ? m_project->project().findTrack(trackId) : nullptr;
    if (!track) return chainResult(false, QStringLiteral("Pick a track first."));
    if (m_project->domain().trackLocked(trackId) || takeBusy())
        return chainResult(false, QStringLiteral("Finish recording before changing the effects."));
    persistence::chainjson::ChainFile chain;
    QString error;
    if (!readChainFile(file, chain, error)) return chainResult(false, error);
    persistence::chainjson::ImportPlan plan;
    std::string why;
    if (!persistence::chainjson::planImport(*track, chain, plan, why)) return chainResult(false, QString::fromStdString(why));
    auto* pedals = plan.guitarSuite ? qobject_cast<GuitarSuiteModel*>(trackEffects(trackId)) : nullptr;
    if (pedals && pedals->busy()) return chainResult(false, QStringLiteral("GuitarSuite is still loading; try again in a moment."));

    // The chain: the effects the user can add go, the file's come in, then the order.
    std::vector<int> removable;
    for (const auto& slot : track->effects) if (effectchain::canRemove(slot.type)) removable.push_back(slot.id);
    for (int id : removable) m_project->removeTrackEffect(trackId, id);
    for (const auto& type : plan.addTypes) m_project->addTrackEffect(trackId, QString::fromStdString(type));
    for (int index = 0; index < int(plan.finalOrder.size()); ++index) {
        const auto* now = m_project->project().findTrack(trackId);
        if (!now) break;
        for (const auto& slot : now->effects) {
            if (slot.type != plan.finalOrder[index]) continue;
            m_project->moveTrackEffect(trackId, slot.id, index);
            break;
        }
    }
    // The settings.
    if (plan.eqGainsDb) for (int band = 0; band < int(plan.eqGainsDb->size()); ++band) m_project->setEqBand(trackId, band, (*plan.eqGainsDb)[band]);
    if (plan.chaosEq) m_project->setChaosEq(trackId, *plan.chaosEq);
    if (plan.harmonium) m_project->setHarmonium(trackId, *plan.harmonium);
    if (plan.drumkit) m_project->setDrumKit(trackId, *plan.drumkit);
    if (plan.electrodrums) m_project->setElectroDrums(trackId, *plan.electrodrums);
    if (plan.piano) m_project->setPiano(trackId, *plan.piano);
    if (plan.chords) m_project->setChords(trackId, *plan.chords);
    for (const auto& [type, settings] : plan.mixEffects) m_project->setMixEffect(trackId, QString::fromStdString(type), settings);
    if (pedals && plan.guitarSuite && !pedals->restoreSessionData(QByteArray::fromStdString(plan.guitarSuite->dump())))
        return chainResult(false, QStringLiteral("The effects were replaced, but the GuitarSuite pedalboard in the file did not load."),
                           plan.kept, plan.skipped, chain.trackType);
    setTransportMessage(QStringLiteral("FX chain imported."));
    return chainResult(true, {}, plan.kept, plan.skipped, chain.trackType);
}

QVariantMap EngineController::exportEffectChain(int trackId, const QUrl& file)
{
    const auto* track = m_project ? m_project->project().findTrack(trackId) : nullptr;
    if (!track) return chainResult(false, QStringLiteral("Pick a track first."));
    if (!file.isLocalFile()) return chainResult(false, QStringLiteral("Choose a local file to export to."));
    nlohmann::json pedals = nullptr;
    const bool hasSuite = std::any_of(track->effects.begin(), track->effects.end(),
                                      [](const TrackEffectSlot& s) { return s.type == effectchain::GuitarSuite; });
    if (hasSuite) {
        auto* model = qobject_cast<GuitarSuiteModel*>(trackEffects(trackId));
        if (model && model->busy()) return chainResult(false, QStringLiteral("GuitarSuite is still loading; try again in a moment."));
        if (model) {
            const std::string data = model->sessionDataStd();
            pedals = nlohmann::json::parse(data, nullptr, false);
            if (pedals.is_discarded() || !pedals.is_object()) pedals = nullptr;
        }
    }
    const auto chain = persistence::chainjson::capture(*track, pedals);
    QSaveFile out(file.toLocalFile());
    const std::string text = persistence::chainjson::toJson(chain).dump(2) + "\n";
    if (!out.open(QIODevice::WriteOnly) || out.write(text.data(), qint64(text.size())) != qint64(text.size()) || !out.commit())
        return chainResult(false, QStringLiteral("Could not write the FX chain file."));
    std::vector<std::string> titles;
    for (const auto& e : chain.effects) {
        const auto* def = effectchain::find(e.type);
        titles.push_back(def ? std::string(def->title) : e.type);
    }
    setTransportMessage(QStringLiteral("FX chain exported."));
    return chainResult(true, {}, titles, {}, chain.trackType);
}
