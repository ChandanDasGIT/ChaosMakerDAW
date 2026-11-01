#include "enginecontroller.h"

#include "core/version.h"
#include "audiodevice/jackaudiodevice.h"
#include "engine/audioengine.h"
#include "engine/enginecommands.h"
#include "engine/enginesnapshot.h"
#include "guitarsuitemodel.h"
#include "plugins/guitarsuite/guitarsuite.h"

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
        m_engine->setInputTrimDb(m_guitarSuite->inputTrimDb());
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
        if (m_pendingGuitarSnapshot)
            m_engine->tryPublish(m_pendingGuitarSnapshot);
        pollTransport();
    });
    m_snapshotTimer.start(20);
    m_sessionTimer.setSingleShot(true);
    m_sessionTimer.setInterval(1500);
    connect(&m_sessionTimer, &QTimer::timeout, this, &EngineController::saveSession);
    connect(m_guitarSuite, &QAbstractItemModel::dataChanged, this, [this] { if (!m_restoring) m_sessionTimer.start(); });
    connect(m_guitarSuite, &GuitarSuiteModel::countChanged, this, [this] { if (!m_restoring) m_sessionTimer.start(); });
    connect(m_guitarSuite, &GuitarSuiteModel::inputTrimDbChanged, this, [this] { if (!m_restoring) m_sessionTimer.start(); });
    connect(m_guitarSuite, &GuitarSuiteModel::lastErrorChanged, this, [this] {
        if (!m_guitarSuite->lastError().isEmpty()) setTransportMessage(m_guitarSuite->lastError());
    });
    connect(m_guitarSuite, &GuitarSuiteModel::busyChanged, this, [this] {
        if (!m_restoringPreset || m_guitarSuite->busy()) return;
        // The loader emits busyChanged before committing or reporting its error.
        QTimer::singleShot(0, this, [this] {
            m_restoringPreset = false;
            m_sessionSaveBlocked = !m_guitarSuite->lastError().isEmpty();
            if (m_sessionSaveBlocked)
                setTransportMessage(m_guitarSuite->lastError() + " · saved session kept unchanged.");
        });
    });
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
    auto snapshot = std::make_unique<EngineSnapshot>();
    snapshot->masterChain.push_back(chain.get());
    snapshot->keepProcessors.push_back(std::move(chain));
    m_pendingGuitarSnapshot = std::move(snapshot);
    if (m_project && !m_restoring) m_sessionTimer.start();
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
    if (m_guitarSuite)
        m_guitarSuite->configure(m_engine->sampleRate(), frames);
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
                    || doc["version"].toInt() != 1 || !doc["guitarSuite"].isObject()
                    || !model->restoreSessionState(doc["project"].toObject(), error)) {
                    m_sessionSaveBlocked = true;
                    setTransportMessage(error.isEmpty() ? "Invalid saved session; original file kept." : error);
                } else {
                    m_numerator = model->project().tempo().numerator(); m_denominator = model->project().tempo().denominator();
                    setBpm(model->project().tempo().bpm()); emit meterChanged();
                    setTransportMessage(error);
                    m_sessionSaveBlocked = false;
                    if (!m_guitarSuite->restoreSessionData(QJsonDocument(doc["guitarSuite"].toObject()).toJson())) {
                        m_sessionSaveBlocked = true;
                        setTransportMessage(m_guitarSuite->lastError() + " · saved session kept unchanged.");
                    } else if (m_guitarSuite->busy()) {
                        m_restoringPreset = true; m_sessionSaveBlocked = true;
                    }
                }
            }
        }
    }
    m_restoring = false;
    connect(model, &TrackListModel::projectChanged, this, &EngineController::refreshProject);
    refreshProject();
}
void EngineController::refreshProject() {
    if (!m_project) return;
    m_engine->publishPlayback(m_project->playbackSnapshot());
    if (!m_restoring) m_sessionTimer.start();
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
    if (track < 0) { setTransportMessage("Arm exactly one Audio, Vocal or Electric Guitar track to record."); return; }
    if (m_repeating) {
        if (!postTransport(int(Type::ClearLoop))) return;
        m_repeating = false;
    }
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/Recordings";
    if (!QDir().mkpath(folder)) { setTransportMessage("Cannot create recordings folder."); return; }
    try {
        const QString path = folder + "/Take-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".wav";
        m_take = std::make_unique<TakeRecorder>(path.toStdString(), m_engine->sampleRate());
        m_engine->clearReleasedRecorder();
        EngineCommand command; command.type = Type::StartRecording; command.recorder = m_take.get();
        if (!m_engine->post(command)) { m_take.reset(); setTransportMessage("Could not start recording; audio queue is full."); return; }
        m_recordTrack = track; m_recordStartBeat = positionBeats(); m_project->lockRecordingTrack(track);
        setTransportMessage("Recording dry input…"); emit transportChanged();
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
        if (audio && audio->frames > 0) m_project->attachTake(m_recordTrack, m_take->startSample, audio);
    }
    setTransportMessage(!error.isEmpty() ? error : audio && audio->frames > 0
        ? QStringLiteral("Recorded %1 seconds · %2").arg(audio->frames / audio->sampleRate, 0, 'f', 2).arg(QString::fromStdString(audio->path))
        : QStringLiteral("Take ended before any audio was captured."));
    m_take.reset(); m_recordTrack = -1;
    if (m_project) setCleanMonitorInput(m_project->cleanMonitorInput());
    emit transportChanged();
    saveSession();
}
bool EngineController::saveSession() {
    if (!restoreSessionOnStartup() || !m_project || m_restoring || m_sessionSaveBlocked || m_guitarSuite->busy()) return false;
    const QString path = sessionPath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) { setTransportMessage("Could not create session folder."); return false; }
    QSaveFile file(path);
    const auto data = QJsonDocument(QJsonObject{{"format", "ChaosMakerSession"}, {"version", 1},
        {"project", m_project->sessionState()}, {"guitarSuite", QJsonDocument::fromJson(m_guitarSuite->sessionData()).object()}}).toJson();
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
