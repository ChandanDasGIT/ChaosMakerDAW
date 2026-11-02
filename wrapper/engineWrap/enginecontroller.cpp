#include "enginecontroller.h"

#include "core/version.h"
#include "audiodevice/jackaudiodevice.h"
#include "engine/audioengine.h"
#include "engine/enginecommands.h"
#include "engine/enginesnapshot.h"
#include "guitarsuitemodel.h"
#include "plugins/guitarsuite/guitarsuite.h"
#include "processor/trackinput.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJSEngine>
#include <QProcess>
#include <QQmlEngine>
#include <algorithm>
#include <cmath>
#include <memory>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QUuid>

EngineController::EngineController(QObject* parent)
    : EngineController(std::make_unique<JackAudioDevice>(), true, parent) {}

EngineController::EngineController(std::unique_ptr<AudioDevice> device, bool discoverSystemDevices, QObject* parent)
    : QObject(parent)
{
    m_engine = std::make_unique<AudioEngine>(std::move(device));
    if (m_engine->start(128)) {
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
            trackLevels[key]=levels;
        }
        m_trackLevels=trackLevels;
        emit trackLevelsChanged();
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
    m_sessionTimer.setSingleShot(true);
    m_sessionTimer.setInterval(1500);
    connect(&m_sessionTimer, &QTimer::timeout, this, &EngineController::saveSession);
    watchEffects(m_guitarSuite);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &EngineController::shutdownSession);
}

EngineController::~EngineController()
{
    m_engine->stop();
    if (m_take) { m_take->finish(); m_take->wait(); }
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
    auto snapshot = std::make_unique<EngineSnapshot>();
    snapshot->masterChain.push_back(chain.get());
    snapshot->keepProcessors.push_back(std::move(chain));
    m_engine->publishMonitor(std::move(snapshot));
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
    if (!m_project || !m_project->project().findTrack(trackId)) return nullptr;
    const auto existing=m_trackEffects.find(trackId);
    if(existing!=m_trackEffects.end())return existing->second.model;
    bindTrackEffects(trackId,new GuitarSuiteModel(this));
    return m_trackEffects.at(trackId).model;
}

void EngineController::bindTrackEffects(int trackId, GuitarSuiteModel* model)
{
    auto& fx=m_trackEffects[trackId];
    fx.model=model;
    fx.input = std::make_shared<TrackInput>();
    fx.playbackInput = std::make_shared<TrackInput>();
    const double rate = m_engine->sampleRate() > 1000 ? m_engine->sampleRate() : 48000;
    const int frames = std::max(128, m_engine->latency().captureFrames);
    fx.input->setTrimDb(fx.model->inputTrimDb());
    fx.input->prepare(rate, frames);
    fx.playbackInput->setTrimDb(fx.model->inputTrimDb()); fx.playbackInput->prepare(rate,frames);
    fx.model->configure(rate, frames);
    connect(fx.model, &GuitarSuiteModel::inputTrimDbChanged, this, [this, trackId] {
        auto& fx = m_trackEffects.at(trackId);
        fx.input->setTrimDb(fx.model->inputTrimDb());
        fx.playbackInput->setTrimDb(fx.model->inputTrimDb());
    });
    watchEffects(fx.model);
    fx.model->setPublisher([this, trackId](std::shared_ptr<GuitarSuite> chain) {
        m_trackEffects.at(trackId).chain = std::move(chain);
        if (m_monitoredTrack == trackId) syncTrackEffects();
        if (!m_restoring) { m_sessionTimer.start(); setProjectDirty(true); }
    });
    fx.model->setPlaybackPublisher([this,trackId](std::shared_ptr<GuitarSuite> chain) {
        m_trackEffects.at(trackId).playbackChain=std::move(chain);
        if(!m_restoring)publishPlayback();
    });
    QQmlEngine::setObjectOwnership(fx.model, QQmlEngine::CppOwnership);
}

void EngineController::clearTrackEffects()
{
    m_monitoredTrack = -1;
    emit monitoredTrackChanged();
    for (auto& [id, fx] : m_trackEffects) delete fx.model;
    m_trackEffects.clear();
    m_trackLevels.clear(); emit trackLevelsChanged();
    m_engine->publishMonitor(std::make_unique<EngineSnapshot>());
}

void EngineController::syncTrackEffects()
{
    if (!m_project) return;
    for (const auto& track : m_project->project().tracks()) {
        trackEffects(track->id);
        auto& fx = m_trackEffects.at(track->id);
        for (int band = 0; band < 10; ++band) {
            fx.input->setEqBand(band, track->eqGainDb[band]);
            fx.playbackInput->setEqBand(band,track->eqGainDb[band]);
        }
    }
    for (auto it = m_trackEffects.begin(); it != m_trackEffects.end();) {
        if (!m_project->project().findTrack(it->first)) {
            delete it->second.model;
            it = m_trackEffects.erase(it);
        } else ++it;
    }
    const int previousTrack = monitoredTrackId();
    // Keep the recording's chain pinned until its final block has been released.
    m_monitoredTrack = m_take ? m_recordTrack : m_project->recordingTrackId();
    const auto* track = m_project->project().findTrack(m_monitoredTrack);
    auto snapshot = std::make_unique<EngineSnapshot>();
    if (track) {
        const auto& fx = m_trackEffects.at(track->id);
        snapshot->monitorMeters=fx.meters;
        snapshot->inputStage = fx.input.get();
        snapshot->keepProcessors.push_back(fx.input);
        if (fx.chain) {
            snapshot->masterChain.push_back(fx.chain.get());
            snapshot->keepProcessors.push_back(fx.chain);
        }
        if (!m_take && (m_monitorInputOverride != QString::fromStdString(track->inputPort) || !m_inputConnected))
            setCleanMonitorInput(QString::fromStdString(track->inputPort));
        const bool anySolo = std::any_of(m_project->project().tracks().begin(), m_project->project().tracks().end(),
            [](const auto& other) { return other->solo; });
        setCleanMonitorGain(track->muted || (anySolo && !track->solo) ? 0 : track->volume);
    }
    m_engine->publishMonitor(std::move(snapshot));
    const bool monitor = track && track->recordArmed;
    if (monitor != m_monitorEnabled) setCleanMonitorEnabled(monitor);
    if (previousTrack != monitoredTrackId()) emit monitoredTrackChanged();
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
    if (m_take) return false;
    if (!m_engine || !m_audioReady || index < 0 || index >= m_inputPorts.size())
        return false;
    const QString source = m_inputPorts[index];
    m_selectedInput = source;
    m_selectedInputIndex = index;
    emit audioRoutingChanged();
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
        fx.model->configure(m_engine->sampleRate(), frames);
    }
    syncTrackEffects();
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
    if (m_take) return false;
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
    const DeviceLatency ioLatency = m_engine->latency();
    const double sampleRate = m_engine->sampleRate();
    const double blockMilliseconds = sampleRate > 0.0
        ? 1000.0 * static_cast<double>(ioLatency.captureFrames) / sampleRate : 0.0;
    QString bufferStatus = QStringLiteral(" · JACK block %1 frames (%2 ms)")
                               .arg(ioLatency.captureFrames)
                               .arg(blockMilliseconds, 0, 'f', 2);
    if (ioLatency.captureFrames != 128)
        bufferStatus += QStringLiteral(" (128 requested)");
    if (!m_connectionError.isEmpty()) {
        setStatus(m_connectionError);
    } else if (m_monitorEnabled && m_inputConnected && m_outputConnected) {
        setStatus(QStringLiteral("Clean monitor · %1 → %2")
                      .arg(m_activeInput, m_selectedOutputLeft) + bufferStatus);
    } else if (m_monitorEnabled) {
        setStatus(QStringLiteral("Monitor armed · select a connected input and output.") + bufferStatus);
    } else if (m_inputConnected && m_outputConnected) {
        setStatus(QStringLiteral("Audio I/O connected · %1 → %2")
                      .arg(m_activeInput, m_selectedOutputLeft) + bufferStatus);
    } else {
        setStatus(QStringLiteral("JACK is running · choose an input and output port.") + bufferStatus);
    }
}

void EngineController::setStatus(const QString& status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void EngineController::setBpm(double bpm)
{
    if (!m_engine || !m_engine->setTempoBpm(bpm) || m_bpm == bpm) return;
    m_bpm = bpm;
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
void EngineController::setTransportMessage(const QString& message) {
    if (m_transportMessage == message) return;
    m_transportMessage = message; emit transportMessageChanged();
}
void EngineController::attachProject(TrackListModel* model) {
    if (!model || m_project == model) return;
    if (m_project) disconnect(m_project, nullptr, this, nullptr);
    clearTrackEffects();
    m_restoringPreset = false;
    m_sessionSaveBlocked = false;
    m_project = model;
    model->setSampleRate(m_engine->sampleRate() > 1000 ? m_engine->sampleRate() : 48000);
    m_restoring = true;
    if (restoreSessionOnStartup()) {
        QFile file(sessionPath());
        if (file.exists()) {
            if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) {
                setTransportMessage("Could not read last session."); m_sessionSaveBlocked = true;
            } else {
                QJsonParseError parse;
                const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
                QString error;
                if (parse.error != QJsonParseError::NoError || doc["format"].toString() != "ChaosMakerSession"
                    || (doc["version"].toInt() != 1 && doc["version"].toInt() != 2)
                    || (doc["version"].toInt() == 1 && !doc["guitarSuite"].isObject())
                    || (doc["version"].toInt() == 2 && (!doc["trackEffects"].isArray()
                        || doc["trackEffects"].toArray().size() != doc["project"]["tracks"].toArray().size()))
                    || !model->restoreSessionState(doc["project"].toObject(), error)) {
                    m_sessionSaveBlocked = true;
                    setTransportMessage(error.isEmpty() ? "Invalid saved session; original file kept." : error);
                } else {
                    m_numerator = model->project().tempo().numerator(); m_denominator = model->project().tempo().denominator();
                    setBpm(model->project().tempo().bpm()); emit meterChanged();
                    setTransportMessage(error);
                    m_sessionSaveBlocked = false;
                    const auto effects = doc["trackEffects"].toArray();
                    int row = 0;
                    for (const auto& track : model->project().tracks()) {
                        auto* fx = qobject_cast<GuitarSuiteModel*>(trackEffects(track->id));
                        const auto state = doc["version"].toInt() == 1 ? doc["guitarSuite"].toObject()
                            : effects[row].toObject();
                        if (!fx->restoreSessionData(QJsonDocument(state).toJson())) {
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
    }
    m_restoring = false;
    connect(model, &TrackListModel::projectChanged, this, &EngineController::refreshProject);
    refreshProject();
    setProjectDirty(model->count()>0);
}
void EngineController::refreshProject() {
    if (!m_project) return;
    if (m_take && m_project->recordingTrackId() != m_recordTrack)
        postTransport(int(EngineCommand::Type::StopRecording));
    syncTrackEffects();
    publishPlayback();
    if (!m_restoring) { m_sessionTimer.start(); setProjectDirty(true); }
}
void EngineController::publishPlayback() {
    if(!m_project)return;
    auto snapshot=m_project->playbackSnapshot();
    for(auto& track:snapshot->tracks) {
        const auto effects=m_trackEffects.find(track.trackId);
        if(effects!=m_trackEffects.end())track.meters=effects->second.meters;
        if(!std::any_of(track.clips.begin(),track.clips.end(),[](const auto& clip){return !clip.effectsPrinted;}))continue;
        const auto it=m_trackEffects.find(track.trackId); if(it==m_trackEffects.end())continue;
        const auto& fx=it->second;
        track.chain.push_back(fx.playbackInput.get()); snapshot->keepProcessors.push_back(fx.playbackInput);
        if(fx.playbackChain) { track.chain.push_back(fx.playbackChain.get()); snapshot->keepProcessors.push_back(fx.playbackChain); }
    }
    m_engine->publishPlayback(std::move(snapshot));
}

void EngineController::setMeter(int numerator, int denominator) {
    if (numerator <= 0 || denominator <= 0) return;
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
    if (m_take || !std::isfinite(beat) || beat < 0 || !m_project) return;
    const double samples = beat * 60 * m_project->project().tempo().sampleRate() / m_bpm;
    if (samples > double(INT64_MAX / 4)) return;
    postTransport(int(EngineCommand::Type::Seek), std::llround(samples));
}
void EngineController::transportAction(const QString& action) {
    using Type = EngineCommand::Type;
    if (action == "stop") { postTransport(int(Type::Stop)); return; }
    if (action == "pause") { postTransport(int(Type::Pause)); return; }
    if (action == "play") { postTransport(int(Type::Play)); return; }
    if (action == "rewind") { seekBeat(0); return; }
    if (action == "forward") { seekBeat(positionBeats() + m_numerator * 4.0 / m_denominator); return; }
    if (action == "repeat") {
        if (m_take) { setTransportMessage("Finish the take before changing repeat."); return; }
        const double end = m_project ? std::max(m_project->contentEndBeats(), m_numerator * 4.0 / m_denominator) : 4;
        const double rate = m_engine->sampleRate() > 1000 ? m_engine->sampleRate() : 48000;
        if (postTransport(int(m_repeating ? Type::ClearLoop : Type::SetLoop), 0, std::max<qint64>(1, std::llround(std::min(double(INT64_MAX / 4), end * 60 * rate / m_bpm))))) {
            m_repeating = !m_repeating; emit transportChanged();
        }
        return;
    }
    if (action != "record") return;
    if (m_take) { postTransport(int(Type::StopRecording)); return; }
    if (!m_audioReady || !m_inputConnected || !m_outputConnected || !m_project) {
        setTransportMessage("Recording needs a connected audio input and output."); return;
    }
    const int track = m_project->recordingTrackId();
    if (track < 0) { setTransportMessage("Arm exactly one Audio, Vocal, Acoustic Guitar or Electric Guitar track to record."); return; }
    if (m_repeating) {
        if (!postTransport(int(Type::ClearLoop))) return;
        m_repeating = false;
    }
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/Recordings";
    if (!QDir().mkpath(folder)) { setTransportMessage("Cannot create recordings folder."); return; }
    try {
        const QString path = folder + "/Take-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".wav";
        const auto* recordingTrack = m_project->project().findTrack(track);
        const bool withFx = recordingTrack->recordsWithFx();
        m_take = std::make_unique<TakeRecorder>(path.toStdString(), m_engine->sampleRate(),
            withFx ? TakeRecorder::Source::PostFx : TakeRecorder::Source::DryInput);
        m_engine->clearReleasedRecorder();
        EngineCommand command; command.type = Type::StartRecording; command.recorder = m_take.get();
        if (!m_engine->post(command)) { m_take.reset(); setTransportMessage("Could not start recording; audio queue is full."); return; }
        m_recordTrack = track; m_recordStartBeat = positionBeats(); m_project->lockRecordingTrack(track);
        setTransportMessage(withFx
            ? QStringLiteral("Recording %1 with FX…").arg(QString::fromStdString(recordingTrack->name))
            : QStringLiteral("Recording dry input · playback FX stay editable…")); emit transportChanged();
    } catch (const std::exception& e) { setTransportMessage(QString::fromUtf8(e.what())); }
}
void EngineController::pollTransport() {
    if (recording() && m_project)
        m_recordStartBeat = m_project->project().tempo().beatAt(m_engine->recordingStart());
    if (m_take && m_engine->releasedRecorder() == m_take.get() && m_take->done()) completeTake();
    // Disarming ends the take on the next audio block.
    if (m_take && m_project && m_project->recordingTrackId() != m_recordTrack && recording())
        postTransport(int(EngineCommand::Type::StopRecording));
    emit transportChanged();
}
void EngineController::completeTake() {
    if (!m_take) return;
    const auto audio = m_take->audio();
    const QString error = QString::fromStdString(m_take->error());
    if (m_project) {
        m_project->lockRecordingTrack(-1);
        if (audio && audio->frames > 0) m_project->attachTake(m_recordTrack, m_take->startSample, audio, m_take->source()==TakeRecorder::Source::PostFx);
    }
    setTransportMessage(!error.isEmpty() ? error : audio && audio->frames > 0
        ? QStringLiteral("Recorded %1 seconds · %2").arg(audio->frames / audio->sampleRate, 0, 'f', 2).arg(QString::fromStdString(audio->path))
        : QStringLiteral("Take ended before any audio was captured."));
    m_take.reset(); m_recordTrack = -1;
    syncTrackEffects();
    emit transportChanged();
    saveSession();
}
void EngineController::setProjectDirty(bool dirty) {
    if(m_projectDirty==dirty)return;
    m_projectDirty=dirty; emit projectDirtyChanged();
}
QJsonObject EngineController::projectDocument() {
    QJsonArray effects;
    for(const auto& track:m_project->project().tracks()) {
        auto* fx=qobject_cast<GuitarSuiteModel*>(trackEffects(track->id));
        effects.append(QJsonDocument::fromJson(fx->sessionData()).object());
    }
    return {{"format","ChaosMakerProject"},{"version",2},{"project",m_project->sessionState()},{"trackEffects",effects}};
}
bool EngineController::saveProject(const QUrl& file) {
    const QUrl target=file.isEmpty()?m_projectFile:file;
    if(!m_project || m_take || projectLoading() || effectsBusy()) { setTransportMessage("Finish recording or loading effects before saving a project."); return false; }
    if(!target.isLocalFile()) { setTransportMessage("Choose a local project file."); return false; }
    QSaveFile output(target.toLocalFile()); const auto bytes=QJsonDocument(projectDocument()).toJson();
    if(!output.open(QIODevice::WriteOnly) || output.write(bytes)!=bytes.size() || !output.commit()) {
        setTransportMessage("Could not save project: "+output.errorString()); return false;
    }
    m_projectFile=QUrl::fromLocalFile(QFileInfo(target.toLocalFile()).absoluteFilePath());
    emit projectFileChanged(); setProjectDirty(false); setTransportMessage("Project saved."); return true;
}
void EngineController::cancelProjectLoad() {
    for(auto* model:m_loadingEffects)delete model;
    m_loadingEffects.clear(); m_loadingProject.reset(); emit projectLoadingChanged();
}
bool EngineController::openProject(const QUrl& file) {
    if(!m_project || m_take || projectLoading() || effectsBusy()) { setTransportMessage("Finish recording or loading effects before opening a project."); return false; }
    if(!file.isLocalFile()) { setTransportMessage("Choose a local project file."); return false; }
    QFile input(file.toLocalFile());
    if(!input.open(QIODevice::ReadOnly) || input.size()>16*1024*1024) { setTransportMessage("Cannot read project file."); return false; }
    QJsonParseError error; const auto doc=QJsonDocument::fromJson(input.readAll(),&error).object();
    const int version=doc["version"].toInt();
    if(error.error!=QJsonParseError::NoError || (doc["format"]!="ChaosMakerProject" && doc["format"]!="ChaosMakerSession")
        || (version!=1 && version!=2) || (version==1 && !doc["guitarSuite"].isObject())
        || (version==2 && (!doc["trackEffects"].isArray() || doc["trackEffects"].toArray().size()!=doc["project"].toObject()["tracks"].toArray().size()))) {
        setTransportMessage("Invalid project file; current project kept."); return false;
    }
    auto candidate=std::make_unique<TrackListModel>();
    candidate->setSampleRate(m_engine->sampleRate()>1000?m_engine->sampleRate():48000);
    if(!candidate->restoreSessionState(doc["project"].toObject(),m_loadingWarning)) { setTransportMessage(m_loadingWarning); return false; }
    m_loadingProject=std::move(candidate); m_loadingFile=file; emit projectLoadingChanged();
    for(int row=0;row<m_loadingProject->count();++row) {
        auto* fx=new GuitarSuiteModel(this); m_loadingEffects.push_back(fx);
        fx->configure(m_engine->sampleRate()>1000?m_engine->sampleRate():48000,std::max(128,m_engine->latency().captureFrames));
        connect(fx,&GuitarSuiteModel::busyChanged,this,[this] { QTimer::singleShot(0,this,[this]{finishProjectLoad();}); });
        const auto state=version==1 ? doc["guitarSuite"].toObject() : doc["trackEffects"].toArray()[row].toObject();
        if(!fx->restoreSessionData(QJsonDocument(state).toJson())) {
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
    m_repeating=false; m_engine->post({EngineCommand::Type::ClearLoop});
    m_projectFile=m_loadingFile; m_loadingProject.reset(); m_sessionSaveBlocked=false;
    m_restoringPreset=false; m_restoring=false; refreshProject(); setProjectDirty(false);
    emit projectFileChanged(); emit projectLoadingChanged();
    setTransportMessage(m_loadingWarning.isEmpty()?"Project opened.":m_loadingWarning);
    emit transportChanged();
}
bool EngineController::newProject() {
    if(!m_project || m_take || projectLoading() || effectsBusy()) { setTransportMessage("Finish recording or loading effects before creating a project."); return false; }
    m_loadingProject=std::make_unique<TrackListModel>();
    m_loadingProject->setSampleRate(m_engine->sampleRate()>1000?m_engine->sampleRate():48000);
    m_loadingFile=QUrl(); m_loadingWarning.clear(); finishProjectLoad(); return !projectLoading();
}

bool EngineController::saveSession() {
    if (!restoreSessionOnStartup() || !m_project || m_restoring || m_sessionSaveBlocked || projectLoading() || effectsBusy()) return false;
    const QString path = sessionPath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) { setTransportMessage("Could not create session folder."); return false; }
    QSaveFile file(path);
    QJsonArray effects;
    for (const auto& track : m_project->project().tracks()) {
        auto* fx = qobject_cast<GuitarSuiteModel*>(trackEffects(track->id));
        effects.append(QJsonDocument::fromJson(fx->sessionData()).object());
    }
    const auto data = QJsonDocument(QJsonObject{{"format", "ChaosMakerSession"}, {"version", 2},
        {"project", m_project->sessionState()}, {"trackEffects", effects}}).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        setTransportMessage("Could not save startup session."); return false;
    }
    return true;
}
void EngineController::shutdownSession() {
    if (m_shutdown) return;
    m_shutdown = true; m_snapshotTimer.stop(); m_sessionTimer.stop();
    m_engine->stop(); // Quiesce JACK before releasing the recorder or its ring.
    if (m_take) { m_take->finish(); m_take->wait(); completeTake(); }
    saveSession();
}
