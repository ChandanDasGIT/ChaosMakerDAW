#include "testkit.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/plugins/chaoseq/chaoseqwrap/chaoseqwrapper.h"
#include "wrapper/instruments/guitar/guitarsuiteeditorwrap/guitarsuiteeditorwrapper.h"
#include "audiodevice/audiodevice.h"
#include "recording/takerecorder.h"
#include "plugins/testsynth/testsynth.h"
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QTimer>
#include <QFile>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QDir>

namespace {
class OfflineDevice final : public AudioDevice {
public:
    AudioCallback* callback = nullptr;
    bool open(AudioCallback* c,int frames) override { callback=c; c->deviceChanged(48000,frames); return true; }
    void close() override { callback=nullptr; }
    double sampleRate() const override { return 48000; }
    int roundTrip=0;   // restMap G10: the round trip this device reports
    DeviceLatency latency() const override { return {128,128,128,roundTrip}; }
    bool setBufferSize(int) override { return true; }
    std::vector<std::string> inputNames() const override { return {"test:input","test:mic","test:pickup"}; }
    std::vector<std::string> outputNames() const override { return {"test:left","test:right"}; }
    std::string connectedInput;
    bool multipleInputs=false, missingInput=false;
    std::string failingSource;
    bool connectInput(const std::string& source) override { connectedInput=source; return true; }
    int inputChannelCapacity() const override { return multipleInputs ? 3 : 1; }
    int connectInputChannel(const std::string& source) override {
        if(source==failingSource)return -1;
        if(!multipleInputs)return connectInput(source)?0:-1;
        const auto names=inputNames();
        const auto it=std::find(names.begin(),names.end(),source);
        return it==names.end() ? -1 : int(it-names.begin());
    }
    bool connectOutput(const std::string&,const std::string&) override { return true; }
    std::string lastError() const override { return {}; }
    float input[128]{}, output[128]{}, right[128]{};
    float mic[128]{}, pickup[128]{};
    void process() {
        float* in[]{input,mic,pickup}; float* out[]{output,right};
        if(callback) callback->process({{in,missingInput?0:(multipleInputs?3:1),128},{out,2,128},nullptr,128});
    }
};
// These existing scenarios exercise recording with live monitoring enabled.
// Set both controls explicitly now that arming no longer changes monitoring.
bool setCaptureAndMonitor(TrackListWrapper& tracks, int id, bool enabled) {
    const auto* track=tracks.project().findTrack(id);
    if (track && track->hasLiveInput() && !tracks.setMonitorEnabled(id,enabled)) return false;
    return tracks.setRecordArmed(id,enabled);
}
void application() {
    if(QCoreApplication::instance()) return;
    static int argc=1; static char name[]="recordingtests"; static char* argv[]{name,nullptr};
    static QCoreApplication app(argc,argv);
}
void tick(int ms=30) { QEventLoop loop; QTimer::singleShot(ms,&loop,&QEventLoop::quit); loop.exec(); }
struct Storage {
    QTemporaryDir dir{QStringLiteral("/tmp/chaosmaker-recording-ui-XXXXXX")};
    Storage() {
        application(); CHECK(dir.isValid());
        QCoreApplication::setOrganizationName("ChaosMakerRecordingTests");
        QCoreApplication::setApplicationName("Tests");
        qputenv("XDG_DATA_HOME",dir.path().toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
    }
};
}
TEST(qtrecording, record_stop_creates_a_clip_and_play_renders_it_then_restores_after_reopen) {
    Storage storage;
    std::vector<float> recorded;
    {
        auto device=std::make_unique<OfflineDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); TrackListWrapper tracks;
        controller.attachProject(&tracks); controller.setRestoreSessionOnStartup(true);
        const int guitar=tracks.addInstrumentTrackOfType("Electric Guitar"); setCaptureAndMonitor(tracks,guitar,true);
        controller.setBpm(90); controller.setMeter(7,8);
        auto* fx=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(guitar)); CHECK(fx->addPedal("Tremolo"));
        const int id=fx->data(fx->index(0),GuitarSuiteModel::PedalIdRole).toInt(); CHECK(fx->setParameter(id,"sustain",0));
        tick(); io->process(); // publish the new effects chain before recording
        for(int i=0;i<128;++i) io->input[i]=float(i)/512;
        controller.transportAction("record"); CHECK(controller.takeBusy()); io->process(); io->process();
        CHECK(controller.recording()); CHECK(controller.playing());
        CHECK(controller.transportMessage().contains("with FX"));
        CHECK(!tracks.removeTrack(guitar)); // do not orphan an active take
        controller.transportAction("stop"); io->process();
        for(int i=0;i<100 && controller.takeBusy();++i) tick(10);
        CHECK(!controller.takeBusy()); CHECK(tracks.clips().size()==1);
        CHECK(!controller.playing()); CHECK(controller.positionSeconds()==0);
        auto snapshot=tracks.playbackSnapshot();
        CHECK(snapshot->keepAudio.size()==1);
        const auto audio=snapshot->keepAudio.front(); CHECK(audio->channels.size()==2);
        recorded=audio->channels[0];
        CHECK(std::abs(recorded[64]-io->input[64])>0.001f);
        setCaptureAndMonitor(tracks,guitar,false); tick(); io->process(); // publish completed take
        controller.transportAction("play"); io->process();
        for(int i=0;i<128;++i) CHECK_NEAR(io->output[i],recorded[i],1e-6);
        CHECK(controller.saveSession()); controller.shutdownSession();
    }
    {
        auto device=std::make_unique<OfflineDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); TrackListWrapper tracks;
        controller.attachProject(&tracks); CHECK(tracks.count()==1); CHECK(tracks.clips().size()==1);
        CHECK(controller.bpm()==90); CHECK(controller.numerator()==7); CHECK(controller.denominator()==8);
        CHECK(!controller.playing()); CHECK(!tracks.cleanMonitorArmed());
        CHECK(qobject_cast<GuitarSuiteModel*>(controller.trackEffects(tracks.trackIdAt(0)))->count()==1);
        tick(); io->process(); controller.transportAction("play"); io->process();
        CHECK_NEAR(io->output[64],recorded[64],1e-6);
        controller.shutdownSession();
    }
}
TEST(qtrecording, closing_during_recording_finalizes_take_and_invalid_session_is_not_overwritten) {
    Storage storage;
    auto device=std::make_unique<OfflineDevice>(); auto* io=device.get();
    EngineController controller(std::move(device),false); TrackListWrapper tracks; controller.attachProject(&tracks);
    const int track=tracks.addAudioTrack(); setCaptureAndMonitor(tracks,track,true);
    controller.setRestoreSessionOnStartup(true); controller.transportAction("record"); io->process();
    controller.shutdownSession(); CHECK(tracks.clips().size()==1); CHECK(!controller.takeBusy());
    const QString path=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/last-session.json";
    { QFile file(path); CHECK(file.open(QIODevice::WriteOnly)); file.write("broken session"); }
    auto other=std::make_unique<OfflineDevice>(); EngineController reopened(std::move(other),false); TrackListWrapper recovered;
    reopened.attachProject(&recovered); CHECK(!reopened.transportMessage().isEmpty());
    recovered.addAudioTrack(); CHECK(!reopened.saveSession()); reopened.shutdownSession();
    QFile file(path); CHECK(file.open(QIODevice::ReadOnly)); CHECK(file.readAll()=="broken session");
}

TEST(qtrecording, audio_takes_remain_dry_with_live_fx_loaded) {
    Storage storage;
    auto device=std::make_unique<OfflineDevice>(); auto* io=device.get();
    EngineController controller(std::move(device),false); TrackListWrapper tracks; controller.attachProject(&tracks);
    const int audio=tracks.addAudioTrack();
    auto* fx=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(audio)); CHECK(fx->addPedal("Tremolo"));
    tick(); io->process();
    std::fill_n(io->input,128,0.25f);
    for(int id : {audio}) {
        setCaptureAndMonitor(tracks,id,true);
        controller.transportAction("record"); CHECK(controller.transportMessage().contains("dry input")); io->process();
        controller.transportAction("stop"); io->process();
        for(int i=0;i<100 && controller.takeBusy();++i) tick(10);
        CHECK(!controller.takeBusy()); setCaptureAndMonitor(tracks,id,false);
    }
    auto snapshot=tracks.playbackSnapshot(); CHECK(snapshot->keepAudio.size()==1);
    for(const auto& audio : snapshot->keepAudio) {
        CHECK(audio->channels.size()==1); CHECK(audio->frames==128);
        for(float sample : audio->channels[0]) CHECK(sample==0.25f);
    }
    controller.shutdownSession();
}

TEST(qtrecording, vocal_and_acoustic_share_live_input_fx_recording_and_session_playback) {
    for(bool vocal : {true,false}) {
        Storage storage;
        std::vector<std::vector<float>> recorded;
        const QString type=vocal ? "Vocal" : "Acoustic Guitar";
        const QString input=vocal ? "test:mic" : "test:pickup";
        {
            auto device=std::make_unique<OfflineDevice>(); auto* io=device.get();
            EngineController controller(std::move(device),false); TrackListWrapper tracks;
            controller.attachProject(&tracks);
            controller.setRestoreSessionOnStartup(true);
            const int track=vocal ? tracks.addVocalTrack() : tracks.addInstrumentTrackOfType(type);
            CHECK(tracks.setTrackInputPort(track,input)); CHECK(tracks.setVolume(track,0.5));
            CHECK(setCaptureAndMonitor(tracks,track,true)); CHECK(tracks.cleanMonitorArmed());
            CHECK(tracks.recordingTrackId()==track); CHECK(io->connectedInput==input.toStdString());
            const auto row=tracks.index(tracks.indexOfTrack(track));
            CHECK(tracks.data(row,TrackListWrapper::LiveInputRole).toBool());
            CHECK(tracks.data(row,TrackListWrapper::RecordsWithFxRole).toBool());
            int eqUpdates=0;
            QObject::connect(&tracks,&TrackListWrapper::cleanMonitorEqChanged,&tracks,[&] { ++eqUpdates; });
            CHECK(tracks.setEqBand(track,5,-6)); CHECK(eqUpdates==1);
            CHECK(tracks.cleanMonitorEqBands()[5].toDouble()==-6);
            CHECK(tracks.applyEqPreset(track,2)); CHECK(eqUpdates==2);
            CHECK(tracks.resetTrackEq(track)); CHECK(eqUpdates==3);
            auto* fx=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(track)); CHECK(fx->addPedal("Tremolo"));
            tick(); io->process();
            for(int i=0;i<128;++i) io->input[i]=float(i+1)/512;
            io->process();
            CHECK(io->output[64]>0);
            // Vocal tracks start without effects (plan 6.1), so the pedal does not colour them;
            // the acoustic guitar's GuitarSuite does.
            if (vocal) CHECK_NEAR(io->output[64],io->input[64]*0.5f,1e-4);
            else CHECK(std::abs(io->output[64]-io->input[64]*0.5f)>0.001f);
            CHECK(io->right[64]==io->output[64]); // same processed sound on both output channels
            controller.transportAction("record"); io->process();
            CHECK(controller.recording()); CHECK(controller.transportMessage().contains("with FX"));
            CHECK(controller.transportMessage().contains(type));
            CHECK(!tracks.setTrackInputPort(track,"test:input")); CHECK(!tracks.removeTrack(track));
            std::vector<float> monitored(io->output,io->output+128);
            CHECK(tracks.setMuted(track,true)); io->process();
            for(float sample : io->output) CHECK(sample==0);
            // Disarming must finish a take for either new instrument type.
            CHECK(setCaptureAndMonitor(tracks,track,false)); tick(); io->process();
            for(int i=0;i<100 && controller.takeBusy();++i) tick(10);
            CHECK(!controller.takeBusy()); CHECK(!controller.recording());
            auto snapshot=tracks.playbackSnapshot(); CHECK(snapshot->keepAudio.size()==1);
            const auto audio=snapshot->keepAudio.front(); CHECK(audio->frames==256);
            recorded=audio->channels; CHECK(recorded.size()==2);
            for(int i=0;i<128;++i) CHECK_NEAR(recorded[0][i],monitored[i]*2,1e-6);
            CHECK(std::abs(recorded[0][192])>0.01f); // muting speakers must not mute the take
            CHECK(tracks.setMuted(track,false)); CHECK(tracks.setVolume(track,1));
            controller.transportAction("stop"); io->process();
            controller.transportAction("play"); io->process();
            for(int i=0;i<128;++i) {
                CHECK_NEAR(io->output[i],recorded[0][i],1e-6);
                CHECK_NEAR(io->right[i],recorded[1][i],1e-6);
            }
            CHECK(controller.saveSession()); controller.shutdownSession();
        }
        {
            auto device=std::make_unique<OfflineDevice>(); auto* io=device.get();
            EngineController controller(std::move(device),false); TrackListWrapper tracks;
            controller.attachProject(&tracks);
            CHECK(tracks.count()==1); CHECK(tracks.clips().size()==1); CHECK(!tracks.cleanMonitorArmed());
            CHECK(tracks.data(tracks.index(0),TrackListWrapper::TypeRole).toString()==type);
            CHECK(tracks.data(tracks.index(0),TrackListWrapper::InputPortRole).toString()==input);
            tick(); io->process(); controller.transportAction("play"); io->process();
            for(int i=0;i<128;++i) {
                CHECK_NEAR(io->output[i],recorded[0][i],1e-6);
                CHECK_NEAR(io->right[i],recorded[1][i],1e-6);
            }
            controller.shutdownSession();
        }
    }
}
TEST(qtrecording, switching_live_instruments_keeps_legacy_single_track_queries_unambiguous) {
    Storage storage; TrackListWrapper tracks;
    const int vocal=tracks.addVocalTrack(), acoustic=tracks.addInstrumentTrackOfType("Acoustic Guitar");
    const int drums=tracks.addInstrumentTrackOfType("Drums");
    CHECK(tracks.setTrackInputPort(vocal,"test:mic")); CHECK(tracks.setTrackInputPort(acoustic,"test:pickup"));
    CHECK(tracks.setVolume(vocal,0.4)); CHECK(tracks.setVolume(acoustic,0.8));
    CHECK(tracks.setEqBand(vocal,4,-3)); CHECK(tracks.setEqBand(acoustic,4,2));
    CHECK(setCaptureAndMonitor(tracks,vocal,true)); CHECK(tracks.recordingTrackId()==vocal);
    CHECK(tracks.cleanMonitorInput()=="test:mic"); CHECK_NEAR(tracks.cleanMonitorGain(),0.4,1e-6);
    CHECK(tracks.cleanMonitorEqBands()[4].toDouble()==-3);
    CHECK(setCaptureAndMonitor(tracks,acoustic,true)); CHECK(tracks.recordingTrackId()==-1);
    CHECK(setCaptureAndMonitor(tracks,vocal,false)); CHECK(tracks.recordingTrackId()==acoustic);
    CHECK(tracks.cleanMonitorInput()=="test:pickup"); CHECK_NEAR(tracks.cleanMonitorGain(),0.8,1e-6);
    CHECK(tracks.cleanMonitorEqBands()[4].toDouble()==2);
    CHECK(tracks.setSolo(vocal,true)); CHECK(tracks.cleanMonitorGain()==0);
    CHECK(tracks.setSolo(vocal,false)); CHECK_NEAR(tracks.cleanMonitorGain(),0.8,1e-6);
    CHECK(setCaptureAndMonitor(tracks,acoustic,false)); CHECK(!tracks.cleanMonitorArmed());
    CHECK(setCaptureAndMonitor(tracks,drums,true)); CHECK(tracks.recordingTrackId()==-1); CHECK(!tracks.cleanMonitorArmed());
}

TEST(qtrecording, each_track_owns_fx_trim_eq_and_recordings_and_restores_after_id_gaps) {
    Storage storage;
    {
        auto device=std::make_unique<OfflineDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); TrackListWrapper tracks; controller.attachProject(&tracks);
        const int removed=tracks.addAudioTrack();
        const int guitar=tracks.addInstrumentTrackOfType("Electric Guitar"), vocal=tracks.addVocalTrack();
        const int acoustic=tracks.addInstrumentTrackOfType("Acoustic Guitar");
        QPointer<QObject> removedFx=controller.trackEffects(removed); CHECK(removedFx);
        CHECK(tracks.removeTrack(removed)); CHECK(removedFx.isNull());
        auto* a=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(guitar));
        auto* b=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(vocal));
        auto* c=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(acoustic));
        CHECK(a && b && c && a!=b && b!=c);
        CHECK(a->addPedal("Delay")); CHECK(a->addPedal("Reverb"));
        CHECK(a->movePedal(1,0));
        for(int row=0;row<a->count();++row) CHECK(a->setBypassed(a->data(a->index(row),GuitarSuiteModel::PedalIdRole).toInt(),true));
        a->setInputTrimDb(-6.020599913); b->setInputTrimDb(-12.041199827);
        std::fill_n(io->input,128,0.25f);
        CHECK(setCaptureAndMonitor(tracks,guitar,true));
        for(int i=0;i<200;++i) io->process();
        CHECK_NEAR(io->output[64],0.125,1e-5);
        // Editing a different track must not switch the monitored chain or touch its parameters.
        CHECK(b->addPedal("Tremolo")); b->setInputTrimDb(-18);
        CHECK(tracks.setEqBand(vocal,5,6));
        for(int i=0;i<20;++i) io->process();
        CHECK_NEAR(io->output[64],0.125,1e-5); CHECK(a->count()==2); CHECK(c->count()==0);
        CHECK(tracks.setEqBand(vocal,5,0)); b->setInputTrimDb(-12.041199827);
        CHECK(b->removePedal(b->data(b->index(0),GuitarSuiteModel::PedalIdRole).toInt()));
        for(int track : {guitar,vocal,acoustic}) {
            for(int other : {guitar,vocal,acoustic}) if(other!=track) setCaptureAndMonitor(tracks,other,false);
            setCaptureAndMonitor(tracks,track,true);
            for(int i=0;i<200;++i) io->process();
            const float expected=track==guitar ? 0.125f : track==vocal ? 0.0625f : 0.25f;
            CHECK_NEAR(io->output[64],expected,1e-5);
            controller.transportAction("record"); io->process();
            // Opening/editing another rack while recording does not redirect capture.
            CHECK(controller.trackEffects(track==guitar ? vocal : guitar));
            controller.transportAction("stop"); io->process();
            for(int i=0;i<100 && controller.takeBusy();++i) tick(10);
            auto snapshot=tracks.playbackSnapshot();
            const auto& render=*std::find_if(snapshot->tracks.begin(),snapshot->tracks.end(),[track](const auto& t){return t.trackId==track;});
            CHECK(render.clips.size()==1);
            CHECK_NEAR(render.clips[0].audio->channels[0][64],expected,1e-5);
            setCaptureAndMonitor(tracks,track,false);
            tracks.setMuted(track,true); // keep previously recorded takes out of later monitor checks
        }
        CHECK(tracks.setEqBand(guitar,2,-3)); CHECK(tracks.setEqBand(vocal,5,4));
        controller.setRestoreSessionOnStartup(true); CHECK(controller.saveSession()); controller.shutdownSession();
    }
    {
        auto device=std::make_unique<OfflineDevice>();
        EngineController controller(std::move(device),false); TrackListWrapper tracks; controller.attachProject(&tracks);
        CHECK(tracks.count()==3); CHECK(tracks.clips().size()==3);
        auto* a=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(tracks.trackIdAt(0)));
        auto* b=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(tracks.trackIdAt(1)));
        auto* c=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(tracks.trackIdAt(2)));
        CHECK(a->count()==2); CHECK(b->count()==0); CHECK(c->count()==0);
        // The vocal has no GuitarSuite (plan 6.1): its saved trim, part of the pedalboard, is dropped.
        CHECK_NEAR(a->inputTrimDb(),-6.020599913,1e-6); CHECK(b->inputTrimDb()==0);
        CHECK(c->inputTrimDb()==0); CHECK(a->data(a->index(0),GuitarSuiteModel::PedalTypeRole).toString()=="pedal.reverb");
        CHECK(a->data(a->index(0),GuitarSuiteModel::BypassedRole).toBool());
        CHECK(tracks.project().findTrack(tracks.trackIdAt(0))->eqGainDb[2]==-3);
        CHECK(tracks.project().findTrack(tracks.trackIdAt(1))->eqGainDb[5]==4);
        CHECK(!tracks.cleanMonitorArmed()); controller.shutdownSession();
    }
}
TEST(qtrecording, legacy_shared_chain_migrates_to_independent_copies_and_invalid_fx_cannot_overwrite_session) {
    Storage storage;
    TrackListWrapper original; original.addInstrumentTrackOfType("Electric Guitar"); original.addVocalTrack();
    GuitarSuiteModel shared; CHECK(shared.addPedal("Tremolo")); shared.setInputTrimDb(-7);
    const QString folder=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    CHECK(QDir().mkpath(folder)); const QString path=folder+"/last-session.json";
    const QJsonObject legacy{{"format","ChaosMakerSession"},{"version",1},{"project",original.sessionState()},
        {"guitarSuite",QJsonDocument::fromJson(shared.sessionData()).object()}};
    { QFile file(path); CHECK(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(legacy).toJson()); }
    { QSettings settings; settings.setValue("session/restoreOnStartup",true); settings.sync(); }
    {
        auto device=std::make_unique<OfflineDevice>(); EngineController controller(std::move(device),false);
        TrackListWrapper tracks; controller.attachProject(&tracks);
        auto* a=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(tracks.trackIdAt(0)));
        auto* b=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(tracks.trackIdAt(1)));
        // The shared chain loads onto the guitar only; the vocal has no GuitarSuite (plan 6.1).
        CHECK(a!=b); CHECK(a->count()==1 && b->count()==0); CHECK(a->inputTrimDb()==-7 && b->inputTrimDb()==0);
        CHECK(a->addPedal("Delay")); CHECK(a->removePedal(a->data(a->index(0),GuitarSuiteModel::PedalIdRole).toInt()));
        CHECK(a->count()==1);
        CHECK(controller.saveSession()); controller.shutdownSession();
    }
    QJsonObject invalid;
    { QFile file(path); CHECK(file.open(QIODevice::ReadOnly)); invalid=QJsonDocument::fromJson(file.readAll()).object(); }
    CHECK(invalid["version"].toInt()==2);
    auto effects=invalid["trackEffects"].toArray(); effects[0]=QJsonObject{{"bad",true}}; invalid["trackEffects"]=effects;
    const auto bytes=QJsonDocument(invalid).toJson();
    { QFile file(path); CHECK(file.open(QIODevice::WriteOnly)); file.write(bytes); }
    {
        auto device=std::make_unique<OfflineDevice>(); EngineController controller(std::move(device),false);
        TrackListWrapper tracks; controller.attachProject(&tracks); CHECK(!controller.saveSession()); controller.shutdownSession();
    }
    QFile file(path); CHECK(file.open(QIODevice::ReadOnly)); CHECK(file.readAll()==bytes);
}

TEST(qtrecording, per_track_async_cabinet_restore_waits_for_all_tracks_and_preserves_failed_sessions) {
    for(bool valid : {true,false}) {
        Storage storage;
        const QString ir=storage.dir.path()+"/cabinet.wav";
        if(valid) {
            TakeRecorder writer(ir.toStdString(),48000); float impulse[]{1,0,0,0};
            CHECK(writer.capture(impulse,4)); writer.finish(); writer.wait(); CHECK(writer.error().empty());
        }
        TrackListWrapper original; original.addInstrumentTrackOfType("Electric Guitar"); original.addInstrumentTrackOfType("Acoustic Guitar");
        auto preset=[&](double trim) {
            return QJsonObject{{"format","GuitarSuitePreset"},{"version",1},{"inputTrimDb",trim},
                {"pedals",QJsonArray{QJsonObject{{"type","pedal.cabinet"},{"irPath",ir},{"parameters",QJsonObject{}}}}}};
        };
        const QString folder=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        CHECK(QDir().mkpath(folder)); const QString path=folder+"/last-session.json";
        const auto bytes=QJsonDocument(QJsonObject{{"format","ChaosMakerSession"},{"version",2},
            {"project",original.sessionState()},{"trackEffects",QJsonArray{preset(-3),preset(-9)}}}).toJson();
        { QFile file(path); CHECK(file.open(QIODevice::WriteOnly)); file.write(bytes); }
        { QSettings settings; settings.setValue("session/restoreOnStartup",true); settings.sync(); }
        auto device=std::make_unique<OfflineDevice>(); EngineController controller(std::move(device),false);
        TrackListWrapper tracks; controller.attachProject(&tracks);
        auto* a=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(tracks.trackIdAt(0)));
        auto* b=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(tracks.trackIdAt(1)));
        CHECK(!controller.saveSession());
        for(int i=0;i<100 && (a->busy() || b->busy());++i) tick(10);
        tick(); CHECK(!a->busy() && !b->busy());
        CHECK(controller.saveSession()==valid);
        if(valid) {
            CHECK(a->count()==1 && b->count()==1); CHECK(a->inputTrimDb()==-3 && b->inputTrimDb()==-9);
            CHECK(a->clearCabinetIr(a->data(a->index(0),GuitarSuiteModel::PedalIdRole).toInt()));
            CHECK(b->data(b->index(0),GuitarSuiteModel::ResourcePathRole).toString()==ir);
        } else CHECK(!controller.transportMessage().isEmpty());
        controller.shutdownSession();
        if(!valid) { QFile file(path); CHECK(file.open(QIODevice::ReadOnly)); CHECK(file.readAll()==bytes); }
    }
}

TEST(qtprojectfiles, save_as_open_and_new_preserve_clips_fx_and_named_file_state) {
    Storage storage; auto device=std::make_unique<OfflineDevice>(); auto* io=device.get();
    EngineController controller(std::move(device),false); TrackListWrapper tracks; controller.attachProject(&tracks);
    // A guitar: only tracks with GuitarSuite keep a pedalboard (plan 6.1).
    const int track=tracks.addInstrumentTrackOfType("Electric Guitar"); CHECK(setCaptureAndMonitor(tracks,track,true));
    auto* fx=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(track)); CHECK(fx->addPedal("Tremolo")); fx->setInputTrimDb(-5);
    std::fill_n(io->input,128,0.2f); controller.transportAction("record"); io->process();
    const QUrl first=QUrl::fromLocalFile(storage.dir.path()+"/first.chaos"), second=QUrl::fromLocalFile(storage.dir.path()+"/second.chaos");
    CHECK(!controller.saveProject(first)); CHECK(!controller.newProject());
    controller.transportAction("stop"); io->process(); for(int i=0;i<100 && controller.takeBusy();++i)tick(10);
    CHECK(controller.projectDirty()); CHECK(controller.saveProject(first)); CHECK(!controller.projectDirty());
    auto before=tracks.playbackSnapshot(); auto audio=before->keepAudio[0];
    const int clip=tracks.project().findTrack(track)->audioClips[0].id;
    CHECK(tracks.moveClipAtBeat(track,clip,2)); CHECK(controller.projectDirty()); CHECK(controller.saveProject(second));
    CHECK(controller.projectFile()==second); CHECK(controller.newProject()); io->process();
    CHECK(tracks.count()==0); CHECK(controller.projectFile().isEmpty()); CHECK(!controller.projectDirty());
    CHECK(controller.openProject(first)); for(int i=0;i<100 && controller.projectLoading();++i)tick(10); io->process();
    CHECK(tracks.count()==1 && tracks.clips().size()==1); CHECK(!controller.projectDirty()); CHECK(!tracks.canUndo());
    CHECK(tracks.project().findTrack(tracks.trackIdAt(0))->audioClips[0].start==0);
    auto* loaded=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(tracks.trackIdAt(0)));
    CHECK(loaded->count()==1); CHECK(loaded->inputTrimDb()==-5); CHECK(!tracks.cleanMonitorArmed());
    CHECK(tracks.playbackSnapshot()->keepAudio[0]->channels==audio->channels);
    CHECK(controller.openProject(second)); io->process(); CHECK(tracks.project().findTrack(tracks.trackIdAt(0))->audioClips[0].start==48000);
    CHECK(controller.saveProject()); CHECK(QFile::exists(QString::fromStdString(audio->path))); controller.shutdownSession();
}
TEST(qtprojectfiles, corrupt_files_and_failed_effect_resources_keep_the_current_document) {
    Storage storage; auto device=std::make_unique<OfflineDevice>(); EngineController controller(std::move(device),false);
    // A guitar, so its (broken) pedalboard is actually loaded (plan 6.1).
    TrackListWrapper tracks; controller.attachProject(&tracks); const int track=tracks.addInstrumentTrackOfType("Electric Guitar");
    CHECK(tracks.addClipAtBeat(track,1,4)>0);
    const QUrl good=QUrl::fromLocalFile(storage.dir.path()+"/good.chaos"); CHECK(controller.saveProject(good));
    const QUrl bad=QUrl::fromLocalFile(storage.dir.path()+"/bad.chaos");
    { QFile file(bad.toLocalFile()); CHECK(file.open(QIODevice::WriteOnly)); file.write("broken"); }
    CHECK(!controller.openProject(bad)); CHECK(tracks.clips().size()==1); CHECK(controller.projectFile()==good);
    QJsonObject doc; { QFile file(good.toLocalFile()); CHECK(file.open(QIODevice::ReadOnly)); doc=QJsonDocument::fromJson(file.readAll()).object(); }
    auto effects=doc["trackEffects"].toArray(); auto fx=effects[0].toObject();
    fx["pedals"]=QJsonArray{QJsonObject{{"type","pedal.cabinet"},{"irPath",storage.dir.path()+"/missing.wav"}}};
    effects[0]=fx; doc["trackEffects"]=effects;
    { QFile file(bad.toLocalFile()); CHECK(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(doc).toJson()); }
    CHECK(controller.openProject(bad)); CHECK(controller.projectLoading());
    for(int i=0;i<100 && controller.projectLoading();++i)tick(10);
    CHECK(!controller.projectLoading()); CHECK(tracks.clips().size()==1); CHECK(controller.projectFile()==good);
    CHECK(!controller.transportMessage().isEmpty()); controller.shutdownSession();
}

TEST(qtrecording, dry_mode_keeps_raw_audio_and_edits_playback_without_changing_printed_takes) {
    Storage storage; auto device=std::make_unique<OfflineDevice>(); auto* io=device.get();
    EngineController controller(std::move(device),false); TrackListWrapper tracks; controller.attachProject(&tracks);
    const int track=tracks.addInstrumentTrackOfType("Electric Guitar"); CHECK(tracks.setRecordDry(track,true));
    auto* fx=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(track)); fx->setInputTrimDb(-6.020599913);
    CHECK(setCaptureAndMonitor(tracks,track,true)); std::fill_n(io->input,128,0.25f);
    for(int i=0;i<200;++i)io->process();
    controller.transportAction("record"); io->process(); CHECK(!tracks.setRecordDry(track,false));
    controller.transportAction("stop"); io->process(); for(int i=0;i<100 && controller.takeBusy();++i)tick(10);
    auto raw=tracks.playbackSnapshot()->keepAudio[0]; CHECK(raw->channels[0][64]==0.25f);
    CHECK(!tracks.project().findTrack(track)->audioClips[0].effectsPrinted);
    CHECK(setCaptureAndMonitor(tracks,track,false)); controller.transportAction("play"); io->process();
    CHECK_NEAR(io->output[64],0.125,1e-5);
    controller.transportAction("stop"); io->process(); fx->setInputTrimDb(-12.041199827);
    controller.transportAction("play"); io->process(); CHECK_NEAR(io->output[64],0.0625,1e-5);
    controller.transportAction("stop"); io->process(); CHECK(tracks.setRecordDry(track,false)); CHECK(setCaptureAndMonitor(tracks,track,true));
    for(int i=0;i<200;++i)io->process();
    controller.transportAction("record"); io->process(); controller.transportAction("stop"); io->process();
    for(int i=0;i<100 && controller.takeBusy();++i)tick(10);
    CHECK(tracks.project().findTrack(track)->audioClips[1].effectsPrinted);
    CHECK(setCaptureAndMonitor(tracks,track,false)); fx->setInputTrimDb(0); controller.transportAction("play"); io->process();
    CHECK_NEAR(io->output[64],0.3125,1e-5); // dry at current trim + unchanged printed take
    CHECK(raw->channels[0][64]==0.25f);
    const QUrl path=QUrl::fromLocalFile(storage.dir.path()+"/modes.chaos"); CHECK(controller.saveProject(path));
    CHECK(controller.newProject()); CHECK(controller.openProject(path)); io->process();
    const auto& clips=tracks.project().findTrack(tracks.trackIdAt(0))->audioClips;
    CHECK(clips.size()==2 && !clips[0].effectsPrinted && clips[1].effectsPrinted);
    controller.transportAction("play"); io->process(); CHECK_NEAR(io->output[64],0.3125,1e-5); controller.shutdownSession();
}

TEST(qttrackmeters, controller_polls_track_levels_and_removes_stale_project_meters) {
    Storage storage; auto device=std::make_unique<OfflineDevice>(); auto* io=device.get();
    EngineController controller(std::move(device),false); TrackListWrapper tracks; controller.attachProject(&tracks);
    const int a=tracks.addAudioTrack(), b=tracks.addVocalTrack();
    CHECK(setCaptureAndMonitor(tracks,a,true)); std::fill_n(io->input,128,0.25f); io->process(); tick(70);
    auto levels=controller.trackLevels();
    CHECK_NEAR(levels[QString::number(a)].toMap()["inputLeftDb"].toDouble(),-12.0412,0.01);
    CHECK_NEAR(levels[QString::number(a)].toMap()["outputRightDb"].toDouble(),-12.0412,0.01);
    CHECK(levels[QString::number(b)].toMap()["inputLeftDb"].toDouble()<=-90);
    CHECK(tracks.setMuted(a,true)); io->process();
    CHECK(tracks.removeTrack(b)); tick(70); CHECK(!controller.trackLevels().contains(QString::number(b)));
    CHECK(controller.newProject()); CHECK(controller.trackLevels().isEmpty());
    controller.shutdownSession();
}

TEST(qtmultirecording, independent_inputs_monitor_fx_and_aligned_wet_dry_takes_survive_reopen) {
    Storage storage; auto device=std::make_unique<OfflineDevice>(); auto* io=device.get(); io->multipleInputs=true;
    EngineController controller(std::move(device),false); TrackListWrapper tracks; controller.attachProject(&tracks);
    // Both guitars: the trim lives in GuitarSuite, which only guitar tracks keep (plan 6.1).
    const int a=tracks.addInstrumentTrackOfType("Electric Guitar"), b=tracks.addInstrumentTrackOfType("Acoustic Guitar");
    CHECK(tracks.setTrackInputPort(a,"test:input")); CHECK(tracks.setTrackInputPort(b,"test:mic"));
    auto* fxA=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(a));
    auto* fxB=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(b));
    fxA->setInputTrimDb(-6.020599913); fxB->setInputTrimDb(-12.041199827);
    CHECK(tracks.setRecordDry(b,true));
    std::fill_n(io->input,128,0.1f); std::fill_n(io->mic,128,0.4f);
    CHECK(setCaptureAndMonitor(tracks,a,true)); CHECK(setCaptureAndMonitor(tracks,b,true));
    for(int i=0;i<200;++i)io->process();
    CHECK(controller.monitoredTracks().size()==2);
    CHECK_NEAR(io->output[64],0.15,1e-5);
    CHECK(tracks.setMuted(b,true)); CHECK(tracks.setVolume(a,0.5)); io->process();
    CHECK_NEAR(io->output[64],0.025,1e-5);
    controller.seekBeat(2); controller.transportAction("record"); io->process(); io->process();
    CHECK(controller.recording()); CHECK(controller.recordingTracks().size()==2);
    for(int id:{a,b}) {
        CHECK(!tracks.removeTrack(id)); CHECK(!tracks.setTrackInputPort(id,"test:pickup")); CHECK(!tracks.setRecordDry(id,true));
    }
    controller.transportAction("pause"); io->process();
    for(int i=0;i<100 && controller.takeBusy();++i)tick(10);
    CHECK(!controller.takeBusy()); CHECK(!controller.playing()); CHECK(controller.positionSeconds()>0);
    auto snapshot=tracks.playbackSnapshot(); CHECK(snapshot->keepAudio.size()==2);
    const auto& clipA=tracks.project().findTrack(a)->audioClips[0]; const auto& clipB=tracks.project().findTrack(b)->audioClips[0];
    CHECK(clipA.start==48000 && clipB.start==clipA.start); CHECK(clipA.length==256 && clipB.length==256);
    CHECK(clipA.effectsPrinted); CHECK(!clipB.effectsPrinted);
    CHECK(snapshot->keepAudio[0]->channels.size()==2); CHECK(snapshot->keepAudio[1]->channels.size()==1);
    CHECK_NEAR(snapshot->keepAudio[0]->channels[0][64],0.05,1e-5); // independent of the fader
    CHECK_NEAR(snapshot->keepAudio[1]->channels[0][64],0.4,1e-6); // untouched source
    CHECK(setCaptureAndMonitor(tracks,a,false)); CHECK(setCaptureAndMonitor(tracks,b,false)); CHECK(tracks.setMuted(b,false));
    controller.seekBeat(2); controller.transportAction("play"); io->process();
    CHECK_NEAR(io->output[64],0.125,1e-5);
    const auto file=QUrl::fromLocalFile(storage.dir.path()+"/multi.chaos"); CHECK(controller.saveProject(file));
    CHECK(controller.newProject()); CHECK(controller.openProject(file)); io->process();
    CHECK(tracks.count()==2); CHECK(!tracks.cleanMonitorArmed());
    controller.seekBeat(2); controller.transportAction("play"); io->process();
    CHECK_NEAR(io->output[64],0.125,1e-5); controller.shutdownSession();
}

TEST(qtmultirecording, disarming_one_track_finishes_only_its_take_and_shutdown_finalizes_the_rest) {
    Storage storage; auto device=std::make_unique<OfflineDevice>(); auto* io=device.get(); io->multipleInputs=true;
    EngineController controller(std::move(device),false); TrackListWrapper tracks; controller.attachProject(&tracks);
    const int a=tracks.addAudioTrack(), b=tracks.addVocalTrack();
    CHECK(tracks.setTrackInputPort(b,"test:mic"));
    CHECK(setCaptureAndMonitor(tracks,a,true)); CHECK(setCaptureAndMonitor(tracks,b,true));
    std::fill_n(io->input,128,0.1f); std::fill_n(io->mic,128,0.2f);
    controller.transportAction("record"); io->process(); io->process();
    int monitorUpdates=0;
    QObject::connect(&controller,&EngineController::monitoredTrackChanged,&controller,[&]{++monitorUpdates;});
    CHECK(setCaptureAndMonitor(tracks,a,false)); io->process();
    CHECK(monitorUpdates>0); CHECK(controller.monitoredTracks().size()==1);
    for(int i=0;i<100 && tracks.project().findTrack(a)->audioClips.empty();++i)tick(10);
    CHECK(controller.recording()); CHECK(controller.recordingTracks().size()==1);
    CHECK(tracks.project().findTrack(a)->audioClips.size()==1);
    io->process(); controller.shutdownSession();
    CHECK(!controller.takeBusy());
    auto snapshot=tracks.playbackSnapshot(); CHECK(snapshot->keepAudio.size()==2);
    CHECK(snapshot->keepAudio[0]->frames==256); CHECK(snapshot->keepAudio[1]->frames==512);
    CHECK_NEAR(snapshot->keepAudio[0]->channels[0][0],0.1,1e-6);
    CHECK_NEAR(snapshot->keepAudio[1]->channels[0][0],0.2,1e-6);
}

TEST(qtmultirecording, shared_input_can_feed_two_independent_chains_and_invalid_routes_do_not_start_takes) {
    Storage storage; auto device=std::make_unique<OfflineDevice>(); auto* io=device.get(); io->multipleInputs=true;
    EngineController controller(std::move(device),false); TrackListWrapper tracks; controller.attachProject(&tracks);
    const int a=tracks.addAudioTrack(), b=tracks.addAudioTrack();
    CHECK(setCaptureAndMonitor(tracks,a,true)); CHECK(setCaptureAndMonitor(tracks,b,true));
    CHECK(tracks.setVolume(a,0.5)); std::fill_n(io->input,128,0.2f); io->process();
    CHECK_NEAR(io->output[64],0.3,1e-6);
    CHECK(tracks.setSolo(b,true)); io->process(); CHECK_NEAR(io->output[64],0.2,1e-6);
    CHECK(tracks.setTrackInputPort(b,"missing:port")); controller.transportAction("record");
    CHECK(!controller.takeBusy()); CHECK(!controller.transportMessage().isEmpty());
    io->failingSource="test:mic";
    CHECK(tracks.setTrackInputPort(b,"test:mic")); controller.transportAction("record");
    CHECK(!controller.takeBusy()); CHECK(controller.transportMessage().contains("connect track input"));
    io->failingSource.clear(); controller.transportAction("record"); io->process();
    CHECK(controller.recording()); io->missingInput=true; io->process();
    for(int i=0;i<100 && controller.takeBusy();++i)tick(10);
    CHECK(!controller.recording()); CHECK(!controller.takeBusy()); CHECK(tracks.clips().size()==2);
    controller.shutdownSession();
}

TEST(qttracknames, rename_validates_names_notifies_views_and_preserves_track_identity_and_history) {
    Storage storage; TrackListWrapper tracks;
    const int a=tracks.addVocalTrack(), b=tracks.addInstrumentTrackOfType("Electric Guitar");
    const int clip=tracks.addClipAtBeat(a,1,4); CHECK(clip>0);
    CHECK(setCaptureAndMonitor(tracks,a,true)); CHECK(tracks.setVolume(a,0.6));
    int changes=0; QList<int> roles;
    QObject::connect(&tracks,&QAbstractItemModel::dataChanged,&tracks,[&](const QModelIndex& first,const QModelIndex&,const QList<int>& changed){
        CHECK(first.row()==0); ++changes; roles=changed;
    });
    const QString name=QString::fromUtf8("  Lead voix — रात 🎤  ");
    CHECK(tracks.renameTrack(a,name)); CHECK(changes==1);
    CHECK(roles.contains(TrackListWrapper::NameRole)); CHECK(roles.contains(Qt::DisplayRole)); CHECK(roles.contains(Qt::EditRole));
    const auto row=tracks.index(0);
    CHECK(tracks.data(row,TrackListWrapper::NameRole).toString()==name.trimmed());
    CHECK(tracks.data(row,Qt::EditRole).toString()==name.trimmed());
    CHECK(tracks.project().findTrack(a)->type=="Vocal"); CHECK(tracks.project().findTrack(a)->recordArmed);
    CHECK_NEAR(tracks.project().findTrack(a)->volume,0.6,1e-6);
    CHECK(tracks.project().findTrack(a)->audioClips[0].id==clip);
    CHECK(tracks.project().findTrack(b)->name=="Electric Guitar 1");
    CHECK(tracks.renameTrack(a,name)); CHECK(changes==1); // no dirty notification for an unchanged name
    for(const auto& invalid:QStringList{"", "   ", "two\nlines", "two\tparts",QString(257,'x'),QString(QChar(0))})
        CHECK(!tracks.renameTrack(a,invalid));
    CHECK(!tracks.renameTrack(-1,"Missing")); CHECK(changes==1);
    CHECK(tracks.setData(row,"Harmony",TrackListWrapper::NameRole)); CHECK(changes==2);
    CHECK(tracks.setData(row,"Final vocal",Qt::EditRole)); CHECK(changes==3);
    CHECK(tracks.undo()); CHECK(tracks.project().findTrack(a)->audioClips.empty());
    CHECK(tracks.project().findTrack(a)->name=="Final vocal"); // existing clip history remains usable
    CHECK(tracks.redo()); CHECK(tracks.project().findTrack(a)->audioClips[0].id==clip);
}

TEST(qttracknames, renaming_dirties_project_persists_on_reopen_and_is_safe_during_recording) {
    Storage storage; auto device=std::make_unique<OfflineDevice>(); auto* io=device.get();
    EngineController controller(std::move(device),false); TrackListWrapper tracks; controller.attachProject(&tracks);
    const int track=tracks.addAudioTrack(); const auto file=QUrl::fromLocalFile(storage.dir.path()+"/names.chaos");
    CHECK(controller.saveProject(file)); CHECK(!controller.projectDirty());
    CHECK(tracks.renameTrack(track,"Audio 1")); CHECK(!controller.projectDirty());
    CHECK(!tracks.renameTrack(track,"   ")); CHECK(!controller.projectDirty());
    CHECK(tracks.renameTrack(track,QString::fromUtf8("Rhythm — गिटार"))); CHECK(controller.projectDirty());
    CHECK(setCaptureAndMonitor(tracks,track,true)); std::fill_n(io->input,128,0.25f);
    controller.transportAction("record"); io->process(); CHECK(controller.recording());
    CHECK(tracks.renameTrack(track,QString::fromUtf8("Main — गिटार"))); io->process();
    CHECK(controller.recording()); CHECK(tracks.recordingTrackId()==track);
    controller.transportAction("stop"); io->process();
    for(int i=0;i<100 && controller.takeBusy();++i)tick(10);
    CHECK(!controller.takeBusy()); CHECK(tracks.clips().size()==1);
    auto recorded=tracks.playbackSnapshot()->keepAudio[0]; CHECK(recorded->frames==256);
    for(float sample:recorded->channels[0])CHECK(sample==0.25f);
    CHECK(controller.saveProject()); CHECK(!controller.projectDirty());
    CHECK(controller.newProject()); CHECK(controller.openProject(file)); io->process();
    CHECK(tracks.count()==1); CHECK(tracks.data(tracks.index(0),TrackListWrapper::NameRole).toString()==QString::fromUtf8("Main — गिटार"));
    CHECK(tracks.project().findTrack(tracks.trackIdAt(0))->type=="Audio");
    CHECK(tracks.playbackSnapshot()->keepAudio[0]->channels==recorded->channels);
    CHECK(!controller.projectDirty()); controller.shutdownSession();
}

TEST(qttrackcolours, colours_validate_notify_clips_and_preserve_other_tracks_and_history) {
    Storage storage; TrackListWrapper tracks;
    const int a=tracks.addVocalTrack(),b=tracks.addAudioTrack();
    const int clip=tracks.addClipAtBeat(a,0,4); CHECK(clip>0);
    const auto other=tracks.project().findTrack(b)->colour;
    int changes=0,clipsChanged=0;
    QObject::connect(&tracks,&QAbstractItemModel::dataChanged,&tracks,[&](const QModelIndex& first,const QModelIndex&,const QList<int>& roles){
        CHECK(first.row()==0); CHECK(roles.contains(TrackListWrapper::ColourRole)); ++changes;
    });
    QObject::connect(&tracks,&TrackListWrapper::clipsChanged,&tracks,[&]{++clipsChanged;});
    CHECK(tracks.setTrackColour(a,"  #A1B2C3  ")); CHECK(changes==1 && clipsChanged==1);
    CHECK(tracks.data(tracks.index(0),TrackListWrapper::ColourRole).toString()=="#a1b2c3");
    CHECK(tracks.clips()[0].toMap()["trackColour"].toString()=="#a1b2c3");
    CHECK(tracks.project().findTrack(b)->colour==other);
    CHECK(tracks.project().findTrack(a)->type=="Vocal");
    CHECK(tracks.setTrackColour(a,"#a1b2c3")); CHECK(changes==1 && clipsChanged==1);
    for(const auto& invalid:QStringList{"", "red", "#abc", "#12345g", "#ff123456", "#1234567"})CHECK(!tracks.setTrackColour(a,invalid));
    CHECK(!tracks.setTrackColour(-1,"#123456")); CHECK(changes==1);
    CHECK(tracks.setData(tracks.index(0),"#765432",TrackListWrapper::ColourRole)); CHECK(changes==2);
    CHECK(tracks.project().findTrack(a)->audioClips[0].id==clip); CHECK(tracks.canUndo());
    // Clip history is unaffected by appearance changes.
    QObject::disconnect(&tracks,&QAbstractItemModel::dataChanged,&tracks,nullptr);
    CHECK(tracks.undo()); CHECK(tracks.project().findTrack(a)->audioClips.empty());
    CHECK(tracks.project().findTrack(a)->colour=="#765432");
    CHECK(tracks.redo()); CHECK(tracks.clips()[0].toMap()["trackColour"].toString()=="#765432");
}

TEST(qttrackcolours, project_and_session_roundtrip_support_legacy_files_and_reject_invalid_colours) {
    Storage storage; auto device=std::make_unique<OfflineDevice>();
    EngineController controller(std::move(device),false); TrackListWrapper tracks; controller.attachProject(&tracks);
    const int a=tracks.addAudioTrack(),b=tracks.addVocalTrack(); CHECK(tracks.addClipAtBeat(a,1,4)>0);
    const auto file=QUrl::fromLocalFile(storage.dir.path()+"/colours.chaos");
    CHECK(controller.saveProject(file)); CHECK(!controller.projectDirty());
    CHECK(!tracks.setTrackColour(a,"broken")); CHECK(!controller.projectDirty());
    CHECK(tracks.setTrackColour(a,QString::fromStdString(tracks.project().findTrack(a)->colour))); CHECK(!controller.projectDirty());
    CHECK(tracks.setTrackColour(a,"#123456")); CHECK(tracks.setTrackColour(b,"#fedcba")); CHECK(controller.projectDirty());
    CHECK(controller.saveProject()); CHECK(!controller.projectDirty());
    TrackListWrapper session; QString error; CHECK(session.restoreSessionState(tracks.sessionState(),error));
    CHECK(session.project().findTrack(session.trackIdAt(0))->colour=="#123456");
    CHECK(session.project().findTrack(session.trackIdAt(1))->colour=="#fedcba");
    CHECK(controller.newProject()); CHECK(controller.openProject(file));
    CHECK(tracks.project().findTrack(tracks.trackIdAt(0))->colour=="#123456");
    CHECK(tracks.project().findTrack(tracks.trackIdAt(1))->colour=="#fedcba");
    CHECK(tracks.clips()[0].toMap()["trackColour"].toString()=="#123456");
    QJsonObject document; { QFile input(file.toLocalFile()); CHECK(input.open(QIODevice::ReadOnly)); document=QJsonDocument::fromJson(input.readAll()).object(); }
    const auto invalid=QUrl::fromLocalFile(storage.dir.path()+"/invalid.chaos");
    auto project=document["project"].toObject(); auto rows=project["tracks"].toArray();
    auto first=rows[0].toObject(); first["colour"]="#invalid"; rows[0]=first; project["tracks"]=rows; document["project"]=project;
    { QFile output(invalid.toLocalFile()); CHECK(output.open(QIODevice::WriteOnly)); output.write(QJsonDocument(document).toJson()); }
    CHECK(!controller.openProject(invalid)); CHECK(controller.projectFile()==file);
    CHECK(tracks.project().findTrack(tracks.trackIdAt(0))->colour=="#123456");
    for(int i=0;i<rows.size();++i) { auto row=rows[i].toObject(); row.remove("colour"); rows[i]=row; }
    project["tracks"]=rows; document["project"]=project;
    const auto legacy=QUrl::fromLocalFile(storage.dir.path()+"/legacy.chaos");
    { QFile output(legacy.toLocalFile()); CHECK(output.open(QIODevice::WriteOnly)); output.write(QJsonDocument(document).toJson()); }
    CHECK(controller.openProject(legacy)); CHECK(tracks.count()==2 && tracks.clips().size()==1);
    CHECK(tracks.project().findTrack(tracks.trackIdAt(0))->colour=="#58b7b1");
    CHECK(tracks.project().findTrack(tracks.trackIdAt(1))->colour=="#6fa8dc");
    CHECK(!controller.projectDirty()); controller.shutdownSession();
}

TEST(qtmidirecording, armed_instrument_track_records_midi_into_timeline_clip) {
    Storage storage;
    auto device = std::make_unique<OfflineDevice>();
    auto* io = device.get();
    EngineController controller(std::move(device), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);

    const int synth = tracks.addInstrumentTrack();
    CHECK(synth >= 0);
    CHECK(tracks.setRecordArmed(synth, true));

    tick(); io->process();
    controller.transportAction("record");
    CHECK(controller.takeBusy());
    io->process();

    controller.sendLiveNoteOn(synth, 60, 100);
    io->process();
    controller.sendLiveNoteOff(synth, 60);
    io->process();

    controller.transportAction("stop");
    io->process();
    for (int i = 0; i < 100 && controller.takeBusy(); ++i) tick(10);

    CHECK(!controller.takeBusy());
    const auto clips = tracks.clips();
    CHECK(clips.size() == 1);
    const auto clip = clips[0].toMap();
    CHECK(clip["trackId"].toInt() == synth);
    const auto notes = clip["notes"].toList();
    CHECK(notes.size() == 1);
    CHECK(notes[0].toMap()["note"].toInt() == 60);
    CHECK(notes[0].toMap()["velocity"].toInt() == 100);
    controller.shutdownSession();
}

TEST(qtrecording, undoing_a_track_removal_restores_its_pedalboard) {
    Storage storage;
    auto device=std::make_unique<OfflineDevice>();
    EngineController controller(std::move(device),false); TrackListWrapper tracks; controller.attachProject(&tracks);
    const int guitar=tracks.addInstrumentTrackOfType("Electric Guitar");
    auto* fx=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(guitar)); CHECK(fx);
    CHECK(fx->addPedal("Delay")); CHECK(fx->addPedal("Reverb")); CHECK(fx->count()==2);
    CHECK(tracks.removeTrack(guitar));
    CHECK(!controller.trackEffects(guitar));
    CHECK(tracks.undo());
    auto* back=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(guitar)); CHECK(back);
    CHECK(back && back->count()==2);
    // A different project must not inherit a removed track's pedalboard
    CHECK(tracks.redo());
    CHECK(controller.newProject());
    const int fresh=tracks.addInstrumentTrackOfType("Electric Guitar");
    auto* freshFx=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(fresh)); CHECK(freshFx);
    CHECK(freshFx && freshFx->count()==0);
    controller.shutdownSession();
}

TEST(qttrackeffects, effects_role_lists_the_chain_and_edits_follow_the_backend_rules) {
    Storage storage; TrackListWrapper tracks;
    const int guitar=tracks.addInstrumentTrackOfType("Electric Guitar"), vocal=tracks.addVocalTrack();
    const auto effects=[&](int id) { return tracks.data(tracks.index(tracks.indexOfTrack(id)),TrackListWrapper::EffectsRole).toList(); };
    CHECK(tracks.roleNames().value(TrackListWrapper::EffectsRole)=="effects");
    // Guitar: GuitarSuite, fixed. Vocal: nothing.
    CHECK(effects(guitar).size()==1);
    const auto suite=effects(guitar)[0].toMap();
    CHECK(suite["type"].toString()=="guitarsuite" && suite["title"].toString()=="GuitarSuite" && !suite["removable"].toBool());
    CHECK(effects(vocal).isEmpty());
    // The picker offers each EQ once: the 10-band Equalizer and Chaos EQ (plan 6.2), then the
    // ten mixing plugins (restMap G7, G7b, G13 noise reduction, G18 pitch monitor).
    CHECK(tracks.addableEffects(vocal).size()==12);
    CHECK(tracks.addableEffects(vocal)[0].toMap()["title"].toString()=="Equalizer");
    CHECK(tracks.addableEffects(vocal)[1].toMap()["title"].toString()=="Chaos EQ");
    int roleChanges=0, projectChanges=0;
    QObject::connect(&tracks,&QAbstractItemModel::dataChanged,&tracks,[&](const QModelIndex&,const QModelIndex&,const QList<int>& roles) {
        if(roles.contains(TrackListWrapper::EffectsRole)) ++roleChanges; });
    QObject::connect(&tracks,&TrackListWrapper::projectChanged,&tracks,[&] { ++projectChanges; });
    const int eq=tracks.addTrackEffect(vocal,"eq"); CHECK(eq>0);
    CHECK(roleChanges==1 && projectChanges==1);
    CHECK(effects(vocal).size()==1 && effects(vocal)[0].toMap()["id"].toInt()==eq && effects(vocal)[0].toMap()["removable"].toBool());
    CHECK(tracks.addableEffects(vocal).size()==11 && tracks.addableEffects(vocal)[0].toMap()["type"].toString()=="chaoseq");
    CHECK(tracks.addTrackEffect(vocal,"eq")==-1 && tracks.addTrackEffect(vocal,"guitarsuite")==-1);
    CHECK(roleChanges==1);                                          // refused edits notify nothing
    // Move and remove on the guitar; GuitarSuite cannot be removed.
    const int guitarEq=tracks.addTrackEffect(guitar,"eq");
    CHECK(tracks.moveTrackEffect(guitar,guitarEq,0));
    CHECK(effects(guitar)[0].toMap()["type"].toString()=="eq" && effects(guitar)[1].toMap()["type"].toString()=="guitarsuite");
    CHECK(!tracks.removeTrackEffect(guitar,suite["id"].toInt()));
    CHECK(tracks.removeTrackEffect(guitar,guitarEq)); CHECK(effects(guitar).size()==1);
    CHECK(!tracks.canUndo());                                       // not undo steps
    // A recording track's chain is locked.
    tracks.domain().lockRecordingTrack(vocal);
    CHECK(!tracks.removeTrackEffect(vocal,eq)); CHECK(tracks.addableEffects(vocal).isEmpty());
    CHECK(tracks.addTrackEffect(guitar,"eq")>0);                    // other tracks are not locked
    tracks.domain().lockRecordingTrack(-1);
    CHECK(tracks.removeTrackEffect(vocal,eq));
}

TEST(qttrackeffects, the_engine_runs_the_eq_only_while_it_is_on_the_track) {
    Storage storage; auto device=std::make_unique<OfflineDevice>(); auto* io=device.get();
    EngineController controller(std::move(device),false); TrackListWrapper tracks; controller.attachProject(&tracks);
    const int vocal=tracks.addVocalTrack();
    CHECK(tracks.setTrackInputPort(vocal,"test:mic")); CHECK(tracks.setVolume(vocal,0.5));
    for(int band=0;band<10;++band) CHECK(tracks.setEqBand(vocal,band,12));
    CHECK(setCaptureAndMonitor(tracks,vocal,true));
    // A 1 kHz tone (an EQ band centre), compared by the level of a whole block.
    for(int i=0;i<128;++i) io->input[i]=float(0.1*std::sin(2*M_PI*1000.0*i/48000.0));
    const auto rms=[](const float* x) { double sum=0; for(int i=0;i<128;++i) sum+=double(x[i])*x[i]; return std::sqrt(sum/128); };
    const auto listen=[&] { tick(); for(int i=0;i<40;++i) io->process(); return rms(io->output); };
    const double dry=rms(io->input)*0.5;
    CHECK_NEAR(listen(),dry,1e-3);                                  // bands set, but no EQ on the chain
    CHECK(controller.saveProject(QUrl::fromLocalFile(storage.dir.path()+"/fx.chaos"))); CHECK(!controller.projectDirty());
    const int eq=tracks.addTrackEffect(vocal,"eq"); CHECK(eq>0);
    CHECK(listen()>dry*2);                                          // the +12 dB boost is heard now
    CHECK(tracks.removeTrackEffect(vocal,eq));
    CHECK_NEAR(listen(),dry,1e-3);
    controller.shutdownSession();
}

TEST(qttrackeffects, chaos_eq_wrapper_edits_the_track_bands_without_rebuilding_playback) {
    Storage storage; TrackListWrapper tracks;
    const int vocal=tracks.addVocalTrack(), other=tracks.addVocalTrack();
    CHECK(tracks.addTrackEffect(vocal,"chaoseq")>0);
    int projectChanges=0, chaosChanges=0;
    QObject::connect(&tracks,&TrackListWrapper::projectChanged,&tracks,[&] { ++projectChanges; });
    QObject::connect(&tracks,&TrackListWrapper::chaosEqChanged,&tracks,[&](int id) { if(id==vocal) ++chaosChanges; });
    ChaosEqWrapper eq, mirror;
    eq.setTrackModel(&tracks); eq.setTrackId(vocal);
    mirror.setTrackModel(&tracks); mirror.setTrackId(vocal);
    CHECK(eq.bandCount()==0 && eq.canAddBand() && eq.bands().isEmpty());
    CHECK(eq.curveFrequencies().size()==ChaosEqWrapper::CurvePoints && eq.totalCurve().size()==ChaosEqWrapper::CurvePoints);
    CHECK(eq.shapes().size()==8 && eq.placements().size()==5 && eq.slopes().size()==4);

    const int first=eq.addBand(1000,3); CHECK(first==0);
    CHECK(chaosChanges==1 && projectChanges==0);                   // no playback rebuild per edit
    auto band=eq.bands()[0].toMap();
    CHECK(band["shape"].toString()=="bell" && std::abs(band["gain"].toDouble()-3)<1e-9 && band["usesGain"].toBool());
    CHECK(mirror.bandCount()==1);                                   // every view of the track follows
    CHECK(tracks.project().findTrack(vocal)->chaosEq.bands[0].inUse);
    CHECK(tracks.project().findTrack(other)->chaosEq.bands[0].inUse==false);
    CHECK(eq.bandCurves().contains("0"));

    CHECK(eq.setBand(first,{{"shape","lowCut"},{"slope",48},{"frequency",1e9}}));
    band=eq.bands()[0].toMap();
    CHECK(band["shape"].toString()=="lowCut" && band["slope"].toInt()==48 && !band["usesGain"].toBool());
    CHECK(band["frequency"].toDouble()<=eq.maxFrequency());          // clamped into range
    // Bad ids refuse the whole change.
    CHECK(!eq.setBand(first,{{"shape","wobble"},{"gain",6}}));
    CHECK(!eq.setBand(first,{{"slope",18}}));
    CHECK(!eq.setBand(5,{{"gain",1}}));                              // slot not in use
    CHECK(eq.bands()[0].toMap()["shape"].toString()=="lowCut");

    // Solo is exclusive.
    const int second=eq.addBand(200,-4); CHECK(second==1);
    CHECK(eq.setBand(first,{{"solo",true}})); CHECK(eq.setBand(second,{{"solo",true}}));
    CHECK(!eq.bands()[0].toMap()["solo"].toBool() && eq.bands()[1].toMap()["solo"].toBool());

    eq.setOutputGain(-50); CHECK(eq.outputGain()==eq.minOutputGain());
    eq.setAutoGain(true); CHECK(eq.autoGain() && mirror.autoGain());
    CHECK(eq.removeBand(first) && eq.bandCount()==1 && eq.bands()[0].toMap()["slot"].toInt()==1);
    // 24 bands at most.
    while(eq.canAddBand()) CHECK(eq.addBand(500,1)>=0);
    CHECK(eq.bandCount()==24 && eq.addBand(500,1)==-1);
    eq.removeAllBands(); CHECK(eq.bandCount()==0);
    CHECK(projectChanges==0);
    // No track: nothing can be added.
    eq.setTrackId(9999); CHECK(!eq.canAddBand() && eq.addBand(100,1)==-1);
}

TEST(qttrackeffects, chaos_eq_reaches_the_engine_and_feeds_the_analyzer) {
    Storage storage;
    auto device=std::make_unique<OfflineDevice>();
    EngineController controller(std::move(device),false); TrackListWrapper tracks; controller.attachProject(&tracks);
    const int vocal=tracks.addVocalTrack(); CHECK(tracks.addTrackEffect(vocal,"chaoseq")>0);
    ChaosEqWrapper eq; eq.setTrackModel(&tracks); eq.setTrackId(vocal); eq.setEngine(&controller);
    const auto processors=controller.chaosEqProcessors(vocal);
    CHECK(processors[0] && processors[1]);
    CHECK(eq.addBand(2000,6)==0);
    for(const auto& p:processors) {
        const auto s=p->settings();
        CHECK(s.bands[0].inUse && std::abs(s.bands[0].filter.gainDb-6)<1e-6);
    }

    // Analyzer: first poll starts listening; audio through the EQ then shows up at its frequency.
    eq.setAnalyzerEnabled(true);
    eq.pollAnalyzer(0.0); CHECK(eq.spectrum().isEmpty());
    CHECK(processors[0]->analyzerListening());
    std::vector<float> left(4096), right(4096);
    for(int i=0;i<4096;++i) left[std::size_t(i)]=right[std::size_t(i)]=float(0.5*std::sin(6.283185307179586*1000.0*i/48000.0));
    float* channels[]{left.data(),right.data()};
    ProcessContext context; context.audio=AudioView{channels,2,4096}; context.sampleRate=48000;
    processors[0]->process(context);
    eq.pollAnalyzer(0.03);
    const auto spectrum=eq.spectrum(); CHECK(spectrum.size()==ChaosEqWrapper::CurvePoints);
    const auto frequencies=eq.curveFrequencies();
    int peak=0;
    for(int i=1;i<spectrum.size();++i) if(spectrum[i].toDouble()>spectrum[peak].toDouble()) peak=i;
    CHECK(std::abs(std::log2(frequencies[peak].toDouble()/1000.0))<0.1);
    eq.setAnalyzerEnabled(false);
    CHECK(eq.spectrum().isEmpty() && !processors[0]->analyzerListening() && !processors[1]->analyzerListening());
}

// An instrument may be playing on the audio thread, so it is prepared once when it is made and
// replaced, not re-prepared, when the buffer changes; the new one must reach the audio thread.
TEST(qtrecording, instrument_is_prepared_once_and_replaced_when_the_buffer_changes) {
    Storage storage;
    auto device=std::make_unique<OfflineDevice>(); auto* io=device.get();
    EngineController controller(std::move(device),false); TrackListWrapper tracks; controller.attachProject(&tracks);
    const int piano=tracks.addInstrumentTrackOfType("Strings");   // still TestSynth, so its voices can be read
    const int audio=tracks.addAudioTrack();
    tick(); io->process();
    const auto first=controller.trackInstrument(piano);
    CHECK(first!=nullptr);
    CHECK(controller.trackInstrument(audio)==nullptr);

    const int drums=tracks.addInstrumentTrackOfType("Drums");    // edits sync every track
    CHECK(tracks.renameTrack(piano,"Keys"));
    tick(); io->process();
    CHECK(controller.trackInstrument(piano)==first);

    // Alone, so no other track's pedalboard republishes playback after the piano is replaced.
    CHECK(tracks.removeTrack(drums)); CHECK(tracks.removeTrack(audio));
    tick(); io->process();
    CHECK(controller.trackInstrument(piano)==first);

    CHECK(controller.setBufferSize(256));
    const auto second=controller.trackInstrument(piano);
    CHECK(second!=nullptr && second!=first);
    CHECK(second->typeId()==first->typeId());

    controller.sendLiveNoteOn(piano,60,100);                     // reaches the new instrument
    io->process();
    const auto* synth=dynamic_cast<const TestSynth*>(second.get());
    CHECK(synth!=nullptr);
    bool sounding=false;
    if(synth) for(const auto& voice:synth->voices()) sounding=sounding||voice.active;
    CHECK(sounding);
    controller.sendLiveNoteOff(piano,60);
    controller.shutdownSession();
}

TEST(qtrecording, the_waveform_of_a_take_grows_while_it_records) {
    Storage storage;
    auto device=std::make_unique<OfflineDevice>(); auto* io=device.get();
    EngineController controller(std::move(device),false); TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int vocal=tracks.addVocalTrack(); CHECK(setCaptureAndMonitor(tracks,vocal,true));
    tick(); io->process();
    CHECK(controller.recordingWaveform(vocal,10).isEmpty());   // not recording
    for(int i=0;i<128;++i) io->input[i]=(i%2 ? 0.5f : -0.5f);
    controller.transportAction("record"); io->process();
    CHECK(controller.recording());
    const auto framesShown=[&] {
        QVariantList peaks; for(int i=0;i<100 && (peaks=controller.recordingWaveform(vocal,4)).isEmpty();++i) tick(5);
        return peaks;
    };
    for(int i=0;i<8;++i) io->process();
    auto first=framesShown();
    CHECK(first.size()==4);
    if(!first.isEmpty()) { CHECK_NEAR(first[0].toList()[0].toDouble(),-0.5,1e-6); CHECK_NEAR(first[0].toList()[1].toDouble(),0.5,1e-6); }
    for(int i=0;i<200;++i) io->process();
    tick(30);
    CHECK(controller.recordingWaveform(vocal,4,0.5,1.0).size()==4);   // a part of it
    CHECK(controller.recordingWaveform(9999,4).isEmpty());
    controller.transportAction("stop"); io->process();
    for(int i=0;i<100 && controller.takeBusy();++i) tick(10);
    CHECK(controller.recordingWaveform(vocal,4).isEmpty());          // the clip takes over
    CHECK(tracks.clips().size()==1);
    controller.shutdownSession();
}

TEST(qtrecording, audio_takes_move_earlier_by_the_reported_round_trip_plus_the_extra_offset) {
    // restMap G10. 48 kHz: the device reports 480 frames (10 ms), the extra offset is 2 ms (96).
    Storage storage;
    auto device=std::make_unique<OfflineDevice>(); auto* io=device.get(); io->roundTrip=480;
    EngineController controller(std::move(device),false); TrackListWrapper tracks;
    controller.attachProject(&tracks);
    CHECK(controller.recordingLatencyCompensation());                 // on by default
    CHECK(controller.recordingExtraOffsetMs()==0.0);
    controller.setRecordingExtraOffsetMs(2.0);
    controller.setRecordingExtraOffsetMs(500.0); CHECK(controller.recordingExtraOffsetMs()==100.0);   // clamped
    controller.setRecordingExtraOffsetMs(2.0);
    CHECK_NEAR(controller.reportedRoundTripMs(),10.0,1e-9);
    const int vocal=tracks.addVocalTrack(); CHECK(setCaptureAndMonitor(tracks,vocal,true));
    const auto take=[&](double beat) {
        tick(); io->process();
        controller.seekBeat(beat); io->process();
        controller.transportAction("record"); CHECK(controller.takeBusy());
        for(int i=0;i<8;++i) io->process();
        controller.transportAction("stop"); io->process();
        for(int i=0;i<100 && controller.takeBusy();++i) tick(10);
        CHECK(!controller.takeBusy());
        const auto& clips=tracks.project().findTrack(vocal)->audioClips;
        CHECK(!clips.empty());
        return clips.back();
    };
    // From beat 1 (24000 at 120 BPM): 576 frames earlier, nothing trimmed.
    auto clip=take(1.0);
    CHECK(clip.start==24000-576); CHECK(clip.sourceOffset==0);
    // From the start: the first 576 frames would sit before 0, so they are trimmed off.
    const auto whole=tracks.audioFor(clip.poolId)->frames;
    clip=take(0.0);
    CHECK(clip.start==0); CHECK(clip.sourceOffset==576);
    CHECK(clip.length==tracks.audioFor(clip.poolId)->frames-576);
    CHECK(whole>576);
    // Off: placed where capture began.
    controller.setRecordingLatencyCompensation(false);
    CHECK(!controller.recordingLatencyCompensation());
    clip=take(2.0);
    CHECK(clip.start==48000); CHECK(clip.sourceOffset==0);
    controller.shutdownSession();
}

TEST(qtrecording, loop_recording_makes_takes_and_punch_keeps_only_the_time_selection) {
    Storage storage;
    auto device=std::make_unique<OfflineDevice>(); auto* io=device.get();
    EngineController controller(std::move(device),false); TrackListWrapper tracks;
    controller.attachProject(&tracks); controller.setBpm(120);
    const int vocal=tracks.addVocalTrack(); CHECK(setCaptureAndMonitor(tracks,vocal,true));
    tick(); io->process();
    for(int i=0;i<128;++i) io->input[i]=0.25f;
    const auto finish=[&] {
        controller.transportAction("stop"); io->process();
        for(int i=0;i<200 && controller.takeBusy();++i) tick(10);
        CHECK(!controller.takeBusy());
    };
    const auto clipAt=[&](double beat) {
        for (const auto& value : tracks.clips()) {
            const auto clip=value.toMap();
            if (std::abs(clip["startBeat"].toDouble()-beat)<1e-6) return clip;
        }
        return QVariantMap{};
    };

    // Repeat over beats 0 .. 0.5 (12000 samples), recorded from 0 for 3.2 passes (restMap G11).
    CHECK(controller.setRepeatRange(0,0.5)); controller.transportAction("repeat"); CHECK(controller.repeating());
    controller.seekBeat(0); io->process();
    controller.transportAction("record"); CHECK(controller.takeBusy());
    for(int i=0;i<300;++i) io->process();
    CHECK(controller.repeating() && controller.positionBeats()<=0.5);   // still looping
    finish();
    CHECK(tracks.clips().size()==1);
    auto clip=clipAt(0);
    CHECK(clip["takeCount"].toInt()==3 && clip["activeTake"].toInt()==2 && std::abs(clip["lengthBeats"].toDouble()-0.5)<1e-6);
    const QVariantList ids{clip["clipId"]};
    CHECK(tracks.takeAction(ids,"next"));
    CHECK(clipAt(0)["activeTake"].toInt()==0);
    CHECK(tracks.takeAction(ids,"set",1) && clipAt(0)["activeTake"].toInt()==1);
    CHECK(!tracks.takeAction(ids,"set",1) && !tracks.takeAction(ids,"bogus"));

    // PUNCH needs a time selection.
    controller.transportAction("repeat"); CHECK(!controller.repeating());
    CHECK(controller.setRepeatRange(0,0));
    controller.transportAction("punch"); CHECK(controller.punch());
    controller.seekBeat(0); io->process();
    controller.transportAction("record");
    CHECK(!controller.takeBusy() && controller.transportMessage().contains("PUNCH"));
    // Punch over beats 0.25 .. 0.5, recorded from 0: the old part becomes the first takes.
    CHECK(controller.setRepeatRange(0.25,0.5));
    controller.transportAction("record"); CHECK(controller.takeBusy());
    controller.transportAction("punch"); CHECK(controller.punch());   // not while a take runs
    for(int i=0;i<120;++i) io->process();
    finish();
    CHECK(tracks.clips().size()==2);
    CHECK(clipAt(0)["takeCount"].toInt()==3);                      // the left part keeps its takes
    const auto punched=clipAt(0.25);
    CHECK(punched["takeCount"].toInt()==4 && punched["activeTake"].toInt()==3);
    CHECK(std::abs(punched["lengthBeats"].toDouble()-0.25)<1e-6);
    CHECK(tracks.takeAction({punched["clipId"]},"crop") && clipAt(0.25)["takeCount"].toInt()==1);
    controller.shutdownSession();
}
