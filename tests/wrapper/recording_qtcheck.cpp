#include "testkit.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/pluginWrap/guitarsuitemodel.h"
#include "audiodevice/audiodevice.h"
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QTimer>
#include <QFile>
#include <QStandardPaths>

namespace {
class OfflineDevice final : public AudioDevice {
public:
    AudioCallback* callback = nullptr;
    bool open(AudioCallback* c,int frames) override { callback=c; c->deviceChanged(48000,frames); return true; }
    void close() override { callback=nullptr; }
    double sampleRate() const override { return 48000; }
    DeviceLatency latency() const override { return {128,128,128}; }
    bool setBufferSize(int) override { return true; }
    std::vector<std::string> inputNames() const override { return {"test:input"}; }
    std::vector<std::string> outputNames() const override { return {"test:left","test:right"}; }
    bool connectInput(const std::string&) override { return true; }
    bool connectOutput(const std::string&,const std::string&) override { return true; }
    std::string lastError() const override { return {}; }
    float input[128]{}, output[128]{};
    void process() {
        float* in[]{input}; float* out[]{output};
        if(callback) callback->process({{in,1,128},{out,1,128},nullptr,128});
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
    {
        auto device=std::make_unique<OfflineDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); TrackListModel tracks;
        controller.attachProject(&tracks); controller.setRestoreSessionOnStartup(true);
        const int guitar=tracks.addInstrumentTrackOfType("Electric Guitar"); tracks.setRecordArmed(guitar,true);
        controller.setBpm(90); controller.setMeter(7,8);
        auto* fx=qobject_cast<GuitarSuiteModel*>(controller.guitarSuite()); CHECK(fx->addPedal("Tremolo"));
        const int id=fx->data(fx->index(0),GuitarSuiteModel::PedalIdRole).toInt(); CHECK(fx->setParameter(id,"sustain",1));
        for(int i=0;i<128;++i) io->input[i]=float(i)/512;
        controller.transportAction("record"); CHECK(controller.takeBusy()); io->process(); io->process();
        CHECK(controller.recording()); CHECK(controller.playing());
        CHECK(!tracks.removeTrack(guitar)); // do not orphan an active take
        controller.transportAction("stop"); io->process();
        for(int i=0;i<100 && controller.takeBusy();++i) tick(10);
        CHECK(!controller.takeBusy()); CHECK(tracks.clips().size()==1);
        CHECK(!controller.playing()); CHECK(controller.positionSeconds()==0);
        tracks.setRecordArmed(guitar,false); tick(); io->process(); // publish completed take
        controller.transportAction("play"); io->process();
        for(int i=0;i<128;++i) CHECK_NEAR(io->output[i],io->input[i],1e-6);
        CHECK(controller.saveSession()); controller.shutdownSession();
    }
    {
        auto device=std::make_unique<OfflineDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); TrackListModel tracks;
        controller.attachProject(&tracks); CHECK(tracks.count()==1); CHECK(tracks.clips().size()==1);
        CHECK(controller.bpm()==90); CHECK(controller.numerator()==7); CHECK(controller.denominator()==8);
        CHECK(!controller.playing()); CHECK(!tracks.cleanMonitorArmed());
        CHECK(qobject_cast<GuitarSuiteModel*>(controller.guitarSuite())->count()==1);
        tick(); io->process(); controller.transportAction("play"); io->process();
        CHECK_NEAR(io->output[64],64.0/512,1e-6);
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
