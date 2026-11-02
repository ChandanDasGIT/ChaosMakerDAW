#include "testkit.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/pluginWrap/guitarsuitemodel.h"
#include "audiodevice/audiodevice.h"
#include "recording/takerecorder.h"
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
    DeviceLatency latency() const override { return {128,128,128}; }
    bool setBufferSize(int) override { return true; }
    std::vector<std::string> inputNames() const override { return {"test:input","test:mic","test:pickup"}; }
    std::vector<std::string> outputNames() const override { return {"test:left","test:right"}; }
    std::string connectedInput;
    bool connectInput(const std::string& source) override { connectedInput=source; return true; }
    bool connectOutput(const std::string&,const std::string&) override { return true; }
    std::string lastError() const override { return {}; }
    float input[128]{}, output[128]{}, right[128]{};
    void process() {
        float* in[]{input}; float* out[]{output,right};
        if(callback) callback->process({{in,1,128},{out,2,128},nullptr,128});
    }
};
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
        EngineController controller(std::move(device),false); TrackListModel tracks;
        controller.attachProject(&tracks); controller.setRestoreSessionOnStartup(true);
        const int guitar=tracks.addInstrumentTrackOfType("Electric Guitar"); tracks.setRecordArmed(guitar,true);
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
        tracks.setRecordArmed(guitar,false); tick(); io->process(); // publish completed take
        controller.transportAction("play"); io->process();
        for(int i=0;i<128;++i) CHECK_NEAR(io->output[i],recorded[i],1e-6);
        CHECK(controller.saveSession()); controller.shutdownSession();
    }
    {
        auto device=std::make_unique<OfflineDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); TrackListModel tracks;
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
    EngineController controller(std::move(device),false); TrackListModel tracks; controller.attachProject(&tracks);
    const int track=tracks.addAudioTrack(); tracks.setRecordArmed(track,true);
    controller.setRestoreSessionOnStartup(true); controller.transportAction("record"); io->process();
    controller.shutdownSession(); CHECK(tracks.clips().size()==1); CHECK(!controller.takeBusy());
    const QString path=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/last-session.json";
    { QFile file(path); CHECK(file.open(QIODevice::WriteOnly)); file.write("broken session"); }
    auto other=std::make_unique<OfflineDevice>(); EngineController reopened(std::move(other),false); TrackListModel recovered;
    reopened.attachProject(&recovered); CHECK(!reopened.transportMessage().isEmpty());
    recovered.addAudioTrack(); CHECK(!reopened.saveSession()); reopened.shutdownSession();
    QFile file(path); CHECK(file.open(QIODevice::ReadOnly)); CHECK(file.readAll()=="broken session");
}

TEST(qtrecording, audio_takes_remain_dry_with_live_fx_loaded) {
    Storage storage;
    auto device=std::make_unique<OfflineDevice>(); auto* io=device.get();
    EngineController controller(std::move(device),false); TrackListModel tracks; controller.attachProject(&tracks);
    const int audio=tracks.addAudioTrack();
    auto* fx=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(audio)); CHECK(fx->addPedal("Tremolo"));
    tick(); io->process();
    std::fill_n(io->input,128,0.25f);
    for(int id : {audio}) {
        tracks.setRecordArmed(id,true);
        controller.transportAction("record"); CHECK(controller.transportMessage().contains("dry input")); io->process();
        controller.transportAction("stop"); io->process();
        for(int i=0;i<100 && controller.takeBusy();++i) tick(10);
        CHECK(!controller.takeBusy()); tracks.setRecordArmed(id,false);
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
            EngineController controller(std::move(device),false); TrackListModel tracks;
            controller.attachProject(&tracks);
            controller.setRestoreSessionOnStartup(true);
            const int track=vocal ? tracks.addVocalTrack() : tracks.addInstrumentTrackOfType(type);
            CHECK(tracks.setTrackInputPort(track,input)); CHECK(tracks.setVolume(track,0.5));
            CHECK(tracks.setRecordArmed(track,true)); CHECK(tracks.cleanMonitorArmed());
            CHECK(tracks.recordingTrackId()==track); CHECK(io->connectedInput==input.toStdString());
            const auto row=tracks.index(tracks.indexOfTrack(track));
            CHECK(tracks.data(row,TrackListModel::LiveInputRole).toBool());
            CHECK(tracks.data(row,TrackListModel::RecordsWithFxRole).toBool());
            int eqUpdates=0;
            QObject::connect(&tracks,&TrackListModel::cleanMonitorEqChanged,&tracks,[&] { ++eqUpdates; });
            CHECK(tracks.setEqBand(track,5,-6)); CHECK(eqUpdates==1);
            CHECK(tracks.cleanMonitorEqBands()[5].toDouble()==-6);
            CHECK(tracks.applyEqPreset(track,2)); CHECK(eqUpdates==2);
            CHECK(tracks.resetTrackEq(track)); CHECK(eqUpdates==3);
            auto* fx=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(track)); CHECK(fx->addPedal("Tremolo"));
            tick(); io->process();
            for(int i=0;i<128;++i) io->input[i]=float(i+1)/512;
            io->process();
            CHECK(io->output[64]>0); CHECK(std::abs(io->output[64]-io->input[64]*0.5f)>0.001f);
            CHECK(io->right[64]==io->output[64]); // same processed sound on both output channels
            controller.transportAction("record"); io->process();
            CHECK(controller.recording()); CHECK(controller.transportMessage().contains("with FX"));
            CHECK(controller.transportMessage().contains(type));
            CHECK(!tracks.setTrackInputPort(track,"test:input")); CHECK(!tracks.removeTrack(track));
            std::vector<float> monitored(io->output,io->output+128);
            CHECK(tracks.setMuted(track,true)); io->process();
            for(float sample : io->output) CHECK(sample==0);
            // Disarming must finish a take for either new instrument type.
            CHECK(tracks.setRecordArmed(track,false)); tick(); io->process();
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
            EngineController controller(std::move(device),false); TrackListModel tracks;
            controller.attachProject(&tracks);
            CHECK(tracks.count()==1); CHECK(tracks.clips().size()==1); CHECK(!tracks.cleanMonitorArmed());
            CHECK(tracks.data(tracks.index(0),TrackListModel::TypeRole).toString()==type);
            CHECK(tracks.data(tracks.index(0),TrackListModel::InputPortRole).toString()==input);
            tick(); io->process(); controller.transportAction("play"); io->process();
            for(int i=0;i<128;++i) {
                CHECK_NEAR(io->output[i],recorded[0][i],1e-6);
                CHECK_NEAR(io->right[i],recorded[1][i],1e-6);
            }
            controller.shutdownSession();
        }
    }
}
TEST(qtrecording, switching_live_instruments_routes_their_input_gain_and_eq_and_rejects_multiple_arms) {
    Storage storage; TrackListModel tracks;
    const int vocal=tracks.addVocalTrack(), acoustic=tracks.addInstrumentTrackOfType("Acoustic Guitar");
    const int drums=tracks.addInstrumentTrackOfType("Drums");
    CHECK(tracks.setTrackInputPort(vocal,"test:mic")); CHECK(tracks.setTrackInputPort(acoustic,"test:pickup"));
    CHECK(tracks.setVolume(vocal,0.4)); CHECK(tracks.setVolume(acoustic,0.8));
    CHECK(tracks.setEqBand(vocal,4,-3)); CHECK(tracks.setEqBand(acoustic,4,2));
    CHECK(tracks.setRecordArmed(vocal,true)); CHECK(tracks.recordingTrackId()==vocal);
    CHECK(tracks.cleanMonitorInput()=="test:mic"); CHECK_NEAR(tracks.cleanMonitorGain(),0.4,1e-6);
    CHECK(tracks.cleanMonitorEqBands()[4].toDouble()==-3);
    CHECK(tracks.setRecordArmed(acoustic,true)); CHECK(tracks.recordingTrackId()==-1);
    CHECK(tracks.setRecordArmed(vocal,false)); CHECK(tracks.recordingTrackId()==acoustic);
    CHECK(tracks.cleanMonitorInput()=="test:pickup"); CHECK_NEAR(tracks.cleanMonitorGain(),0.8,1e-6);
    CHECK(tracks.cleanMonitorEqBands()[4].toDouble()==2);
    CHECK(tracks.setSolo(vocal,true)); CHECK(tracks.cleanMonitorGain()==0);
    CHECK(tracks.setSolo(vocal,false)); CHECK_NEAR(tracks.cleanMonitorGain(),0.8,1e-6);
    CHECK(tracks.setRecordArmed(acoustic,false)); CHECK(!tracks.cleanMonitorArmed());
    CHECK(tracks.setRecordArmed(drums,true)); CHECK(tracks.recordingTrackId()==-1); CHECK(!tracks.cleanMonitorArmed());
}

TEST(qtrecording, each_track_owns_fx_trim_eq_and_recordings_and_restores_after_id_gaps) {
    Storage storage;
    {
        auto device=std::make_unique<OfflineDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); TrackListModel tracks; controller.attachProject(&tracks);
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
        CHECK(tracks.setRecordArmed(guitar,true));
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
            for(int other : {guitar,vocal,acoustic}) if(other!=track) tracks.setRecordArmed(other,false);
            tracks.setRecordArmed(track,true);
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
            tracks.setRecordArmed(track,false);
            tracks.setMuted(track,true); // keep previously recorded takes out of later monitor checks
        }
        CHECK(tracks.setEqBand(guitar,2,-3)); CHECK(tracks.setEqBand(vocal,5,4));
        controller.setRestoreSessionOnStartup(true); CHECK(controller.saveSession()); controller.shutdownSession();
    }
    {
        auto device=std::make_unique<OfflineDevice>();
        EngineController controller(std::move(device),false); TrackListModel tracks; controller.attachProject(&tracks);
        CHECK(tracks.count()==3); CHECK(tracks.clips().size()==3);
        auto* a=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(tracks.trackIdAt(0)));
        auto* b=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(tracks.trackIdAt(1)));
        auto* c=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(tracks.trackIdAt(2)));
        CHECK(a->count()==2); CHECK(b->count()==0); CHECK(c->count()==0);
        CHECK_NEAR(a->inputTrimDb(),-6.020599913,1e-6); CHECK_NEAR(b->inputTrimDb(),-12.041199827,1e-6);
        CHECK(c->inputTrimDb()==0); CHECK(a->data(a->index(0),GuitarSuiteModel::PedalTypeRole).toString()=="pedal.reverb");
        CHECK(a->data(a->index(0),GuitarSuiteModel::BypassedRole).toBool());
        CHECK(tracks.project().findTrack(tracks.trackIdAt(0))->eqGainDb[2]==-3);
        CHECK(tracks.project().findTrack(tracks.trackIdAt(1))->eqGainDb[5]==4);
        CHECK(!tracks.cleanMonitorArmed()); controller.shutdownSession();
    }
}
TEST(qtrecording, legacy_shared_chain_migrates_to_independent_copies_and_invalid_fx_cannot_overwrite_session) {
    Storage storage;
    TrackListModel original; original.addInstrumentTrackOfType("Electric Guitar"); original.addVocalTrack();
    GuitarSuiteModel shared; CHECK(shared.addPedal("Tremolo")); shared.setInputTrimDb(-7);
    const QString folder=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    CHECK(QDir().mkpath(folder)); const QString path=folder+"/last-session.json";
    const QJsonObject legacy{{"format","ChaosMakerSession"},{"version",1},{"project",original.sessionState()},
        {"guitarSuite",QJsonDocument::fromJson(shared.sessionData()).object()}};
    { QFile file(path); CHECK(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(legacy).toJson()); }
    { QSettings settings; settings.setValue("session/restoreOnStartup",true); settings.sync(); }
    {
        auto device=std::make_unique<OfflineDevice>(); EngineController controller(std::move(device),false);
        TrackListModel tracks; controller.attachProject(&tracks);
        auto* a=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(tracks.trackIdAt(0)));
        auto* b=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(tracks.trackIdAt(1)));
        CHECK(a!=b); CHECK(a->count()==1 && b->count()==1); CHECK(a->inputTrimDb()==-7 && b->inputTrimDb()==-7);
        CHECK(a->removePedal(a->data(a->index(0),GuitarSuiteModel::PedalIdRole).toInt())); CHECK(b->count()==1);
        CHECK(controller.saveSession()); controller.shutdownSession();
    }
    QJsonObject invalid;
    { QFile file(path); CHECK(file.open(QIODevice::ReadOnly)); invalid=QJsonDocument::fromJson(file.readAll()).object(); }
    CHECK(invalid["version"].toInt()==2);
    auto effects=invalid["trackEffects"].toArray(); effects[1]=QJsonObject{{"bad",true}}; invalid["trackEffects"]=effects;
    const auto bytes=QJsonDocument(invalid).toJson();
    { QFile file(path); CHECK(file.open(QIODevice::WriteOnly)); file.write(bytes); }
    {
        auto device=std::make_unique<OfflineDevice>(); EngineController controller(std::move(device),false);
        TrackListModel tracks; controller.attachProject(&tracks); CHECK(!controller.saveSession()); controller.shutdownSession();
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
        TrackListModel original; original.addInstrumentTrackOfType("Electric Guitar"); original.addVocalTrack();
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
        TrackListModel tracks; controller.attachProject(&tracks);
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
    EngineController controller(std::move(device),false); TrackListModel tracks; controller.attachProject(&tracks);
    const int track=tracks.addVocalTrack(); CHECK(tracks.setRecordArmed(track,true));
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
    TrackListModel tracks; controller.attachProject(&tracks); const int track=tracks.addAudioTrack();
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
    EngineController controller(std::move(device),false); TrackListModel tracks; controller.attachProject(&tracks);
    const int track=tracks.addInstrumentTrackOfType("Electric Guitar"); CHECK(tracks.setRecordDry(track,true));
    auto* fx=qobject_cast<GuitarSuiteModel*>(controller.trackEffects(track)); fx->setInputTrimDb(-6.020599913);
    CHECK(tracks.setRecordArmed(track,true)); std::fill_n(io->input,128,0.25f);
    for(int i=0;i<200;++i)io->process();
    controller.transportAction("record"); io->process(); CHECK(!tracks.setRecordDry(track,false));
    controller.transportAction("stop"); io->process(); for(int i=0;i<100 && controller.takeBusy();++i)tick(10);
    auto raw=tracks.playbackSnapshot()->keepAudio[0]; CHECK(raw->channels[0][64]==0.25f);
    CHECK(!tracks.project().findTrack(track)->audioClips[0].effectsPrinted);
    CHECK(tracks.setRecordArmed(track,false)); controller.transportAction("play"); io->process();
    CHECK_NEAR(io->output[64],0.125,1e-5);
    controller.transportAction("stop"); io->process(); fx->setInputTrimDb(-12.041199827);
    controller.transportAction("play"); io->process(); CHECK_NEAR(io->output[64],0.0625,1e-5);
    controller.transportAction("stop"); io->process(); CHECK(tracks.setRecordDry(track,false)); CHECK(tracks.setRecordArmed(track,true));
    for(int i=0;i<200;++i)io->process();
    controller.transportAction("record"); io->process(); controller.transportAction("stop"); io->process();
    for(int i=0;i<100 && controller.takeBusy();++i)tick(10);
    CHECK(tracks.project().findTrack(track)->audioClips[1].effectsPrinted);
    CHECK(tracks.setRecordArmed(track,false)); fx->setInputTrimDb(0); controller.transportAction("play"); io->process();
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
    EngineController controller(std::move(device),false); TrackListModel tracks; controller.attachProject(&tracks);
    const int a=tracks.addAudioTrack(), b=tracks.addVocalTrack();
    CHECK(tracks.setRecordArmed(a,true)); std::fill_n(io->input,128,0.25f); io->process(); tick(70);
    auto levels=controller.trackLevels();
    CHECK_NEAR(levels[QString::number(a)].toMap()["inputLeftDb"].toDouble(),-12.0412,0.01);
    CHECK_NEAR(levels[QString::number(a)].toMap()["outputRightDb"].toDouble(),-12.0412,0.01);
    CHECK(levels[QString::number(b)].toMap()["inputLeftDb"].toDouble()<=-90);
    CHECK(tracks.setMuted(a,true)); io->process();
    CHECK(tracks.removeTrack(b)); tick(70); CHECK(!controller.trackLevels().contains(QString::number(b)));
    CHECK(controller.newProject()); CHECK(controller.trackLevels().isEmpty());
    controller.shutdownSession();
}
