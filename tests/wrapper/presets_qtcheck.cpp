#include "testkit.h"
#include "recording/takerecorder.h"
#include "wrapper/instruments/guitar/guitarsuiteeditorwrap/guitarsuiteeditorwrapper.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"
#include "instruments/guitar/plugins/guitarsuite/guitarsuite.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QEventLoop>
#include <QTimer>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>

namespace {
void application() {
    static int argc=1;static char name[]="wrappertests";static char* argv[]{name,nullptr};
    static QCoreApplication app(argc,argv);
}
struct Fixture {
    QTemporaryDir dir{QStringLiteral("/tmp/chaosmaker-presets-XXXXXX")};
    Fixture(){application();CHECK(dir.isValid());}
    QString path(const QString& name)const{return dir.filePath(name);}
    QUrl url(const QString& name)const{return QUrl::fromLocalFile(path(name));}
    void write(const QString& name,const QByteArray& bytes){QFile file(path(name));CHECK(file.open(QIODevice::WriteOnly));CHECK(file.write(bytes)==bytes.size());}
    void wav(){
        QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("RIFF",4);stream<<quint32(40);stream.writeRawData("WAVEfmt ",8);stream<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(48000)<<quint32(96000)<<quint16(2)<<quint16(16);
        stream.writeRawData("data",4);stream<<quint32(4)<<qint16(16384)<<qint16(0);write("test.wav",bytes);
    }
};
void wait(GuitarSuiteModel& model) {
    if(!model.busy())return;
    QEventLoop loop;QTimer timer;timer.setSingleShot(true);
    QObject::connect(&model,&GuitarSuiteModel::busyChanged,&loop,[&]{if(!model.busy())loop.quit();});
    QObject::connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit);timer.start(5000);loop.exec();CHECK(!model.busy());
}
int id(GuitarSuiteModel& model,int row){return model.data(model.index(row),GuitarSuiteModel::PedalIdRole).toInt();}
float parameter(GuitarSuiteModel& model,int row,const char* name){
    for(const auto& item:model.data(model.index(row),GuitarSuiteModel::ParametersRole).toList())
        if(item.toMap()["id"].toString()==name)return item.toMap()["value"].toFloat();
    return -1000;
}
QVariantList bands(TrackListWrapper& model,int track){return model.data(model.index(model.indexOfTrack(track)),TrackListWrapper::EqBandsRole).toList();}
}
TEST(qtpresets, eq_factory_and_custom_presets_restore_all_bands_on_only_the_selected_track) {
    Fixture f;TrackListWrapper tracks;
    const int guitar=tracks.addInstrumentTrackOfType("Electric Guitar"),other=tracks.addVocalTrack();
    int updates=0;QObject::connect(&tracks,&TrackListWrapper::cleanMonitorEqChanged,[&]{++updates;});
    // The clean monitor follows the monitor button (record arm is separate), so monitor the track.
    CHECK(tracks.setMonitorEnabled(guitar,true));const int before=updates;
    CHECK(tracks.eqPresetNames().size()==7);CHECK(tracks.applyEqPreset(guitar,4));CHECK(updates==before+1);
    CHECK(tracks.setEqBand(guitar,0,-7.5));const auto saved=bands(tracks,guitar);
    CHECK(tracks.saveEqPreset(guitar,f.url("custom.eqpreset")));CHECK(tracks.resetTrackEq(guitar));
    CHECK(tracks.loadEqPreset(guitar,f.url("custom.eqpreset")));CHECK(bands(tracks,guitar)==saved);
    for(const auto& value:bands(tracks,other))CHECK(value.toFloat()==0);
    CHECK(!tracks.applyEqPreset(guitar,100));CHECK(!tracks.saveEqPreset(-1,f.url("invalid.eqpreset")));
}
TEST(qtpresets, invalid_eq_files_leave_current_curve_unchanged) {
    Fixture f;TrackListWrapper tracks;const int track=tracks.addVocalTrack();CHECK(tracks.applyEqPreset(track,2));const auto saved=bands(tracks,track);
    CHECK(!tracks.loadEqPreset(track,f.url("missing.eqpreset")));CHECK(!tracks.eqPresetError().isEmpty());
    f.write("bad.eqpreset","not json");CHECK(!tracks.loadEqPreset(track,f.url("bad.eqpreset")));
    CHECK(tracks.saveEqPreset(track,f.url("valid.eqpreset")));
    QFile file(f.path("valid.eqpreset"));CHECK(file.open(QIODevice::ReadOnly));auto root=QJsonDocument::fromJson(file.readAll()).object();file.close();
    auto gains=root["gainsDb"].toArray();gains[4]=20;root["gainsDb"]=gains;
    f.write("bad.eqpreset",QJsonDocument(root).toJson());CHECK(!tracks.loadEqPreset(track,f.url("bad.eqpreset")));CHECK(bands(tracks,track)==saved);
    gains[4]=0;root["gainsDb"]=gains;auto hz=root["frequenciesHz"].toArray();hz[0]=32;root["frequenciesHz"]=hz;
    f.write("bad.eqpreset",QJsonDocument(root).toJson());CHECK(!tracks.loadEqPreset(track,f.url("bad.eqpreset")));CHECK(bands(tracks,track)==saved);
}
TEST(qtpresets, cabinet_ir_is_loaded_off_thread_and_saved_with_relative_reference) {
    Fixture f;f.wav();GuitarSuiteModel model;std::shared_ptr<GuitarSuite> chain;
    model.setPublisher([&](auto next){chain=std::move(next);});CHECK(model.addPedal("Amp"));CHECK(model.addPedal("Cabinet IR"));
    const int cabinet=id(model,1);CHECK(model.setParameter(cabinet,"level",-3));CHECK(model.setBypassed(cabinet,true));model.setInputTrimDb(-6);
    const auto original=chain;CHECK(model.loadCabinetIr(cabinet,f.url("test.wav")));CHECK(model.busy());CHECK(chain==original);
    CHECK(!model.removePedal(cabinet));wait(model);CHECK(model.lastError().isEmpty());CHECK(chain!=original);
    CHECK(model.data(model.index(1),GuitarSuiteModel::ResourcePathRole).toString()==f.path("test.wav"));
    CHECK(model.data(model.index(1),GuitarSuiteModel::BypassedRole).toBool());CHECK(parameter(model,1,"level")==-3);
    CHECK(model.savePresetTo(f.url("chain.gspreset")));
    QFile file(f.path("chain.gspreset"));CHECK(file.open(QIODevice::ReadOnly));const auto saved=QJsonDocument::fromJson(file.readAll()).object();file.close();
    CHECK(saved["pedals"].toArray()[1].toObject()["irPath"].toString()=="test.wav");
    CHECK(model.clearCabinetIr(cabinet));CHECK(model.data(model.index(1),GuitarSuiteModel::ResourcePathRole).toString().isEmpty());
    CHECK(model.loadPresetFrom(f.url("chain.gspreset")));wait(model);CHECK(model.lastError().isEmpty());CHECK(model.count()==2);
    CHECK(parameter(model,1,"level")==-3);CHECK(model.inputTrimDb()==-6);CHECK(model.data(model.index(1),GuitarSuiteModel::BypassedRole).toBool());
}
TEST(qtpresets, missing_ir_or_invalid_wav_keeps_the_playing_chain_unchanged) {
    Fixture f;f.wav();GuitarSuiteModel model;std::shared_ptr<GuitarSuite> chain;
    model.setPublisher([&](auto next){chain=std::move(next);});CHECK(model.addPedal("Cabinet IR"));const int cabinet=id(model,0);
    CHECK(model.loadCabinetIr(cabinet,f.url("test.wav")));wait(model);CHECK(model.lastError().isEmpty());
    CHECK(model.savePresetTo(f.url("chain.gspreset")));const auto original=chain;
    f.write("bad.wav","invalid");CHECK(model.loadCabinetIr(cabinet,f.url("bad.wav")));wait(model);
    CHECK(!model.lastError().isEmpty());CHECK(chain==original);
    CHECK(QFile::remove(f.path("test.wav")));CHECK(model.loadPresetFrom(f.url("chain.gspreset")));wait(model);
    CHECK(!model.lastError().isEmpty());CHECK(chain==original);CHECK(id(model,0)==cabinet);
}
TEST(qtpresets, pitch_pedals_restore_order_parameters_and_bypass) {
    Fixture f;GuitarSuiteModel model;
    CHECK(model.addPedal("Octaver"));CHECK(model.addPedal("Pitch Shifter"));
    CHECK(model.setParameter(id(model,0),"dry",0.2));CHECK(model.setParameter(id(model,0),"down",0.7));CHECK(model.setParameter(id(model,0),"up",0.3));
    const int shift=id(model,1);CHECK(model.setParameter(shift,"semitones",-5));CHECK(model.setParameter(shift,"mix",1));
    CHECK(model.setBypassed(shift,true));CHECK(model.movePedal(1,0));CHECK(model.savePresetTo(f.url("pitch.gspreset")));
    CHECK(model.removePedal(shift));CHECK(model.loadPresetFrom(f.url("pitch.gspreset")));
    CHECK(model.count()==2);CHECK(model.data(model.index(0),GuitarSuiteModel::PedalTypeRole).toString()=="pedal.pitchshift");
    CHECK(parameter(model,0,"semitones")==-5);CHECK(parameter(model,0,"mix")==1);
    CHECK(model.data(model.index(0),GuitarSuiteModel::BypassedRole).toBool());
    CHECK(parameter(model,1,"dry")==0.2f);CHECK(parameter(model,1,"down")==0.7f);CHECK(parameter(model,1,"up")==0.3f);
}
TEST(qtpresets, crunch_and_distortion_restore_order_tone_parameters_and_bypass) {
    Fixture f; GuitarSuiteModel model;
    CHECK(model.availablePedals().contains("Crunch")); CHECK(model.availablePedals().contains("Distortion"));
    CHECK(model.addPedal("Crunch")); CHECK(model.addPedal("Distortion"));
    CHECK(model.setParameter(id(model,0),"drive",18)); CHECK(model.setParameter(id(model,0),"body",-2));
    const int distortion = id(model,1);
    CHECK(model.setParameter(distortion,"drive",36)); CHECK(model.setParameter(distortion,"tight",0.8));
    CHECK(model.setParameter(distortion,"mid",-4)); CHECK(model.setParameter(distortion,"midfreq",900));
    CHECK(model.setParameter(distortion,"level",-12)); CHECK(model.setBypassed(distortion,true));
    CHECK(model.movePedal(1,0)); CHECK(model.savePresetTo(f.url("metal.gspreset")));
    CHECK(model.removePedal(distortion)); CHECK(model.loadPresetFrom(f.url("metal.gspreset")));
    CHECK(model.count()==2); CHECK(model.data(model.index(0),GuitarSuiteModel::PedalTypeRole).toString()=="pedal.distortion");
    CHECK(parameter(model,0,"drive")==36); CHECK(parameter(model,0,"tight")==0.8f);
    CHECK(parameter(model,0,"mid")==-4); CHECK(parameter(model,0,"midfreq")==900);
    CHECK(parameter(model,0,"level")==-12); CHECK(model.data(model.index(0),GuitarSuiteModel::BypassedRole).toBool());
    CHECK(parameter(model,1,"drive")==18); CHECK(parameter(model,1,"body")==-2);
}
TEST(qtpresets, parametric_eq_fuzz_and_phaser_restore_controls_order_and_bypass) {
    Fixture f;GuitarSuiteModel model;
    CHECK(model.addPedal("Parametric EQ"));CHECK(model.addPedal("Fuzz"));CHECK(model.addPedal("Phaser"));
    CHECK(model.setParameter(id(model,0),"midfreq",1200));CHECK(model.setParameter(id(model,0),"midgain",-4));
    CHECK(model.setParameter(id(model,0),"midq",2));CHECK(model.setParameter(id(model,0),"level",-3));
    CHECK(model.setParameter(id(model,1),"sustain",0.8));CHECK(model.setParameter(id(model,1),"tone",0.7));
    CHECK(model.setBypassed(id(model,1),true));
    CHECK(model.setParameter(id(model,2),"sync",1));CHECK(model.setParameter(id(model,2),"division",3));
    CHECK(model.setParameter(id(model,2),"feedback",0.6));CHECK(model.movePedal(0,2));
    CHECK(model.savePresetTo(f.url("extra.gspreset")));CHECK(model.removePedal(id(model,0)));
    CHECK(model.loadPresetFrom(f.url("extra.gspreset")));CHECK(model.count()==3);
    CHECK(model.data(model.index(0),GuitarSuiteModel::PedalTypeRole).toString()=="pedal.fuzz");
    CHECK(parameter(model,0,"sustain")==0.8f);CHECK(parameter(model,0,"tone")==0.7f);
    CHECK(model.data(model.index(0),GuitarSuiteModel::BypassedRole).toBool());
    CHECK(parameter(model,1,"sync")==1);CHECK(parameter(model,1,"division")==3);CHECK(parameter(model,1,"feedback")==0.6f);
    CHECK(parameter(model,2,"midfreq")==1200);CHECK(parameter(model,2,"midgain")==-4);
    CHECK(parameter(model,2,"midq")==2);CHECK(parameter(model,2,"level")==-3);
}
TEST(qtpresets, factory_presets_apply_exactly_their_declared_chains) {
    Fixture fixture; GuitarSuiteModel model;
    const QStringList names{QStringLiteral("Djent"),QStringLiteral("MetalSolo"),QStringLiteral("RockChords"),
                            QStringLiteral("RockSolo"),QStringLiteral("Jazz")};
    CHECK(model.factoryPresetNames()==names);
    int published=0;
    model.setPublisher([&](auto){++published;});
    CHECK(model.addPedal("Fuzz")); model.setInputTrimDb(-7);
    const auto originalPath=model.presetFilePath();
    for(const auto& name:names) {
        QFile file(QStringLiteral(":/GuitarSuitePresets/%1.gspreset").arg(name));
        CHECK(file.open(QIODevice::ReadOnly));
        const auto expected=QJsonDocument::fromJson(file.readAll()).object()["pedals"].toArray();
        const int before=published;
        const bool applied=model.applyFactoryPreset(name);CHECK(applied);if(!applied)return;
        CHECK(!model.busy());CHECK(model.lastError().isEmpty());CHECK(published==before+1);
        CHECK(model.inputTrimDb()==-7);CHECK(model.presetFilePath()==originalPath);
        CHECK(model.count()==expected.size());
        for(int row=0;row<expected.size();++row) {
            const auto pedal=expected[row].toObject();
            CHECK(model.data(model.index(row),GuitarSuiteModel::PedalTypeRole).toString()==pedal["type"].toString());
            CHECK(model.data(model.index(row),GuitarSuiteModel::BypassedRole).toBool()==pedal["bypassed"].toBool());
            const auto settings=pedal["parameters"].toObject();
            for(auto it=settings.begin();it!=settings.end();++it) {
                const auto actual=model.data(model.index(row),GuitarSuiteModel::ParametersRole).toList();
                bool found=false;
                for(const auto& entry:actual) {
                    const auto descriptor=entry.toMap();
                    if(descriptor["id"].toString()!=it.key())continue;
                    found=true;CHECK_NEAR(descriptor["value"].toDouble(),it.value().toDouble(),1e-5);
                    CHECK(it.value().toDouble()>=descriptor["min"].toDouble());
                    CHECK(it.value().toDouble()<=descriptor["max"].toDouble());
                }
                CHECK(found);
            }
        }
    }
    const int count=model.count(),before=published;
    CHECK(!model.applyFactoryPreset("Other"));CHECK(model.count()==count);CHECK(published==before);
}

TEST(qtpresets, factory_presets_preserve_cached_cabinet_and_allow_custom_save_load) {
    Fixture fixture;fixture.wav(); GuitarSuiteModel model;
    model.setPublisher([](auto){});
    CHECK(model.addPedal("Cabinet IR"));
    int cabinet=id(model,0);
    CHECK(model.loadCabinetIr(cabinet,fixture.url("test.wav")));wait(model);
    CHECK(model.setParameter(cabinet,"mix",0.8));CHECK(model.setParameter(cabinet,"level",-4));
    CHECK(model.setBypassed(cabinet,true));model.setInputTrimDb(-5);
    // Applying a factory tone must reuse the cached IR rather than reread its file.
    CHECK(QFile::remove(fixture.path("test.wav")));
    CHECK(model.applyFactoryPreset("Djent"));CHECK(!model.busy());
    CHECK(model.data(model.index(3),GuitarSuiteModel::ResourcePathRole).toString()==fixture.path("test.wav"));
    CHECK(parameter(model,3,"mix")==0.8f);CHECK(parameter(model,3,"level")==-4);
    CHECK(!model.data(model.index(3),GuitarSuiteModel::BypassedRole).toBool());
    CHECK(model.applyFactoryPreset("Jazz"));CHECK(!model.busy());
    CHECK(model.data(model.index(2),GuitarSuiteModel::ResourcePathRole).toString()==fixture.path("test.wav"));
    CHECK(parameter(model,2,"mix")==0.8f);CHECK(parameter(model,2,"level")==-4);CHECK(model.inputTrimDb()==-5);
    fixture.wav();
    CHECK(model.setParameter(id(model,1),"master",-7));CHECK(model.setBypassed(id(model,4),true));
    CHECK(model.savePresetTo(fixture.url("my-jazz.gspreset")));
    CHECK(model.applyFactoryPreset("Djent"));CHECK(model.loadPresetFrom(fixture.url("my-jazz.gspreset")));wait(model);
    CHECK(model.lastError().isEmpty());CHECK(model.count()==5);CHECK(parameter(model,1,"master")==-7);
    CHECK(model.data(model.index(4),GuitarSuiteModel::BypassedRole).toBool());CHECK(model.inputTrimDb()==-5);
}

TEST(qtpresets, session_roundtrip_keeps_audio_controls_tempo_and_tremolo_sustain) {
    Fixture f; TrackListWrapper tracks;
    const int guitar=tracks.addInstrumentTrackOfType("Electric Guitar");
    tracks.setTempo(97.5,7,8); tracks.setVolume(guitar,0.6); tracks.setMuted(guitar,true); tracks.setSolo(guitar,true);
    tracks.setEqBand(guitar,4,-3); tracks.setRecordArmed(guitar,true); tracks.setTrackInputPort(guitar,"test:input");
    auto audio=std::make_shared<AudioData>(); audio->path=f.path("take.wav").toStdString(); audio->sampleRate=48000;
    audio->frames=128; audio->channels={std::vector<float>(128,0.25f)};
    {
        TakeRecorder recorder(audio->path,48000); CHECK(recorder.capture(audio->channels[0].data(),128)); recorder.finish(); recorder.wait();
        CHECK(recorder.error().empty());
    }
    CHECK(tracks.attachTake(guitar,24000,audio));
    const auto state=tracks.sessionState(); TrackListWrapper restored; QString error;
    CHECK(restored.restoreSessionState(state,error)); CHECK(error.isEmpty()); CHECK(restored.count()==1);
    CHECK_NEAR(restored.project().tempo().bpm(),97.5,1e-9); CHECK(restored.project().tempo().numerator()==7);
    const auto& track=*restored.project().tracks().front(); CHECK(track.muted && track.solo); CHECK(!track.recordArmed);
    CHECK_NEAR(track.volume,0.6,1e-6); CHECK(track.eqGainDb[4]==-3); CHECK(track.inputPort=="test:input");
    auto snapshot=restored.playbackSnapshot(); CHECK(snapshot->tracks[0].clips.size()==1);
    CHECK(snapshot->tracks[0].clips[0].start==24000); CHECK(snapshot->keepAudio[0]->channels[0]==audio->channels[0]);
    GuitarSuiteModel pedals; CHECK(pedals.addPedal("Tremolo")); const int tremolo=id(pedals,0);
    CHECK(pedals.setParameter(tremolo,"sustain",1)); CHECK(pedals.setParameter(tremolo,"hold",310));
    CHECK(pedals.setParameter(tremolo,"fade",450)); CHECK(pedals.setParameter(tremolo,"threshold",-48));
    GuitarSuiteModel other; CHECK(other.restoreSessionData(pedals.sessionData()));
    CHECK(parameter(other,0,"sustain")==1); CHECK(parameter(other,0,"hold")==310);
    CHECK(parameter(other,0,"fade")==450); CHECK(parameter(other,0,"threshold")==-48);
    CHECK(QFile::remove(f.path("take.wav")));
    CHECK(restored.restoreSessionState(state,error)); CHECK(!error.isEmpty()); CHECK(restored.clips().size()==1);
    CHECK(restored.playbackSnapshot()->tracks[0].clips.empty());
    CHECK(restored.sessionState()["tracks"].toArray()[0].toObject()["clips"].toArray()[0].toObject()["hasAudio"].toBool());
    auto broken=state; broken["bpm"]=0; const auto before=restored.sessionState();
    CHECK(!restored.restoreSessionState(broken,error)); CHECK(restored.sessionState()==before);
}
