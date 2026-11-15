#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <map>
#include <QPointer>
#include "wrapper/common/trackListModelWrap.h"
class TakeRecorder;
struct RecordingGroup;
#include "common/recording/recordingcontroller.h"
#include "common/session/projectsession.h"

class ElectricWrap;
using GuitarSuiteModel = ElectricWrap;
class GuitarSuite;
class TrackInput;
struct EngineSnapshot;
#include "common/engine/snapshotbuilder.h"

class QQmlEngine;
class QJSEngine;
class AudioEngine;
class AudioDevice;

#include "audioEngineWrap.h"

// The engine in QML. Phase 1 adds the AudioEngine here: start, stop, xrun count and the test tone.
class EngineController : public AudioEngineWrap {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool playing READ playing NOTIFY transportChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY transportChanged)
    Q_PROPERTY(bool countInEnabled READ countInEnabled WRITE setCountInEnabled NOTIFY countInSettingsChanged)
    Q_PROPERTY(bool countingIn READ countingIn NOTIFY transportChanged)
    Q_PROPERTY(int countInBeatsRemaining READ countInBeatsRemaining NOTIFY transportChanged)
    Q_PROPERTY(bool takeBusy READ takeBusy NOTIFY transportChanged)
    Q_PROPERTY(double positionBeats READ positionBeats NOTIFY transportChanged)
    Q_PROPERTY(double positionSeconds READ positionSeconds NOTIFY transportChanged)
    Q_PROPERTY(double recordingStartBeats READ recordingStartBeats NOTIFY transportChanged)
    Q_PROPERTY(int recordingTrackId READ recordingTrackId NOTIFY transportChanged)
    Q_PROPERTY(QVariantList recordingTracks READ recordingTracks NOTIFY transportChanged)
    Q_PROPERTY(int numerator READ numerator NOTIFY meterChanged)
    Q_PROPERTY(int denominator READ denominator NOTIFY meterChanged)
    Q_PROPERTY(bool repeating READ repeating NOTIFY transportChanged)
    // Punch (restMap G11): recording keeps only what lies inside the time selection.
    Q_PROPERTY(bool punch READ punch NOTIFY transportChanged)
    // The timeline's time selection in beats (it is the repeat range); end <= start: none. For
    // pages in other windows (Noise Reduction's Learn, restMap G13).
    Q_PROPERTY(double timeSelectionStartBeat READ timeSelectionStartBeat NOTIFY transportChanged)
    Q_PROPERTY(double timeSelectionEndBeat READ timeSelectionEndBeat NOTIFY transportChanged)
    Q_PROPERTY(bool metronomeEnabled READ metronomeEnabled WRITE setMetronomeEnabled NOTIFY metronomeChanged)
    Q_PROPERTY(double metronomeVolume READ metronomeVolume WRITE setMetronomeVolume NOTIFY metronomeChanged)
    Q_PROPERTY(QString transportMessage READ transportMessage NOTIFY transportMessageChanged)
    Q_PROPERTY(bool restoreSessionOnStartup READ restoreSessionOnStartup WRITE setRestoreSessionOnStartup NOTIFY restoreSessionOnStartupChanged)
    // Stop playback (metronome included) at the end of the arrangement; saved preference, default on.
    Q_PROPERTY(bool stopPlaybackAtEnd READ stopPlaybackAtEnd WRITE setStopPlaybackAtEnd NOTIFY stopPlaybackAtEndChanged)
    // restMap G10: audio takes are moved earlier by the device's round trip plus the extra offset.
    Q_PROPERTY(bool recordingLatencyCompensation READ recordingLatencyCompensation WRITE setRecordingLatencyCompensation NOTIFY recordingLatencyChanged)
    Q_PROPERTY(double recordingExtraOffsetMs READ recordingExtraOffsetMs WRITE setRecordingExtraOffsetMs NOTIFY recordingLatencyChanged)
    Q_PROPERTY(QVariantMap monitorLevels READ monitorLevels NOTIFY monitorLevelsChanged)
    Q_PROPERTY(QVariantMap trackLevels READ trackLevels NOTIFY trackLevelsChanged)
    Q_PROPERTY(double bpm READ bpm WRITE setBpm NOTIFY bpmChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QStringList inputPorts READ inputPorts CONSTANT)
    Q_PROPERTY(QStringList inputDevices READ inputDevices NOTIFY inputDevicesChanged)
    Q_PROPERTY(QStringList outputDevices READ outputDevices NOTIFY outputDevicesChanged)
    Q_PROPERTY(QStringList bufferSizes READ bufferSizes CONSTANT)
    Q_PROPERTY(int selectedBufferSize READ selectedBufferSize NOTIFY bufferSizeChanged)
    Q_PROPERTY(int selectedInputIndex READ selectedInputIndex NOTIFY audioRoutingChanged)
    Q_PROPERTY(int selectedOutputIndex READ selectedOutputIndex NOTIFY outputDeviceChanged)
    Q_PROPERTY(QStringList midiInputPorts READ midiInputPorts NOTIFY midiInputPortsChanged)
    Q_PROPERTY(QString connectedMidiPort READ connectedMidiPort NOTIFY midiInputPortsChanged)
    Q_PROPERTY(QObject* guitarSuite READ guitarSuite CONSTANT)
    Q_PROPERTY(int monitoredTrackId READ monitoredTrackId NOTIFY monitoredTrackChanged)
    Q_PROPERTY(QVariantList monitoredTracks READ monitoredTracks NOTIFY monitoredTrackChanged)
    Q_PROPERTY(QUrl projectFile READ projectFile NOTIFY projectFileChanged)
    Q_PROPERTY(bool projectDirty READ projectDirty NOTIFY projectDirtyChanged)
    Q_PROPERTY(bool projectLoading READ projectLoading NOTIFY projectLoadingChanged)
    Q_PROPERTY(double masterVolume READ masterVolume WRITE setMasterVolume NOTIFY masterVolumeChanged)
    Q_PROPERTY(bool masterMuted READ masterMuted WRITE setMasterMuted NOTIFY masterMutedChanged)

public:
    // Inject a device for offline integration checks; system discovery is only for JACK.
    EngineController(std::unique_ptr<AudioDevice> device, bool discoverSystemDevices, QObject* parent = nullptr);
    static EngineController* instance();
    // The track's two Chaos EQ processors (monitor, playback); null entries when the track has
    // none yet. For ChaosEqWrap's analyzer. Instances are replaced when the buffer changes.
    std::array<std::shared_ptr<ChaosEq>, 2> chaosEqProcessors(int trackId) const;
    // Mixing plugins (restMap G7): the gain reduction now (dB, the larger of the monitor and
    // playback instances) for a page's meter, and the processors themselves (tests).
    Q_INVOKABLE double mixEffectReductionDb(int trackId, const QString& type) const;
    std::shared_ptr<MixEffect> mixEffectProcessor(int trackId, const QString& type, bool playback) const;
    // The instrument track's instrument; null for other tracks. It is prepared once, when it is
    // made, and replaced (never re-prepared) when the buffer changes: the audio thread may be
    // running it.
    std::shared_ptr<Processor> trackInstrument(int trackId) const;
    static EngineController* create(QQmlEngine*, QJSEngine*);
    ~EngineController() override;

    bool playing() const;
    bool recording() const;
    bool countInEnabled() const { return m_countInEnabled; }
    void setCountInEnabled(bool enabled);
    bool countingIn() const;
    int countInBeatsRemaining() const;
    bool takeBusy() const { return m_recordingController.isBusy(); }
    // For an offline render (the export dialog): the open project, and live audio stopped and
    // silent so the render can use the same processors; the snapshot is the song to render.
    // resumeAfterOfflineRender() brings live audio back and shows `message`.
    TrackListModelWrap* projectModel() const { return m_project; }
    std::unique_ptr<EngineSnapshot> suspendForOfflineRender();
    void resumeAfterOfflineRender(const QString& message);
    void showTransportMessage(const QString& message) { setTransportMessage(message); }
    QVariantList recordingTracks() const;
    // The waveform recorded so far on a track, [[min, max], ...] in `bins` columns covering
    // from..to (0..1 of what is recorded), for the timeline's recording box (live);
    // empty when the track is not recording audio.
    Q_INVOKABLE QVariantList recordingWaveform(int trackId, int bins, double from = 0.0, double to = 1.0) const;
    QVariantList monitoredTracks() const;
    double positionBeats() const;
    double positionSeconds() const;
    double recordingStartBeats() const { return m_recordingController.recordStartBeat(); }
    int recordingTrackId() const { return m_recordingController.primaryRecordingTrackId(); }
    int numerator() const { return m_numerator; }
    int denominator() const { return m_denominator; }
    bool repeating() const { return m_repeating; }
    bool punch() const { return m_punch; }
    double timeSelectionStartBeat() const { return m_repeatStartBeat; }
    double timeSelectionEndBeat() const { return m_repeatEndBeat; }
    bool metronomeEnabled() const { return m_metronomeEnabled; }
    double metronomeVolume() const { return m_metronomeVolume; }
    void setMetronomeEnabled(bool enabled);
    void setMetronomeVolume(double volume);
    QString transportMessage() const { return m_transportMessage; }
    bool restoreSessionOnStartup() const;
    void setRestoreSessionOnStartup(bool enabled);
    bool stopPlaybackAtEnd() const;
    void setStopPlaybackAtEnd(bool enabled);
    bool recordingLatencyCompensation() const;
    void setRecordingLatencyCompensation(bool enabled);
    double recordingExtraOffsetMs() const;
    void setRecordingExtraOffsetMs(double ms);
    // The round trip the audio device reports now, in ms (0 when unknown or not running).
    Q_INVOKABLE double reportedRoundTripMs() const;
    Q_INVOKABLE void attachProject(TrackListModelWrap* model);
    Q_INVOKABLE void transportAction(const QString& action);
    Q_INVOKABLE void seekBeat(double beat);
    Q_INVOKABLE bool setRepeatRange(double startBeat, double endBeat);
    Q_INVOKABLE void setMeter(int numerator, int denominator);
    Q_INVOKABLE bool saveSession();
    QUrl projectFile() const {
        return m_projectSession.currentFilePath().empty() ? QUrl() : QUrl::fromLocalFile(QString::fromStdString(m_projectSession.currentFilePath()));
    }
    bool projectDirty() const { return m_projectSession.isDirty(); }
    bool projectLoading() const { return bool(m_loadingProject); }
    Q_INVOKABLE bool saveProject(const QUrl& file = QUrl());
    // A track's FX chain as a .chaosfx file (persistence/effectchainjson). Each returns
    // {ok, error, kept: [effect titles], skipped: [titles the track can't take], trackType}.
    // preview reads a file and says what importing it would do, changing nothing; import
    // REPLACES the track's chain (effects the track type can't take are skipped); export
    // writes the track's chain with every setting, the GuitarSuite pedalboard too.
    Q_INVOKABLE QVariantMap previewEffectChain(int trackId, const QUrl& file);
    Q_INVOKABLE QVariantMap importEffectChain(int trackId, const QUrl& file);
    Q_INVOKABLE QVariantMap exportEffectChain(int trackId, const QUrl& file);
    Q_INVOKABLE bool openProject(const QUrl& file);
    Q_INVOKABLE bool newProject();
    Q_INVOKABLE void shutdownSession();
    QVariantMap monitorLevels() const { return m_monitorLevels; }
    QVariantMap trackLevels() const { return m_trackLevels; }
    Q_INVOKABLE void clearTrackClips(int trackId);
    Q_INVOKABLE void clearMonitorClips();
    double masterVolume() const { return m_masterVolume; }
    Q_INVOKABLE void setMasterVolume(double volume);
    bool masterMuted() const { return m_masterMuted; }
    Q_INVOKABLE void setMasterMuted(bool muted);
    double bpm() const { return m_bpm; }
    void setBpm(double bpm);
    QString version() const;
    QString status() const { return m_status; }
    // The audio device as last polled (audioInfoChanged): the status line and the top bar's
    // device line build their texts from these.
    double audioRate() const { return m_infoRate; }
    int bufferFrames() const { return m_infoFrames; }
    int requestedBufferFrames() const;
    int captureFrames() const { return m_infoCapture; }
    int playbackFrames() const { return m_infoPlayback; }
    int roundTripFrames() const { return m_infoRoundTrip; }
    std::uint64_t xruns() const { return m_infoXruns; }
    void resetXruns();
    QStringList inputPorts() const { return m_inputPorts; }
    QStringList inputDevices() const { return m_inputDevices; }
    QStringList outputDevices() const { return m_outputDevices; }
    QStringList bufferSizes() const { return {QStringLiteral("32"), QStringLiteral("64"), QStringLiteral("128"),
                                               QStringLiteral("256"), QStringLiteral("512"), QStringLiteral("1024")}; }
    int selectedBufferSize() const;
    int selectedInputIndex() const { return m_selectedInputIndex; }
    int selectedOutputIndex() const { return m_selectedOutputIndex; }
    QObject* guitarSuite() const;
    int monitoredTrackId() const { return m_monitorEnabled ? m_monitoredTrack : -1; }
    Q_INVOKABLE QObject* trackEffects(int trackId);
    Q_INVOKABLE void setCleanMonitorEnabled(bool enabled);
    Q_INVOKABLE void setCleanMonitorGain(double gain);
    Q_INVOKABLE void setCleanMonitorEqBands(const QVariantList& bands);
    Q_INVOKABLE bool selectInput(int index);
    Q_INVOKABLE bool setOutputDevice(int index);
    Q_INVOKABLE bool setBufferSize(int frames);
    Q_INVOKABLE QString inputPortForDevice(int index) const;
    Q_INVOKABLE bool setCleanMonitorInput(const QString& port);
    Q_INVOKABLE void sendLiveNoteOn(int trackId, int note, int velocity = 100);
    Q_INVOKABLE void sendLiveNoteOff(int trackId, int note);
    // A controller change to the track's instrument, as a MIDI keyboard would send it (the
    // piano page's sustain pedal: 64).
    Q_INVOKABLE void sendLiveControl(int trackId, int controller, int value);
    QStringList midiInputPorts() const;
    QString connectedMidiPort() const;
    Q_INVOKABLE bool connectMidiInput(const QString& portName);
    // Looks for MIDI devices again (the IN popup of instrument tracks calls it when it opens).
    Q_INVOKABLE void refreshMidiInputs();

signals:
    void projectFileChanged();
    void projectDirtyChanged();
    void projectLoadingChanged();
    void transportChanged();
    void transportMessageChanged();
    void restoreSessionOnStartupChanged();
    void stopPlaybackAtEndChanged();
    void recordingLatencyChanged();
    void meterChanged();
    void monitorLevelsChanged();
    void trackLevelsChanged();
    void monitoredTrackChanged();
    void bpmChanged();
    void metronomeChanged();
    void countInSettingsChanged();
    void statusChanged();
    void audioInfoChanged();
    void audioRoutingChanged();
    void outputDevicesChanged();
    void outputDeviceChanged();
    void inputDevicesChanged();
    void bufferSizeChanged();
    void masterVolumeChanged();
    void masterMutedChanged();
    void midiInputPortsChanged();

private:
    void setProjectDirty(bool dirty);
    QJsonObject projectDocument();
    void finishProjectLoad();
    void cancelProjectLoad();
    void bindTrackEffects(int trackId, GuitarSuiteModel* model);
    QUrl m_loadingFile;
    ProjectSession m_projectSession;
    std::unique_ptr<TrackListModelWrap> m_loadingProject;
    std::vector<GuitarSuiteModel*> m_loadingEffects;
    QString m_loadingWarning;
    explicit EngineController(QObject* parent);
    void setStatus(const QString& status);
    void updateAudioStatus();
    // Emits audioInfoChanged when the device rate, buffer or xrun count changed (polled).
    void refreshAudioInfo();
    void discoverOutputDevices();
    void discoverInputDevices();
    void connectOutputDevicePorts(const QString& description);
    void queueGuitarSuite(std::shared_ptr<GuitarSuite> chain);
    void syncTrackEffects();
    void clearTrackEffects();
    void watchEffects(GuitarSuiteModel* model);
    bool effectsBusy() const;
    struct TrackEffects : public SnapshotBuilder::TrackEffects {
        GuitarSuiteModel* model = nullptr;
    };
    std::map<int, TrackEffects> m_trackEffects;
    // Pedalboards (session data) of removed tracks, so undoing the removal restores them.
    // Cleared whenever the effects are cleared (new/open/attach project).
    std::map<int, std::string> m_removedTrackEffects;
    int m_monitoredTrack = -1;
    QString m_trackRoutingError;
    bool m_bindingEffects = false;

    void refreshProject();
    void publishPlayback();
    void pollTransport();
    void completeTake();
    void setTransportMessage(const QString& message);
    bool postRepeatLoop();
    bool postMetronome(bool enabled, double volume);
    bool postTransport(int type, qint64 a = 0, qint64 b = 0);
    QPointer<TrackListModelWrap> m_project;
    RecordingController m_recordingController;
    QTimer m_sessionTimer;
    bool m_repeating = false, m_shutdown = false, m_restoring = false, m_sessionSaveBlocked = false, m_restoringPreset = false;
    int m_numerator = 4, m_denominator = 4;
    double m_repeatStartBeat = 0, m_repeatEndBeat = 0;
    // restMap G11: punch on / off, and how this recording's takes land (loop, punch range),
    // fixed when recording starts; recordStart is filled in per take.
    bool m_punch = false;
    TrackList::TakeShape m_takeShape;
    // Beats to engine samples as the loop uses them (nullopt: out of range).
    std::optional<std::pair<std::int64_t, std::int64_t>> rangeSamples(double startBeat, double endBeat) const;
    TrackList::TakeShape recordingShape(std::int64_t recordStart) const;
    double m_masterVolume = 1.0;
    bool m_masterMuted = false;
    QString m_transportMessage;
    QString m_status = QStringLiteral("not started");
    // Last values sent to QML by refreshAudioInfo().
    double m_infoRate = 0.0;
    int m_infoFrames = 0;
    int m_infoCapture = 0;
    int m_infoPlayback = 0;
    int m_infoRoundTrip = 0;
    std::uint64_t m_infoXruns = 0;
    GuitarSuiteModel* m_guitarSuite = nullptr;
    QTimer m_snapshotTimer;
    QTimer m_meterTimer;
    QVariantMap m_monitorLevels;
    QVariantMap m_trackLevels;
    double m_bpm = 120.0;
    bool m_metronomeEnabled = false, m_countInEnabled = false, m_countInMessagePending = false;
    double m_metronomeVolume = 0.5;
    bool m_monitorEnabled = false;
    bool m_inputConnected = false;
    bool m_outputConnected = false;
    QStringList m_inputPorts;
    QStringList m_inputDevices;
    QStringList m_inputDevicePorts;
    QStringList m_outputPorts;
    QStringList m_outputDevices;
    QStringList m_outputDeviceNames;
    QString m_selectedInput;
    QString m_activeInput;
    QString m_monitorInputOverride;
    QString m_selectedOutputLeft;
    QString m_selectedOutputRight;
    QString m_connectionError;
    int m_selectedInputIndex = -1;
    int m_selectedOutputIndex = -1;
};
