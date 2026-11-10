#include <QtTest>
#include <QStyleHints>
#include "plugins/mixfx/mixfxspec.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/settings/appsettingswrap/appsettingswrapper.h"
#include "wrapper/instruments/guitar/guitarsuiteeditorwrap/guitarsuiteeditorwrapper.h"
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QSignalSpy>
#include <QQmlProperty>
#include <QQuickItem>
#include <QPointer>
#include <QQuickWindow>
#include <memory>
#include <functional>
#include <QSettings>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QAbstractItemModelTester>
#include <QJSValue>
#include <QDirIterator>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QMetaProperty>
#include <QRegularExpression>
#include <QJsonArray>
#include <QJsonObject>
#include "audiodevice/audiodevice.h"

class MultiInputUiDevice final : public AudioDevice {
public:
    AudioCallback* callback=nullptr;
    float inputA=0, inputB=0, outputSample=0;
    bool open(AudioCallback* c,int frames) override { callback=c; c->deviceChanged(48000,frames); return true; }
    void close() override { callback=nullptr; }
    double sampleRate() const override { return 48000; }
    DeviceLatency latency() const override { return {128,128,128}; }
    bool setBufferSize(int) override { return true; }
    std::vector<std::string> inputNames() const override { return {"test:input","test:mic"}; }
    std::vector<std::string> outputNames() const override { return {"test:left","test:right"}; }
    bool connectInput(const std::string&) override { return true; }
    int inputChannelCapacity() const override { return 2; }
    int connectInputChannel(const std::string& source) override { return source=="test:input" ? 0 : source=="test:mic" ? 1 : -1; }
    bool connectOutput(const std::string&,const std::string&) override { return true; }
    std::string lastError() const override { return {}; }
    void process() {
        float a[128]{},b[128]{},left[128]{},right[128]{}; float* in[]{a,b}; float* out[]{left,right};
        std::fill_n(a,128,inputA); std::fill_n(b,128,inputB);
        if(callback)callback->process({{in,2,128},{out,2,128},nullptr,128});
        outputSample=left[64];
    }
};


// A ThemedDialog: its title and button row on the dark panel, in the theme's colours.
static void verifyThemedDialog(QObject* dialog)
{
    auto* title=qvariant_cast<QQuickItem*>(dialog->property("header")); QVERIFY(title);
    QCOMPARE(title->objectName(),QString("themedDialogTitle"));
    QCOMPARE(title->property("color").value<QColor>(),QColor("#ededed"));
    QVERIFY(title->property("text").toString().length()>0);
    auto* buttons=qvariant_cast<QQuickItem*>(dialog->property("footer")); QVERIFY(buttons);
    QCOMPARE(buttons->objectName(),QString("themedDialogButtons"));
    auto* row=qvariant_cast<QQuickItem*>(buttons->property("background")); QVERIFY(row);
    QCOMPARE(row->property("color").value<QColor>().alpha(),0);               // the dark panel shows through
    auto* panel=qvariant_cast<QQuickItem*>(dialog->property("background")); QVERIFY(panel);
    QCOMPARE(panel->property("color").value<QColor>(),QColor("#202020"));
    int count=0;
    for(auto* child:buttons->findChildren<QQuickItem*>())
        if(child->inherits("QQuickAbstractButton")) {
            ++count;
            auto* back=qvariant_cast<QQuickItem*>(child->property("background")); QVERIFY(back);
            QVERIFY(back->property("color").value<QColor>().lightness()<90);   // dark chip, not a grey system button
        }
    QVERIFY(count>=2);
}

class TransportUiTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QVERIFY(storage.isValid());
        qputenv("XDG_DATA_HOME", storage.path().toUtf8());
        qputenv("CHAOSMAKER_SAMPLE_KITS", (storage.path() + "/no-sample-kits").toUtf8());   // never the real kit
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, storage.path());
        QCoreApplication::setOrganizationName("ChaosMakerTest");
        QCoreApplication::setApplicationName("TransportTest");
        qputenv("PIPEWIRE_REMOTE", "codex-test-no-server");
        qputenv("PULSE_SERVER", "unix:/tmp/chaosmaker-test-no-server");
        engine=std::make_unique<QQmlApplicationEngine>();
        engine->loadFromModule("TransportTests","TransportHarness");
        QVERIFY(!engine->rootObjects().isEmpty());
        window=qobject_cast<QQuickWindow*>(engine->rootObjects().first());QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        field=window->findChild<QQuickItem*>("bpmField");QVERIFY(field);
        bar=window->findChild<QObject*>("transport");QVERIFY(bar);
    }
    void init() {
        EngineController::instance()->setBpm(120);
        QCoreApplication::processEvents();
    }
    void cleanup() {
        auto* popup=window->findChild<QObject*>("quickSettingsPopup");
        if(popup)QMetaObject::invokeMethod(popup,"close");
        QCoreApplication::processEvents();
    }
    void cleanupTestCase() { engine.reset(); }
    void quick_settings_popup_contains_controls_and_closes_cleanly() {
        auto* controller=EngineController::instance(); controller->setMeter(4,4);
        auto* button=visualItem(window->contentItem(),"quickSettingsButton"); QVERIFY(button); QVERIFY(button->isVisible());
        auto* popup=window->findChild<QObject*>("quickSettingsPopup"); QVERIFY(popup);
        QVERIFY(!popup->property("visible").toBool());
        // BPM and time signature sit on the transport bar itself, not in the popup.
        QVERIFY(field->isVisible());
        auto* numerator=visualItem(window->contentItem(),"meterNumerator");
        auto* denominator=visualItem(window->contentItem(),"meterDenominator");
        QVERIFY(numerator); QVERIFY(denominator); QVERIFY(numerator->isVisible()); QVERIFY(denominator->isVisible());
        QVERIFY(button->property("checkable").toBool()); QVERIFY(!button->property("checked").toBool());
        click(button); QTRY_VERIFY(popup->property("visible").toBool()); QTest::qWait(20);
        QVERIFY(button->property("checked").toBool());
        auto* content=popup->property("contentItem").value<QQuickItem*>(); QVERIFY(content);
        const auto verifyCenter=[&] {
            const QPointF center=content->mapToScene(QPointF(content->width()/2,content->height()/2));
            QVERIFY(std::abs(center.x()-window->width()/2.0)<1);
            QVERIFY(std::abs(center.y()-window->height()/2.0)<1);
        };
        verifyCenter();
        // The popup holds metronome, count-in and snap grid only.
        QVERIFY(visualItem(content,"metronomeToggle"));
        QVERIFY(visualItem(content,"metronomeVolume"));
        QVERIFY(visualItem(content,"countInToggle"));
        QVERIFY(visualItem(content,"snapGridSelector"));
        QVERIFY(!visualItem(content,"bpmField")); QVERIFY(!visualItem(content,"meterNumerator"));
        click(button); QTRY_VERIFY(!popup->property("visible").toBool());
        QVERIFY(!button->property("checked").toBool());
        click(button); QTRY_VERIFY(popup->property("visible").toBool()); QVERIFY(button->property("checked").toBool());
        const QSize original=window->size(); window->resize(1000,800); QTest::qWait(30); verifyCenter();
        window->resize(original); QTest::qWait(30); verifyCenter();
        auto* close=visualItem(window->contentItem(),"closeQuickSettings"); QVERIFY(close); click(close);
        QTRY_VERIFY(!popup->property("visible").toBool()); QVERIFY(!button->property("checked").toBool());
        // Inline meter and BPM editing, with the popup closed.
        click(numerator); QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
        QTest::keyClick(window,Qt::Key_3); QTest::keyClick(window,Qt::Key_Return);
        QCOMPARE(controller->numerator(),3);
        click(denominator); QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
        QTest::keyClick(window,Qt::Key_8); QTest::keyClick(window,Qt::Key_Return);
        QCOMPARE(controller->denominator(),8);
        click(field); QTest::qWait(10); QTest::keyClick(window,Qt::Key_9); QTest::keyClick(window,Qt::Key_5);
        auto* other=window->findChild<QQuickItem*>("otherFocus"); QVERIFY(other); click(other);
        QCOMPARE(controller->bpm(),95.0); QCOMPARE(field->property("text").toString(),QString("95"));
        // Escape and an outside click both close the popup.
        click(button); QTRY_VERIFY(popup->property("visible").toBool());
        QTest::keyClick(window,Qt::Key_Escape); QTRY_VERIFY(!popup->property("visible").toBool());
        QVERIFY(!button->property("checked").toBool());
        click(button); QTRY_VERIFY(popup->property("visible").toBool());
        click(other);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QVERIFY(!button->property("checked").toBool());
        QVERIFY(other->hasActiveFocus());
        controller->setMeter(4,4);
    }
    void transport_bars_beats_readout_follows_position_and_meter() {
        auto device=std::make_unique<MultiInputUiDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); controller.setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller.attachProject(&tracks); tracks.addAudioTrack();
        const auto restore=qScopeGuard([&] { window->setProperty("transport",QVariant::fromValue(EngineController::instance())); });
        window->setProperty("transport",QVariant::fromValue(&controller));
        auto* readout=visualItem(window->contentItem(),"transportBarsBeats"); QVERIFY(readout); QVERIFY(readout->isVisible());
        controller.setMeter(4,4); controller.seekBeat(0); io->process();
        QTRY_COMPARE(readout->property("text").toString(),QString("1.1.00"));
        controller.seekBeat(5.5); io->process();
        QTRY_COMPARE(readout->property("text").toString(),QString("2.2.50"));
        // 6/8 counts eighth-note beats: 2.75 quarter notes is beat 6 and a half of bar 1.
        controller.setMeter(6,8); controller.seekBeat(2.75); io->process();
        QTRY_COMPARE(readout->property("text").toString(),QString("1.6.50"));
        controller.seekBeat(3.0); io->process();
        QTRY_COMPARE(readout->property("text").toString(),QString("2.1.00"));
        controller.shutdownSession();
    }
    void recording_count_in_uses_one_bar_and_can_be_cancelled() {
        auto device=std::make_unique<MultiInputUiDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); controller.setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller.attachProject(&tracks); const int track=tracks.addAudioTrack();
        QVERIFY(tracks.setRecordArmed(track,true)); controller.setMeter(3,8);
        const auto restore=qScopeGuard([&] { window->setProperty("transport",QVariant::fromValue(EngineController::instance())); });
        window->setProperty("transport",QVariant::fromValue(&controller));
        openQuickSettings();
        auto* count=visualItem(window->contentItem(),"countInToggle"); QVERIFY(count);
        QVERIFY(!controller.countInEnabled()); click(count); QVERIFY(controller.countInEnabled());
        controller.seekBeat(4); controller.transportAction("record"); io->process();
        QVERIFY(controller.takeBusy()); QVERIFY(controller.countingIn()); QVERIFY(!controller.recording());
        QCOMPARE(controller.countInBeatsRemaining(),3); QVERIFY(tracks.clips().isEmpty());
        QTRY_COMPARE(count->property("text").toString(),QString("IN 3")); QVERIFY(!count->isEnabled());
        controller.setCountInEnabled(false); QVERIFY(controller.countInEnabled());
        controller.setBpm(90); QCOMPARE(controller.bpm(),120.0);
        controller.setMeter(4,4); QCOMPARE(controller.numerator(),3); QCOMPARE(controller.denominator(),8);
        for(int i=0;i<280;++i)io->process();
        QVERIFY(controller.countingIn()); QCOMPARE(controller.countInBeatsRemaining(),1);
        io->process(); QVERIFY(!controller.countingIn()); QVERIFY(controller.recording());
        controller.transportAction("pause"); io->process(); QTRY_VERIFY(!controller.takeBusy());
        QCOMPARE(tracks.clips().size(),1);
        const auto& clip=tracks.project().findTrack(track)->audioClips[0];
        QCOMPARE(clip.start,qint64(96000)); QCOMPARE(clip.length,qint64(96));
        // R cancels the next count-in without creating a partial take.
        QTest::keyClick(window,Qt::Key_R); io->process(); QVERIFY(controller.countingIn());
        QTest::keyClick(window,Qt::Key_R); io->process(); QTRY_VERIFY(!controller.takeBusy());
        QVERIFY(!controller.countingIn()); QVERIFY(!controller.playing()); QCOMPARE(tracks.clips().size(),1);
        click(count); QVERIFY(!controller.countInEnabled());
        QTest::keyClick(window,Qt::Key_R); io->process(); QVERIFY(controller.recording()); QVERIFY(!controller.countingIn());
        controller.transportAction("pause"); io->process(); QTRY_VERIFY(!controller.takeBusy());
        QCOMPARE(tracks.clips().size(),2); controller.shutdownSession();
    }
    void metronome_controls_toggle_click_and_adjust_volume() {
        auto device=std::make_unique<MultiInputUiDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); controller.setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller.attachProject(&tracks);
        QCOMPARE(tracks.count(),0);
        const auto restore=qScopeGuard([&] { window->setProperty("transport",QVariant::fromValue(EngineController::instance())); });
        window->setProperty("transport",QVariant::fromValue(&controller));
        openQuickSettings();
        auto* toggle=visualItem(window->contentItem(),"metronomeToggle");
        auto* volume=visualItem(window->contentItem(),"metronomeVolume"); QVERIFY(toggle); QVERIFY(volume);
        QVERIFY(!controller.metronomeEnabled()); QCOMPARE(controller.metronomeVolume(),0.5);
        click(toggle); QVERIFY(controller.metronomeEnabled()); QVERIFY(toggle->property("active").toBool());
        volume->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Right);
        QVERIFY(std::abs(controller.metronomeVolume()-0.51)<1e-6);
        controller.setMetronomeVolume(0.25); QCOMPARE(volume->property("value").toDouble(),0.25);
        controller.setMetronomeVolume(-1); controller.setMetronomeVolume(2); QCOMPARE(controller.metronomeVolume(),0.25);
        controller.transportAction("play"); io->process(); QVERIFY(std::abs(io->outputSample)>0.0001f);
        controller.transportAction("pause"); io->process(); QCOMPARE(io->outputSample,0.0f);
        click(toggle); QVERIFY(!controller.metronomeEnabled());
        controller.shutdownSession();
    }
    void playback_pauses_red_line_at_the_last_block_across_tracks() {
        auto device=std::make_unique<MultiInputUiDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); controller.setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller.attachProject(&tracks);
        const int a=tracks.addAudioTrack(),b=tracks.addAudioTrack();
        tracks.addAudioClip(a,"first",0,128); tracks.addAudioClip(b,"last",200,57);
        QVERIFY(tracks.setMuted(b,true)); // Muted/offline blocks still define the arrangement end.
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        const auto restore=qScopeGuard([&] {
            timeline->setVisible(false); timeline->setProperty("trackModel",QVariant());
            timeline->setProperty("transport",QVariant::fromValue(EngineController::instance()));
            window->setProperty("transport",QVariant::fromValue(EngineController::instance()));
        });
        timeline->setProperty("transport",QVariant::fromValue(&controller));
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true);
        window->setProperty("transport",QVariant::fromValue(&controller));
        auto* line=visualItem(timeline,"timelinePlayhead"); QVERIFY(line);
        controller.transportAction("play"); io->process(); io->process();
        QVERIFY(controller.playing()); io->process();
        QVERIFY(!controller.playing()); QCOMPARE(controller.positionSeconds(),257.0/48000);
        QTRY_VERIFY(!bar->property("playing").toBool());
        QTRY_COMPARE(line->x(),257.0/48000*timeline->property("pixelsPerSecond").toDouble());
        const double stopped=line->x(); io->process(); QTest::qWait(30); QCOMPARE(line->x(),stopped);
        controller.shutdownSession();
    }
    void space_shortcut_plays_and_pauses_without_rewinding_data() {
        QTest::addColumn<bool>("emptyTrack");
        QTest::newRow("no tracks") << false;
        QTest::newRow("empty track") << true;
    }
    void space_shortcut_plays_and_pauses_without_rewinding() {
        QFETCH(bool,emptyTrack);
        auto device=std::make_unique<MultiInputUiDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); controller.setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller.attachProject(&tracks);
        if(emptyTrack)tracks.addAudioTrack();
        const auto restore=qScopeGuard([&] { window->setProperty("transport",QVariant::fromValue(EngineController::instance())); });
        window->setProperty("transport",QVariant::fromValue(&controller));
        // Space must control transport even when a different button has focus.
        auto* record=visualItem(window->contentItem(),"transport_record"); QVERIFY(record);
        record->forceActiveFocus();
        QTest::keyClick(window,Qt::Key_Space); io->process();
        QVERIFY(controller.playing()); QVERIFY(!controller.takeBusy());
        QTRY_VERIFY(bar->property("playing").toBool());
        const double position=controller.positionSeconds(); QVERIFY(position>0);
        QTest::keyClick(window,Qt::Key_Space); io->process();
        QVERIFY(!controller.playing()); QCOMPARE(controller.positionSeconds(),position);
        QTRY_VERIFY(!bar->property("playing").toBool());
        QTest::keyClick(window,Qt::Key_Space); io->process();
        QVERIFY(controller.playing()); QVERIFY(controller.positionSeconds()>position);
        for(int i=0;i<100;++i)io->process();
        QVERIFY(controller.playing());
        controller.transportAction("pause"); io->process();
        auto* play=visualItem(window->contentItem(),"transport_play"); QVERIFY(play); click(play); io->process();
        QVERIFY(controller.playing());
        controller.shutdownSession();
    }
    void stopping_recording_pauses_the_red_playhead_data() {
        QTest::addColumn<bool>("useButton");
        QTest::newRow("R shortcut") << false;
        QTest::newRow("Record button") << true;
    }
    void stopping_recording_pauses_the_red_playhead() {
        QFETCH(bool,useButton);
        auto device=std::make_unique<MultiInputUiDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); controller.setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller.attachProject(&tracks);
        const int track=tracks.addAudioTrack();
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        const auto restore=qScopeGuard([&] {
            window->setProperty("transport",QVariant::fromValue(EngineController::instance()));
            timeline->setVisible(false); timeline->setProperty("trackModel",QVariant());
            timeline->setProperty("transport",QVariant::fromValue(EngineController::instance()));
        });
        window->setProperty("transport",QVariant::fromValue(&controller));
        timeline->setProperty("transport",QVariant::fromValue(&controller));
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true);
        auto* line=visualItem(timeline,"timelinePlayhead"); QVERIFY(line);
        auto* record=visualItem(window->contentItem(),"transport_record"); QVERIFY(record);
        const auto toggleRecord=[&] {
            if(useButton)click(record);
            else QTest::keyClick(window,Qt::Key_R);
        };
        auto* other=window->findChild<QQuickItem*>("otherFocus"); QVERIFY(other); other->forceActiveFocus();
        toggleRecord();
        QVERIFY(!controller.takeBusy()); QVERIFY(controller.transportMessage().contains("Arm at least one"));
        QVERIFY(tracks.setRecordArmed(track,true));
        toggleRecord();
        QVERIFY(controller.takeBusy());
        io->process();
        QVERIFY(controller.recording()); QVERIFY(controller.playing());
        QTRY_VERIFY(line->x()>0);
        const double initialLineX=line->x();
        for(int i=0;i<50;++i)io->process();
        QTRY_VERIFY(line->x()>initialLineX);
        const double stopPosition=controller.positionSeconds();
        toggleRecord(); io->process();
        QVERIFY(!controller.recording()); QVERIFY(!controller.playing());
        QCOMPARE(controller.positionSeconds(),stopPosition);
        QTRY_VERIFY(!controller.takeBusy()); QCOMPARE(tracks.clips().size(),1);
        QTRY_VERIFY(!bar->property("playing").toBool());
        QTRY_COMPARE(line->x(),stopPosition*timeline->property("pixelsPerSecond").toDouble());
        const double stoppedLineX=line->x();
        for(int i=0;i<50;++i)io->process();
        QTest::qWait(50);
        QCOMPARE(controller.positionSeconds(),stopPosition); QCOMPARE(line->x(),stoppedLineX);
        // Seek back into the recorded clip before resuming playback.
        controller.seekBeat(0);
        QTest::keyClick(window,Qt::Key_Space); io->process();
        QVERIFY(controller.playing()); QVERIFY(!controller.recording());
        QTRY_VERIFY(line->x()>0 && line->x()<stoppedLineX);
        // Space also finishes a later take and pauses the playhead.
        QTest::keyClick(window,Qt::Key_R); io->process();
        QVERIFY(controller.recording());
        QTRY_VERIFY(bar->property("playing").toBool());
        QTest::keyClick(window,Qt::Key_Space); io->process();
        QVERIFY(!controller.recording()); QVERIFY(!controller.playing());
        QVERIFY(controller.positionSeconds()>0);
        QTRY_VERIFY(!controller.takeBusy()); QCOMPARE(tracks.clips().size(),2);
        controller.shutdownSession();
    }
    void transport_shortcuts_preserve_text_editing_and_ignore_key_repeat() {
        QSignalSpy actions(bar,SIGNAL(transportAction(QString))); QVERIFY(actions.isValid());
        click(field); QTest::qWait(10); QVERIFY(field->hasActiveFocus());
        QTest::keyClick(window,Qt::Key_R); QTest::keyClick(window,Qt::Key_Space);
        QCOMPARE(field->property("text").toString(),QString("r "));
        QTest::keyClick(window,Qt::Key_R,Qt::ControlModifier);
        QCOMPARE(actions.count(),0);
        QTest::keyClick(window,Qt::Key_Return);
        auto* other=window->findChild<QQuickItem*>("otherFocus"); QVERIFY(other); other->forceActiveFocus();
        QTest::keyClick(window,Qt::Key_R,Qt::ControlModifier);
        QCOMPARE(actions.count(),0); // Ctrl+R has no transport binding.
        for(auto key:{Qt::Key_Space,Qt::Key_R}) {
            QKeyEvent repeat(QEvent::KeyPress,key,Qt::NoModifier,key==Qt::Key_R ? "r" : " ",true);
            QCoreApplication::sendEvent(window,&repeat);
        }
        QCOMPARE(actions.count(),0);
        QTest::keyClick(window,Qt::Key_R); QCOMPARE(actions.count(),1);
        QCOMPARE(actions.first().first().toString(),QString("record"));
    }
    void click_and_type_replaces_old_bpm() {
        click(field);QTest::qWait(10);
        QVERIFY(field->hasActiveFocus());
        QTest::keyClick(window,Qt::Key_9);QTest::keyClick(window,Qt::Key_0);QTest::keyClick(window,Qt::Key_Return);
        QCOMPARE(bar->property("bpm").toDouble(),90.0);
    }
    void decimal_bpm_and_decimal_comma_commit_on_enter() {
        click(field);QTest::qWait(10);
        for(auto key : {Qt::Key_1,Qt::Key_3,Qt::Key_7,Qt::Key_Period,Qt::Key_5})QTest::keyClick(window,key);
        QTest::keyClick(window,Qt::Key_Return);QCOMPARE(bar->property("bpm").toDouble(),137.5);
        click(field);QTest::qWait(10);
        for(auto key : {Qt::Key_9,Qt::Key_7,Qt::Key_Comma,Qt::Key_5})QTest::keyClick(window,key);
        QTest::keyClick(window,Qt::Key_Return);QCOMPARE(bar->property("bpm").toDouble(),97.5);
    }
    void focus_loss_commits_and_invalid_text_restores_the_last_value() {
        auto* other=window->findChild<QQuickItem*>("otherFocus");QVERIFY(other);
        click(field);QTest::qWait(10);QTest::keyClick(window,Qt::Key_8);QTest::keyClick(window,Qt::Key_5);
        click(other);QCOMPARE(bar->property("bpm").toDouble(),85.0);
        click(field);QTest::qWait(10);QTest::keyClick(window,Qt::Key_0);QTest::keyClick(window,Qt::Key_Return);
        QCOMPARE(bar->property("bpm").toDouble(),85.0);QCOMPARE(field->property("text").toString(),QString("85"));
    }
    void pending_tempo_text_survives_external_updates_until_commit() {
        click(field); QTest::qWait(10);
        for (auto key : {Qt::Key_1, Qt::Key_3, Qt::Key_7, Qt::Key_Comma, Qt::Key_5})
            QTest::keyClick(window, key);
        EngineController::instance()->setBpm(95.0);
        QCoreApplication::processEvents();
        QCOMPARE(field->property("text").toString(), QString("137,5"));
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(bar->property("bpm").toDouble(), 137.5);
        QCOMPARE(field->property("text").toString(), QString("137.5"));
    }
    void step_buttons_update_bpm_and_clamp_to_the_supported_range() {
        auto* plus=window->findChild<QQuickItem*>("bpmIncrease");auto* minus=window->findChild<QQuickItem*>("bpmDecrease");
        QVERIFY(plus);QVERIFY(minus);click(plus);QCOMPARE(bar->property("bpm").toDouble(),121.0);
        click(minus);QCOMPARE(bar->property("bpm").toDouble(),120.0);
        EngineController::instance()->setBpm(10000.0);click(plus);QCOMPARE(bar->property("bpm").toDouble(),10000.0);
        EngineController::instance()->setBpm(0.01);click(minus);QCOMPARE(bar->property("bpm").toDouble(),0.01);
    }
    void bpm_remains_editable_while_the_actual_pedalboard_is_open() {
        auto* board=window->findChild<QObject*>("guitarSuitePanel");QVERIFY(board);
        QVERIFY(QMetaObject::invokeMethod(board,"open"));QTRY_VERIFY(board->property("visible").toBool());
        click(field);QTest::qWait(10);
        QTest::keyClick(window,Qt::Key_9);QTest::keyClick(window,Qt::Key_0);QTest::keyClick(window,Qt::Key_Return);
        QCOMPARE(EngineController::instance()->bpm(),90.0);
        QCOMPARE(bar->property("bpm").toDouble(),90.0);
        QVERIFY(board->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(board,"close"));
    }
    void phaser_sync_controls_update_the_actual_pedal_parameter() {
        auto* model=qobject_cast<GuitarSuiteModel*>(EngineController::instance()->guitarSuite());QVERIFY(model);
        QVERIFY(model->addPedal("Phaser"));const int row=model->count()-1;
        const int id=model->data(model->index(row),GuitarSuiteModel::PedalIdRole).toInt();
        auto* board=window->findChild<QObject*>("guitarSuitePanel");QVERIFY(board);
        QVERIFY(QMetaObject::invokeMethod(board,"open"));QTRY_VERIFY(board->property("visible").toBool());
        auto* sync=visualItem(window->contentItem(),"phaserBpmSync");
        auto* rate=visualItem(window->contentItem(),"pedal.phaser.rate");
        auto* division=visualItem(window->contentItem(),"phaserBeatDivision");
        QVERIFY(sync);QVERIFY(rate);QVERIFY(division);QVERIFY(rate->isEnabled());
        click(sync);QTRY_VERIFY(sync->property("checked").toBool());
        rate=visualItem(window->contentItem(),"pedal.phaser.rate");QVERIFY(rate);QVERIFY(!rate->isEnabled());
        bool synced=false;
        for(const auto& parameter:model->data(model->index(row),GuitarSuiteModel::ParametersRole).toList())
            if(parameter.toMap()["id"].toString()=="sync") synced=parameter.toMap()["value"].toFloat()==1;
        QVERIFY(synced);QVERIFY(model->setParameter(id,"division",3));
        QTRY_COMPARE(division->property("currentIndex").toInt(),3);
        click(sync);QTRY_VERIFY(!sync->property("checked").toBool());
        rate=visualItem(window->contentItem(),"pedal.phaser.rate");QVERIFY(rate);QVERIFY(rate->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(board,"close"));QVERIFY(model->removePedal(id));
    }
    void updating_a_pedal_parameter_preserves_its_dial_and_displays_the_new_value() {
        auto* model=qobject_cast<GuitarSuiteModel*>(EngineController::instance()->guitarSuite());QVERIFY(model);
        QVERIFY(model->addPedal("Parametric EQ"));const int row=model->count()-1;
        const int id=model->data(model->index(row),GuitarSuiteModel::PedalIdRole).toInt();
        auto* board=window->findChild<QObject*>("guitarSuitePanel");QVERIFY(board);
        QVERIFY(QMetaObject::invokeMethod(board,"open"));QTRY_VERIFY(board->property("visible").toBool());
        QPointer<QQuickItem> dial=visualItem(window->contentItem(),"pedal.parametriceq.lowfreq");QVERIFY(dial);
        QVERIFY(model->setParameter(id,"lowfreq",300));QCoreApplication::processEvents();
        QVERIFY2(dial,"Parameter edits must not destroy the dial while it holds a mouse grab");
        QTRY_COMPARE(dial->property("value").toDouble(),300.0);
        dial->forceActiveFocus();QTest::keyClick(window,Qt::Key_Right);
        QTRY_VERIFY(dial && dial->property("value").toDouble()>300);
        bool updated=false;
        for(const auto& parameter:model->data(model->index(row),GuitarSuiteModel::ParametersRole).toList())
            if(parameter.toMap()["id"].toString()=="lowfreq")updated=parameter.toMap()["value"].toDouble()>300;
        QVERIFY(updated);QVERIFY(QMetaObject::invokeMethod(board,"close"));QVERIFY(model->removePedal(id));
    }
    void pedal_parameters_are_compact_slider_rows_that_reset_on_double_click() {
        auto* model=qobject_cast<GuitarSuiteModel*>(EngineController::instance()->guitarSuite());QVERIFY(model);
        QVERIFY(model->addPedal("Parametric EQ"));const int row=model->count()-1;
        const int id=model->data(model->index(row),GuitarSuiteModel::PedalIdRole).toInt();
        auto* board=window->findChild<QObject*>("guitarSuitePanel");QVERIFY(board);
        const auto restore=qScopeGuard([&] { QMetaObject::invokeMethod(board,"close"); model->removePedal(id); });
        QVERIFY(QMetaObject::invokeMethod(board,"open"));QTRY_VERIFY(board->property("visible").toBool());
        QTest::qWait(200);
        const auto parameter=[&](const QString& key) {
            for(const auto& p:model->data(model->index(row),GuitarSuiteModel::ParametersRole).toList())
                if(p.toMap()["id"].toString()==key) return p.toMap();
            return QVariantMap();
        };
        QPointer<QQuickItem> slider=visualItem(window->contentItem(),"pedal.parametriceq.lowfreq");QVERIFY(slider);
        auto* readout=visualItem(window->contentItem(),"pedal.parametriceq.lowfreq.value");QVERIFY(readout);
        auto* next=visualItem(window->contentItem(),"pedal.parametriceq.lowgain");QVERIFY(next);
        QVERIFY(slider->inherits("QQuickSlider"));
        // A row is compact: the next parameter starts well within a dial's old 84 px row.
        const double gap=next->mapToScene(QPointF()).y()-slider->mapToScene(QPointF()).y();
        QVERIFY2(gap>0 && gap<40,qPrintable(QString::number(gap)));
        QVERIFY(slider->width()>slider->height()*3);                  // horizontal
        const QVariantMap info=parameter("lowfreq");
        QVERIFY(info.contains("default"));
        const double def=info["default"].toDouble();
        // Drag towards the right end: the parameter follows and the readout shows it.
        const QPoint mid=slider->mapToScene(QPointF(slider->width()*0.5,slider->height()/2)).toPoint();
        const QPoint right=slider->mapToScene(QPointF(slider->width()*0.9,slider->height()/2)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,mid);
        QTest::mouseMove(window,right,20);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,right);
        const double dragged=parameter("lowfreq")["value"].toDouble();
        QVERIFY(dragged!=def);
        QTRY_VERIFY(slider);
        QCOMPARE(slider->property("value").toDouble(),dragged);
        QCOMPARE(readout->property("text").toString(),model->formatValue(dragged,info["unit"].toString()));
        // The plain wheel scrolls the card (given more rows than fit); it never changes a value.
        QQuickItem* card=slider->parentItem();
        while(card && !card->inherits("QQuickFlickable")) card=card->parentItem();
        QVERIFY(card);
        const auto restoreHeight=card->property("contentHeight");
        card->setProperty("contentHeight",card->height()*3);
        QCOMPARE(card->property("contentY").toDouble(),0.0);
        QWheelEvent wheel(QPointF(mid),window->mapToGlobal(mid),QPoint(),QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(window,&wheel); QCoreApplication::processEvents();
        QTRY_VERIFY(card->property("contentY").toDouble()>0);
        QCOMPARE(parameter("lowfreq")["value"].toDouble(),dragged);
        card->setProperty("contentY",0.0); card->setProperty("contentHeight",restoreHeight);
        QTest::qWait(50);
        // Double-click returns to the pedal's default.
        const QPoint at=slider->mapToScene(QPointF(slider->width()*0.2,slider->height()/2)).toPoint();
        QTest::mouseDClick(window,Qt::LeftButton,Qt::NoModifier,at,10);
        QTRY_VERIFY(std::abs(parameter("lowfreq")["value"].toDouble()-def)<1e-4);
        QTRY_VERIFY(slider && std::abs(slider->property("value").toDouble()-def)<1e-4);
    }
    void factory_preset_picker_applies_djent_and_jazz() {
        auto* model=qobject_cast<GuitarSuiteModel*>(EngineController::instance()->guitarSuite());QVERIFY(model);
        auto* board=window->findChild<QObject*>("guitarSuitePanel");QVERIFY(board);
        QVERIFY(QMetaObject::invokeMethod(board,"open"));QTRY_VERIFY(board->property("visible").toBool());
        auto* picker=visualItem(window->contentItem(),"guitarFactoryPreset");
        auto* apply=visualItem(window->contentItem(),"applyGuitarFactoryPreset");QVERIFY(picker);QVERIFY(apply);
        // Every factory preset is offered: Djent, Jazz, MetalSolo, RockChords, RockSolo.
        QCOMPARE(picker->property("count").toInt(),5);
        const auto textAt=[&](int i) { QString text; QMetaObject::invokeMethod(picker,"textAt",Q_RETURN_ARG(QString,text),Q_ARG(int,i)); return text; };
        const auto indexOf=[&](const QString& name) { for(int i=0;i<5;++i) if(textAt(i).contains(name)) return i; return -1; };
        QStringList names; for(int i=0;i<5;++i) names<<textAt(i);
        for(const char* name:{"Djent","Jazz","MetalSolo","RockChords","RockSolo"})
            QVERIFY2(indexOf(name)>=0,qPrintable(names.join(", ")));
        // Djent (rebuilt in a254d48): gate, overdrive, amp, cabinet, EQ.
        picker->setProperty("currentIndex",indexOf("Djent"));click(apply);
        QCOMPARE(model->count(),5);
        QCOMPARE(model->data(model->index(0),GuitarSuiteModel::PedalTypeRole).toString(),QString("pedal.noisegate"));
        QCOMPARE(model->data(model->index(1),GuitarSuiteModel::PedalTypeRole).toString(),QString("pedal.overdrive"));
        picker->setProperty("currentIndex",indexOf("Jazz"));click(apply);
        QCOMPARE(model->count(),5);
        QCOMPARE(model->data(model->index(0),GuitarSuiteModel::PedalTypeRole).toString(),QString("pedal.compressor"));
        QCOMPARE(model->data(model->index(4),GuitarSuiteModel::PedalTypeRole).toString(),QString("pedal.reverb"));
        QVERIFY(QMetaObject::invokeMethod(board,"close"));
        while(model->count())QVERIFY(model->removePedal(model->data(model->index(0),GuitarSuiteModel::PedalIdRole).toInt()));
    }
    void pedal_cards_show_the_sections_the_backend_rules_choose() {
        auto* model=qobject_cast<GuitarSuiteModel*>(EngineController::instance()->guitarSuite()); QVERIFY(model);
        const int before=model->count();
        for(const char* pedal:{"Tuner","Octaver","Tremolo"})QVERIFY(model->addPedal(pedal));
        auto* board=window->findChild<QObject*>("guitarSuitePanel"); QVERIFY(board);
        const auto restore=qScopeGuard([&] {
            QMetaObject::invokeMethod(board,"close");
            while(model->count()>before)model->removePedal(model->data(model->index(model->count()-1),GuitarSuiteModel::PedalIdRole).toInt());
        });
        QVERIFY(QMetaObject::invokeMethod(board,"open")); QTRY_VERIFY(board->property("visible").toBool());
        QTest::qWait(200);
        QStringList texts;
        std::function<void(QQuickItem*)> collect=[&](QQuickItem* item) {
            const auto text=item->property("text");
            if(text.isValid() && item->isVisible())texts<<text.toString();
            for(auto* child:item->childItems())collect(child);
        };
        collect(board->property("contentItem").value<QQuickItem*>());
        // Tuner card: string buttons, reference text and status from the backend; no dials
        for(const char* label:{"6 · E2","5 · A2","1 · E4","Standard tuning · A4 = 440 Hz","Play one open string"})
            QVERIFY2(texts.contains(QString::fromUtf8(label)),label);
        QVERIFY(!visualItem(window->contentItem(),"pedal.tuner.string"));
        // Octaver card: the pitch delay note
        QVERIFY(texts.contains("Shifted voices have a short processing delay. Best with clear single notes."));
        // Tremolo card: Rate is a dial, sync/division/sustain are not; free-rate hint shown
        QVERIFY(visualItem(window->contentItem(),"pedal.tremolo.rate"));
        QVERIFY(!visualItem(window->contentItem(),"pedal.tremolo.sync"));
        QVERIFY(!visualItem(window->contentItem(),"pedal.tremolo.division"));
        QVERIFY(texts.contains("Free rate · adjust Rate below"));
        QVERIFY(texts.contains("1/4 · Quarter note") || texts.contains("1/2 · Half note")); // division box
    }
    void tremolo_sustain_checkbox_updates_parameter() {
        auto* model=qobject_cast<GuitarSuiteModel*>(EngineController::instance()->guitarSuite()); QVERIFY(model);
        QVERIFY(model->addPedal("Tremolo")); const int row=model->count()-1;
        const int id=model->data(model->index(row),GuitarSuiteModel::PedalIdRole).toInt();
        auto* board=window->findChild<QObject*>("guitarSuitePanel"); QVERIFY(board);
        QVERIFY(QMetaObject::invokeMethod(board,"open")); QTRY_VERIFY(board->property("visible").toBool());
        auto* toggle=visualItem(window->contentItem(),"tremoloSustainToggle"); QVERIFY(toggle);
        QTest::qWait(200);
        click(toggle); QTRY_VERIFY(toggle->property("checked").toBool());
        bool enabled=false;
        for(const auto& value:model->data(model->index(row),GuitarSuiteModel::ParametersRole).toList())
            if(value.toMap()["id"].toString()=="sustain") enabled=value.toMap()["value"].toInt()==1;
        QVERIFY(enabled);
        QVERIFY(visualItem(window->contentItem(),"pedal.tremolo.hold"));
        QVERIFY(visualItem(window->contentItem(),"pedal.tremolo.fade"));
        QVERIFY(QMetaObject::invokeMethod(board,"close")); QVERIFY(model->removePedal(id));
    }
    void status_strip_shows_device_readouts_and_resets_xruns() {
        auto* controller=EngineController::instance();
        auto* strip=window->findChild<QQuickItem*>("testStatusStrip"); QVERIFY(strip);
        const auto restore=qScopeGuard([&] { strip->setVisible(false); controller->resetXruns(); });
        strip->setVisible(true); controller->resetXruns(); QTest::qWait(30);
        auto* rate=visualItem(strip,"statusSampleRate"); auto* buffer=visualItem(strip,"statusBuffer");
        auto* xruns=visualItem(strip,"statusXruns"); auto* message=visualItem(strip,"statusMessage");
        QVERIFY(rate); QVERIFY(buffer); QVERIFY(xruns); QVERIFY(message);
        // The readouts show the controller's backend-formatted text.
        QCOMPARE(rate->property("text").toString(),controller->sampleRateText());
        QCOMPARE(buffer->property("text").toString(),controller->bufferText());
        QVERIFY(!controller->status().contains("JACK block"));    // no longer repeated in the message
        QCOMPARE(xruns->property("text").toString(),QString("0 xruns"));
        const QColor calm=xruns->property("color").value<QColor>();
        // Dropouts reported by the device show up (polled) and turn the readout orange.
        controller->engine()->xrunOccurred(); controller->engine()->xrunOccurred();
        QTRY_COMPARE(xruns->property("text").toString(),QString("2 xruns"));
        QCOMPARE(controller->xrunCount(),2);
        QVERIFY(xruns->property("color").value<QColor>()!=calm);
        // Clicking it starts counting again.
        click(xruns);
        QTRY_COMPARE(xruns->property("text").toString(),QString("0 xruns"));
        QCOMPARE(xruns->property("color").value<QColor>(),calm);
    }
    void icons_live_only_in_icons_qml_and_the_icons_folder() {
        const QString root=QStringLiteral(CHAOSMAKER_SOURCE_DIR);
        // Code points used as icons (arrows, technical, shapes, dingbats, braille, roman numerals,
        // emoji, × ‹ › ✓ ✕). Typographic text such as − · — … stays allowed.
        const auto isIconSymbol=[](char32_t c) {
            return (c>=0x2190 && c<=0x21FF) || (c>=0x2300 && c<=0x23FF) || (c>=0x25A0 && c<=0x25FF)
                || (c>=0x2600 && c<=0x27BF) || (c>=0x27F0 && c<=0x27FF) || (c>=0x2800 && c<=0x28FF)
                || (c>=0x2160 && c<=0x2188) || (c>=0x1F300 && c<=0x1FAFF)
                || c==0x00D7 || c==0x2039 || c==0x203A || c==0x2713 || c==0x2715;
        };
        QStringList offenders;
        QStringList files{root+"/Main.qml"};
        QDirIterator it(root+"/frontend",{"*.qml"},QDir::Files,QDirIterator::Subdirectories);
        while(it.hasNext()) files<<it.next();
        for(const QString& path:files) {
            if(path.endsWith("/frontend/icons/Icons.qml")) continue;
            QFile file(path); QVERIFY2(file.open(QIODevice::ReadOnly),qPrintable(path));
            int line=0;
            for(const QByteArray& bytes:file.readAll().split('\n')) {
                ++line;
                for(auto c:QString::fromUtf8(bytes).toUcs4())
                    if(isIconSymbol(c)) { offenders<<QString("%1:%2").arg(path.mid(root.size()+1)).arg(line); break; }
            }
        }
        QVERIFY2(offenders.isEmpty(),qPrintable("Icon symbols outside icons/Icons.qml: "+offenders.join(", ")));
        // Icons.qml writes every value as code points ("▶"), never as a literal symbol.
        QFile icons(root+"/frontend/icons/Icons.qml"); QVERIFY(icons.open(QIODevice::ReadOnly));
        const QString iconsText=QString::fromUtf8(icons.readAll());
        const QRegularExpression value(QStringLiteral("readonly property string (\\w+):\\s*\"([^\"]*)\""));
        int count=0;
        for(auto match=value.globalMatch(iconsText);match.hasNext();) {
            const auto m=match.next(); ++count;
            for(QChar c:m.captured(2)) QVERIFY2(c.unicode()<0x80,qPrintable("Write Icons."+m.captured(1)+" as \\u escapes, got: "+m.captured(2)));
        }
        QVERIFY(count>=20);
        // Every icon has a value at runtime (escapes decoded, the magnet as one code point).
        auto* singleton=engine->singletonInstance<QObject*>("TransportTests","Icons"); QVERIFY(singleton);
        const QMetaObject* meta=singleton->metaObject();
        int checked=0;
        for(int i=meta->propertyOffset();i<meta->propertyCount();++i,++checked)
            QVERIFY2(!meta->property(i).read(singleton).toString().isEmpty(),meta->property(i).name());
        QCOMPARE(checked,count);
        QCOMPARE(singleton->property("snap").toString(),QString::fromUcs4(U"\U0001F9F2"));
        QCOMPARE(singleton->property("close").toString(),QString(QChar(0x00D7)));
        // Drawn icons live in frontend/icons/ and are listed in Icons.qml.
        QDirIterator drawn(root+"/frontend/icons",{"*.qml"},QDir::Files);
        int drawnCount=0;
        while(drawn.hasNext()) {
            const QString name=QFileInfo(drawn.next()).fileName();
            if(name==QLatin1String("Icons.qml")) continue;
            ++drawnCount;
            QVERIFY2(iconsText.contains(name),qPrintable(name+" is not listed in Icons.qml"));
        }
        QVERIFY(drawnCount>=1);
        // And the transport bar really shows them.
        auto* bar=window->findChild<QQuickItem*>("transport"); QVERIFY(bar);
        QStringList glyphs;
        std::function<void(QQuickItem*)> collect=[&](QQuickItem* item) {
            if(item->property("glyph").isValid()) glyphs<<item->property("glyph").toString();
            for(auto* child:item->childItems()) collect(child);
        };
        collect(bar);
        for(const char* name:{"goToStart","skipForward","record","play","pause","stop","loop"})
            QVERIFY2(glyphs.contains(singleton->property(name).toString()),name);
    }
    void colours_live_only_in_the_theme_folder() {
        // Plan 4.1: every colour is a token in frontend/theme/ (Theme.qml and its area palettes).
        const QString root=QStringLiteral(CHAOSMAKER_SOURCE_DIR);
        const QRegularExpression hex(QStringLiteral("\"#[0-9a-fA-F]{3,8}\""));
        QStringList offenders;
        QStringList files{root+"/Main.qml"};
        QDirIterator it(root+"/frontend",{"*.qml"},QDir::Files,QDirIterator::Subdirectories);
        while(it.hasNext()) files<<it.next();
        for(const QString& path:files) {
            const QString rel=path.mid(root.size()+1);
            if(rel.startsWith("frontend/theme/")) continue;
            QFile file(path); QVERIFY2(file.open(QIODevice::ReadOnly),qPrintable(rel));
            int line=0;
            for(const QByteArray& bytes:file.readAll().split('\n')) {
                ++line;
                if(hex.match(QString::fromUtf8(bytes)).hasMatch()) offenders<<QString("%1:%2").arg(rel).arg(line);
            }
        }
        QVERIFY2(offenders.isEmpty(),qPrintable("Hard-coded colours outside frontend/theme/: "+offenders.join(", ")));
        // The piano roll palette is reachable through Theme.
        auto* theme=engine->singletonInstance<QObject*>("TransportTests","Theme"); QVERIFY(theme);
        auto* pianoRoll=qvariant_cast<QObject*>(theme->property("pianoRoll")); QVERIFY(pianoRoll);
        QCOMPARE(pianoRoll->property("toolButton").value<QColor>(),QColor("#2f323a"));
        auto* trackList=qvariant_cast<QObject*>(theme->property("trackList")); QVERIFY(trackList);
        QCOMPARE(trackList->property("rowSelected").value<QColor>(),QColor("#2a343d"));
        QCOMPARE(theme->property("trackColors").toList().size(),12);   // the palette the picker offers
        for(const char* area:{"timeline","mixer","transport","guitarSuite","effects"})
            QVERIFY2(qvariant_cast<QObject*>(theme->property(area)),area);
    }
    void controls_use_the_shared_themed_components() {
        // Plan 4.3: menus, entries, separators, text buttons, drop-downs and sliders come from
        // frontend/common/ (ThemedMenu, ThemedMenuItem, ThemedMenuSeparator, PanelButton,
        // ThemedComboBox, ToggleChip, ThemedSlider); nothing else re-styles Qt's plain ones.
        const QString root=QStringLiteral(CHAOSMAKER_SOURCE_DIR);
        const QRegularExpression plain(QStringLiteral("(^|[^\\w.])(Menu|MenuItem|MenuSeparator|Button|ComboBox|Slider)\\s*\\{"));
        QStringList offenders;
        QStringList files{root+"/Main.qml"};
        QDirIterator it(root+"/frontend",{"*.qml"},QDir::Files,QDirIterator::Subdirectories);
        while(it.hasNext()) files<<it.next();
        for(const QString& path:files) {
            const QString rel=path.mid(root.size()+1);
            if(rel.startsWith("frontend/common/")) continue;
            QFile file(path); QVERIFY2(file.open(QIODevice::ReadOnly),qPrintable(rel));
            int line=0;
            for(const QByteArray& bytes:file.readAll().split('\n')) {
                ++line;
                if(plain.match(QString::fromUtf8(bytes)).hasMatch()) offenders<<QString("%1:%2").arg(rel).arg(line);
            }
        }
        QVERIFY2(offenders.isEmpty(),qPrintable("Plain menus/buttons/drop-downs/sliders outside frontend/common/: "+offenders.join(", ")));
        // A converted entry keeps its name, shortcut text and the shared look.
        auto* save=visualObject(window->contentItem(),"fileSaveItem");
        if(!save) save=window->findChild<QObject*>("fileSaveItem");
        QVERIFY(save);
        QCOMPARE(save->property("shortcutText").toString(),QString("Ctrl+S"));
        QCOMPARE(save->property("implicitHeight").toDouble(),30.0);
    }
    void toggle_chips_light_up_lighten_on_hover_and_fade_when_disabled() {
        TrackListWrapper tracks; const int vocal=tracks.addVocalTrack(), synth=tracks.addInstrumentTrack();
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        const auto restore=qScopeGuard([&] { list->setVisible(false); list->setProperty("trackModel",QVariant()); });
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true); QTest::qWait(30);
        const auto bg=[](QQuickItem* chip) { return qvariant_cast<QQuickItem*>(chip->property("background"))->property("color").value<QColor>(); };
        auto* theme=engine->singletonInstance<QObject*>("TransportTests","Theme"); QVERIFY(theme);
        const QColor chip=theme->property("chip").value<QColor>(), chipHover=theme->property("chipHover").value<QColor>();
        // Action chip (FX): lightens under the mouse.
        auto* fx=visualItem(list,"trackFxButton"+QString::number(vocal)); QVERIFY(fx); QVERIFY(fx->inherits("QQuickToolButton"));
        QCOMPARE(bg(fx),chip);
        QTest::mouseMove(window,fx->mapToScene(QPointF(fx->width()/2,fx->height()/2)).toPoint());
        QTRY_COMPARE(bg(fx),chipHover);
        // Toggle chip (M): no hover change, lit in mute colours when active.
        auto* mute=visualItem(list,"trackMuteButton"+QString::number(vocal)); QVERIFY(mute);
        QTest::mouseMove(window,mute->mapToScene(QPointF(mute->width()/2,mute->height()/2)).toPoint());
        QTest::qWait(30); QCOMPARE(bg(mute),chip);
        QVERIFY(tracks.setMuted(vocal,true)); QCOMPARE(bg(mute),theme->property("muteBg").value<QColor>());
        // MON works on an instrument track too (its live input is MIDI); a disabled chip fades.
        auto* liveMon=visualItem(list,"trackMonitorButton"+QString::number(vocal));
        auto* midiMon=visualItem(list,"trackMonitorButton"+QString::number(synth));
        QVERIFY(liveMon); QVERIFY(midiMon);
        QCOMPARE(liveMon->opacity(),1.0);
        QVERIFY(midiMon->isEnabled()); QCOMPARE(midiMon->opacity(),1.0);
        midiMon->setEnabled(false); QVERIFY(midiMon->opacity()<0.6);
        midiMon->setEnabled(true);
    }
    void text_is_never_tiny_and_symbol_controls_have_accessible_names() {
        // Plan 4.5: no text below Theme.microSize (9 px) anywhere in the QML sources.
        const QString root=QStringLiteral(CHAOSMAKER_SOURCE_DIR);
        const QRegularExpression size(QStringLiteral("(pixelSize|textSize)\\s*:\\s*(\\d+)\\b"));
        QStringList tiny;
        QDirIterator it(root+"/frontend",{"*.qml"},QDir::Files,QDirIterator::Subdirectories);
        QStringList files{root+"/Main.qml"};
        while(it.hasNext()) files<<it.next();
        for(const QString& path:files) {
            QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
            int line=0;
            for(const QByteArray& bytes:file.readAll().split('\n')) {
                ++line;
                for(auto m=size.globalMatch(QString::fromUtf8(bytes));m.hasNext();)
                    if(m.next().captured(2).toInt()<9) tiny<<QString("%1:%2").arg(path.mid(root.size()+1)).arg(line);
            }
        }
        QVERIFY2(tiny.isEmpty(),qPrintable("Text below 9 px: "+tiny.join(", ")));
        // Controls that only show a symbol or an abbreviation carry a spoken name.
        const auto name=[](QQuickItem* item) { return QQmlProperty::read(item,"Accessible.name",qmlContext(item)).toString(); };
        auto* play=visualItem(window->contentItem(),"transport_play"); QVERIFY(play);
        QCOMPARE(name(play),QString("Play (Space)"));
        TrackListWrapper tracks; const int id=tracks.addVocalTrack();
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        const auto restore=qScopeGuard([&] { list->setVisible(false); list->setProperty("trackModel",QVariant()); });
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true); QTest::qWait(30);
        auto* remove=visualItem(list,"trackRemoveButton"+QString::number(id)); QVERIFY(remove);
        QCOMPARE(name(remove),QString("Remove track"));
        auto* mute=visualItem(list,"trackMuteButton"+QString::number(id)); QVERIFY(mute);
        QCOMPARE(name(mute),QString("Mute disabled"));
        QVERIFY(tracks.setMuted(id,true)); QCOMPARE(name(mute),QString("Mute enabled"));
    }
    void stop_at_end_setting_is_a_saved_settings_menu_toggle() {
        auto* controller=EngineController::instance();
        QSignalSpy changed(controller,SIGNAL(stopPlaybackAtEndChanged()));
        QVERIFY(controller->stopPlaybackAtEnd()); // default on
        const auto toggle=[&] {
            auto* settings=visualItem(window->contentItem(),"settingsButton"); QVERIFY(settings); click(settings);
            QQuickItem* option=nullptr;
            QTRY_VERIFY((option=visualItem(window->contentItem(),"stopAtEndSetting")));
            QCOMPARE(option->property("text").toString(),QString("Stop playback at the end"));
            QCOMPARE(option->property("checked").toBool(),controller->stopPlaybackAtEnd());
            click(option);
        };
        toggle(); QTRY_VERIFY(!controller->stopPlaybackAtEnd());
        QCOMPARE(QSettings().value("playback/stopAtEnd").toBool(),false);
        toggle(); QTRY_VERIFY(controller->stopPlaybackAtEnd());
        QCOMPARE(QSettings().value("playback/stopAtEnd").toBool(),true);
        QCOMPARE(changed.count(),2);
    }
    void track_popups_are_shared_and_follow_the_card_that_opened_them() {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller->attachProject(&tracks);
        const int a=tracks.addInstrumentTrackOfType("Electric Guitar"),b=tracks.addInstrumentTrackOfType("Electric Guitar"),
                  vocal=tracks.addVocalTrack();
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        const auto restore=qScopeGuard([&] { list->setVisible(false); list->setProperty("trackModel",QVariant()); });
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true); QTest::qWait(30);
        // Plan 5.1: one instance of each popup, however many tracks there are.
        for(const char* stem:{"trackEffectsPanel","trackColourPopup","trackColourDialog","trackInputPopup"}) {
            int found=0;
            for(auto* object:list->findChildren<QObject*>()) if(object->objectName().startsWith(stem)) ++found;
            QVERIFY2(found==1,qPrintable(QString("%1: %2 instances").arg(stem).arg(found)));
        }
        // The panel covers the track list, so it is closed before opening FX on another track.
        QObject* panel=nullptr;
        const auto openFx=[&](int id) {
            if(panel && panel->property("visible").toBool()) {
                click(visualItem(window->contentItem(),"closeTrackEffects")); QTRY_VERIFY(!panel->property("visible").toBool());
            }
            click(visualItem(list,"trackFxButton"+QString::number(id)));
            panel=visualObject(window->contentItem(),"trackEffectsPanel"+QString::number(id)); QVERIFY(panel);
            QTRY_VERIFY(panel->property("visible").toBool());
        };
        openFx(a);
        QCOMPARE(panel->property("effectsModel").value<QObject*>(),controller->trackEffects(a));
        QCOMPARE(panel->property("selectedEffect").toString(),QString("guitarsuite")); // first on its chain
        addEffectFromPicker("eq");
        QTRY_COMPARE(panel->property("selectedEffect").toString(),QString("eq"));
        // The same panel serves every track; each track keeps its own page and pedalboard.
        QObject* const shared=panel;
        openFx(b); QCOMPARE(panel,shared);
        QCOMPARE(panel->property("selectedEffect").toString(),QString("guitarsuite"));
        QCOMPARE(panel->property("effectsModel").value<QObject*>(),controller->trackEffects(b));
        openFx(vocal); QCOMPARE(panel,shared); QVERIFY(!panel->property("supportsGuitarSuite").toBool());
        QCOMPARE(panel->property("selectedEffect").toString(),QString());          // no effects yet
        QCOMPARE(panel->property("effectsModel").value<QObject*>(),static_cast<QObject*>(nullptr));
        openFx(a);
        QCOMPARE(panel->property("selectedEffect").toString(),QString("eq"));
        QCOMPARE(panel->property("effectsModel").value<QObject*>(),controller->trackEffects(a));
        QVERIFY(visualObject(window->contentItem(),"trackGuitarSuiteEditor"+QString::number(a)));
        click(visualItem(window->contentItem(),"closeTrackEffects")); QTRY_VERIFY(!panel->property("visible").toBool());
        // The input popup opens for the card whose IN button was clicked.
        click(visualItem(list,"trackInputButton"+QString::number(vocal)));
        auto* input=visualObject(window->contentItem(),"trackInputPopup"+QString::number(vocal)); QVERIFY(input);
        QTRY_VERIFY(input->property("visible").toBool());
        QCOMPARE(input->property("trackName").toString(),QString::fromStdString(tracks.project().findTrack(vocal)->name));
        QMetaObject::invokeMethod(input,"close"); QTRY_VERIFY(!input->property("visible").toBool());
        controller->shutdownSession();
    }
    void main_window_piano_roll_edits_its_clip_and_names_drum_parts() {
        // The real main window's piano roll (a Loader's component) must reach the project's
        // tracks: before, its "trackModel: trackModel" bound to itself and it stood alone.
        QTest::failOnWarning(QRegularExpression("^(?!This plugin does not support raise\\(\\)).*"));
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        QQmlComponent component(engine.get()); component.loadFromModule("TransportTests","Main");
        std::unique_ptr<QObject> created(component.create()); QVERIFY2(created,qPrintable(component.errorString()));
        auto* main=qobject_cast<QQuickWindow*>(created.get()); QVERIFY(main);
        auto* settings=qvariant_cast<AppSettingsWrapper*>(main->property("layoutSettings")); QVERIFY(settings);
        const auto restore=qScopeGuard([&] {
            settings->setPianoRollPresentation("dock"); controller->shutdownSession();
            created.reset(); window->requestActivate(); [[maybe_unused]] const bool active=QTest::qWaitForWindowActive(window);
        });
        settings->setPianoRollPresentation("dock");
        auto* tracks=main->findChild<TrackListWrapper*>(); QVERIFY(tracks);
        const int drums=tracks->addInstrumentTrackOfType("Drums"), clip=tracks->addClipAtBeat(drums,0,4);
        QVERIFY(QMetaObject::invokeMethod(main,"openPianoRoll",Q_ARG(QVariant,drums),Q_ARG(QVariant,clip),
                                          Q_ARG(QVariant,QString("Clip")),Q_ARG(QVariant,4.0)));
        auto* loader=main->findChild<QObject*>("dockedPianoRollLoader"); QVERIFY(loader);
        auto* view=loader->property("item").value<QObject*>(); QVERIFY(view);
        auto* backend=view->findChild<QObject*>("pianoRollBackend"); QVERIFY(backend);
        QCOMPARE(backend->property("trackList").value<QObject*>(),static_cast<QObject*>(tracks));
        QVERIFY(backend->property("bound").toBool());
        QTRY_VERIFY(backend->property("drumMode").toBool());
        auto* kick=visualItem(qobject_cast<QQuickItem*>(view),"drumKeyLabel60");
        QVERIFY(kick); QTRY_VERIFY(kick->isVisible());
        QCOMPARE(kick->property("text").toString(),QString("Kick"));
        // A note drawn in the piano roll lands in the clip.
        int id=-1;
        QVERIFY(QMetaObject::invokeMethod(backend,"addNote",Q_RETURN_ARG(int,id),Q_ARG(int,62),Q_ARG(double,0.0),Q_ARG(double,1.0),Q_ARG(int,100)));
        QTRY_COMPARE(int(tracks->domain().clipNotes(drums,clip).size()),1);
        QVERIFY(main->grabWindow().save("/tmp/chaosmaker-main-drum-piano-roll.png"));
    }
    void piano_rolls_load_on_first_use_and_only_for_the_chosen_presentation() {
        // The real main window: any QML warning (e.g. a broken binding) fails the test. The
        // offscreen platform cannot raise windows; that warning is not about the QML.
        QTest::failOnWarning(QRegularExpression("^(?!This plugin does not support raise\\(\\)).*"));
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        QQmlComponent component(engine.get()); component.loadFromModule("TransportTests","Main");
        std::unique_ptr<QObject> created(component.create()); QVERIFY2(created,qPrintable(component.errorString()));
        auto* main=qobject_cast<QQuickWindow*>(created.get()); QVERIFY(main);
        auto* settings=qvariant_cast<AppSettingsWrapper*>(main->property("layoutSettings")); QVERIFY(settings);
        const auto restore=qScopeGuard([&] {
            settings->setPianoRollPresentation("dock"); controller->shutdownSession();
            // Hand focus back to the harness window: later tests check keyboard focus there.
            created.reset(); window->requestActivate(); [[maybe_unused]] const bool active=QTest::qWaitForWindowActive(window);
        });
        settings->setPianoRollPresentation("dock");
        auto* tracks=main->findChild<TrackListWrapper*>(); QVERIFY(tracks);
        const int track=tracks->addInstrumentTrack(), clip=tracks->addClipAtBeat(track,0,4);
        auto* docked=main->findChild<QObject*>("dockedPianoRollLoader"); QVERIFY(docked);
        auto* floating=main->findChild<QObject*>("floatingPianoRollLoader"); QVERIFY(floating);
        const auto item=[](QObject* loader) { return loader->property("item").value<QObject*>(); };
        // Nothing is created at startup.
        QVERIFY(!item(docked)); QVERIFY(!item(floating));
        QVERIFY(QMetaObject::invokeMethod(main,"openPianoRoll",Q_ARG(QVariant,track),Q_ARG(QVariant,clip),
                                          Q_ARG(QVariant,QString("Clip")),Q_ARG(QVariant,4.0)));
        QObject* view=item(docked); QVERIFY(view); QVERIFY(!item(floating));
        QCOMPARE(view->property("clipId").toInt(),clip);
        QVERIFY(main->property("pianoRollMinimumDockHeight").toDouble()>0);
        // Back to the mixer and again: the same view, its tool kept.
        view->setProperty("activeTool","eraser");
        QVERIFY(QMetaObject::invokeMethod(main,"togglePianoRoll")); QCOMPARE(main->property("pianoRollShown").toBool(),false);
        QCOMPARE(item(docked),view);
        QVERIFY(QMetaObject::invokeMethod(main,"togglePianoRoll")); QCOMPARE(main->property("pianoRollShown").toBool(),true);
        QCOMPARE(item(docked),view); QCOMPARE(view->property("activeTool").toString(),QString("eraser"));
        // Floating preference: the dock stays while still on screen, then unloads once hidden.
        settings->setPianoRollPresentation("window");
        QCOMPARE(item(docked),view);
        QVERIFY(QMetaObject::invokeMethod(main,"togglePianoRoll"));
        QTRY_VERIFY(!item(docked)); QVERIFY(!item(floating));
        QVERIFY(QMetaObject::invokeMethod(main,"togglePianoRoll"));
        auto* window=main->findChild<QQuickWindow*>("floatingPianoRollWindow"); QVERIFY(window);
        QTRY_VERIFY(window->isVisible()); QVERIFY(item(floating)); QVERIFY(!item(docked));
        QCOMPARE(item(floating)->property("clipId").toInt(),clip);
        window->close(); QTRY_VERIFY(!window->isVisible()); QVERIFY(item(floating));
    }
    void chord_zone_mapping_is_a_preference() {
        // Preferences: own octave per zone by default, zone keys the other choice; a click saves it.
        auto* bridge=window->findChild<AppSettingsWrapper*>("appSettingsBridge"); QVERIFY(bridge);
        const auto restoreBridge=qScopeGuard([&] { bridge->setChordZoneMapping("octaves"); });
        QCOMPARE(bridge->chordZoneMapping(),QString("octaves"));
        auto* settings=visualItem(window->contentItem(),"settingsButton"); QVERIFY(settings); click(settings);
        QQuickItem* preferences=nullptr;
        QTRY_VERIFY((preferences=visualItem(window->contentItem(),"preferencesSetting"))); click(preferences);
        QQuickItem* keys=nullptr; QQuickItem* octaves=nullptr;
        QTRY_VERIFY((keys=visualItem(window->contentItem(),"chordZoneKeys")) && keys->isVisible());
        octaves=visualItem(window->contentItem(),"chordZoneOctaves"); QVERIFY(octaves);
        QVERIFY(octaves->property("checked").toBool()); QVERIFY(!keys->property("checked").toBool());
        click(keys); QTRY_COMPARE(bridge->chordZoneMapping(),QString("zonekeys"));
        QCOMPARE(QSettings().value("chords/zoneMapping").toString(),QString("zonekeys"));
        QCOMPARE(AppSettingsWrapper::storedChordZoneMapping(),QString("zonekeys"));
        click(octaves); QTRY_COMPARE(bridge->chordZoneMapping(),QString("octaves"));
        QCOMPARE(QSettings().value("chords/zoneMapping").toString(),QString("octaves"));
        QMetaObject::invokeMethod(window->findChild<QObject*>("preferencesPopup"),"close");
    }
    void default_record_mode_is_a_preference_and_new_tracks_follow_it() {
        // Preferences: two radio buttons, Dry checked by default; a click saves the choice.
        auto* bridge=window->findChild<AppSettingsWrapper*>("appSettingsBridge"); QVERIFY(bridge);
        const auto restoreBridge=qScopeGuard([&] { bridge->setDefaultRecordMode("dry"); });
        QCOMPARE(bridge->defaultRecordMode(),QString("dry"));
        auto* settings=visualItem(window->contentItem(),"settingsButton"); QVERIFY(settings); click(settings);
        QQuickItem* preferences=nullptr;
        QTRY_VERIFY((preferences=visualItem(window->contentItem(),"preferencesSetting"))); click(preferences);
        QQuickItem* print=nullptr; QQuickItem* dry=nullptr;
        QTRY_VERIFY((print=visualItem(window->contentItem(),"defaultRecordModePrint")) && print->isVisible());
        dry=visualItem(window->contentItem(),"defaultRecordModeDry"); QVERIFY(dry);
        QVERIFY(dry->property("checked").toBool()); QVERIFY(!print->property("checked").toBool());
        click(print); QTRY_COMPARE(bridge->defaultRecordMode(),QString("print"));
        QCOMPARE(QSettings().value("recording/defaultMode").toString(),QString("print"));
        click(dry); QTRY_COMPARE(bridge->defaultRecordMode(),QString("dry"));
        QCOMPARE(QSettings().value("recording/defaultMode").toString(),QString("dry"));
        QMetaObject::invokeMethod(window->findChild<QObject*>("preferencesPopup"),"close");

        // The main window binds its track list to the preference.
        QTest::failOnWarning(QRegularExpression("^(?!This plugin does not support raise\\(\\)).*"));
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        QQmlComponent component(engine.get()); component.loadFromModule("TransportTests","Main");
        std::unique_ptr<QObject> created(component.create()); QVERIFY2(created,qPrintable(component.errorString()));
        auto* main=qobject_cast<QQuickWindow*>(created.get()); QVERIFY(main);
        auto* mainSettings=qvariant_cast<AppSettingsWrapper*>(main->property("layoutSettings")); QVERIFY(mainSettings);
        const auto restore=qScopeGuard([&] {
            mainSettings->setDefaultRecordMode("dry"); controller->shutdownSession();
            created.reset(); window->requestActivate(); [[maybe_unused]] const bool active=QTest::qWaitForWindowActive(window);
        });
        auto* tracks=main->findChild<TrackListWrapper*>(); QVERIFY(tracks);
        QVERIFY(tracks->property("newTrackRecordDry").toBool());
        const int dryVocal=tracks->addVocalTrack();
        QVERIFY(tracks->project().findTrack(dryVocal)->recordDry);              // Dry by default
        mainSettings->setDefaultRecordMode("print");
        QVERIFY(!tracks->property("newTrackRecordDry").toBool());
        const int printGuitar=tracks->addInstrumentTrackOfType("Electric Guitar");
        QVERIFY(!tracks->project().findTrack(printGuitar)->recordDry);
        QVERIFY(tracks->project().findTrack(dryVocal)->recordDry);              // existing tracks keep theirs
    }
    void track_card_chips_wrap_and_fit_at_the_narrowest_track_list() {
        TrackListWrapper tracks; const int id=tracks.addVocalTrack();
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        const qreal oldWidth=list->width();
        const auto restore=qScopeGuard([&] { list->setVisible(false); list->setWidth(oldWidth); list->setProperty("trackModel",QVariant()); });
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true);
        const QString n=QString::number(id);
        auto* accent=visualItem(list,"trackColourAccent"+n); QVERIFY(accent);
        auto* card=accent->parentItem(); QVERIFY(card);
        for(const qreal width:{340.0,180.0}) {
            list->setWidth(width); QTest::qWait(30);
            const QRectF cardRect=card->mapRectToScene(QRectF(0,0,card->width(),card->height()));
            QList<QRectF> placed;
            for(const char* stem:{"trackMuteButton","trackSoloButton","trackInputButton","trackMonitorButton",
                                   "trackRecordButton","trackFxButton","trackRemoveButton","trackVolume"}) {
                auto* item=visualItem(list,stem+n); QVERIFY2(item,stem);
                const QRectF rect=item->mapRectToScene(QRectF(0,0,item->width(),item->height()));
                const QString where=QString("%1 at %2 px: %3,%4 %5x%6").arg(stem).arg(width).arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
                QVERIFY2(item->isVisible() && rect.width()>0 && rect.height()>0,qPrintable(where));
                QVERIFY2(cardRect.contains(rect),qPrintable(where+" outside card"));
                for(const QRectF& other:placed) QVERIFY2(!other.intersects(rect),qPrintable(where+" overlaps"));
                placed.append(rect);
            }
            // Mute (first) and remove share the top line; at 180 px FX drops to a second line.
            QCOMPARE(placed[6].top(),placed[0].top());
            QCOMPARE(placed[5].top()>placed[0].top(),width<200);
        }
    }
    void layout_sizes_are_saved_and_restored_between_sessions() {
        QSettings qs;
        const auto restore=qScopeGuard([&] {
            for(const char* key:{AppSettings::KeyTrackListWidth,AppSettings::KeyMixerHeight,AppSettings::KeyPianoRollDockHeight})
                qs.remove(QString::fromLatin1(key));
        });
        {
            AppSettingsWrapper settings;
            QCOMPARE(settings.trackListWidth(),230.0); QCOMPARE(settings.mixerHeight(),180.0);
            QCOMPARE(settings.pianoRollDockHeight(),380.0);
            QSignalSpy changed(&settings,&AppSettingsWrapper::layoutSizesChanged);
            settings.setProperty("trackListWidth",312.0);
            settings.setProperty("mixerHeight",900.0);              // clamped by the backend
            settings.setProperty("pianoRollDockHeight",450.0);
            settings.setProperty("pianoRollDockHeight",450.0);      // unchanged: not saved again
            QCOMPARE(changed.count(),3);
            QCOMPARE(settings.mixerHeight(),480.0);
            QCOMPARE(settings.clampTrackListWidth(10),180.0);
        }
        // Main.qml reads the sizes through the top bar's settings button (topBar.appSettings.bridge).
        auto* topBar=window->findChild<QQuickItem*>("testTopBar"); QVERIFY(topBar);
        auto* bridge=qvariant_cast<QObject*>(qvariant_cast<QObject*>(topBar->property("appSettings"))->property("bridge"));
        QVERIFY(qobject_cast<AppSettingsWrapper*>(bridge));
        for(const char* size:{"trackListWidth","mixerHeight","pianoRollDockHeight"})
            QVERIFY2(bridge->property(size).canConvert<double>() && bridge->property(size).toDouble()>0,size);
        AppSettingsWrapper next;                                    // a new session reads them back
        QCOMPARE(next.trackListWidth(),312.0); QCOMPARE(next.mixerHeight(),480.0);
        QCOMPARE(next.pianoRollDockHeight(),450.0);
        qs.setValue(QString::fromLatin1(AppSettings::KeyMixerHeight),"huge"); qs.sync();
        AppSettingsWrapper corrupted;                               // bad stored text: default
        QCOMPARE(corrupted.mixerHeight(),180.0);
        corrupted.resetToDefaults();
        AppSettingsWrapper reset;
        QCOMPARE(reset.trackListWidth(),230.0); QCOMPARE(reset.pianoRollDockHeight(),380.0);
    }
    void device_selectors_live_in_preferences_not_the_top_bar() {
        auto* top=window->findChild<QQuickItem*>("testTopBar"); QVERIFY(top);
        for(const char* name:{"bufferSizeSelector","outputDeviceSelector","midiInputSelector"})
            QVERIFY2(!visualItem(top,name),name);
        auto* settings=visualItem(window->contentItem(),"settingsButton"); QVERIFY(settings); click(settings);
        QQuickItem* option=nullptr;
        QTRY_VERIFY((option=visualItem(window->contentItem(),"preferencesSetting")));
        click(option);
        auto* popup=window->findChild<QObject*>("preferencesPopup"); QVERIFY(popup);
        QTRY_VERIFY(popup->property("visible").toBool());
        const auto closePopup=qScopeGuard([&] { QMetaObject::invokeMethod(popup,"close"); QCoreApplication::processEvents(); });
        auto* content=popup->property("contentItem").value<QQuickItem*>(); QVERIFY(content);
        auto* controller=EngineController::instance();
        auto* buffer=visualItem(content,"bufferSizeSelector"); QVERIFY(buffer); QVERIFY(buffer->isVisible());
        auto* output=visualItem(content,"outputDeviceSelector"); QVERIFY(output); QVERIFY(output->isVisible());
        auto* midi=visualItem(content,"midiInputSelector"); QVERIFY(midi); QVERIFY(midi->isVisible());
        QCOMPARE(buffer->property("count").toInt(),int(controller->property("bufferSizes").toStringList().size()));
        QCOMPARE(output->property("count").toInt(),int(controller->property("outputDevices").toStringList().size()));
        QCOMPARE(midi->property("count").toInt(),int(controller->property("midiInputPorts").toStringList().size()));
        // Every row fits inside the dialog.
        const QRectF box=content->mapRectToScene(QRectF(0,0,content->width(),content->height()));
        for(auto* item:{buffer,output,midi}) {
            const QRectF r=item->mapRectToScene(QRectF(0,0,item->width(),item->height()));
            QVERIFY(box.contains(r));
        }
    }
    void startup_setting_saves_and_restores_session_stopped() {
        auto* controller=EngineController::instance();
        controller->setRestoreSessionOnStartup(false);
        TrackListWrapper first; controller->attachProject(&first);
        const int track=first.addInstrumentTrackOfType("Electric Guitar");
        first.setVolume(track,0.42); first.setRecordArmed(track,true);
        controller->setBpm(88); controller->setMeter(3,4);
        auto* settings=visualItem(window->contentItem(),"settingsButton"); QVERIFY(settings); click(settings);
        QQuickItem* option=nullptr;
        QTRY_VERIFY((option=visualItem(window->contentItem(),"restoreSessionSetting")));
        click(option); QTRY_VERIFY(controller->restoreSessionOnStartup());
        QVERIFY(controller->saveSession());
        TrackListWrapper second; controller->attachProject(&second);
        QCOMPARE(second.count(),1); QCOMPARE(controller->bpm(),88.0); QCOMPARE(controller->numerator(),3);
        QVERIFY(!controller->playing()); QVERIFY(!controller->recording()); QVERIFY(!second.cleanMonitorArmed());
        QCOMPARE(second.data(second.index(0),TrackListWrapper::VolumeRole).toFloat(),0.42f);
        controller->setRestoreSessionOnStartup(false);
    }
    void track_effects_switch_in_place_and_keep_edits_on_the_selected_track() {
        auto* controller=EngineController::instance();
        controller->setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller->attachProject(&tracks);
        const int guitar=tracks.addInstrumentTrackOfType("Electric Guitar"), vocal=tracks.addVocalTrack();
        auto* guitarFx=qobject_cast<GuitarSuiteModel*>(controller->trackEffects(guitar));
        auto* vocalFx=qobject_cast<GuitarSuiteModel*>(controller->trackEffects(vocal));
        QVERIFY(guitarFx && vocalFx && guitarFx!=vocalFx);
        QVERIFY(guitarFx->addPedal("Parametric EQ"));
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true);
        QCoreApplication::processEvents();
        auto* fxButton=visualItem(window->contentItem(),"trackFxButton"+QString::number(guitar)); QVERIFY(fxButton);
        click(fxButton);
        auto* panel=visualObject(window->contentItem(),"trackEffectsPanel"+QString::number(guitar)); QVERIFY(panel);
        QTRY_VERIFY(panel->property("visible").toBool());
        // A guitar starts with GuitarSuite; its EQ is added from the picker.
        QCOMPARE(panel->property("selectedEffect").toString(),QString("guitarsuite"));
        // Rows are rebuilt when the chain changes, so look them up each time.
        const auto row=[&](const char* type) { return visualItem(window->contentItem(),QString("selectEffect_")+type); };
        QVERIFY(row("guitarsuite"));
        QVERIFY(!visualItem(window->contentItem(),"removeEffect_guitarsuite")->isVisible()); // fixed on guitars
        addEffectFromPicker("eq");
        QVERIFY(visualItem(window->contentItem(),"addEffectButton")->isEnabled());            // Chaos EQ is still offered
        addEffectFromPicker("chaoseq");
        QVERIFY(visualItem(window->contentItem(),"addEffectButton")->isEnabled());            // the mixing plugins (G7)
        for(const char* mix:{"compressor","deesser","reverb","chaosverb"}) addEffectFromPicker(mix);
        QVERIFY(!visualItem(window->contentItem(),"addEffectButton")->isEnabled());           // nothing left to add
        for(const char* mix:{"compressor","deesser","reverb","chaosverb"}) {                    // back to the guitar's chain
            click(visualItem(window->contentItem(),QString("removeEffect_")+mix)); QTRY_VERIFY(!row(mix));
        }
        click(visualItem(window->contentItem(),"removeEffect_chaoseq"));
        QTRY_VERIFY(!row("chaoseq"));
        click(row("guitarsuite")); QTRY_COMPARE(panel->property("selectedEffect").toString(),QString("guitarsuite"));
        QPointer<QQuickItem> dial=visualItem(window->contentItem(),"pedal.parametriceq.lowfreq"); QVERIFY(dial);
        const int id=guitarFx->data(guitarFx->index(0),GuitarSuiteModel::PedalIdRole).toInt();
        QVERIFY(guitarFx->setParameter(id,"lowfreq",300)); QTRY_COMPARE(dial->property("value").toDouble(),300.0);
        QVERIFY(row("eq")); click(row("eq"));
        QTRY_COMPARE(panel->property("selectedEffect").toString(),QString("eq"));
        QVERIFY(panel->property("visible").toBool()); QVERIFY(tracks.setEqBand(guitar,5,4));
        auto* band=visualItem(window->contentItem(),"trackEqBand5"); QVERIFY(band);
        QTRY_COMPARE(band->property("value").toDouble(),4.0);
        click(row("guitarsuite")); QVERIFY(dial); QTRY_COMPARE(dial->property("value").toDouble(),300.0);
        QCOMPARE(vocalFx->count(),0); QCOMPARE(tracks.project().findTrack(vocal)->eqGainDb[5],0.0f);
        // Drag the EQ's grip above GuitarSuite: the chain order (and the engine's) follows.
        const auto order=[&] { QStringList types; for(const auto& slot:tracks.project().findTrack(guitar)->effects) types<<QString::fromStdString(slot.type); return types; };
        QCOMPARE(order(),(QStringList{"guitarsuite","eq"}));
        auto* grip=visualItem(window->contentItem(),"effectGrip_eq"); QVERIFY(grip);
        const QPoint from=grip->mapToScene(QPointF(grip->width()/2,grip->height()/2)).toPoint();
        QTest::mousePress(fxWindow(),Qt::LeftButton,Qt::NoModifier,from);
        for(int dy=6;dy<=62;dy+=8) QTest::mouseMove(fxWindow(),from-QPoint(0,dy));
        QTest::mouseRelease(fxWindow(),Qt::LeftButton,Qt::NoModifier,from-QPoint(0,62));
        QTRY_COMPARE(order(),(QStringList{"eq","guitarsuite"}));
        // Remove the EQ with its ×; the panel falls back to GuitarSuite.
        click(visualItem(window->contentItem(),"removeEffect_eq"));
        QTRY_COMPARE(order(),QStringList{"guitarsuite"});
        QTRY_COMPARE(panel->property("selectedEffect").toString(),QString("guitarsuite"));
        QVERIFY(visualItem(window->contentItem(),"addEffectButton")->isEnabled());
        // The FX window stays open while editing the transport (the user clicks back into the
        // main window, which makes it the active one again).
        window->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(window));
        click(field); QTest::qWait(10); QTest::keyClick(window,Qt::Key_9); QTest::keyClick(window,Qt::Key_1); QTest::keyClick(window,Qt::Key_Return);
        QCOMPARE(controller->bpm(),91.0); QVERIFY(panel->property("visible").toBool());
        window->grabWindow().save("/tmp/projectdaw-track-effects.png");
        // One shared panel (plan 5.1): removing its track closes it.
        QVERIFY(tracks.removeTrack(guitar));
        QTRY_VERIFY(!panel->property("visible").toBool());
        QCOMPARE(panel->property("trackId").toInt(),-1);
        list->setVisible(false); list->setProperty("trackModel",QVariant());
    }
    void fx_window_exports_a_chain_and_imports_it_onto_another_track_after_asking() {
        // "Export chain..." / "Import chain..." in the FX window's header. The file dialogs are
        // skipped (the panel's exportChain / previewChain take the file); import asks first and
        // names what the track can't take.
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller->attachProject(&tracks);
        const int guitar=tracks.addInstrumentTrackOfType("Electric Guitar"), vocal=tracks.addVocalTrack();
        QVERIFY(tracks.addTrackEffect(guitar,"eq")>=0); QVERIFY(tracks.setEqBand(guitar,4,-3.5));
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true);
        QObject* panel=nullptr;
        const auto restore=qScopeGuard([&] {
            if(panel) QMetaObject::invokeMethod(panel,"close");
            list->setVisible(false); list->setProperty("trackModel",QVariant::fromValue(static_cast<TrackListWrapper*>(nullptr)));
            window->requestActivate(); (void)QTest::qWaitForWindowActive(window);
        });
        QTest::qWait(30);
        const QString file=QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("guitar.chaosfx");
        QDir().mkpath(QFileInfo(file).absolutePath());
        auto* fx=visualItem(list,"trackFxButton"+QString::number(guitar)); QVERIFY(fx); click(fx);
        panel=visualObject(window->contentItem(),"trackEffectsPanel"+QString::number(guitar)); QVERIFY(panel);
        QTRY_VERIFY(panel->property("visible").toBool());
        QVERIFY(visualItem(window->contentItem(),"exportChainButton")); QVERIFY(visualItem(window->contentItem(),"importChainButton"));
        QVERIFY(visualItem(window->contentItem(),"importChainButton")->isEnabled());

        QMetaObject::invokeMethod(panel,"exportChain",Q_ARG(QVariant,QVariant(QUrl::fromLocalFile(file))));
        auto* message=panel->findChild<QObject*>("chainMessage"); QVERIFY(message);
        QTRY_VERIFY(message->property("visible").toBool());
        QCOMPARE(message->property("heading").toString(),QString("FX chain exported"));
        QVERIFY(message->property("detail").toString().contains("GuitarSuite"));
        QVERIFY(QFile::exists(file));
        QMetaObject::invokeMethod(message->findChild<QQuickItem*>("chainMessageOk"),"click");
        QTRY_VERIFY(!message->property("visible").toBool());
        QMetaObject::invokeMethod(panel,"close"); QTRY_VERIFY(!panel->property("visible").toBool());

        // Onto the vocal: the panel asks, saying GuitarSuite is skipped; Replace brings the EQ.
        auto* vfx=visualItem(list,"trackFxButton"+QString::number(vocal)); QVERIFY(vfx); click(vfx);
        panel=visualObject(window->contentItem(),"trackEffectsPanel"+QString::number(vocal)); QVERIFY(panel);
        QTRY_VERIFY(panel->property("visible").toBool());
        QMetaObject::invokeMethod(panel,"previewChain",Q_ARG(QVariant,QVariant(QUrl::fromLocalFile(file))));
        auto* confirm=panel->findChild<QObject*>("chainImportConfirm"); QVERIFY(confirm);
        QTRY_VERIFY(confirm->property("visible").toBool());
        QVERIFY(confirm->findChild<QQuickItem*>("chainImportKept")->property("text").toString().contains("Equalizer"));
        auto* skipped=confirm->findChild<QQuickItem*>("chainImportSkipped"); QVERIFY(skipped);
        QVERIFY(skipped->isVisible()); QVERIFY(skipped->property("text").toString().contains("GuitarSuite"));
        QVERIFY(tracks.project().findTrack(vocal)->effects.empty());           // nothing changed yet
        QMetaObject::invokeMethod(confirm->findChild<QQuickItem*>("chainImportOk"),"click");
        QTRY_VERIFY(!confirm->property("visible").toBool());
        QCOMPARE(int(tracks.project().findTrack(vocal)->effects.size()),1);
        QCOMPARE(QString::fromStdString(tracks.project().findTrack(vocal)->effects[0].type),QString("eq"));
        QCOMPARE(tracks.project().findTrack(vocal)->eqGainDb[4],-3.5f);
        QTRY_COMPARE(panel->property("selectedEffect").toString(),QString("eq"));   // the page follows

        // A file that is not a chain: a message, nothing asked.
        const QString junk=QFileInfo(file).absolutePath()+"/junk.chaosfx";
        { QFile f(junk); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("nope"); }
        QMetaObject::invokeMethod(panel,"previewChain",Q_ARG(QVariant,QVariant(QUrl::fromLocalFile(junk))));
        QTRY_VERIFY(message->property("visible").toBool() || panel->findChild<QObject*>("chainMessage")->property("visible").toBool());
        QVERIFY(!confirm->property("visible").toBool());
        QMetaObject::invokeMethod(panel->findChild<QObject*>("chainMessage")->findChild<QQuickItem*>("chainMessageOk"),"click");
    }
    void vocal_effects_offer_eq_and_recording_controls_without_guitarsuite() {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller->attachProject(&tracks); const int vocal=tracks.addVocalTrack();
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true);
        QTest::qWait(30);
        auto* arm=visualItem(list,"trackRecordButton"+QString::number(vocal));
        auto* input=visualItem(list,"trackInputButton"+QString::number(vocal));
        QVERIFY(arm); QVERIFY(input); QVERIFY(input->isVisible()); QVERIFY(input->isEnabled());
        click(arm); QVERIFY(tracks.project().findTrack(vocal)->recordArmed);
        auto* fx=visualItem(list,"trackFxButton"+QString::number(vocal)); QVERIFY(fx); click(fx);
        auto* panel=visualObject(window->contentItem(),"trackEffectsPanel"+QString::number(vocal)); QVERIFY(panel);
        QTRY_VERIFY(panel->property("visible").toBool());
        QVERIFY(!panel->property("supportsGuitarSuite").toBool());
        // A vocal starts with no effects: both hints show until one is added.
        QCOMPARE(panel->property("selectedEffect").toString(),QString());
        QVERIFY(visualItem(window->contentItem(),"emptyChainHint")->isVisible());
        QVERIFY(visualItem(window->contentItem(),"addEffectHint")->isVisible());
        addEffectFromPicker("eq");
        QTRY_COMPARE(panel->property("selectedEffect").toString(),QString("eq"));
        QVERIFY(!visualItem(window->contentItem(),"emptyChainHint")->isVisible());
        QVERIFY(!visualItem(window->contentItem(),"addEffect_guitarsuite"));   // never offered to a vocal
        QVERIFY(!visualItem(window->contentItem(),"selectEffect_guitarsuite"));
        QVERIFY(!visualObject(window->contentItem(),"trackGuitarSuiteEditor"+QString::number(vocal)));
        auto* band=visualItem(window->contentItem(),"trackEqBand5"); QVERIFY(band); QVERIFY(band->isVisible());
        band->forceActiveFocus(); QTest::keyClick(fxWindow(),Qt::Key_Up);
        QCOMPARE(tracks.project().findTrack(vocal)->eqGainDb[5],0.5f);
        auto* mode=visualItem(window->contentItem(),"trackRecordingMode"); QVERIFY(mode);
        QVERIFY(mode->isVisible()); QVERIFY(mode->isEnabled());
        mode->forceActiveFocus(); QTest::keyClick(fxWindow(),Qt::Key_Down);
        QTRY_VERIFY(tracks.project().findTrack(vocal)->recordDry);
        QTest::keyClick(fxWindow(),Qt::Key_Up); QTRY_VERIFY(!tracks.project().findTrack(vocal)->recordDry);
        // Unsupported navigation cannot reveal the pedal page, including after reopen.
        panel->setProperty("selectedEffect","guitarsuite");
        QCOMPARE(panel->property("selectedEffect").toString(),QString("eq"));
        auto* close=visualItem(window->contentItem(),"closeTrackEffects"); QVERIFY(close); click(close);
        QTRY_VERIFY(!panel->property("visible").toBool()); click(fx); QTRY_VERIFY(panel->property("visible").toBool());
        QCOMPARE(panel->property("selectedEffect").toString(),QString("eq"));
        QCOMPARE(tracks.project().findTrack(vocal)->eqGainDb[5],0.5f);
        QVERIFY(window->grabWindow().save("/tmp/chaosmaker-vocal-eq.png"));
        close=visualItem(window->contentItem(),"closeTrackEffects"); QVERIFY(close); click(close);
        QTRY_VERIFY(!panel->property("visible").toBool()); click(arm);
        QVERIFY(!tracks.project().findTrack(vocal)->recordArmed);
        list->setVisible(false); list->setProperty("trackModel",QVariant());
    }
    void virtual_piano_floats_and_plays_clicked_keys() {
        // One KEYS switch: it turns on the typing keyboard and opens the on-screen piano.
        auto* chip=visualItem(window->contentItem(),"typingKeyboardButton"); QVERIFY(chip);
        QVERIFY(!visualItem(window->contentItem(),"virtualKeyboardButton"));   // no second switch
        auto* piano=window->findChild<QQuickWindow*>("virtualKeyboardWindow"); QVERIFY(piano);
        auto* typing=window->findChild<QObject*>("typingKeyboard"); QVERIFY(typing);
        QVERIFY(!piano->isVisible()); QVERIFY(!typing->property("enabled").toBool());
        const auto restore=qScopeGuard([&] {
            typing->setProperty("enabled",false); piano->setVisible(false);
            window->requestActivate(); QTest::qWaitForWindowActive(window);   // later tests type into the main window
        });
        click(chip);
        QTRY_VERIFY(piano->isVisible());
        QVERIFY(typing->property("enabled").toBool());
        QVERIFY(piano->flags() & Qt::Tool);                         // floats over the main window
        QVERIFY(piano->flags() & Qt::WindowDoesNotAcceptFocus);     // and leaves it the keyboard focus
        auto* bridge=piano->contentItem()->findChild<QObject*>("virtualKeyboardBridge");
        if (!bridge) bridge=piano->findChild<QObject*>("virtualKeyboardBridge");
        QVERIFY(bridge);
        QSignalSpy sent(bridge,SIGNAL(noteSent(int,int,bool)));
        // A full-size keyboard: all 88 keys, A0 .. C8.
        QVERIFY(visualItem(piano->contentItem(),"virtualKey21")); QVERIFY(visualItem(piano->contentItem(),"virtualKey108"));
        QVERIFY(!visualItem(piano->contentItem(),"virtualKey20")); QVERIFY(!visualItem(piano->contentItem(),"virtualKey109"));
        // The band marks the keys KEYS reaches: Tab (G3, 55) .. backslash (F#5, 78).
        auto* band=visualItem(piano->contentItem(),"virtualKeyboardTypingBand"); QVERIFY(band);
        const auto edges=[&](int low,int high) {
            auto* first=visualItem(piano->contentItem(),"virtualKey"+QString::number(low));
            auto* last=visualItem(piano->contentItem(),"virtualKey"+QString::number(high));
            const qreal left=band->mapToScene(QPointF(0,0)).x(), right=band->mapToScene(QPointF(band->width(),0)).x();
            return std::abs(left-first->mapToScene(QPointF(0,0)).x())<1.5
                && std::abs(right-last->mapToScene(QPointF(last->width(),0)).x())<1.5;
        };
        QTRY_VERIFY(edges(55,78));
        // The top line says what E (Sa) plays; the line under the keys lists the KEYS controls.
        auto* range=visualItem(piano->contentItem(),"virtualKeyboardRangeLabel"); QVERIFY(range);
        QVERIFY(range->property("text").toString().contains("E = C4"));
        auto* hint=visualItem(piano->contentItem(),"virtualKeyboardShortcutHint"); QVERIFY(hint);
        QVERIFY(hint->isVisible());
        QCOMPARE(hint->property("text").toString(),QString("Z / X octave down / up  ·  C / V velocity down / up"));

        auto* c4=visualItem(piano->contentItem(),"virtualKey60"); QVERIFY(c4);
        auto* d4=visualItem(piano->contentItem(),"virtualKey62"); QVERIFY(d4);
        const QPoint at=c4->mapToScene(QPointF(c4->width()/2,c4->height()*0.9)).toPoint();   // low on the key: loud
        QTest::mousePress(piano,Qt::LeftButton,Qt::NoModifier,at);
        QTRY_COMPARE(sent.count(),1);
        QCOMPARE(sent[0][0].toInt(),60); QVERIFY(sent[0][1].toInt()>110); QVERIFY(sent[0][2].toBool());
        QTRY_COMPARE(c4->property("color").value<QColor>(),QColor("#60cdff"));          // lit (Theme.accent) while held
        const QPoint next=d4->mapToScene(QPointF(d4->width()/2,d4->height()*0.9)).toPoint();
        QTest::mouseMove(piano,next);                                // a glissando to D4
        QTRY_COMPARE(sent.count(),3);
        QCOMPARE(sent[1][0].toInt(),60); QVERIFY(!sent[1][2].toBool());
        QCOMPARE(sent[2][0].toInt(),62); QVERIFY(sent[2][2].toBool());
        QVERIFY(piano->grabWindow().save("/tmp/chaosmaker-virtual-piano.png"));
        click(chip);                                                 // KEYS off: the piano closes, its note ends
        QTRY_VERIFY(!piano->isVisible());
        QVERIFY(!typing->property("enabled").toBool());
        QTRY_COMPARE(sent.count(),4);
        QCOMPARE(sent[3][0].toInt(),62); QVERIFY(!sent[3][2].toBool());
        QTest::mouseRelease(piano,Qt::LeftButton,Qt::NoModifier,next);

        // KEYS an octave down (Z): the band follows; up again (X).
        click(chip); QTRY_VERIFY(piano->isVisible()); QVERIFY(typing->property("enabled").toBool());
        QTest::keyClick(window,Qt::Key_Z);
        QTRY_COMPARE(typing->property("octave").toInt(),3);
        QTRY_VERIFY(edges(43,66));
        QTRY_VERIFY(range->property("text").toString().contains("E = C3"));
        QTest::keyClick(window,Qt::Key_X);
        QTRY_COMPARE(typing->property("octave").toInt(),4);
        QTRY_VERIFY(edges(55,78));
        QTRY_VERIFY(range->property("text").toString().contains("E = C4"));
        QTest::keyClick(window,Qt::Key_X);
        QTRY_VERIFY(range->property("text").toString().contains("E = C5"));
        QTest::keyClick(window,Qt::Key_Z);
        QTRY_COMPARE(typing->property("octave").toInt(),4);
        piano->close();                                              // closing the piano turns KEYS off
        QTRY_VERIFY(!typing->property("enabled").toBool());
        QCOMPARE(chip->property("text").toString(),QString("Off"));
    }
    void virtual_piano_follows_the_selected_track_scale_changer() {
        TrackListWrapper tracks;
        const int harmonium=tracks.addInstrumentTrackOfType("Harmonium");
        const int drums=tracks.addInstrumentTrackOfType("Drums");
        auto* bar=window->findChild<QObject*>("transport"); QVERIFY(bar);
        auto* chip=visualItem(window->contentItem(),"typingKeyboardButton"); QVERIFY(chip);
        auto* piano=window->findChild<QQuickWindow*>("virtualKeyboardWindow"); QVERIFY(piano);
        auto* typing=window->findChild<QObject*>("typingKeyboard"); QVERIFY(typing);
        const auto restore=qScopeGuard([&] {
            typing->setProperty("enabled",false); piano->setVisible(false);
            bar->setProperty("trackModel",QVariant());
            window->requestActivate(); (void)QTest::qWaitForWindowActive(window);
        });
        bar->setProperty("trackModel",QVariant::fromValue(&tracks));
        QVERIFY(tracks.selectTrack(harmonium));
        auto settings=*tracks.harmonium(harmonium); settings.transpose=2;   // C plays D
        QVERIFY(tracks.setHarmonium(harmonium,settings));
        click(chip); QTRY_VERIFY(piano->isVisible()); QVERIFY(typing->property("enabled").toBool());
        QCOMPARE(typing->property("octave").toInt(),4);

        auto* bridge=piano->contentItem()->findChild<QObject*>("virtualKeyboardBridge");
        if (!bridge) bridge=piano->findChild<QObject*>("virtualKeyboardBridge");
        QVERIFY(bridge);
        auto* band=visualItem(piano->contentItem(),"virtualKeyboardTypingBand"); QVERIFY(band);
        auto* range=visualItem(piano->contentItem(),"virtualKeyboardRangeLabel"); QVERIFY(range);
        const auto key=[&](int note) { return visualItem(piano->contentItem(),"virtualKey"+QString::number(note)); };
        const auto edges=[&](int low,int high) {
            const qreal left=band->mapToScene(QPointF(0,0)).x(), right=band->mapToScene(QPointF(band->width(),0)).x();
            return std::abs(left-key(low)->mapToScene(QPointF(0,0)).x())<1.5
                && std::abs(right-key(high)->mapToScene(QPointF(key(high)->width(),0)).x())<1.5;
        };
        const QColor accent("#60cdff");
        // The band and the top line show what sounds: Tab .. backslash play A3 .. G#5, E plays D4.
        QTRY_VERIFY(edges(57,80));
        QVERIFY(range->property("text").toString().contains("A3 – G#5"));
        QVERIFY(range->property("text").toString().contains("E = D4"));

        // Typing E sends C4 (the harmonium makes it D4): D4 lights, C4 does not.
        QTest::keyPress(window,Qt::Key_E);
        QTRY_COMPARE(key(62)->property("color").value<QColor>(),accent);
        QVERIFY(key(60)->property("color").value<QColor>()!=accent);
        QTest::keyRelease(window,Qt::Key_E);
        QTRY_VERIFY(key(62)->property("color").value<QColor>()!=accent);

        // Clicking D4 sends C4, so the harmonium sounds the key clicked; D4 lights.
        QSignalSpy sent(bridge,SIGNAL(noteSent(int,int,bool)));
        const QPoint at=key(62)->mapToScene(QPointF(key(62)->width()/2,key(62)->height()*0.9)).toPoint();
        QTest::mousePress(piano,Qt::LeftButton,Qt::NoModifier,at);
        QTRY_COMPARE(sent.count(),1);
        QCOMPARE(sent[0][0].toInt(),60); QVERIFY(sent[0][2].toBool());
        QTRY_COMPARE(key(62)->property("color").value<QColor>(),accent);
        // The scale changes while the key is held: the note sent still ends (no stuck note).
        settings.transpose=3; QVERIFY(tracks.setHarmonium(harmonium,settings));
        QTRY_VERIFY(range->property("text").toString().contains("E = D#4"));
        QTRY_VERIFY(edges(58,81));
        QTest::mouseRelease(piano,Qt::LeftButton,Qt::NoModifier,at);
        QTRY_COMPARE(sent.count(),2);
        QCOMPARE(sent[1][0].toInt(),60); QVERIFY(!sent[1][2].toBool());

        // A track with no scale changer: everything back where it was.
        QVERIFY(tracks.selectTrack(drums));
        QTRY_VERIFY(edges(55,78));
        QVERIFY(range->property("text").toString().contains("E = C4"));
        QTest::mousePress(piano,Qt::LeftButton,Qt::NoModifier,at);
        QTRY_COMPARE(sent.count(),3);
        QCOMPARE(sent[2][0].toInt(),62);
        QTest::mouseRelease(piano,Qt::LeftButton,Qt::NoModifier,at);
        QTRY_COMPARE(sent.count(),4);
    }
    void floating_windows_show_the_themed_tooltip_colours() {
        // The FX panel and the on-screen piano are windows of their own; they do not inherit the
        // main window's palette, so each sets the Theme's tooltip colours (not black on yellow).
        const auto tooltipColours=[](QQuickWindow* w) {
            auto* palette=w->property("palette").value<QObject*>();
            return palette ? std::pair(palette->property("toolTipBase").value<QColor>(),palette->property("toolTipText").value<QColor>())
                           : std::pair(QColor(),QColor());
        };
        const auto themed=std::pair(QColor("#2b2d32"),QColor("#f3f4f6"));   // Theme.tooltipBg / tooltipText

        auto* piano=window->findChild<QQuickWindow*>("virtualKeyboardWindow"); QVERIFY(piano);
        QCOMPARE(tooltipColours(piano),themed);

        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller->attachProject(&tracks);
        const int d=tracks.addInstrumentTrackOfType("Drums");
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true);
        const auto restore=qScopeGuard([&] {
            list->setVisible(false); list->setProperty("trackModel",QVariant::fromValue(static_cast<TrackListWrapper*>(nullptr)));
        });
        QTest::qWait(30);
        auto* fx=visualItem(list,"trackFxButton"+QString::number(d)); QVERIFY(fx); click(fx);
        auto* panel=visualObject(window->contentItem(),"trackEffectsPanel"+QString::number(d)); QVERIFY(panel);
        QTRY_VERIFY(panel->property("visible").toBool());
        auto* fxWin=qobject_cast<QQuickWindow*>(fxWindow()); QVERIFY(fxWin); QVERIFY(fxWin!=window);
        QCOMPARE(tooltipColours(fxWin),themed);
        // Close it with its × (as the user does) and hand the keyboard back to the main window.
        auto* close=visualItem(window->contentItem(),"closeTrackEffects"); QVERIFY(close); click(close);
        QTRY_VERIFY(!panel->property("visible").toBool());
        window->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(window));
    }
    void close_buttons_share_one_look_soft_round_on_hover_red_when_removing() {
        // Every × is the shared CloseButton (frontend/common): no other file draws Icons.close.
        const QString root=QStringLiteral(CHAOSMAKER_SOURCE_DIR);
        QStringList offenders;
        QDirIterator it(root+"/frontend",{"*.qml"},QDir::Files,QDirIterator::Subdirectories);
        QStringList files{root+"/Main.qml"}; while(it.hasNext()) files<<it.next();
        for(const QString& path:files) {
            const QString rel=path.mid(root.size()+1);
            if(rel=="frontend/common/CloseButton.qml"||rel=="frontend/icons/Icons.qml") continue;
            QFile file(path); QVERIFY2(file.open(QIODevice::ReadOnly),qPrintable(rel));
            if(file.readAll().contains("Icons.close")) offenders<<rel;
        }
        QVERIFY2(offenders.isEmpty(),qPrintable("× drawn outside CloseButton: "+offenders.join(", ")));

        auto* theme=engine->singletonInstance<QObject*>("TransportTests","Theme"); QVERIFY(theme);
        const auto colour=[&](const char* name) { return theme->property(name).value<QColor>(); };
        const auto backdrop=[](QQuickItem* b) { return b->property("backdrop").value<QColor>(); };
        const auto glyph=[](QQuickItem* b) { return qvariant_cast<QQuickItem*>(b->property("contentItem"))->property("color").value<QColor>(); };
        const auto hover=[](QWindow* w,QQuickItem* b) { QTest::mouseMove(w,b->mapToScene(QPointF(b->width()/2,b->height()/2)).toPoint()); };

        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller->attachProject(&tracks);
        const int vocal=tracks.addVocalTrack();
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        const auto restore=qScopeGuard([&] {
            list->setVisible(false); list->setProperty("trackModel",QVariant::fromValue(static_cast<TrackListWrapper*>(nullptr)));
        });
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true); QTest::qWait(30);

        // Remove track: a muted × at rest, a red round backdrop and a white × under the mouse.
        auto* remove=visualItem(list,"trackRemoveButton"+QString::number(vocal)); QVERIFY(remove);
        QTest::mouseMove(window,QPoint(1,1)); QTRY_COMPARE(backdrop(remove),QColor(Qt::transparent));
        QCOMPARE(glyph(remove),colour("textMuted"));
        hover(window,remove);
        QTRY_COMPARE(backdrop(remove),colour("recordBg"));
        QCOMPARE(glyph(remove),colour("textStrong"));

        // Close effects: a soft (not white-washed) round backdrop under the mouse.
        auto* fx=visualItem(list,"trackFxButton"+QString::number(vocal)); QVERIFY(fx); click(fx);
        auto* panel=visualObject(window->contentItem(),"trackEffectsPanel"+QString::number(vocal)); QVERIFY(panel);
        QTRY_VERIFY(panel->property("visible").toBool());
        auto* close=visualItem(window->contentItem(),"closeTrackEffects"); QVERIFY(close);
        QCOMPARE(backdrop(close),QColor(Qt::transparent));
        fxWindow()->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(fxWindow()));
        QTest::mouseMove(fxWindow(),QPoint(2,2)); hover(fxWindow(),close);
        QTRY_COMPARE(backdrop(close),colour("hoverOverlay"));
        QCOMPARE(glyph(close),colour("textStrong"));
        QVERIFY(backdrop(close).alpha()<64);                        // a light veil, never a white block
        click(close); QTRY_VERIFY(!panel->property("visible").toBool());
        window->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(window));
    }
    void sliders_are_rounded_with_a_round_handle_and_a_pill_on_faders() {
        // ThemedSlider: a rounded track filled up to the value, a round handle that grows under
        // the mouse (10 -> 14 px) and a pill across vertical faders. The handle's layout box
        // stays 13 px, so layouts keep their size. A track's own sliders (VOL, CLIP) are drawn
        // in the track's colour, a little brighter under the mouse.
        auto* theme=engine->singletonInstance<QObject*>("TransportTests","Theme"); QVERIFY(theme);
        const auto colour=[&](const char* name) { return theme->property(name).value<QColor>(); };
        const auto part=[](QQuickItem* slider,const char* name) {
            for(auto* child:slider->findChildren<QQuickItem*>()) if(child->objectName()==name) return child;
            return static_cast<QQuickItem*>(nullptr);
        };
        TrackListWrapper tracks; const int vocal=tracks.addVocalTrack();
        QVERIFY(tracks.setTrackColour(vocal,"#3fb950"));
        const QColor green("#3fb950");
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        auto* mixer=window->findChild<QQuickItem*>("testMixer"); QVERIFY(mixer);
        const auto restore=qScopeGuard([&] {
            list->setVisible(false); list->setProperty("trackModel",QVariant());
            mixer->setVisible(false); mixer->setProperty("trackModel",QVariant());
        });
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true); QTest::qWait(30);

        auto* volume=visualItem(list,"trackVolume"+QString::number(vocal)); QVERIFY(volume);
        auto* knob=part(volume,"sliderKnob"); auto* fill=part(volume,"sliderFill"); QVERIFY(knob); QVERIFY(fill);
        QCOMPARE(qvariant_cast<QQuickItem*>(volume->property("handle"))->implicitHeight(),13.0);
        QTest::mouseMove(window,QPoint(1,1));
        QTRY_COMPARE(knob->width(),10.0); QCOMPARE(knob->height(),10.0);
        QCOMPARE(knob->property("radius").toDouble(),5.0);                    // a circle
        QTRY_COMPARE(fill->property("color").value<QColor>(),green);           // the track's colour
        QCOMPARE(QQmlProperty::read(knob,"border.color").value<QColor>(),green); // the handle's ring too
        QVERIFY(fill->property("radius").toDouble()>0);                        // a rounded track
        QTest::mouseMove(window,volume->mapToScene(QPointF(volume->width()/2,volume->height()/2)).toPoint());
        QTRY_COMPARE(knob->width(),14.0);
        QTRY_COMPARE(fill->property("color").value<QColor>(),green.lighter(130));
        QTest::mouseMove(window,QPoint(1,1));
        QTRY_COMPARE(knob->width(),10.0);
        QVERIFY(tracks.setTrackColour(vocal,"#e5484d"));                       // a new colour follows
        QTRY_COMPARE(fill->property("color").value<QColor>(),QColor("#e5484d"));

        // A vertical fader (a track's CLIP): a pill lying across the track, filled up from the
        // bottom in the track colour; greyed while the track has no clip.
        mixer->setProperty("trackModel",QVariant::fromValue(&tracks)); mixer->setVisible(true); QTest::qWait(30);
        auto* clipFader=visualItem(mixer,"mixerClipGain"+QString::number(vocal)); QVERIFY(clipFader);
        QCOMPARE(clipFader->property("tint").value<QColor>(),QColor("#e5484d"));
        QCOMPARE(part(clipFader,"sliderFill")->property("color").value<QColor>(),colour("textMuted"));
        QVERIFY(tracks.addClipAtBeat(vocal,0,4)>0);
        QTRY_VERIFY(clipFader->isEnabled());
        auto* pill=part(clipFader,"sliderKnob"); auto* rise=part(clipFader,"sliderFill"); QVERIFY(pill); QVERIFY(rise);
        QTRY_VERIFY(pill->width()>pill->height());
        QCOMPARE(pill->property("radius").toDouble(),pill->height()/2);
        QTRY_COMPARE(rise->property("color").value<QColor>(),QColor("#e5484d"));
        auto* track=rise->parentItem();
        QCOMPARE(rise->y()+rise->height(),track->height());                    // reaches the bottom
    }
    void desk_faders_have_a_slot_ticks_and_a_metal_cap() {
        // HardwareSlider: the FX pages, the master fader and the metronome volume look like a
        // mixing desk: a dark slot, a metal cap across it (upright on a horizontal slider, flat
        // on a vertical fader) with an indicator line that turns accent while dragged. The cap's
        // layout box stays 13 px. A track's own sliders stay ThemedSlider in the track colour.
        const QString root=QStringLiteral(CHAOSMAKER_SOURCE_DIR);
        QStringList offenders;
        QDirIterator it(root+"/frontend",{"*.qml"},QDir::Files,QDirIterator::Subdirectories);
        while(it.hasNext()) {
            const QString path=it.next(), rel=path.mid(root.size()+1);
            if(!rel.startsWith("frontend/plugins/")&&!rel.startsWith("frontend/instruments/")&&rel!="frontend/settings/QuickSettings.qml") continue;
            QFile file(path); QVERIFY2(file.open(QIODevice::ReadOnly),qPrintable(rel));
            if(file.readAll().contains("ThemedSlider")) offenders<<rel;
        }
        QVERIFY2(offenders.isEmpty(),qPrintable("FX sliders must be HardwareSlider: "+offenders.join(", ")));

        auto* theme=engine->singletonInstance<QObject*>("TransportTests","Theme"); QVERIFY(theme);
        const auto colour=[&](const char* name) { return theme->property(name).value<QColor>(); };
        const auto part=[](QQuickItem* slider,const char* name) {
            for(auto* child:slider->findChildren<QQuickItem*>()) if(child->objectName()==name) return child;
            return static_cast<QQuickItem*>(nullptr);
        };
        TrackListWrapper tracks; tracks.addVocalTrack();
        auto* mixer=window->findChild<QQuickItem*>("testMixer"); QVERIFY(mixer);
        const auto restore=qScopeGuard([&] { mixer->setVisible(false); mixer->setProperty("trackModel",QVariant()); });
        mixer->setProperty("trackModel",QVariant::fromValue(&tracks)); mixer->setVisible(true); QTest::qWait(30);
        auto* master=visualItem(mixer,"mixerMasterFader"); QVERIFY(master);
        QVERIFY(master->inherits("QQuickSlider"));
        QCOMPARE(qvariant_cast<QQuickItem*>(master->property("handle"))->implicitHeight(),13.0);
        auto* cap=part(master,"faderCap"); auto* line=part(master,"faderLine"); QVERIFY(cap); QVERIFY(line); QVERIFY(part(master,"faderSlot"));
        QVERIFY(cap->width()>cap->height());                                  // flat across the fader
        QTest::mouseMove(window,QPoint(1,1));
        QTRY_COMPARE(line->property("color").value<QColor>(),colour("faderCapLine"));
        const QPoint at=cap->mapToScene(QPointF(cap->width()/2,cap->height()/2)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,at);
        QTRY_COMPARE(line->property("color").value<QColor>(),colour("accent"));  // lit while dragged
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,at);
        QTRY_COMPARE(line->property("color").value<QColor>(),colour("faderCapLine"));

        // The metronome volume (horizontal): the cap stands upright.
        auto* button=visualItem(window->contentItem(),"quickSettingsButton"); QVERIFY(button);
        auto* popup=window->findChild<QObject*>("quickSettingsPopup"); QVERIFY(popup);
        click(button); QTRY_VERIFY(popup->property("visible").toBool());
        auto* volume=visualItem(window->contentItem(),"metronomeVolume"); QVERIFY(volume);
        auto* upright=part(volume,"faderCap"); QVERIFY(upright);
        QVERIFY(upright->height()>upright->width());
        click(visualItem(window->contentItem(),"closeQuickSettings"));
        QTRY_VERIFY(!popup->property("visible").toBool());
    }
    void drum_kit_lives_in_fx_and_edits_its_settings() {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller->attachProject(&tracks);
        const int d=tracks.addInstrumentTrackOfType("Drums");
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true);
        const auto restore=qScopeGuard([&] {
            list->setVisible(false); list->setProperty("trackModel",QVariant::fromValue(static_cast<TrackListWrapper*>(nullptr)));
        });
        QTest::qWait(30);
        const auto settings=[&]{ return tracks.project().findTrack(d)->drumkit; };
        auto* fx=visualItem(list,"trackFxButton"+QString::number(d)); QVERIFY(fx); click(fx);
        auto* panel=visualObject(window->contentItem(),"trackEffectsPanel"+QString::number(d)); QVERIFY(panel);
        QTRY_VERIFY(panel->property("visible").toBool());
        QCOMPARE(panel->property("selectedEffect").toString(),QString("drumkit"));   // opens on its page
        QVERIFY(panel->property("supportsDrumKit").toBool());
        QVERIFY(!visualItem(window->contentItem(),"removeEffect_drumkit")->isVisible());   // fixed

        auto* kick=visualItem(window->contentItem(),"drumKitKick"); QVERIFY(kick); QTRY_VERIFY(kick->isVisible());
        QQuickItem* slider=nullptr;
        for (auto* child : kick->findChildren<QQuickItem*>()) if (child->inherits("QQuickSlider")) slider=child;
        QVERIFY(slider);
        slider->forceActiveFocus(); QTest::keyClick(fxWindow(),Qt::Key_Left);
        QTRY_VERIFY(settings().kickDb<0.0);

        auto* room=visualItem(window->contentItem(),"drumKitRoom"); QVERIFY(room);
        room->forceActiveFocus(); QTest::keyClick(fxWindow(),Qt::Key_Down);
        QTRY_VERIFY(settings().room==drumkit::Room::Full);

        // The drawn kit: a press on the snare's head plays Snare (harder in the middle), on
        // its rim Snare edge.
        auto* picture=visualItem(window->contentItem(),"drumKitPicture"); QVERIFY(picture); QVERIFY(picture->height()>100);
        auto* bridge=visualObject(window->contentItem(),"drumKitBridge"); QVERIFY(bridge);
        QSignalSpy played(bridge,SIGNAL(played(int,int)));
        auto* snare=visualItem(picture,"drumPiece_snare"); QVERIFY(snare);
        const qreal scale=picture->property("s").toReal(), rx=snare->property("rx").toReal()*scale, ry=snare->property("ry").toReal()*scale;
        const auto pressAt=[&](QPointF local) {
            const QPoint at=snare->mapToScene(local).toPoint();
            QTest::mousePress(fxWindow(),Qt::LeftButton,Qt::NoModifier,at); QTest::mouseRelease(fxWindow(),Qt::LeftButton,Qt::NoModifier,at);
        };
        pressAt(QPointF(rx,ry));                                    // the centre
        QTRY_COMPARE(played.count(),1);
        QCOMPARE(played[0][0].toInt(),62); QVERIFY(played[0][1].toInt()>=120);   // the middle hits hard
        pressAt(QPointF(rx+rx*0.9,ry));                             // the rim
        QTRY_COMPARE(played.count(),2);
        QCOMPARE(played[1][0].toInt(),64);
        auto* pedal=visualItem(picture,"drumPiece_hihatPedal"); QVERIFY(pedal); click(pedal);
        QTRY_COMPARE(played.count(),3);
        QCOMPARE(played[2][0].toInt(),68);

        auto* status=visualItem(window->contentItem(),"drumKitStatus"); QVERIFY(status);
        QTRY_VERIFY(status->isVisible());                          // the tests have no kit: it says where it looked
        QVERIFY(status->property("text").toString().contains("not found"));
        QVERIFY(window->grabWindow().save("/tmp/chaosmaker-drumkit-page.png"));

        auto* reset=visualItem(window->contentItem(),"drumKitReset"); QVERIFY(reset);
        click(reset); QTRY_VERIFY(settings()==drumkit::Settings{});
        auto* close=visualItem(window->contentItem(),"closeTrackEffects"); QVERIFY(close); click(close);
        QTRY_VERIFY(!panel->property("visible").toBool());
    }
    void drum_pages_offer_grooves_that_go_on_the_track_and_save_from_a_clip() {
        // Both drum pages have the Grooves section: factory grooves grouped by time signature
        // (the project's first), "Add to track" puts one on the track as a clip, the selected
        // clip can be saved as a groove of your own, which can be renamed and deleted. The
        // buttons are pressed directly: the section may be scrolled out of view.
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        controller->setMeter(4,4);
        TrackListWrapper tracks; controller->attachProject(&tracks);
        const int e=tracks.addInstrumentTrackOfType("Electronic Drums");
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true);
        QObject* panel=nullptr;
        const auto restore=qScopeGuard([&] {
            if(panel) QMetaObject::invokeMethod(panel,"close");
            list->setVisible(false); list->setProperty("trackModel",QVariant::fromValue(static_cast<TrackListWrapper*>(nullptr)));
            window->requestActivate(); (void)QTest::qWaitForWindowActive(window);
        });
        QTest::qWait(30);
        auto* fx=visualItem(list,"trackFxButton"+QString::number(e)); QVERIFY(fx); click(fx);
        panel=visualObject(window->contentItem(),"trackEffectsPanel"+QString::number(e)); QVERIFY(panel);
        QTRY_VERIFY(panel->property("visible").toBool());
        QTRY_VERIFY(visualItem(window->contentItem(),"electroDrumsGrooves"));
        auto* bridge=visualObject(window->contentItem(),"drumGrooveBridge"); QVERIFY(bridge);
        const auto press=[&](const char* name) {
            auto* button=visualItem(window->contentItem(),name); QVERIFY2(button,name);
            QVERIFY2(button->isEnabled(),name);
            QMetaObject::invokeMethod(button,"click");
        };
        const auto grooveNames=[&] {
            QStringList out; for(const auto& g:bridge->property("grooves").toList()) out<<g.toMap().value("name").toString(); return out;
        };

        // Six factory grooves, the project's 4/4 first; a 4/4 one is picked.
        QCOMPARE(grooveNames().size(),6);
        QVERIFY(visualItem(window->contentItem(),"groove_Rock A")); QVERIFY(visualItem(window->contentItem(),"groove_Waltz A"));
        QVERIFY(visualItem(window->contentItem(),"groove_Groove B (3+2+2)"));
        QTRY_VERIFY(visualItem(window->contentItem(),"groove_Rock A")->property("active").toBool());
        QVERIFY(!visualItem(window->contentItem(),"grooveMeterNote")->isVisible());

        // Add four bars of Rock A at the start.
        press("grooveAdd");
        QTRY_COMPARE(int(tracks.domain().clips().size()),1);
        const auto clip=tracks.domain().clips().front();
        QCOMPARE(clip.trackId,e); QCOMPARE(clip.startBeat,0.0); QCOMPARE(clip.lengthBeats,16.0);
        QVERIFY(!clip.notes.empty());

        // A 7/8 groove in a 4/4 project says its bars won't line up.
        QMetaObject::invokeMethod(visualItem(window->contentItem(),"groove_Groove A (2+2+3)"),"click");
        QTRY_VERIFY(visualItem(window->contentItem(),"grooveMeterNote")->isVisible());
        QVERIFY(!visualItem(window->contentItem(),"grooveRename")->isVisible());   // factory: no rename/delete

        // Save the clip as a groove of my own.
        QVERIFY(!visualItem(window->contentItem(),"grooveSaveClip")->isEnabled());
        tracks.selectClip(e,clip.clipId);
        QTRY_VERIFY(visualItem(window->contentItem(),"grooveSaveClip")->isEnabled());
        press("grooveSaveClip");
        auto* dialog=visualObject(window->contentItem(),"grooveNameDialog"); if(!dialog) dialog=fxWindow()->findChild<QObject*>("grooveNameDialog");
        QVERIFY(dialog); QTRY_VERIFY(dialog->property("visible").toBool());
        auto* field=dialog->findChild<QQuickItem*>("grooveNameField"); QVERIFY(field);
        field->setProperty("text","Backbeat");
        QMetaObject::invokeMethod(dialog->findChild<QQuickItem*>("grooveNameOk"),"click");
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QTRY_VERIFY(grooveNames().contains("Backbeat"));
        QTRY_VERIFY(visualItem(window->contentItem(),"groove_Backbeat"));
        QVERIFY(visualItem(window->contentItem(),"groove_Backbeat")->property("active").toBool());   // the new one is picked
        QTRY_VERIFY(visualItem(window->contentItem(),"grooveRename")->isVisible());

        // Saving again under the same name asks once more, then replaces.
        press("grooveSaveClip");
        QTRY_VERIFY(dialog->property("visible").toBool());
        field->setProperty("text","backbeat");
        QMetaObject::invokeMethod(dialog->findChild<QQuickItem*>("grooveNameOk"),"click");
        QTRY_VERIFY(dialog->findChild<QQuickItem*>("grooveReplaceNote")->isVisible());
        QVERIFY(dialog->property("visible").toBool());
        QMetaObject::invokeMethod(dialog->findChild<QQuickItem*>("grooveNameOk"),"click");
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(grooveNames().size(),7);

        // Rename, then delete it.
        press("grooveRename");
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(field->property("text").toString(),QString("backbeat"));
        field->setProperty("text","Two and four");
        QMetaObject::invokeMethod(dialog->findChild<QQuickItem*>("grooveNameOk"),"click");
        QTRY_VERIFY(grooveNames().contains("Two and four"));
        QVERIFY(!grooveNames().contains("backbeat"));
        press("grooveDelete");
        auto* confirm=visualObject(window->contentItem(),"grooveDeleteDialog"); if(!confirm) confirm=fxWindow()->findChild<QObject*>("grooveDeleteDialog");
        QVERIFY(confirm); QTRY_VERIFY(confirm->property("visible").toBool());
        QMetaObject::invokeMethod(confirm->findChild<QQuickItem*>("grooveDeleteOk"),"click");
        QTRY_COMPARE(grooveNames().size(),6);
        QCOMPARE(int(tracks.domain().clips().size()),1);                       // the clip stays

        // The acoustic kit's page has the same section.
        const int d=tracks.addInstrumentTrackOfType("Drums");
        QTest::qWait(30);
        QMetaObject::invokeMethod(panel,"close");
        auto* fx2=visualItem(list,"trackFxButton"+QString::number(d)); QVERIFY(fx2); click(fx2);
        auto* panel2=visualObject(window->contentItem(),"trackEffectsPanel"+QString::number(d)); QVERIFY(panel2);
        QTRY_VERIFY(panel2->property("visible").toBool());
        QTRY_VERIFY(visualItem(window->contentItem(),"drumKitGrooves"));
        panel=panel2;
    }
    void chords_track_is_a_kind_of_its_own_and_its_page_has_eight_strips() {
        // Chords sits beside Vocal and Instrument in the picker; its FX window opens on the
        // strips page (autoplay, key, progressions, eight strips), the grand piano's page after.
        // Controls are pressed directly: the window's size varies on this machine (U4).
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller->attachProject(&tracks);
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        QObject* panel=nullptr;
        const auto restore=qScopeGuard([&] {
            if(panel) QMetaObject::invokeMethod(panel,"close");
            list->setVisible(false); list->setProperty("trackModel",QVariant::fromValue(static_cast<TrackListWrapper*>(nullptr)));
            window->requestActivate(); (void)QTest::qWaitForWindowActive(window);
        });
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true); QTest::qWait(30);
        auto* add=visualItem(list,"addTrackButton"); QVERIFY(add); click(add); QTest::qWait(20);
        QVERIFY(visualItem(window->contentItem(),"trackChoice_Vocal")); QVERIFY(visualItem(window->contentItem(),"trackChoice_Instrument"));
        auto* chordsChoice=visualItem(window->contentItem(),"trackChoice_Chords"); QVERIFY(chordsChoice); click(chordsChoice);
        QTRY_COMPARE(tracks.count(),1);
        const int id=tracks.project().tracks()[0]->id;
        QCOMPARE(tracks.project().tracks()[0]->type,std::string("Chords"));

        QTest::qWait(30);
        auto* fx=visualItem(list,"trackFxButton"+QString::number(id)); QVERIFY(fx); click(fx);
        panel=visualObject(window->contentItem(),"trackEffectsPanel"+QString::number(id)); QVERIFY(panel);
        QTRY_VERIFY(panel->property("visible").toBool());
        QCOMPARE(panel->property("selectedEffect").toString(),QString("chords"));     // opens on its strips
        QVERIFY(panel->property("supportsChords").toBool()); QVERIFY(panel->property("supportsPiano").toBool());
        QTRY_VERIFY(visualItem(window->contentItem(),"chordStrips"));
        for(int i=0;i<8;++i) {
            QVERIFY(visualItem(window->contentItem(),"chordStripName"+QString::number(i)));
            QVERIFY(visualItem(window->contentItem(),"chordStripChord"+QString::number(i)));
            QVERIFY(visualItem(window->contentItem(),"chordStripBass"+QString::number(i)));
            QVERIFY(visualItem(window->contentItem(),"chordStripHigh"+QString::number(i)));
            QVERIFY(visualItem(window->contentItem(),"chordStripLow"+QString::number(i)));
            QVERIFY(visualItem(window->contentItem(),"chordStripCaret"+QString::number(i)));
        }
        auto* bridge=visualObject(window->contentItem(),"chordsBridge"); QVERIFY(bridge);
        const auto press=[&](const QString& name) {
            auto* item=visualItem(window->contentItem(),name); QVERIFY2(item,qPrintable(name));
            QMetaObject::invokeMethod(item,"click");
        };
        // Volume and bass with chords: same volume and bass on by default; a click saves them.
        QVERIFY(visualItem(window->contentItem(),"chordsDynamicsEven")->property("active").toBool());
        QVERIFY(visualItem(window->contentItem(),"chordsBassWithChords")->property("active").toBool());
        press("chordsDynamicsTop");
        QTRY_COMPARE(tracks.chords(id)->dynamics,1);
        QVERIFY(visualItem(window->contentItem(),"chordsDynamicsTop")->property("active").toBool());
        press("chordsBassWithChords");
        QTRY_VERIFY(!tracks.chords(id)->bassWithChords);
        press("chordsBassWithChords");
        QTRY_VERIFY(tracks.chords(id)->bassWithChords);
        press("chordsDynamicsMeter");
        QTRY_COMPARE(tracks.chords(id)->dynamics,2);
        QVERIFY(visualItem(window->contentItem(),"chordsDynamicsMeter")->property("active").toBool());
        QVERIFY(!visualItem(window->contentItem(),"chordsDynamicsTop")->property("active").toBool());
        press("chordsDynamicsEven");
        QTRY_COMPARE(tracks.chords(id)->dynamics,0);
        // Autoplay: a menu. Click it: Off, then Flowing, Pop, Comping, Ballad; hover a style for
        // its four variations (no numbers anywhere); the button shows the one playing.
        QVERIFY(!visualItem(window->contentItem(),"chordsAutoplay3"));
        auto* autoplayButton=visualItem(window->contentItem(),"chordsAutoplay"); QVERIFY(autoplayButton);
        QCOMPARE(autoplayButton->property("displayText").toString(),QString::fromUtf8("Comping \u00b7 Pushes"));
        auto* autoplayMenu=autoplayButton->findChild<QObject*>("chordsAutoplayMenu"); QVERIFY(autoplayMenu);
        QVERIFY(!autoplayMenu->property("visible").toBool());
        QMetaObject::invokeMethod(autoplayButton,"click");
        QTRY_VERIFY(autoplayMenu->property("visible").toBool());
        QCOMPARE(autoplayMenu->property("count").toInt(),6);                         // Off, a line, four styles
        QStringList styles;
        for(int i=2;i<6;++i) {
            QObject* entry=nullptr;
            QMetaObject::invokeMethod(autoplayMenu,"itemAt",Q_RETURN_ARG(QQuickItem*,reinterpret_cast<QQuickItem*&>(entry)),Q_ARG(int,i));
            QVERIFY(entry); styles<<entry->property("text").toString();
            QCOMPARE(entry->property("checked").toBool(),i==4);                       // Comping is playing
        }
        QCOMPARE(styles,QStringList({"Flowing","Pop","Comping","Ballad"}));
        auto* flowing=autoplayButton->findChild<QObject*>("chordsAutoplayStyle1"); QVERIFY(flowing);
        QCOMPARE(flowing->property("count").toInt(),4);
        QStringList names;
        for(int i=0;i<4;++i) {
            QObject* entry=nullptr;
            QMetaObject::invokeMethod(flowing,"itemAt",Q_RETURN_ARG(QQuickItem*,reinterpret_cast<QQuickItem*&>(entry)),Q_ARG(int,i));
            QVERIFY(entry); names<<entry->property("text").toString();
        }
        QCOMPARE(names,QStringList({"Rolling","Alberti","Open","Cascade"}));
        QVERIFY(autoplayButton->findChild<QObject*>("chordsAutoplayChoice9")->property("checked").toBool());   // Comping · Pushes ticked
        QMetaObject::invokeMethod(flowing,"open");
        QTRY_VERIFY(flowing->property("visible").toBool());
        auto* open=autoplayButton->findChild<QObject*>("chordsAutoplayChoice3"); QVERIFY(open);
        QMetaObject::invokeMethod(open,"triggered");
        QTRY_COMPARE(tracks.chords(id)->autoplay,1);
        QCOMPARE(tracks.chords(id)->variation,2);
        QTRY_VERIFY(!autoplayMenu->property("visible").toBool());
        QCOMPARE(autoplayButton->property("displayText").toString(),QString::fromUtf8("Flowing \u00b7 Open"));
        QVERIFY(open->property("checked").toBool());
        for(const auto& entry:bridge->property("autoplayModes").toList())
            QVERIFY(!entry.toMap().value("title").toString().contains(QRegularExpression("[0-9]")));

        // A strip's chord: click its name, pick the key's V7, OK.
        QVERIFY(visualItem(window->contentItem(),"chordStripName2"));
        auto* dialog=visualObject(window->contentItem(),"chordEditDialog"); if(!dialog) dialog=fxWindow()->findChild<QObject*>("chordEditDialog");
        QVERIFY(dialog);
        QMetaObject::invokeMethod(visualItem(window->contentItem(),"chordStripCaret2"),"click");   // the caret opens it
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto* content=dialog->property("contentItem").value<QQuickItem*>(); QVERIFY(content);
        const auto inDialog=[&](const char* n) { return visualItem(content,n); };
        auto* field=inDialog("chordEditField"); QVERIFY(field);
        QCOMPARE(field->property("text").toString(),QString("Em"));
        QMetaObject::invokeMethod(inDialog("chordEditDegree5"),"click");
        QCOMPARE(field->property("text").toString(),QString("G"));
        QMetaObject::invokeMethod(inDialog("chordEditSeventh"),"click");
        QMetaObject::invokeMethod(inDialog("chordEditDegree5"),"click");
        QCOMPARE(field->property("text").toString(),QString("G7"));
        QMetaObject::invokeMethod(inDialog("chordEditOk"),"click");
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(tracks.chords(id)->chords[2],std::string("G7"));
        QTRY_VERIFY(visualItem(window->contentItem(),"chordStripName2"));
        // A chord that does not read keeps the dialog open with a message.
        QMetaObject::invokeMethod(dialog,"edit",Q_ARG(QVariant,QVariant(0)));
        QTRY_VERIFY(dialog->property("visible").toBool());
        field->setProperty("text","Hm7");
        QMetaObject::invokeMethod(inDialog("chordEditOk"),"click");
        QVERIFY(dialog->property("visible").toBool());
        QVERIFY(bridge->property("lastError").toString().contains("Hm7"));
        QMetaObject::invokeMethod(dialog,"close");

        // Minor refills the strips with the key chords at once; there is no Fill button.
        QVERIFY(!visualItem(window->contentItem(),"chordsFill"));
        auto* box=visualItem(window->contentItem(),"chordsProgression"); QVERIFY(box);
        QCOMPARE(box->property("displayText").toString(),QString("Custom"));   // the chords edited above
        press("chordsKeyMinor");
        QTRY_VERIFY(tracks.chords(id)->key.minor);
        QTRY_COMPARE(tracks.chords(id)->chords[1],std::string("Ddim"));
        QCOMPARE(tracks.chords(id)->chords[0],std::string("Cm"));
        QTRY_COMPARE(box->property("currentIndex").toInt(),0);
        QCOMPARE(box->property("displayText").toString(),QString("Key chords"));
        // Picking a progression fills the strips in the key.
        int andalusian=-1; const auto progs=bridge->property("progressions").toList();
        for(const auto& p:progs) if(p.toMap().value("name").toString().startsWith("Andalusian")) andalusian=p.toMap().value("index").toInt();
        QVERIFY(andalusian>=0);
        QMetaObject::invokeMethod(bridge,"setKey",Q_ARG(int,9),Q_ARG(bool,true));
        QTRY_COMPARE(tracks.chords(id)->chords[0],std::string("Am"));
        QMetaObject::invokeMethod(box,"activated",Q_ARG(int,andalusian));
        QTRY_COMPARE(tracks.chords(id)->chords[3],std::string("E"));
        QCOMPARE(tracks.chords(id)->chords[1],std::string("G"));
        QTRY_COMPARE(box->property("currentIndex").toInt(),andalusian);

        // The grand piano's page is the second page.
        QVERIFY(visualItem(window->contentItem(),"selectEffect_piano"));
        panel->setProperty("selectedEffect","piano");
        QTRY_COMPARE(panel->property("selectedEffect").toString(),QString("piano"));
        QTRY_VERIFY(visualObject(window->contentItem(),"trackPiano"+QString::number(id)));
    }
    void electronic_drums_live_in_fx_and_the_drawn_machine_plays_and_edits() {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller->attachProject(&tracks);
        const int e=tracks.addInstrumentTrackOfType("Electronic Drums");
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true);
        const auto restore=qScopeGuard([&] {
            list->setVisible(false); list->setProperty("trackModel",QVariant::fromValue(static_cast<TrackListWrapper*>(nullptr)));
        });
        QTest::qWait(30);
        const auto settings=[&]{ return tracks.project().findTrack(e)->electrodrums; };
        auto* fx=visualItem(list,"trackFxButton"+QString::number(e)); QVERIFY(fx); click(fx);
        auto* panel=visualObject(window->contentItem(),"trackEffectsPanel"+QString::number(e)); QVERIFY(panel);
        QTRY_VERIFY(panel->property("visible").toBool());
        QCOMPARE(panel->property("selectedEffect").toString(),QString("electrodrums"));   // opens on its page
        QVERIFY(panel->property("supportsElectroDrums").toBool());
        QVERIFY(!panel->property("supportsDrumKit").toBool());
        QVERIFY(!visualItem(window->contentItem(),"removeEffect_electrodrums")->isVisible());   // fixed

        auto* picture=visualItem(window->contentItem(),"electroDrumsPicture"); QVERIFY(picture);
        QTRY_VERIFY(picture->isVisible()); QVERIFY(picture->height()>100);
        auto* bridge=visualObject(window->contentItem(),"electroDrumsBridge"); QVERIFY(bridge);
        QSignalSpy played(bridge,SIGNAL(played(int,int)));

        // Sixteen pads; the middle of a pad hits hard and picks its drum for the knobs.
        for (int key : {55,56,57,60,61,62,63,64,65,66,67,68,69,70,73,75})
            QVERIFY2(visualItem(picture,"electroPad_"+QString::number(key)),qPrintable(QString::number(key)));
        auto* clap=visualItem(picture,"electroPad_63"); QVERIFY(clap); click(clap);
        QTRY_COMPARE(played.count(),1);
        QCOMPARE(played[0][0].toInt(),63); QVERIFY(played[0][1].toInt()>=120);
        auto* selected=visualItem(window->contentItem(),"electroDrumsSelected"); QVERIFY(selected);
        QTRY_COMPARE(selected->property("text").toString(),QString("CLAP"));

        // The knobs edit the picked drum only.
        auto* level=visualItem(window->contentItem(),"electroDrumsLevel"); QVERIFY(level);
        QQuickItem* slider=nullptr;
        for (auto* child : level->findChildren<QQuickItem*>()) if (child->inherits("QQuickSlider")) slider=child;
        QVERIFY(slider);
        slider->forceActiveFocus(); QTest::keyClick(fxWindow(),Qt::Key_Left);
        QTRY_VERIFY(settings().drum(electrodrums::Drum::Clap).levelDb<0.0);
        QCOMPARE(settings().drum(electrodrums::Drum::Kick).levelDb,0.0);

        // A style button lights and sets the style; the display shows it.
        auto* style909=visualItem(picture,"electroStyle_909"); QVERIFY(style909); click(style909);
        QTRY_VERIFY(settings().style==electrodrums::Style::Style909);
        auto* display=visualItem(picture,"electroDisplayStyle"); QVERIFY(display);
        QTRY_COMPARE(display->property("text").toString(),QString("909"));

        // The volume knob: dragging up turns it up.
        auto* knob=visualItem(picture,"electroVolumeKnob"); QVERIFY(knob);
        const QPoint from=knob->mapToScene(QPointF(knob->width()/2,knob->height()/2)).toPoint();
        QTest::mousePress(fxWindow(),Qt::LeftButton,Qt::NoModifier,from);
        QTest::mouseMove(fxWindow(),from-QPoint(0,10)); QTest::mouseMove(fxWindow(),from-QPoint(0,30));
        QTest::mouseRelease(fxWindow(),Qt::LeftButton,Qt::NoModifier,from-QPoint(0,30));
        QTRY_VERIFY(settings().volumeDb>0.0);
        QVERIFY(window->grabWindow().save("/tmp/chaosmaker-electrodrums-page.png"));

        auto* reset=visualItem(window->contentItem(),"electroDrumsReset"); QVERIFY(reset);
        click(reset); QTRY_VERIFY(settings()==electrodrums::Settings{});
        auto* close=visualItem(window->contentItem(),"closeTrackEffects"); QVERIFY(close); click(close);
        QTRY_VERIFY(!panel->property("visible").toBool());
    }
    void grand_piano_lives_in_fx_and_the_drawn_piano_plays_and_edits() {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller->attachProject(&tracks);
        const int p=tracks.addInstrumentTrackOfType("Piano");
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true);
        const auto restore=qScopeGuard([&] {
            list->setVisible(false); list->setProperty("trackModel",QVariant::fromValue(static_cast<TrackListWrapper*>(nullptr)));
        });
        QTest::qWait(30);
        const auto settings=[&]{ return tracks.project().findTrack(p)->piano; };
        auto* fx=visualItem(list,"trackFxButton"+QString::number(p)); QVERIFY(fx); click(fx);
        auto* panel=visualObject(window->contentItem(),"trackEffectsPanel"+QString::number(p)); QVERIFY(panel);
        QTRY_VERIFY(panel->property("visible").toBool());
        QCOMPARE(panel->property("selectedEffect").toString(),QString("piano"));   // opens on its page
        QVERIFY(panel->property("supportsPiano").toBool());
        QVERIFY(!visualItem(window->contentItem(),"removeEffect_piano")->isVisible());   // fixed

        auto* picture=visualItem(window->contentItem(),"pianoPicture"); QVERIFY(picture);
        QTRY_VERIFY(picture->isVisible()); QVERIFY(picture->height()>100);
        auto* bridge=visualObject(window->contentItem(),"pianoBridge"); QVERIFY(bridge);
        QSignalSpy played(bridge,SIGNAL(played(int,int))), pedal(bridge,SIGNAL(pedalSent(bool)));

        // All 88 keys; a click near a key's front edge is loud, near its back soft; dragging
        // along the keys plays them in turn.
        for (int key : {21,22,59,60,61,107,108}) QVERIFY2(visualItem(picture,"pianoKey"+QString::number(key)),qPrintable(QString::number(key)));
        QVERIFY(!visualItem(picture,"pianoKey20") && !visualItem(picture,"pianoKey109"));
        auto* c4=visualItem(picture,"pianoKey60"); auto* d4=visualItem(picture,"pianoKey62"); QVERIFY(c4 && d4);
        auto* w=fxWindow();
        const QPoint front=c4->mapToScene(QPointF(c4->width()/2,c4->height()-3)).toPoint();
        QTest::mousePress(w,Qt::LeftButton,Qt::NoModifier,front);
        QTRY_COMPARE(played.count(),1);
        QCOMPARE(played[0][0].toInt(),60); QVERIFY(played[0][1].toInt()>=120);
        QTest::mouseMove(w,d4->mapToScene(QPointF(d4->width()/2,d4->height()-3)).toPoint());
        QTRY_COMPARE(played.count(),2);
        QCOMPARE(played[1][0].toInt(),62);
        QTest::mouseRelease(w,Qt::LeftButton,Qt::NoModifier,d4->mapToScene(QPointF(d4->width()/2,d4->height()-3)).toPoint());
        auto* cs4=visualItem(picture,"pianoKey61"); QVERIFY(cs4);  // a black key, at its back: soft
        const QPoint back=cs4->mapToScene(QPointF(cs4->width()/2,3)).toPoint();
        QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,back);
        QTRY_COMPARE(played.count(),3);
        QCOMPARE(played[2][0].toInt(),61); QVERIFY(played[2][1].toInt()<60);

        // The pedal is a switch.
        auto* pedalItem=visualItem(picture,"pianoPedal"); QVERIFY(pedalItem); click(pedalItem);
        QTRY_COMPARE(pedal.count(),1); QVERIFY(pedal[0][0].toBool());

        // Settings.
        auto* release=visualItem(window->contentItem(),"pianoRelease"); QVERIFY(release);
        QQuickItem* slider=nullptr;
        for (auto* child : release->findChildren<QQuickItem*>()) if (child->inherits("QQuickSlider")) slider=child;
        QVERIFY(slider);
        slider->forceActiveFocus(); QTest::keyClick(w,Qt::Key_Right);
        QTRY_VERIFY(settings().release>piano::DefaultRelease);
        auto* transpose=visualItem(window->contentItem(),"pianoTranspose"); QVERIFY(transpose);
        slider=nullptr;
        for (auto* child : transpose->findChildren<QQuickItem*>()) if (child->inherits("QQuickSlider")) slider=child;
        QVERIFY(slider);
        slider->forceActiveFocus(); QTest::keyClick(w,Qt::Key_Right); QTest::keyClick(w,Qt::Key_Right);
        QTRY_COMPARE(settings().transpose,2);
        QCOMPARE(tracks.project().findTrack(p)->keyShift(),2);
        QStringList texts;
        for (auto* child : transpose->findChildren<QQuickItem*>()) if (child->property("text").isValid()) texts<<child->property("text").toString();
        QVERIFY2(texts.contains("C4 plays D4 (+2)"),qPrintable(texts.join(" | ")));

        auto* status=visualItem(window->contentItem(),"pianoStatus"); QVERIFY(status);
        QTRY_VERIFY(status->isVisible());                          // the tests have no piano: it says where it looked
        QVERIFY(status->property("text").toString().contains("not found"));
        QVERIFY(qobject_cast<QQuickWindow*>(w)->grabWindow().save("/tmp/chaosmaker-piano-page.png"));

        auto* reset=visualItem(window->contentItem(),"pianoReset"); QVERIFY(reset);
        click(reset); QTRY_VERIFY(settings()==piano::Settings{});
        auto* close=visualItem(window->contentItem(),"closeTrackEffects"); QVERIFY(close); click(close);
        QTRY_VERIFY(!panel->property("visible").toBool());
    }
    void harmonium_lives_in_fx_like_guitarsuite_and_edits_its_settings() {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller->attachProject(&tracks);
        const int h=tracks.addInstrumentTrackOfType("Harmonium"), piano=tracks.addInstrumentTrackOfType("Piano");
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true);
        QTest::qWait(30);
        const auto settings=[&]{ return tracks.project().findTrack(h)->harmonium; };
        QVERIFY(!visualItem(list,"trackInstrumentButton"+QString::number(h)));   // no separate button
        auto* fx=visualItem(list,"trackFxButton"+QString::number(h)); QVERIFY(fx); click(fx);
        auto* panel=visualObject(window->contentItem(),"trackEffectsPanel"+QString::number(h)); QVERIFY(panel);
        QTRY_VERIFY(panel->property("visible").toBool());
        QCOMPARE(panel->property("selectedEffect").toString(),QString("harmonium"));   // opens on its page
        QVERIFY(panel->property("supportsHarmonium").toBool());
        auto* row=visualItem(window->contentItem(),"selectEffect_harmonium"); QVERIFY(row); QVERIFY(row->isVisible());
        QVERIFY(!visualItem(window->contentItem(),"removeEffect_harmonium")->isVisible());   // fixed, like GuitarSuite
        QVERIFY(!visualItem(window->contentItem(),"addEffect_harmonium"));

        auto* female=visualItem(window->contentItem(),"harmoniumFemale"); QVERIFY(female); QVERIFY(female->isVisible());
        click(female); QTRY_VERIFY(settings().indian.female);
        click(female); QTRY_VERIFY(!settings().indian.female);

        auto* bellows=visualItem(window->contentItem(),"harmoniumBellows"); QVERIFY(bellows);
        QQuickItem* slider=nullptr;
        for (auto* child : bellows->findChildren<QQuickItem*>()) if (child->inherits("QQuickSlider")) slider=child;
        QVERIFY(slider);
        slider->forceActiveFocus(); QTest::keyClick(fxWindow(),Qt::Key_Right);
        QTRY_VERIFY(settings().bellows>0.75);

        auto* sustain=visualItem(window->contentItem(),"harmoniumSustain"); QVERIFY(sustain); QVERIFY(sustain->isVisible());
        QCOMPARE(sustain->property("valueText").toString(),QString("1.5 s"));   // on by default
        slider=nullptr;
        for (auto* child : sustain->findChildren<QQuickItem*>()) if (child->inherits("QQuickSlider")) slider=child;
        QVERIFY(slider);
        slider->forceActiveFocus(); QTest::keyClick(fxWindow(),Qt::Key_Right);
        QTRY_VERIFY(settings().sustainSeconds>1.55);

        auto* up=visualItem(window->contentItem(),"harmoniumTransposeUp"); QVERIFY(up);
        click(up); click(up); QTRY_COMPARE(settings().transpose,2);
        auto* label=visualItem(window->contentItem(),"harmoniumTranspose"); QVERIFY(label);
        QCOMPARE(label->property("text").toString(),QString("C plays D (+2)"));

        auto* drone=visualItem(window->contentItem(),"harmoniumDrone0"); QVERIFY(drone);
        drone->forceActiveFocus(); QTest::keyClick(fxWindow(),Qt::Key_Down);
        QTRY_COMPARE(settings().drones[0],36);

        auto* style=visualItem(window->contentItem(),"harmoniumStyle"); QVERIFY(style);
        style->forceActiveFocus(); QTest::keyClick(fxWindow(),Qt::Key_Down);
        QTRY_VERIFY(settings().style==harmonium::Style::Western);
        QTRY_VERIFY(!visualItem(window->contentItem(),"harmoniumMale")->isVisible());
        auto* vox=visualItem(window->contentItem(),"harmoniumVoxHumana"); QVERIFY(vox); QVERIFY(vox->isVisible());
        click(vox); QTRY_VERIFY(settings().western.voxHumana);

        // Effects still go after the instrument page; the page stays reachable.
        auto* add=visualItem(window->contentItem(),"addEffectButton"); QVERIFY(add); QVERIFY(add->isEnabled());
        addEffectFromPicker("eq");
        QTRY_COMPARE(panel->property("selectedEffect").toString(),QString("eq"));
        click(visualItem(window->contentItem(),"selectEffect_harmonium"));
        QTRY_COMPARE(panel->property("selectedEffect").toString(),QString("harmonium"));
        QVERIFY(window->grabWindow().save("/tmp/chaosmaker-harmonium.png"));

        auto* reset=visualItem(window->contentItem(),"harmoniumReset"); QVERIFY(reset);
        click(reset); QTRY_VERIFY(settings()==harmonium::Settings{});
        auto* close=visualItem(window->contentItem(),"closeTrackEffects"); QVERIFY(close); click(close);
        QTRY_VERIFY(!panel->property("visible").toBool());

        // A piano has no page yet: its FX panel has no harmonium.
        click(visualItem(list,"trackFxButton"+QString::number(piano)));
        auto* pianoPanel=visualObject(window->contentItem(),"trackEffectsPanel"+QString::number(piano)); QVERIFY(pianoPanel);
        QTRY_VERIFY(pianoPanel->property("visible").toBool());
        QVERIFY(!pianoPanel->property("supportsHarmonium").toBool());
        close=visualItem(window->contentItem(),"closeTrackEffects"); QVERIFY(close); click(close);
        QTRY_VERIFY(!pianoPanel->property("visible").toBool());
        list->setVisible(false); list->setProperty("trackModel",QVariant());
    }
    void in_on_instrument_tracks_picks_midi_and_input_popups_stay_inside_the_window() {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller->attachProject(&tracks);
        const int vocal=tracks.addVocalTrack(), h=tracks.addInstrumentTrackOfType("Harmonium");
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true);
        QTest::qWait(30);
        const auto inside=[&](QObject* popup) {
            const double x=popup->property("x").toDouble(), y=popup->property("y").toDouble();
            const double w=popup->property("width").toDouble(), ht=popup->property("height").toDouble();
            return x>=0 && y>=0 && x+w<=window->width() && y+ht<=window->height();
        };
        for (const int id : {h, vocal}) {
            click(visualItem(list,"trackInputButton"+QString::number(id)));
            auto* popup=visualObject(window->contentItem(),"trackInputPopup"+QString::number(id)); QVERIFY(popup);
            QTRY_VERIFY(popup->property("visible").toBool());
            QVERIFY2(inside(popup),"the IN popup runs outside the window");
            const bool midi=id==h;
            QCOMPARE(popup->property("midi").toBool(),midi);
            const auto choices=popup->property("choices").toStringList();
            QCOMPARE(choices,midi ? controller->midiInputPorts() : controller->property("inputDevices").toStringList());
            QVERIFY(visualItem(window->contentItem(),midi ? "midiInputOff" : "defaultInput"));
            QMetaObject::invokeMethod(popup,"close"); QTRY_VERIFY(!popup->property("visible").toBool());
        }
        // A track list narrower than the popup (the app's left dock): it used to open off the
        // window's left edge.
        const double oldWidth=list->width();
        list->setWidth(220); QTest::qWait(30);
        click(visualItem(list,"trackInputButton"+QString::number(h)));
        auto* popup=visualObject(window->contentItem(),"trackInputPopup"+QString::number(h)); QVERIFY(popup);
        QTRY_VERIFY(popup->property("visible").toBool());
        QVERIFY2(inside(popup),"the IN popup runs outside the window next to a narrow track list");
        QMetaObject::invokeMethod(popup,"close"); QTRY_VERIFY(!popup->property("visible").toBool());
        list->setWidth(oldWidth); QTest::qWait(30);
        list->setVisible(false); list->setProperty("trackModel",QVariant());
    }
    void keys_switch_plays_notes_from_the_computer_keyboard() {
        auto* keys=bar->findChild<QObject*>("typingKeyboard"); QVERIFY(keys);
        auto* chip=visualItem(window->contentItem(),"typingKeyboardButton"); QVERIFY(chip);
        auto* other=window->findChild<QQuickItem*>("otherFocus"); QVERIFY(other);
        QSignalSpy notes(keys,SIGNAL(noteSent(int,int,bool)));
        const auto last=[&](int index=-1){ return notes.at(index<0 ? notes.size()+index : index); };
        other->forceActiveFocus();

        QTest::keyClick(window,Qt::Key_E);                        // off: the key is not taken
        QCOMPARE(notes.size(),0);

        click(chip); QTRY_VERIFY(keys->property("enabled").toBool());
        QCOMPARE(chip->property("text").toString(),QString("Sa = C4"));
        other->forceActiveFocus();
        QTest::keyPress(window,Qt::Key_E);                        // Sa
        QCOMPARE(notes.size(),1);
        QCOMPARE(last().at(0).toInt(),60); QCOMPARE(last().at(1).toInt(),100); QVERIFY(last().at(2).toBool());
        QKeyEvent repeat(QEvent::KeyPress,Qt::Key_E,Qt::NoModifier,"e",true);   // the key held down
        QCoreApplication::sendEvent(window,&repeat);
        QCOMPARE(notes.size(),1);
        QTest::keyRelease(window,Qt::Key_E);
        QCOMPARE(notes.size(),2); QCOMPARE(last().at(0).toInt(),60); QVERIFY(!last().at(2).toBool());

        // The top of the layout: Tab is lower Pa, Backspace upper Ma, backslash upper tivra
        // Ma, all played rather than moving the focus or deleting.
        QTest::keyClick(window,Qt::Key_Tab);
        QCOMPARE(notes.size(),4); QCOMPARE(last(-2).at(0).toInt(),55);
        QVERIFY(other->hasActiveFocus());
        QTest::keyClick(window,Qt::Key_Backspace);
        QCOMPARE(notes.size(),6); QCOMPARE(last(-2).at(0).toInt(),77);
        QTest::keyClick(window,Qt::Key_Backslash);
        QCOMPARE(notes.size(),8); QCOMPARE(last(-2).at(0).toInt(),78);
        QTest::keyClick(window,Qt::Key_4);                        // komal Re on the number row
        QCOMPARE(notes.size(),10); QCOMPARE(last(-2).at(0).toInt(),61);

        QTest::keyClick(window,Qt::Key_X);                        // octave up: no note
        QCOMPARE(notes.size(),10);
        QCOMPARE(chip->property("text").toString(),QString("Sa = C5"));
        QTest::keyClick(window,Qt::Key_R);                        // Re, an octave up
        QCOMPARE(notes.size(),12); QCOMPARE(last(-2).at(0).toInt(),74);
        QTest::keyClick(window,Qt::Key_Z);
        QCOMPARE(chip->property("text").toString(),QString("Sa = C4"));

        QTest::keyClick(window,Qt::Key_Q,Qt::ControlModifier);    // shortcuts keep working
        QTest::keyClick(window,Qt::Key_A);                        // the old layout: nothing
        QTest::keyClick(window,Qt::Key_S);
        QCOMPARE(notes.size(),12);

        field->forceActiveFocus();                                // typing in a field reaches it
        QTest::keyClick(window,Qt::Key_5);
        QTest::keyClick(window,Qt::Key_Backspace);
        QCOMPARE(notes.size(),12);

        other->forceActiveFocus();                                // turning off ends held notes
        QTest::keyPress(window,Qt::Key_U);                        // Pa
        QCOMPARE(notes.size(),13); QCOMPARE(last().at(0).toInt(),67);
        click(chip); QTRY_VERIFY(!keys->property("enabled").toBool());
        QCOMPARE(notes.size(),14); QCOMPARE(last().at(0).toInt(),67); QVERIFY(!last().at(2).toBool());
        QTest::keyRelease(window,Qt::Key_U);                      // the late release changes nothing
        QTest::keyClick(window,Qt::Key_E);
        QCOMPARE(notes.size(),14);
        QCOMPARE(chip->property("text").toString(),QString("Off"));
    }
    void chaos_eq_adds_drags_edits_and_removes_bands_on_the_curve() {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller->attachProject(&tracks); const int vocal=tracks.addVocalTrack();
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true);
        QTest::qWait(30);
        auto* fx=visualItem(list,"trackFxButton"+QString::number(vocal)); QVERIFY(fx); click(fx);
        auto* panel=visualObject(window->contentItem(),"trackEffectsPanel"+QString::number(vocal)); QVERIFY(panel);
        QTRY_VERIFY(panel->property("visible").toBool());
        addEffectFromPicker("chaoseq");
        QTRY_COMPARE(panel->property("selectedEffect").toString(),QString("chaoseq"));
        const auto chaos=[&]() -> const chaoseq::Settings& { return tracks.project().findTrack(vocal)->chaosEq; };
        auto* plot=visualItem(window->contentItem(),"chaosEqPlot"); QVERIFY(plot); QTRY_VERIFY(plot->isVisible());
        QVERIFY(visualItem(window->contentItem(),"chaosEqNoSelection")->isVisible());
        // Double-click above the 0 dB line, left of centre: a boosting bell appears there.
        const QPoint at=plot->mapToScene(QPointF(plot->width()*0.4,plot->height()*0.3)).toPoint();
        QTest::mouseDClick(fxWindow(),Qt::LeftButton,Qt::NoModifier,at);
        QTRY_VERIFY(chaos().bands[0].inUse);
        const double startHz=chaos().bands[0].filter.frequencyHz;
        QCOMPARE(int(chaos().bands[0].filter.shape),int(eqband::Shape::Bell));
        QVERIFY(chaos().bands[0].filter.gainDb>1.0);
        QVERIFY(std::abs(std::log2(startHz/(20*std::pow(1000.0,0.4))))<0.1);
        QTRY_VERIFY(visualItem(window->contentItem(),"chaosEqInspector")->isVisible());
        // The engine's processors follow without a playback rebuild.
        QTRY_VERIFY(controller->chaosEqProcessors(vocal)[0]->settings().bands[0].inUse);
        // Drag the node right and down: higher frequency, less gain.
        auto* node=visualItem(window->contentItem(),"chaosEqNode0"); QVERIFY(node); QTRY_VERIFY(node->isVisible());
        const QPoint from=node->mapToScene(QPointF(node->width()/2,node->height()/2)).toPoint();
        const double startGain=chaos().bands[0].filter.gainDb;
        QTest::mousePress(fxWindow(),Qt::LeftButton,Qt::NoModifier,from);
        for(int d=8;d<=80;d+=8) QTest::mouseMove(fxWindow(),from+QPoint(d,d/4));
        QTest::mouseRelease(fxWindow(),Qt::LeftButton,Qt::NoModifier,from+QPoint(80,20));
        QTRY_VERIFY(chaos().bands[0].filter.frequencyHz>startHz*1.2);
        QVERIFY(chaos().bands[0].filter.gainDb<startGain);
        // Scroll on the node: narrower (higher Q).
        const double startQ=chaos().bands[0].filter.q;
        const QPointF mid=node->mapToScene(QPointF(node->width()/2,node->height()/2));
        QWheelEvent wheel(mid,fxWindow()->mapToGlobal(mid),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(fxWindow(),&wheel); QCoreApplication::processEvents();
        QTRY_VERIFY(chaos().bands[0].filter.q>startQ);
        // Inspector buttons.
        click(visualItem(window->contentItem(),"chaosEqBypass")); QTRY_VERIFY(chaos().bands[0].bypassed);
        click(visualItem(window->contentItem(),"chaosEqBypass")); QTRY_VERIFY(!chaos().bands[0].bypassed);
        click(visualItem(window->contentItem(),"chaosEqSolo")); QTRY_VERIFY(chaos().bands[0].solo);
        click(visualItem(window->contentItem(),"chaosEqSolo")); QTRY_VERIFY(!chaos().bands[0].solo);
        click(visualItem(window->contentItem(),"chaosEqAutoGain")); QTRY_VERIFY(chaos().autoGain);
        QVERIFY(window->grabWindow().save("/tmp/chaosmaker-chaos-eq.png"));
        // Select the node and press Delete: the band goes away.
        click(node); QTest::keyClick(fxWindow(),Qt::Key_Delete);
        QTRY_VERIFY(!chaos().bands[0].inUse);
        QTRY_VERIFY(visualItem(window->contentItem(),"chaosEqNoSelection")->isVisible());
        auto* close=visualItem(window->contentItem(),"closeTrackEffects"); QVERIFY(close); click(close);
        QTRY_VERIFY(!panel->property("visible").toBool());
        // Closed: the analyzer stops listening.
        QTRY_VERIFY(!controller->chaosEqProcessors(vocal)[0]->analyzerListening());
        list->setVisible(false); list->setProperty("trackModel",QVariant());
    }
    // A mixing plugin's page (restMap G7), as a user meets it: added from the picker on a vocal
    // track, its cards shown, a slider moved and reset, a choice picked, bypass switched.
    void checkMixEffectPage(const QString& type,const QStringList& cards,const QString& slider,const QString& choice,bool meter) {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller->attachProject(&tracks); const int vocal=tracks.addVocalTrack();
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        const auto restore=qScopeGuard([&] { list->setVisible(false); list->setProperty("trackModel",QVariant()); });
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true);
        QTest::qWait(30);
        auto* fxButton=visualItem(list,"trackFxButton"+QString::number(vocal)); QVERIFY(fxButton); click(fxButton);
        auto* panel=visualObject(window->contentItem(),"trackEffectsPanel"+QString::number(vocal)); QVERIFY(panel);
        QTRY_VERIFY(panel->property("visible").toBool());
        addEffectFromPicker(type);
        QTRY_COMPARE(panel->property("selectedEffect").toString(),type);
        auto* page=visualItem(window->contentItem(),"trackMixEffect"); QVERIFY(page); QTRY_VERIFY(page->isVisible());
        for(const auto& card:cards) {
            auto* item=visualItem(page,"mixEffectCard_"+card); QVERIFY2(item,qPrintable(card)); QTRY_VERIFY(item->isVisible());
        }
        auto* meterCard=visualItem(page,"mixEffectMeter"); QVERIFY(meterCard); QCOMPARE(meterCard->isVisible(),meter);
        const auto settings=[&] { return *tracks.mixEffect(vocal,type.toStdString()); };
        const auto defaults=mixfx::defaults(type.toStdString());
        // Click near the slider's right end: the value jumps there, the engine follows.
        auto* bar=visualItem(page,type+"."+slider); QVERIFY(bar); QTRY_VERIFY(bar->isVisible());
        QTest::mouseClick(fxWindow(),Qt::LeftButton,Qt::NoModifier,bar->mapToScene(QPointF(bar->width()*0.9,bar->height()/2)).toPoint());
        QTRY_VERIFY(settings().values.at(slider.toStdString())!=defaults.values.at(slider.toStdString()));
        QTRY_COMPARE(controller->mixEffectProcessor(vocal,type,true)->parameter(slider.toStdString())->get(),
                     settings().values.at(slider.toStdString()));
        auto* valueText=visualItem(page,type+"."+slider+".value"); QVERIFY(valueText);
        QVERIFY(!valueText->property("text").toString().isEmpty());
        // Double-click: back to its default.
        QTest::qWait(qApp->styleHints()->mouseDoubleClickInterval()+50);
        QTest::mouseDClick(fxWindow(),Qt::LeftButton,Qt::NoModifier,bar->mapToScene(QPointF(bar->width()*0.9,bar->height()/2)).toPoint());
        QTRY_COMPARE(settings().values.at(slider.toStdString()),defaults.values.at(slider.toStdString()));
        // A choice: pick its second entry.
        if(!choice.isEmpty()) {
            auto* combo=visualItem(page,type+"."+choice+".choice"); QVERIFY(combo); QVERIFY(combo->isVisible());
            QMetaObject::invokeMethod(combo,"activated",Q_ARG(int,1));
            QTRY_COMPARE(settings().values.at(choice.toStdString()),1.0f);
            combo->setProperty("currentIndex",1);
        }
        // Bypass on the header, then back on.
        auto* bypass=visualItem(page,"mixEffectBypass"); QVERIFY(bypass);
        click(bypass); QTRY_VERIFY(settings().bypassed);
        QCOMPARE(bypass->property("text").toString(),QString("BYP"));
        QTRY_COMPARE(controller->mixEffectProcessor(vocal,type,false)->parameter("bypass")->get(),1.0f);
        click(bypass); QTRY_VERIFY(!settings().bypassed);
        if(auto* quick=qobject_cast<QQuickWindow*>(fxWindow())) QVERIFY(quick->grabWindow().save("/tmp/chaosmaker-mixfx-"+type+".png"));
        auto* close=visualItem(window->contentItem(),"closeTrackEffects"); QVERIFY(close); click(close);
        QTRY_VERIFY(!panel->property("visible").toBool());
    }
    void compressor_page_shows_its_cards_and_edits_the_track() {
        checkMixEffectPage("compressor",{"Compressor","Timing","Output"},"threshold","style",true);
    }
    void deesser_page_shows_its_cards_and_edits_the_track() {
        checkMixEffectPage("deesser",{"Detection","Reduction"},"frequency","mode",true);
    }
    void reverb_page_shows_its_cards_and_edits_the_track() {
        checkMixEffectPage("reverb",{"Space","Tone","Output"},"decay","type",false);
    }
    void chaosverb_page_shows_its_cards_and_edits_the_track() {
        checkMixEffectPage("chaosverb",{"Character","Space","Movement","Tone"},"decay","mode",false);
    }
    void double_click_empty_track_area_adds_vocals_without_interfering_with_renaming() {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller->attachProject(&tracks);
        auto* list=window->findChild<QQuickItem*>("testTrackList");
        auto* mixer=window->findChild<QQuickItem*>("testMixer"); QVERIFY(list); QVERIFY(mixer);
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true);
        mixer->setProperty("trackModel",QVariant::fromValue(&tracks)); mixer->setVisible(true);
        QTest::qWait(30);
        auto* empty=visualItem(list,"emptyTrackArea"); QVERIFY(empty); QVERIFY(empty->height()>0);
        auto position=[&] { return empty->mapToScene(QPointF(30,30)).toPoint(); };
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,position()); QCOMPARE(tracks.count(),0);
        QTest::mouseDClick(window,Qt::RightButton,Qt::NoModifier,position()); QCOMPARE(tracks.count(),0);
        for(int count=1;count<=2;++count) {
            QTest::mouseDClick(window,Qt::LeftButton,Qt::NoModifier,position());
            QTRY_COMPARE(tracks.count(),count);
            QTRY_COMPARE(mixer->property("stripCount").toInt(),count);
            const auto* track=tracks.project().findTrack(tracks.trackIdAt(count-1)); QVERIFY(track);
            QCOMPARE(track->type,std::string("Vocal"));
            QCOMPARE(QString::fromStdString(track->name),QString("Vocal %1").arg(count));
            QVERIFY(!track->recordArmed); QTest::qWait(30);
        }
        const int first=tracks.trackIdAt(0);
        auto* name=visualItem(list,"trackNameLabel"+QString::number(first)); QVERIFY(name);
        QTest::mouseDClick(window,Qt::LeftButton,Qt::NoModifier,name->mapToScene(QPointF(20,10)).toPoint());
        auto* editor=visualItem(list,"trackNameEditor"+QString::number(first)); QVERIFY(editor);
        QTRY_VERIFY(editor->isVisible()); QCOMPARE(tracks.count(),2);
        QTest::keyClick(window,Qt::Key_Escape); QVERIFY(!editor->isVisible());
        // The region still follows the last track after a row is removed.
        QVERIFY(tracks.removeTrack(first)); QTest::qWait(30);
        QTest::mouseDClick(window,Qt::LeftButton,Qt::NoModifier,position()); QTRY_COMPARE(tracks.count(),2);
        QCOMPARE(tracks.project().findTrack(tracks.trackIdAt(1))->name,std::string("Vocal 3"));
        list->setVisible(false); list->setProperty("trackModel",QVariant());
        mixer->setVisible(false); mixer->setProperty("trackModel",QVariant());
    }
    void backend_catalog_choices_create_instruments_and_vocals_through_the_mirrored_bridge() {
        TrackListWrapper tracks;
        QAbstractItemModelTester tester(&tracks, QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto* list = window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        const auto restore = qScopeGuard([&] { list->setVisible(false); list->setProperty("trackModel", QVariant()); });
        list->setProperty("trackModel", QVariant::fromValue(&tracks)); list->setVisible(true);
        QTest::qWait(20);
        auto* add = visualItem(list, "addTrackButton"); QVERIFY(add); click(add); QTest::qWait(20);
        auto* instrument = visualItem(window->contentItem(), "trackChoice_Instrument"); QVERIFY(instrument); click(instrument); QTest::qWait(20);
        QVERIFY(!visualItem(window->contentItem(), "trackChoice_Electric Guitar"));   // inside Guitar now
        auto* guitar = visualItem(window->contentItem(), "trackChoice_group:Guitar"); QVERIFY(guitar); click(guitar); QTest::qWait(20);
        QVERIFY(visualItem(window->contentItem(), "trackChoice_Acoustic Guitar"));
        auto* electric = visualItem(window->contentItem(), "trackChoice_Electric Guitar"); QVERIFY(electric); click(electric);
        QTRY_COMPARE(tracks.count(), 1);
        QCOMPARE(tracks.project().tracks()[0]->type, std::string("Electric Guitar"));
        QCOMPARE(tracks.project().tracks()[0]->name, std::string("Electric Guitar 1"));
        QVERIFY(tracks.project().tracks()[0]->hasLiveInput());
        auto* popup = visualObject(window->contentItem(), "addTrackPopup"); QVERIFY(popup);
        QTRY_VERIFY(!popup->property("visible").toBool()); click(add); QTest::qWait(20);
        auto* vocal = visualItem(window->contentItem(), "trackChoice_Vocal"); QVERIFY(vocal); click(vocal);
        QTRY_COMPARE(tracks.count(), 2);
        QCOMPARE(tracks.project().tracks()[1]->type, std::string("Vocal"));
        QCOMPARE(tracks.project().tracks()[1]->name, std::string("Vocal 1"));

        // Drums > Electronic; the back arrow steps back one screen at a time.
        QTRY_VERIFY(!popup->property("visible").toBool()); click(add); QTest::qWait(20);
        instrument = visualItem(window->contentItem(), "trackChoice_Instrument"); QVERIFY(instrument); click(instrument); QTest::qWait(20);
        auto* drums = visualItem(window->contentItem(), "trackChoice_group:Drums"); QVERIFY(drums); click(drums); QTest::qWait(20);
        QCOMPARE(popup->property("addTrackStage").toString(), QString("variant"));
        auto* back = visualItem(window->contentItem(), "addTrackBack"); QVERIFY(back); click(back); QTest::qWait(20);
        QCOMPARE(popup->property("addTrackStage").toString(), QString("instrument"));
        drums = visualItem(window->contentItem(), "trackChoice_group:Drums"); QVERIFY(drums); click(drums); QTest::qWait(20);
        QVERIFY(visualItem(window->contentItem(), "trackChoice_Drums"));
        auto* electronic = visualItem(window->contentItem(), "trackChoice_Electronic Drums"); QVERIFY(electronic); click(electronic);
        QTRY_COMPARE(tracks.count(), 3);
        QCOMPARE(tracks.project().tracks()[2]->type, std::string("Electronic Drums"));
        QTRY_VERIFY(!popup->property("visible").toBool());
        QCOMPARE(popup->property("addTrackStage").toString(), QString("kind"));
    }
    void track_names_edit_inline_commit_cancel_and_update_mixer_and_fx() {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller->attachProject(&tracks);
        const int a=tracks.addAudioTrack(),b=tracks.addVocalTrack();
        auto* list=window->findChild<QQuickItem*>("testTrackList");
        auto* mixer=window->findChild<QQuickItem*>("testMixer"); QVERIFY(list); QVERIFY(mixer);
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true);
        mixer->setProperty("trackModel",QVariant::fromValue(&tracks)); mixer->setVisible(true); QTest::qWait(30);
        auto* label=visualItem(list,"trackNameLabel"+QString::number(a));
        auto* editor=visualItem(list,"trackNameEditor"+QString::number(a));
        auto* mixerName=visualItem(mixer,"mixerName"+QString::number(a));
        auto* other=window->findChild<QQuickItem*>("otherFocus");
        QVERIFY(label); QVERIFY(editor); QVERIFY(mixerName); QVERIFY(other); QVERIFY(!editor->isVisible());
        auto begin=[&] {
            QTest::mouseDClick(window,Qt::LeftButton,Qt::NoModifier,label->mapToScene(QPointF(30,10)).toPoint());
            QTRY_VERIFY(editor->isVisible()); QVERIFY(editor->hasActiveFocus());
            QCOMPARE(editor->property("selectedText").toString(),label->property("text").toString());
        };
        begin();
        for(auto key:{Qt::Key_L,Qt::Key_E,Qt::Key_A,Qt::Key_D})QTest::keyClick(window,key);
        QTest::keyClick(window,Qt::Key_Return);
        QTRY_VERIFY(!editor->isVisible()); QCOMPARE(label->property("text").toString(),QString("lead"));
        QCOMPARE(mixerName->property("text").toString(),QString("lead"));
        begin(); editor->setProperty("text","Cancelled"); QTest::keyClick(window,Qt::Key_Escape);
        QVERIFY(!editor->isVisible()); QCOMPARE(label->property("text").toString(),QString("lead"));
        begin(); editor->setProperty("text","   "); QTest::keyClick(window,Qt::Key_Return);
        QVERIFY(!editor->isVisible()); QCOMPARE(label->property("text").toString(),QString("lead"));
        begin(); editor->setProperty("text","  Rhythm guitar  "); click(other);
        QTRY_VERIFY(!editor->isVisible()); QCOMPARE(label->property("text").toString(),QString("Rhythm guitar"));
        QCOMPARE(mixerName->property("text").toString(),QString("Rhythm guitar"));
        auto* fx=visualItem(list,"trackFxButton"+QString::number(a)); QVERIFY(fx); click(fx);
        auto* panel=visualObject(window->contentItem(),"trackEffectsPanel"+QString::number(a)); QVERIFY(panel);
        QTRY_VERIFY(panel->property("visible").toBool());
        QCOMPARE(panel->property("trackName").toString(),QString("Rhythm guitar"));
        QVERIFY(tracks.renameTrack(a,"Final guitar"));
        QCOMPARE(panel->property("trackName").toString(),QString("Final guitar"));
        QCOMPARE(label->property("text").toString(),QString("Final guitar"));
        QCOMPARE(mixerName->property("text").toString(),QString("Final guitar"));
        QCOMPARE(tracks.project().findTrack(b)->name,std::string("Vocal 1"));
        auto* close=visualItem(window->contentItem(),"closeTrackEffects"); QVERIFY(close); click(close);
        QTRY_VERIFY(!panel->property("visible").toBool());
        list->setVisible(false); list->setProperty("trackModel",QVariant());
        mixer->setVisible(false); mixer->setProperty("trackModel",QVariant());
    }
    void track_list_and_mixer_share_mute_solo_order_colours_and_tooltips() {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller->attachProject(&tracks);
        auto* list=window->findChild<QQuickItem*>("testTrackList");
        auto* mixer=window->findChild<QQuickItem*>("testMixer"); QVERIFY(list); QVERIFY(mixer);
        const auto restore=qScopeGuard([&] {
            for(auto* view:{list,mixer}) { view->setVisible(false); view->setProperty("trackModel",QVariant()); }
        });
        for(auto* view:{list,mixer}) { view->setProperty("trackModel",QVariant::fromValue(&tracks)); view->setVisible(true); }
        QTest::qWait(30);
        // Empty list: the hint names both ways to add a track; "+" opens the track picker.
        auto* hint=visualItem(list,"emptyTrackHint"); QVERIFY(hint);
        QVERIFY(hint->isVisible());
        QCOMPARE(hint->property("text").toString(),QString("Click + to add a track, or double-click here for a Vocal track"));
        auto* add=visualItem(list,"addTrackButton"); QVERIFY(add);
        QCOMPARE(QQmlProperty::read(add,"ToolTip.text",qmlContext(add)).toString(),QString("Add track…"));
        const int a=tracks.addAudioTrack(); QTest::qWait(30);
        QVERIFY(!hint->isVisible());
        const QString id=QString::number(a);
        auto* listMute=visualItem(list,"trackMuteButton"+id); auto* listSolo=visualItem(list,"trackSoloButton"+id);
        auto* mixMute=visualItem(mixer,"mixerMute"+id); auto* mixSolo=visualItem(mixer,"mixerSolo"+id);
        QVERIFY(listMute); QVERIFY(listSolo); QVERIFY(mixMute); QVERIFY(mixSolo);
        // Same order in both: M left of S.
        QVERIFY(listMute->mapToScene(QPointF()).x()<listSolo->mapToScene(QPointF()).x());
        QVERIFY(mixMute->mapToScene(QPointF()).x()<mixSolo->mapToScene(QPointF()).x());
        const auto look=[](QQuickItem* button) {
            auto* bg=qvariant_cast<QQuickItem*>(button->property("background"));
            auto* text=qvariant_cast<QQuickItem*>(button->property("contentItem"));
            return QList<QColor>{bg->property("color").value<QColor>(),bg->property("border.color").value<QColor>(),
                                 text->property("color").value<QColor>()};
        };
        const auto tip=[](QQuickItem* button) { return QQmlProperty::read(button,"ToolTip.text",qmlContext(button)).toString(); };
        QCOMPARE(tip(listMute),tip(mixMute)); QCOMPARE(tip(listSolo),tip(mixSolo));
        // Mute (orange) and solo (yellow) look the same in both panels, and differ from each other.
        click(listMute); QTRY_VERIFY(mixMute->property("active").toBool());
        click(mixSolo); QTRY_VERIFY(listSolo->property("active").toBool());
        QTest::qWait(10);
        QCOMPARE(look(listMute),look(mixMute));
        QCOMPARE(look(listSolo),look(mixSolo));
        QCOMPARE(look(listMute).first(),QColor("#653f28"));
        QVERIFY(look(listMute)!=look(listSolo));
        QCOMPARE(tip(listMute),tip(mixMute)); QCOMPARE(tip(listSolo),tip(mixSolo));
        // Selecting the track keeps its colour strip in the track colour.
        auto* strip=visualItem(list,"trackColourAccent"+id); QVERIFY(strip);
        const QColor colour=strip->property("color").value<QColor>();
        tracks.selectTrack(a); QTest::qWait(10);
        QCOMPARE(strip->property("color").value<QColor>(),colour);
        controller->shutdownSession();
    }
    void track_colour_picker_updates_cards_timeline_and_mixer_and_cancels_custom_edits() {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller->attachProject(&tracks);
        const int a=tracks.addVocalTrack(),b=tracks.addInstrumentTrackOfType("Electric Guitar");
        const int first=tracks.addClipAtBeat(a,0,4),second=tracks.addClipAtBeat(a,4,4),other=tracks.addClipAtBeat(b,0,4);
        auto* list=window->findChild<QQuickItem*>("testTrackList");
        auto* mixer=window->findChild<QQuickItem*>("testMixer");
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(list); QVERIFY(mixer); QVERIFY(timeline);
        list->setHeight(540); timeline->setX(340); timeline->setWidth(760);
        for(auto* view:{list,mixer,timeline}) { view->setProperty("trackModel",QVariant::fromValue(&tracks)); view->setVisible(true); }
        QTest::qWait(30);
        auto* button=visualItem(list,"trackColourButton"+QString::number(a)); QVERIFY(button); click(button);
        auto* popup=visualObject(window->contentItem(),"trackColourPopup"+QString::number(a)); QVERIFY(popup);
        QTRY_VERIFY(popup->property("visible").toBool());
        auto* choice=visualItem(window->contentItem(),"trackColourChoice"+QString::number(a)+"_8"); QVERIFY(choice); click(choice);
        QTRY_VERIFY(!popup->property("visible").toBool());
        auto verify=[&](const QString& colour) {
            const QColor expected(colour);
            QCOMPARE(QString::fromStdString(tracks.project().findTrack(a)->colour),colour);
            for(const auto& name:{"trackColourAccent"+QString::number(a),"mixerColour"+QString::number(a),"timelineLaneColour"+QString::number(a)}) {
                auto* accent=visualItem(window->contentItem(),name); QVERIFY(accent);
                QCOMPARE(accent->property("color").value<QColor>(),expected);
            }
            for(int clip:{first,second}) {
                auto* block=visualItem(timeline,"clipBlock"+QString::number(clip)); QVERIFY(block);
                QCOMPARE(block->property("trackColour").value<QColor>(),expected);
                QCOMPARE(block->property("color").value<QColor>(),expected.darker(240));
            }
            auto* untouched=visualItem(timeline,"clipBlock"+QString::number(other)); QVERIFY(untouched);
            QCOMPARE(untouched->property("trackColour").value<QColor>(),QColor("#6fa8dc"));
        };
        QCoreApplication::processEvents(); verify("#d97c73");
        click(button); QTRY_VERIFY(popup->property("visible").toBool());
        auto* custom=visualItem(window->contentItem(),"customTrackColour"+QString::number(a)); QVERIFY(custom); click(custom);
        auto* dialog=visualObject(window->contentItem(),"trackColourDialog"+QString::number(a)); QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool()); dialog->setProperty("selectedColor",QColor("#112233"));
        QVERIFY(QMetaObject::invokeMethod(dialog,"reject")); QTRY_VERIFY(!dialog->property("visible").toBool()); verify("#d97c73");
        click(button); QTRY_VERIFY(popup->property("visible").toBool()); click(custom);
        QTRY_VERIFY(dialog->property("visible").toBool()); dialog->setProperty("selectedColor",QColor("#4d91c2"));
        QVERIFY(QMetaObject::invokeMethod(dialog,"accept")); QTRY_VERIFY(!dialog->property("visible").toBool());
        QCoreApplication::processEvents(); verify("#4d91c2");
        // The dialog lives inside the main window, so closing it leaves keyboard input working.
        QCOMPARE(QGuiApplication::focusWindow(),static_cast<QWindow*>(window));
        auto* focusTarget=window->findChild<QQuickItem*>("otherFocus"); QVERIFY(focusTarget);
        focusTarget->forceActiveFocus(); QVERIFY(focusTarget->hasActiveFocus());
        QVERIFY(window->grabWindow().save("/tmp/chaosmaker-track-colours.png"));
        for(auto* view:{list,mixer,timeline}) { view->setVisible(false); view->setProperty("trackModel",QVariant()); }
        list->setHeight(650); timeline->setX(0); timeline->setWidth(1000);
    }
    void adjacent_empty_cells_merge_from_the_context_menu() {
        TrackListWrapper tracks; const int track=tracks.addAudioTrack();
        const int a=tracks.addClipAtBeat(track,0,2),b=tracks.addClipAtBeat(track,2,2),c=tracks.addClipAtBeat(track,4,2);
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        const auto restore=qScopeGuard([&] { timeline->setVisible(false); timeline->setProperty("trackModel",QVariant()); });
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true); QTest::qWait(20);
        timeline->forceActiveFocus(); QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
        QVERIFY(timeline->property("canMergeSelection").toBool());
        auto* last=visualItem(timeline,"clipBlock"+QString::number(c)); QVERIFY(last);
        QTest::mouseClick(window,Qt::RightButton,Qt::NoModifier,last->mapToScene(QPointF(30,30)).toPoint());
        auto* merge=visualItem(window->contentItem(),"mergeSelectedClips"); QVERIFY(merge); QVERIFY(merge->isEnabled()); click(merge);
        QTRY_COMPARE(tracks.clips().size(),1); QCOMPARE(tracks.project().findTrack(track)->audioClips[0].id,a);
        QCOMPARE(tracks.project().findTrack(track)->audioClips[0].length,qint64(144000));
        QCOMPARE(timeline->property("selectedClipId").toInt(),a);
        QVERIFY(tracks.undo()); QCOMPARE(tracks.clips().size(),3);
        QCOMPARE(tracks.project().findTrack(track)->audioClips[1].id,b);
    }
    void clip_drag_moves_between_tracks_with_preview_and_undo() {
        TrackListWrapper tracks; const int a=tracks.addAudioTrack(),b=tracks.addVocalTrack(),c=tracks.addAudioTrack();
        const int clip=tracks.addClipAtBeat(a,2,2);
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        const auto restore=qScopeGuard([&] { timeline->setVisible(false); timeline->setProperty("trackModel",QVariant()); timeline->setProperty("snapMode",1); });
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true); QTest::qWait(20);
        auto* block=visualItem(timeline,"clipBlock"+QString::number(clip)); QVERIFY(block);
        // A vertical move must preserve timing even when the clip is off the selected grid.
        timeline->setProperty("snapMode",0);
        const QPoint vertical=block->mapToScene(QPointF(30,30)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,vertical);
        QTest::mouseMove(window,vertical+QPoint(0,106));
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,vertical+QPoint(0,106));
        QCOMPARE(tracks.project().findTrack(b)->audioClips[0].start,qint64(48000));
        QVERIFY(tracks.undo()); timeline->setProperty("snapMode",1);
        block=visualItem(timeline,"clipBlock"+QString::number(clip)); QVERIFY(block);
        const QPoint start=block->mapToScene(QPointF(30,30)).toPoint(),end=start+QPoint(48,106);
        const double originalY=block->y();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start); QTest::mouseMove(window,end);
        QCOMPARE(block->y(),originalY+106); // Preview uses the destination lane before committing.
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,end);
        QCOMPARE(tracks.project().findTrack(a)->audioClips.size(),std::size_t(0));
        QCOMPARE(tracks.project().findTrack(b)->audioClips[0].id,clip);
        QCOMPARE(tracks.project().findTrack(b)->audioClips[0].start,qint64(72000));
        QCOMPARE(timeline->property("selectedTrackId").toInt(),b);
        QVERIFY(tracks.undo()); QCOMPARE(tracks.project().findTrack(a)->audioClips[0].id,clip);
        QVERIFY(tracks.redo()); QCOMPARE(tracks.project().findTrack(b)->audioClips[0].id,clip);
        block=visualItem(timeline,"clipBlock"+QString::number(clip)); QVERIFY(block);
        const QPoint back=block->mapToScene(QPointF(30,30)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,back); QTest::mouseMove(window,back-QPoint(0,106));
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,back-QPoint(0,106));
        QCOMPARE(tracks.project().findTrack(a)->audioClips[0].id,clip);
        QCOMPARE(tracks.project().findTrack(a)->audioClips[0].start,qint64(72000));
        QCOMPARE(tracks.project().findTrack(c)->audioClips.size(),std::size_t(0));
    }
    void clip_keyboard_operations_across_tracks_data() {
        QTest::addColumn<int>("mode"); QTest::addColumn<int>("denominator");
        for(int mode:{0,1,3,6})QTest::newRow(qPrintable(QString("4/4 mode %1").arg(mode))) << mode << 4;
        for(int mode:{0,1,3})QTest::newRow(qPrintable(QString("3/8 mode %1").arg(mode))) << mode << 8;
    }
    void clip_keyboard_operations_across_tracks() {
        QFETCH(int,mode); QFETCH(int,denominator);
        auto* controller=EngineController::instance(); controller->setMeter(denominator==8 ? 3 : 4,denominator);
        TrackListWrapper tracks; const int a=tracks.addAudioTrack(),b=tracks.addAudioTrack(),c=tracks.addAudioTrack();
        const int first=tracks.addClipAtBeat(a,2,2),second=tracks.addClipAtBeat(b,4,2),hidden=tracks.addClipAtBeat(c,40,2);
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        const auto restore=qScopeGuard([&] {
            timeline->setVisible(false); timeline->setProperty("trackModel",QVariant()); timeline->setProperty("snapMode",1);
            controller->setMeter(4,4);
        });
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setProperty("snapMode",mode); timeline->setVisible(true);
        timeline->setProperty("timeRangeStartBeat",1); timeline->setProperty("timeRangeEndBeat",4);
        timeline->forceActiveFocus(); QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
        const auto selected=[&] { return selectedIds(timeline); };
        QCOMPARE(selected().size(),3); QVERIFY(selected().contains(hidden));
        const double step=mode==6 ? 120.0/60000 : mode==0 ? (denominator==8 ? 1.5 : 4.0)
            : 4.0/denominator/std::pow(2.0,mode-1);
        const qint64 delta=std::llround(step*24000);
        QTest::keyClick(window,Qt::Key_Right);
        QCOMPARE(tracks.project().findTrack(a)->audioClips[0].start,qint64(48000)+delta);
        QCOMPARE(tracks.project().findTrack(b)->audioClips[0].start,qint64(96000)+delta);
        QCOMPARE(tracks.project().findTrack(c)->audioClips[0].start,qint64(960000)+delta);
        QVERIFY(tracks.undo()); QCOMPARE(tracks.project().findTrack(a)->audioClips[0].start,qint64(48000));
        QCOMPARE(tracks.project().findTrack(c)->audioClips[0].start,qint64(960000));
        QVERIFY(tracks.redo()); QTest::keyClick(window,Qt::Key_Left);
        QCOMPARE(tracks.project().findTrack(a)->audioClips[0].start,qint64(48000));
        QCOMPARE(tracks.project().findTrack(b)->audioClips[0].start,qint64(96000));
        QTest::keyClick(window,Qt::Key_Delete); QCOMPARE(tracks.clips().size(),0); QCOMPARE(selected().size(),0);
        QVERIFY(tracks.undo()); QCOMPARE(tracks.clips().size(),3);
        QCOMPARE(tracks.project().findTrack(a)->audioClips[0].id,first);
        QCOMPARE(tracks.project().findTrack(b)->audioClips[0].id,second);
        QTest::keyClick(window,Qt::Key_Right); QCOMPARE(tracks.project().findTrack(a)->audioClips[0].start,qint64(48000));
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); QCOMPARE(selected().size(),3);
        QTest::keyClick(window,Qt::Key_Escape); QCOMPARE(selected().size(),0);
        QCOMPARE(timeline->property("selectedClipId").toInt(),-1);
        QCOMPARE(timeline->property("rangeStartBeat").toDouble(),1.0); QCOMPARE(timeline->property("rangeEndBeat").toDouble(),4.0);
        QTest::keyClick(window,Qt::Key_Delete); QCOMPARE(tracks.clips().size(),3);
    }
    void clip_keyboard_operations_preserve_text_and_reject_negative_group_positions() {
        TrackListWrapper tracks; const int a=tracks.addAudioTrack(),b=tracks.addAudioTrack();
        const int first=tracks.addClipAtBeat(a,0,2); tracks.addClipAtBeat(b,4,2);
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        const auto restore=qScopeGuard([&] { timeline->setVisible(false); timeline->setProperty("trackModel",QVariant()); EngineController::instance()->setBpm(120); });
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true); QTest::qWait(20);
        timeline->forceActiveFocus(); QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
        QSignalSpy changes(&tracks,&TrackListWrapper::clipsChanged);
        QTest::keyClick(window,Qt::Key_Left); QCOMPARE(changes.count(),0);
        QCOMPARE(tracks.project().findTrack(a)->audioClips[0].start,qint64(0));
        QCOMPARE(tracks.project().findTrack(b)->audioClips[0].start,qint64(96000));
        QTest::keyClick(window,Qt::Key_Escape);
        auto* block=visualItem(timeline,"clipBlock"+QString::number(first)); QVERIFY(block); click(block);
        click(field); QTest::qWait(10); QVERIFY(field->hasActiveFocus());
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
        QCOMPARE(field->property("selectedText").toString(),QString("120"));
        QTest::keyClick(window,Qt::Key_Left); QTest::keyClick(window,Qt::Key_Delete);
        QCOMPARE(field->property("text").toString(),QString("20")); QCOMPARE(changes.count(),0);
        QCOMPARE(selectedIds(timeline).size(),1);
        QTest::keyClick(window,Qt::Key_Escape);
        QCOMPARE(selectedIds(timeline).size(),1);
        auto* other=window->findChild<QQuickItem*>("otherFocus"); QVERIFY(other); click(other);
        QTest::keyClick(window,Qt::Key_Delete); QTest::keyClick(window,Qt::Key_Right); QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
        QCOMPARE(changes.count(),0); QCOMPARE(tracks.clips().size(),2);
    }
    void adjustable_snap_grid_applies_to_group_edits_and_ranges_data() {
        QTest::addColumn<int>("mode"); QTest::addColumn<int>("numerator"); QTest::addColumn<int>("denominator");
        for(int mode=0;mode<7;++mode)QTest::newRow(qPrintable(QString("4/4 mode %1").arg(mode))) << mode << 4 << 4;
        for(int mode:{0,1,3,6})QTest::newRow(qPrintable(QString("3/8 mode %1").arg(mode))) << mode << 3 << 8;
    }
    void adjustable_snap_grid_applies_to_group_edits_and_ranges() {
        QFETCH(int,mode); QFETCH(int,numerator); QFETCH(int,denominator);
        auto* controller=EngineController::instance(); controller->setMeter(numerator,denominator);
        TrackListWrapper tracks; const int a=tracks.addAudioTrack(),b=tracks.addAudioTrack();
        const int x=tracks.addClipAtBeat(a,2,4),y=tracks.addClipAtBeat(b,3.25,4);
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        const auto restore=qScopeGuard([&] {
            timeline->setVisible(false); timeline->setProperty("trackModel",QVariant()); timeline->setProperty("snapMode",1);
            controller->setMeter(4,4); controller->setRepeatRange(0,0);
        });
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true);
        openQuickSettings(); auto* picker=visualItem(window->contentItem(),"snapGridSelector"); QVERIFY(picker);
        click(picker); QTest::keyClick(window,Qt::Key_Home);
        for(int i=0;i<mode;++i)QTest::keyClick(window,Qt::Key_Down);
        QTest::keyClick(window,Qt::Key_Return); QTRY_COMPARE(timeline->property("snapMode").toInt(),mode);
        const double step=mode==6 ? 0 : mode==0 ? numerator*4.0/denominator : 4.0/denominator/std::pow(2.0,mode-1);
        QCOMPARE(timeline->property("snapStepBeats").toDouble(),step);
        QCOMPARE(timeline->property("gridUnitWidth").toDouble(),48*(step>0 ? step : 4.0/denominator));
        auto* close=visualItem(window->contentItem(),"closeQuickSettings"); QVERIFY(close); click(close); QTest::qWait(20);
        auto* first=visualItem(timeline,"clipBlock"+QString::number(x));
        auto* second=visualItem(timeline,"clipBlock"+QString::number(y)); QVERIFY(first); QVERIFY(second);
        click(first); QTest::mouseClick(window,Qt::LeftButton,Qt::ControlModifier,second->mapToScene(QPointF(30,30)).toPoint());
        const auto drag=[&](QQuickItem* item,int distance) {
            const QPoint start=item->mapToScene(QPointF(item->width()>20 ? 30 : item->width()/2,20)).toPoint();
            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
            QTest::mouseMove(window,start+QPoint(distance,0));
            QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,start+QPoint(distance,0));
        };
        const auto snap=[&](double beat) { return step>0 ? std::round(beat/step)*step : beat; };
        const qint64 delta=std::llround((snap(2+37.0/48)-2)*24000);
        drag(first,37);
        QCOMPARE(tracks.project().findTrack(a)->audioClips[0].start,qint64(48000)+delta);
        QCOMPARE(tracks.project().findTrack(b)->audioClips[0].start,qint64(78000)+delta);
        QVERIFY(tracks.undo());
        auto* left=visualItem(timeline,"clipLeftTrim"+QString::number(x)); QVERIFY(left); drag(left,37);
        QCOMPARE(tracks.project().findTrack(a)->audioClips[0].start,qint64(48000)+delta);
        QCOMPARE(tracks.project().findTrack(a)->audioClips[0].sourceOffset,delta);
        QVERIFY(tracks.undo());
        auto* right=visualItem(timeline,"clipRightTrim"+QString::number(x)); QVERIFY(right); drag(right,37);
        const qint64 resized=std::llround((snap(6+37.0/48)-2)*24000);
        QCOMPARE(tracks.project().findTrack(a)->audioClips[0].length,resized);
        QCOMPARE(tracks.project().findTrack(b)->audioClips[0].length,resized);
        QVERIFY(tracks.undo());
        const QPoint start=timeline->mapToScene(QPointF(65,300)).toPoint(),end=timeline->mapToScene(QPointF(175,300)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        QTest::mouseMove(window,end); QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,end);
        const double rangeStart=step>0 ? std::floor((65.0/48)/step)*step : 65.0/48;
        const double rangeEnd=step>0 ? std::ceil((175.0/48)/step)*step : 175.0/48;
        QCOMPARE(timeline->property("rangeStartBeat").toDouble(),rangeStart);
        QCOMPARE(timeline->property("rangeEndBeat").toDouble(),rangeEnd);
    }
    void snap_off_allows_subbeat_clip_lengths() {
        TrackListWrapper tracks; const int track=tracks.addAudioTrack(),clip=tracks.addClipAtBeat(track,2,1);
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        const auto restore=qScopeGuard([&] { timeline->setVisible(false); timeline->setProperty("trackModel",QVariant()); timeline->setProperty("snapMode",1); });
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setProperty("snapMode",6); timeline->setVisible(true);
        QTest::qWait(20); auto* grip=visualItem(timeline,"clipRightTrim"+QString::number(clip)); QVERIFY(grip);
        const QPoint start=grip->mapToScene(QPointF(4,20)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        QTest::mouseMove(window,start-QPoint(37,0)); QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,start-QPoint(37,0));
        QCOMPARE(tracks.project().findTrack(track)->audioClips[0].length,qint64(5500));
    }
    void manual_timeline_scrolling_is_not_overridden_by_transport_updates() {
        auto device=std::make_unique<MultiInputUiDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); controller.setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller.attachProject(&tracks);
        const int track=tracks.addAudioTrack(); tracks.addClipAtBeat(track,0,80);
        for(int i=0;i<5;++i)tracks.addAudioTrack();
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        const auto restore=qScopeGuard([&] {
            timeline->setVisible(false); timeline->setProperty("trackModel",QVariant());
            timeline->setProperty("transport",QVariant::fromValue(EngineController::instance()));
        });
        timeline->setProperty("transport",QVariant::fromValue(&controller));
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true);
        QTest::qWait(30);
        auto* scroll=visualItem(timeline,"timelineScroll");
        auto* horizontal=visualItem(timeline,"timelineHorizontalScroll");
        auto* vertical=visualItem(timeline,"timelineVerticalScroll");
        QVERIFY(scroll); QVERIFY(horizontal); QVERIFY(vertical);
        const auto dragBar=[&](QQuickItem* bar,int distance,bool horizontalAxis) {
            const double center=bar->property("position").toDouble()+bar->property("size").toDouble()/2;
            const QPoint start=bar->mapToScene(horizontalAxis ? QPointF(bar->width()*center,bar->height()/2)
                : QPointF(bar->width()/2,bar->height()*center)).toPoint();
            const QPoint end=start+(horizontalAxis ? QPoint(distance,0) : QPoint(0,distance));
            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
            QTest::mouseMove(window,end,20); QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,end);
        };
        dragBar(horizontal,250,true);
        const double stoppedX=scroll->property("contentX").toDouble(); QVERIFY(stoppedX>100);
        QTest::qWait(100); QCOMPARE(scroll->property("contentX").toDouble(),stoppedX);
        dragBar(vertical,40,false);
        QVERIFY(timeline->property("verticalScrollPosition").toDouble()>0);
        controller.seekBeat(60); controller.transportAction("play"); io->process();
        QTRY_VERIFY(scroll->property("contentX").toDouble()>stoppedX);
        dragBar(horizontal,-250,true);
        const double playingX=scroll->property("contentX").toDouble();
        QVERIFY(!timeline->property("followPlayhead").toBool());
        for(int i=0;i<5;++i)io->process(); QTest::qWait(100);
        QCOMPARE(scroll->property("contentX").toDouble(),playingX);
        controller.transportAction("pause"); io->process();
        QTRY_VERIFY(!timeline->property("playing").toBool());
        dragBar(horizontal,100,true);
        const double pausedX=scroll->property("contentX").toDouble(); QVERIFY(pausedX>playingX);
        QTest::qWait(100); QCOMPARE(scroll->property("contentX").toDouble(),pausedX);
        controller.transportAction("stop"); io->process(); QTRY_COMPARE(scroll->property("contentX").toDouble(),0.0);
        dragBar(horizontal,100,true); const double afterStop=scroll->property("contentX").toDouble(); QVERIFY(afterStop>0);
        QTest::qWait(100); QCOMPARE(scroll->property("contentX").toDouble(),afterStop);
        // Horizontal trackpad scrolling and Shift-wheel navigate without changing zoom.
        const double zoom=timeline->property("pixelsPerSecond").toDouble();
        const QPointF local=timeline->mapToScene(QPointF(500,250));
        const auto wheel=[&](QPoint angle,Qt::KeyboardModifiers modifiers) {
            QWheelEvent event(local,window->mapToGlobal(local.toPoint()),QPoint(),angle,Qt::NoButton,modifiers,Qt::NoScrollPhase,false);
            QCoreApplication::sendEvent(window,&event); QCoreApplication::processEvents();
        };
        wheel(QPoint(-120,0),Qt::NoModifier);
        const double trackpadX=scroll->property("contentX").toDouble(); QVERIFY(trackpadX>afterStop);
        wheel(QPoint(0,-120),Qt::ShiftModifier);
        const double shiftX=scroll->property("contentX").toDouble(); QVERIFY(shiftX>trackpadX);
        QCOMPARE(timeline->property("pixelsPerSecond").toDouble(),zoom);
        // A plain wheel scrolls the tracks up and down, not the zoom or the time position.
        QVERIFY(QMetaObject::invokeMethod(timeline,"scrollVertically",Q_ARG(QVariant,-1.0e6)));
        wheel(QPoint(0,-120),Qt::NoModifier);
        const double scrolledY=timeline->property("verticalScrollPosition").toDouble(); QCOMPARE(scrolledY,90.0);
        wheel(QPoint(0,120),Qt::NoModifier);
        QCOMPARE(timeline->property("verticalScrollPosition").toDouble(),0.0);
        QCOMPARE(scroll->property("contentX").toDouble(),shiftX);
        QCOMPARE(timeline->property("pixelsPerSecond").toDouble(),zoom);
        // Ctrl+wheel zooms (in on wheel-up, out on wheel-down) and leaves the vertical position alone.
        wheel(QPoint(0,120),Qt::ControlModifier);
        QVERIFY(timeline->property("pixelsPerSecond").toDouble()>zoom);
        wheel(QPoint(0,-120),Qt::ControlModifier);
        QVERIFY(qAbs(timeline->property("pixelsPerSecond").toDouble()-zoom)<zoom*1e-9);
        QCOMPARE(timeline->property("verticalScrollPosition").toDouble(),0.0);
        controller.shutdownSession();
    }
    void pause_preserves_timeline_view_data() {
        QTest::addColumn<bool>("repeatBeyondClips");
        QTest::newRow("empty arrangement") << false;
        QTest::newRow("repeat beyond clips") << true;
    }
    void pause_preserves_timeline_view() {
        QFETCH(bool,repeatBeyondClips);
        auto device=std::make_unique<MultiInputUiDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); controller.setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller.attachProject(&tracks);
        if(repeatBeyondClips) {
            tracks.addClipAtBeat(tracks.addAudioTrack(),0,4);
            QVERIFY(controller.setRepeatRange(40,60)); controller.transportAction("repeat");
        }
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        const auto restore=qScopeGuard([&] {
            timeline->setVisible(false); timeline->setProperty("trackModel",QVariant());
            timeline->setProperty("transport",QVariant::fromValue(EngineController::instance()));
        });
        timeline->setProperty("transport",QVariant::fromValue(&controller));
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true);
        auto* scroll=visualItem(timeline,"timelineScroll"); auto* line=visualItem(timeline,"timelinePlayhead");
        QVERIFY(scroll); QVERIFY(line);
        controller.seekBeat(40); controller.transportAction("play"); io->process();
        QTRY_VERIFY(scroll->property("contentX").toDouble()>0);
        const double viewport=scroll->property("contentX").toDouble();
        const double width=scroll->property("contentWidth").toDouble();
        const double screenX=line->mapToScene(QPointF()).x(),position=controller.positionSeconds();
        controller.transportAction("pause"); io->process();
        QTRY_VERIFY(!timeline->property("playing").toBool()); QTest::qWait(50);
        QCOMPARE(controller.positionSeconds(),position);
        QCOMPARE(scroll->property("contentX").toDouble(),viewport);
        QCOMPARE(scroll->property("contentWidth").toDouble(),width);
        QCOMPARE(line->mapToScene(QPointF()).x(),screenX);
        controller.transportAction("play"); io->process();
        QVERIFY(controller.positionSeconds()>position);
        controller.transportAction("stop"); io->process();
        QTRY_COMPARE(scroll->property("contentX").toDouble(),0.0);
        controller.shutdownSession();
    }
    void normal_drag_selects_grid_range_and_repeat_stays_inside_it() {
        auto device=std::make_unique<MultiInputUiDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); controller.setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller.attachProject(&tracks); const int track=tracks.addAudioTrack();
        tracks.addClipAtBeat(track,0,8);
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        const auto restore=qScopeGuard([&] {
            timeline->setVisible(false); timeline->setProperty("trackModel",QVariant());
            timeline->setProperty("transport",QVariant::fromValue(EngineController::instance()));
            window->setProperty("transport",QVariant::fromValue(EngineController::instance()));
        });
        timeline->setProperty("transport",QVariant::fromValue(&controller));
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true);
        window->setProperty("transport",QVariant::fromValue(&controller)); QTest::qWait(30);
        const auto range=[&](int a,int b) {
            const QPoint from=timeline->mapToScene(QPointF(a,300)).toPoint(),to=timeline->mapToScene(QPointF(b,300)).toPoint();
            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,from);
            QTest::mouseMove(window,to); QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,to);
        };
        range(65,175);
        QCOMPARE(timeline->property("rangeStartBeat").toDouble(),1.0);
        QCOMPARE(timeline->property("rangeEndBeat").toDouble(),4.0);
        QCOMPARE(selectedIds(timeline).size(),0);
        auto* repeat=visualItem(window->contentItem(),"transport_repeat"); QVERIFY(repeat); click(repeat);
        QVERIFY(controller.repeating()); controller.transportAction("play");
        io->process(); QCOMPARE(controller.positionSeconds(),128.0/48000); // Play the lead-in from zero.
        bool wrapped=false; double previous=controller.positionBeats();
        for(int i=0;i<1500;++i) {
            io->process(); const double beat=controller.positionBeats();
            if(beat<previous)wrapped=true;
            QVERIFY(beat<=4); if(wrapped)QVERIFY(beat>=1);
            previous=beat;
        }
        QVERIFY(wrapped);
        QVERIFY(!controller.setRepeatRange(4,1)); QVERIFY(!controller.setRepeatRange(-1,3));
        range(310,210); // Reverse drag updates an active loop.
        QCOMPARE(timeline->property("rangeStartBeat").toDouble(),4.0);
        QCOMPARE(timeline->property("rangeEndBeat").toDouble(),7.0);
        wrapped=false; previous=controller.positionBeats();
        for(int i=0;i<1500;++i) {
            io->process(); const double beat=controller.positionBeats();
            if(beat<previous)wrapped=true;
            QVERIFY(beat<=7); if(wrapped)QVERIFY(beat>=4);
            previous=beat;
        }
        QVERIFY(wrapped);
        // A plain click clears the range; Repeat falls back to the full project.
        const QPoint clear=timeline->mapToScene(QPointF(400,300)).toPoint();
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,clear);
        QCOMPARE(timeline->property("rangeEndBeat").toDouble(),0.0);
        controller.seekBeat(0); io->process(); QVERIFY(controller.positionBeats()<1);
        controller.shutdownSession();
    }
    void rectangular_selection_moves_trims_and_deletes_across_tracks() {
        TrackListWrapper tracks; const int a=tracks.addAudioTrack(),b=tracks.addAudioTrack(),c=tracks.addAudioTrack();
        const int x=tracks.addClipAtBeat(a,2,4),y=tracks.addClipAtBeat(b,3,4),z=tracks.addClipAtBeat(c,2,4);
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        const auto restore=qScopeGuard([&] { timeline->setVisible(false); timeline->setProperty("trackModel",QVariant()); });
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true);
        QTest::qWait(30);
        const QPoint from=timeline->mapToScene(QPointF(120,70)).toPoint(),to=timeline->mapToScene(QPointF(300,235)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::ShiftModifier,from);
        QTest::mouseMove(window,to);
        auto* marquee=visualItem(timeline,"timelineSelection"); QVERIFY(marquee); QVERIFY(marquee->isVisible());
        QTest::mouseRelease(window,Qt::LeftButton,Qt::ShiftModifier,to);
        QVERIFY(!marquee->isVisible());
        QCOMPARE(selectedIds(timeline).size(),2);
        auto* block=visualItem(timeline,"clipBlock"+QString::number(x)); QVERIFY(block);
        const QPoint start=block->mapToScene(QPointF(30,30)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        QTest::mouseMove(window,start+QPoint(96,0)); QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,start+QPoint(96,0));
        QCOMPARE(tracks.project().findTrack(a)->audioClips[0].start,qint64(96000));
        QCOMPARE(tracks.project().findTrack(b)->audioClips[0].start,qint64(120000));
        QCOMPARE(tracks.project().findTrack(c)->audioClips[0].start,qint64(48000));
        QVERIFY(tracks.undo());
        auto* grip=visualItem(timeline,"clipRightTrim"+QString::number(x)); QVERIFY(grip);
        const QPoint edge=grip->mapToScene(QPointF(4,20)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,edge);
        QTest::mouseMove(window,edge-QPoint(48,0)); QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,edge-QPoint(48,0));
        QCOMPARE(tracks.project().findTrack(a)->audioClips[0].length,qint64(72000));
        QCOMPARE(tracks.project().findTrack(b)->audioClips[0].length,qint64(72000));
        QVERIFY(tracks.undo());
        QTest::keyClick(window,Qt::Key_Delete);
        QCOMPARE(tracks.clips().size(),1); QCOMPARE(tracks.project().findTrack(c)->audioClips[0].id,z);
        QVERIFY(tracks.undo()); QCOMPARE(tracks.clips().size(),3);
        QCOMPARE(tracks.project().findTrack(a)->audioClips[0].id,x); QCOMPARE(tracks.project().findTrack(b)->audioClips[0].id,y);
    }
    void multiple_clip_selection_and_merge_context_action() {
        TrackListWrapper tracks; const int a=tracks.addAudioTrack(),b=tracks.addAudioTrack();
        const int left=tracks.addClipAtBeat(a,0,4),right=tracks.splitClipAtBeat(a,left,2);
        const int other=tracks.addClipAtBeat(b,0,4);
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        const auto restore=qScopeGuard([&] { timeline->setVisible(false); timeline->setProperty("trackModel",QVariant()); });
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true);
        QTest::qWait(30);
        auto* first=visualItem(timeline,"clipBlock"+QString::number(left));
        auto* second=visualItem(timeline,"clipBlock"+QString::number(right)); QVERIFY(first); QVERIFY(second);
        click(first);
        const QPoint secondPoint=second->mapToScene(QPointF(second->width()/2,second->height()/2)).toPoint();
        QTest::mouseClick(window,Qt::LeftButton,Qt::ControlModifier,secondPoint);
        QCOMPARE(selectedIds(timeline).size(),2);
        QVERIFY(timeline->property("canMergeSelection").toBool());
        QTest::mouseClick(window,Qt::RightButton,Qt::NoModifier,first->mapToScene(QPointF(30,30)).toPoint());
        auto* merge=visualItem(window->contentItem(),"mergeSelectedClips"); QVERIFY(merge); QVERIFY(merge->isEnabled());
        click(merge); QTRY_COMPARE(tracks.clips().size(),2);
        QCOMPARE(tracks.project().findTrack(a)->audioClips[0].length,qint64(96000));
        QVERIFY(tracks.undo()); QTRY_COMPARE(tracks.clips().size(),3);
        auto* otherBlock=visualItem(timeline,"clipBlock"+QString::number(other)); QVERIFY(otherBlock);
        QTest::mouseClick(window,Qt::LeftButton,Qt::ControlModifier,otherBlock->mapToScene(QPointF(30,30)).toPoint());
        QVERIFY(!timeline->property("canMergeSelection").toBool());
        const QPoint start=timeline->mapToScene(QPointF(220,40)).toPoint();
        const QPoint end=timeline->mapToScene(QPointF(20,300)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::ShiftModifier,start);
        QTest::mouseMove(window,end); QTest::mouseRelease(window,Qt::LeftButton,Qt::ShiftModifier,end);
        QCOMPARE(selectedIds(timeline).size(),3);
    }
    void volume_envelope_lane_edits_points_bypasses_and_keeps_rows_aligned() {
        TrackListWrapper tracks; const int a=tracks.addAudioTrack(),b=tracks.addAudioTrack();
        const int clipB=tracks.addClipAtBeat(b,0,2);
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        auto* trackList=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(trackList);
        const auto restore=qScopeGuard([&] {
            timeline->setVisible(false); timeline->setProperty("trackModel",QVariant());
            trackList->setVisible(false); trackList->setProperty("trackModel",QVariant());
        });
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true);
        QTest::qWait(20);
        const auto& envelope=tracks.project().findTrack(a)->volumeEnvelope;
        auto* block=visualItem(timeline,"clipBlock"+QString::number(clipB)); QVERIFY(block);
        QCOMPARE(block->y(),38.0+106+8);

        // V shows the selected track's lane under it; the rows below move down.
        QVERIFY(tracks.selectTrack(a)); timeline->forceActiveFocus();
        const bool undoBeforeV=tracks.canUndo(),redoBeforeV=tracks.canRedo();
        QTest::keyClick(window,Qt::Key_V);
        QTRY_VERIFY(envelope.shown());
        auto* lane=visualItem(timeline,"volumeEnvelope"+QString::number(a)); QVERIFY(lane); QVERIFY(lane->isVisible());
        QCOMPARE(lane->y(),38.0+106);
        QCOMPARE(lane->height(),72.0);
        QTRY_COMPARE(block->y(),38.0+106+72+8);
        // The new lane starts with one point at the start, on the 0 dB line.
        QCOMPARE(envelope.points().size(),std::size_t(1));
        QCOMPARE(envelope.points()[0].position,qint64(0));
        auto* startPoint=visualItem(timeline,"envelopePoint"+QString::number(a)+"_0"); QVERIFY(startPoint);
        QCOMPARE(startPoint->x()+startPoint->width()/2,0.0);
        QVERIFY(std::abs(startPoint->y()+startPoint->height()/2-(1-tracks.envelopeUnityLevel())*72)<0.5);
        QCOMPARE(tracks.canUndo(),undoBeforeV); QCOMPARE(tracks.canRedo(),redoBeforeV);   // no undo step of its own

        // Double-click: a point at the mouse (2 beats in, half way up the lane).
        const double beatWidth=timeline->property("beatWidth").toDouble();
        auto* area=visualItem(timeline,"envelopeArea"+QString::number(a)); QVERIFY(area);
        QTest::mouseDClick(window,Qt::LeftButton,Qt::NoModifier,area->mapToScene(QPointF(2*beatWidth,36)).toPoint());
        QTRY_COMPARE(envelope.points().size(),std::size_t(2));
        QVERIFY(std::abs(envelope.points()[1].position-48000)<=600);
        QVERIFY(std::abs(envelope.points()[1].level-0.5)<0.03);

        // Drag it right and up: one undo step.
        auto* point=visualItem(timeline,"envelopePoint"+QString::number(a)+"_1"); QVERIFY(point);
        const QPoint from=point->mapToScene(QPointF(point->width()/2,point->height()/2)).toPoint();
        const QPoint to=from+QPoint(int(beatWidth),-18);
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,from);
        QTest::mouseMove(window,from+QPoint(int(beatWidth)/2,-9));
        QTest::mouseMove(window,to);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,to);
        QVERIFY(std::abs(envelope.points()[1].position-72000)<=1200);
        QVERIFY(std::abs(envelope.points()[1].level-0.75)<0.03);
        QVERIFY(tracks.undo());
        QVERIFY(std::abs(envelope.points()[1].position-48000)<=600);
        QVERIFY(tracks.redo());

        // Right-click a point: deleted (undoable).
        point=visualItem(timeline,"envelopePoint"+QString::number(a)+"_1"); QVERIFY(point);
        QTest::mouseClick(window,Qt::RightButton,Qt::NoModifier,point->mapToScene(QPointF(point->width()/2,point->height()/2)).toPoint());
        QTRY_COMPARE(envelope.points().size(),std::size_t(1));
        QVERIFY(tracks.undo()); QCOMPARE(envelope.points().size(),std::size_t(2));

        // Shift+right-click a point: its curve shape menu (restMap G6b).
        point=visualItem(timeline,"envelopePoint"+QString::number(a)+"_0"); QVERIFY(point);
        const QPoint startCentre=point->mapToScene(QPointF(point->width()/2,point->height()/2)).toPoint();
        QTest::mouseClick(window,Qt::RightButton,Qt::ShiftModifier,startCentre);
        QCOMPARE(envelope.points().size(),std::size_t(2));   // not deleted
        auto* linear=visualItem(window->contentItem(),"envelopeShape_linear"); QVERIFY(linear);
        QTRY_VERIFY(linear->isVisible());
        QVERIFY(linear->property("checked").toBool()); QVERIFY(linear->isEnabled());
        auto* smooth=visualItem(window->contentItem(),"envelopeShape_smooth"); QVERIFY(smooth);
        QVERIFY(!smooth->property("checked").toBool());
        QTest::qWait(300);   // as below: the menu finishes opening first
        click(smooth);
        QTRY_VERIFY(envelope.points()[0].shape==automation::CurveShape::Smooth);
        QTRY_VERIFY(!linear->isVisible());
        QTest::mouseClick(window,Qt::RightButton,Qt::ShiftModifier,startCentre);
        QTRY_VERIFY(smooth->isVisible());
        QVERIFY(smooth->property("checked").toBool()); QVERIFY(!linear->property("checked").toBool());
        QTest::keyClick(window,Qt::Key_Escape); QTRY_VERIFY(!smooth->isVisible());
        QVERIFY(tracks.undo()); QVERIFY(envelope.points()[0].shape==automation::CurveShape::Linear);
        QVERIFY(tracks.redo());
        // The last point has no curve after it: shapes greyed out; Delete point works.
        point=visualItem(timeline,"envelopePoint"+QString::number(a)+"_1"); QVERIFY(point);
        QTest::mouseClick(window,Qt::RightButton,Qt::ShiftModifier,point->mapToScene(QPointF(point->width()/2,point->height()/2)).toPoint());
        QTRY_VERIFY(smooth->isVisible()); QVERIFY(!smooth->isEnabled());
        auto* deletePoint=visualItem(window->contentItem(),"envelopePointDeleteAction"); QVERIFY(deletePoint);
        QTest::qWait(300);   // let the menu finish opening: a click straight away is sometimes lost
        click(deletePoint);
        QTRY_COMPARE(envelope.points().size(),std::size_t(1));
        QVERIFY(tracks.undo()); QCOMPARE(envelope.points().size(),std::size_t(2));

        // The menu: bypass, then over the clips (the lane goes away, the rows close up).
        QTest::mouseClick(window,Qt::RightButton,Qt::NoModifier,area->mapToScene(QPointF(5*beatWidth,60)).toPoint());
        auto* bypass=visualItem(window->contentItem(),"envelopeBypassAction"); QVERIFY(bypass);
        QTRY_VERIFY(bypass->isVisible()); click(bypass);
        QTRY_VERIFY(envelope.bypassed());
        QVERIFY(tracks.undo()); QVERIFY(!envelope.bypassed());
        QTest::mouseClick(window,Qt::RightButton,Qt::NoModifier,area->mapToScene(QPointF(5*beatWidth,60)).toPoint());
        auto* placement=visualItem(window->contentItem(),"envelopePlacementAction"); QVERIFY(placement);
        QTRY_VERIFY(placement->isVisible()); click(placement);
        QTRY_VERIFY(envelope.overClips());
        QTRY_COMPARE(block->y(),38.0+106+8);
        QCOMPARE(lane->y(),38.0+8);
        QVERIFY(envelope.points().size()==2);   // the view change kept the points and was not an undo step

        // Back in its own lane: the track list leaves the same room, with the Volume header.
        QVERIFY(tracks.setEnvelopeOverClips(a,false));
        timeline->setVisible(false);
        trackList->setProperty("trackModel",QVariant::fromValue(&tracks)); trackList->setVisible(true);
        QTest::qWait(20);
        auto* header=visualItem(trackList,"envelopeLaneHeader"+QString::number(a)); QVERIFY(header);
        QVERIFY(header->isVisible()); QCOMPARE(header->height(),72.0);
        auto* card=visualItem(trackList,"trackColourAccent"+QString::number(b)); QVERIFY(card);
        QCOMPARE(card->parentItem()->y(),106.0+72);
        auto* bypassChip=visualItem(trackList,"envelopeBypassButton"+QString::number(a)); QVERIFY(bypassChip);
        click(bypassChip); QTRY_VERIFY(envelope.bypassed());
        QVERIFY(tracks.toggleEnvelopeShown(a));
        QTRY_VERIFY(!header->isVisible());
        QTRY_COMPARE(card->parentItem()->y(),106.0);
    }
    void undo_redo_buttons_restore_deleted_clips() {
        TrackListWrapper tracks;
        auto* top=window->findChild<QObject*>("testTopBar"); QVERIFY(top);
        top->setProperty("trackModel",QVariant::fromValue(&tracks));
        const int track=tracks.addAudioTrack(); const int clip=tracks.addClipAtBeat(track,0,4);
        QVERIFY(tracks.removeAudioClip(track,clip));
        // Use the real Edit > Undo / Redo items (a mouse click cannot hit the 1x1 test hooks).
        QVERIFY(!visualItem(window->contentItem(),"fileUndoItem")); // moved out of the File menu
        chooseEditItem("editUndoItem"); QCOMPARE(tracks.clips().size(),1);
        chooseEditItem("editRedoItem"); QCOMPARE(tracks.clips().size(),0);
        top->setProperty("trackModel",QVariant());
    }
    void view_menu_ticks_follow_the_panels_and_requests_toggles() {
        auto* top=window->findChild<QObject*>("testTopBar"); QVERIFY(top);
        const auto restore=qScopeGuard([&] {
            top->setProperty("mixerShown",true); top->setProperty("pianoRollShown",false);
            top->setProperty("pianoRollAvailable",false);
        });
        QSignalSpy mixerToggles(top,SIGNAL(toggleMixerRequested())); QVERIFY(mixerToggles.isValid());
        QSignalSpy pianoToggles(top,SIGNAL(togglePianoRollRequested())); QVERIFY(pianoToggles.isValid());
        auto* button=visualItem(window->contentItem(),"viewMenuButton"); QVERIFY(button);
        auto* menu=window->findChild<QObject*>("viewMenu"); QVERIFY(menu);
        const auto openMenu=[&] { click(button); QTRY_VERIFY(menu->property("visible").toBool()); };
        const auto entry=[&](const char* name) { auto* item=visualItem(window->contentItem(),name); return item; };

        openMenu();
        QVERIFY(entry("viewMixerItem")); QVERIFY(entry("viewPianoRollItem"));
        QVERIFY(entry("viewMixerItem")->property("checked").toBool());
        QVERIFY(!entry("viewPianoRollItem")->property("checked").toBool());
        QVERIFY(!entry("viewPianoRollItem")->isEnabled()); // no clip opened yet
        click(entry("viewMixerItem")); QTRY_VERIFY(!menu->property("visible").toBool());
        QCOMPARE(mixerToggles.count(),1); QCOMPARE(pianoToggles.count(),0);

        // The window decides; the tick follows its state, also right after a click.
        top->setProperty("mixerShown",false); top->setProperty("pianoRollAvailable",true);
        openMenu();
        QVERIFY(!entry("viewMixerItem")->property("checked").toBool());
        QVERIFY(entry("viewPianoRollItem")->isEnabled());
        click(entry("viewPianoRollItem")); QTRY_VERIFY(!menu->property("visible").toBool());
        QCOMPARE(pianoToggles.count(),1);
        top->setProperty("pianoRollShown",true);
        openMenu();
        QVERIFY(entry("viewPianoRollItem")->property("checked").toBool());
        QVERIFY(!entry("viewMixerItem")->property("checked").toBool());
        QMetaObject::invokeMethod(menu,"close"); QTRY_VERIFY(!menu->property("visible").toBool());
    }
    void panel_tabs_light_only_the_shown_panel_and_request_toggles() {
        auto* top=window->findChild<QObject*>("testTopBar"); QVERIFY(top);
        const auto restore=qScopeGuard([&] {
            top->setProperty("mixerShown",true); top->setProperty("pianoRollShown",false);
            top->setProperty("pianoRollAvailable",false);
        });
        QSignalSpy mixerToggles(top,SIGNAL(toggleMixerRequested())); QVERIFY(mixerToggles.isValid());
        QSignalSpy pianoToggles(top,SIGNAL(togglePianoRollRequested())); QVERIFY(pianoToggles.isValid());
        QVERIFY(!visualItem(window->contentItem(),"toggleMixerButton")); // replaced by the tabs
        auto* mixerTab=visualItem(window->contentItem(),"mixerTab"); QVERIFY(mixerTab);
        auto* pianoTab=visualItem(window->contentItem(),"pianoRollTab"); QVERIFY(pianoTab);
        // Mixer on screen, no clip opened yet.
        QVERIFY(mixerTab->property("shown").toBool()); QVERIFY(!pianoTab->property("shown").toBool());
        QVERIFY(!pianoTab->isEnabled());
        click(pianoTab); QCOMPARE(pianoToggles.count(),0);
        click(mixerTab); QCOMPARE(mixerToggles.count(),1);
        // Docked piano roll on screen: only the Piano Roll part is lit (the old button stayed lit).
        top->setProperty("mixerShown",false); top->setProperty("pianoRollShown",true); top->setProperty("pianoRollAvailable",true);
        QVERIFY(!mixerTab->property("shown").toBool()); QVERIFY(pianoTab->property("shown").toBool());
        QVERIFY(pianoTab->isEnabled());
        click(pianoTab); QCOMPARE(pianoToggles.count(),1);
        click(mixerTab); QCOMPARE(mixerToggles.count(),2);
        // Bottom panel hidden: nothing lit.
        top->setProperty("pianoRollShown",false);
        QVERIFY(!mixerTab->property("shown").toBool()); QVERIFY(!pianoTab->property("shown").toBool());
    }
    void edit_menu_cuts_copies_pastes_duplicates_and_deletes_timeline_clips() {
        auto device=std::make_unique<MultiInputUiDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); controller.setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller.attachProject(&tracks);
        const int track=tracks.addAudioTrack(); tracks.addClipAtBeat(track,0,4);
        auto* top=window->findChild<QObject*>("testTopBar"); QVERIFY(top);
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        const auto restore=qScopeGuard([&] {
            timeline->setVisible(false); timeline->setProperty("trackModel",QVariant());
            timeline->setProperty("transport",QVariant::fromValue(EngineController::instance()));
            top->setProperty("trackModel",QVariant());
        });
        timeline->setProperty("transport",QVariant::fromValue(&controller));
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true);
        top->setProperty("trackModel",QVariant::fromValue(&tracks));
        // Nothing selected: only Select All (and nothing to paste yet).
        auto* editButton=visualItem(window->contentItem(),"editMenuButton"); QVERIFY(editButton);
        auto* editMenu=window->findChild<QObject*>("editMenu"); QVERIFY(editMenu);
        click(editButton); QTRY_VERIFY(editMenu->property("visible").toBool());
        for(const char* name:{"editCutItem","editCopyItem","editPasteItem","editDuplicateItem","editDeleteItem"}) {
            auto* item=visualItem(window->contentItem(),name); QVERIFY2(item,name); QVERIFY2(!item->isEnabled(),name);
        }
        auto* selectAll=visualItem(window->contentItem(),"editSelectAllItem"); QVERIFY(selectAll); QVERIFY(selectAll->isEnabled());
        QMetaObject::invokeMethod(editMenu,"close"); QTRY_VERIFY(!editMenu->property("visible").toBool());

        chooseEditItem("editSelectAllItem"); QCOMPARE(selectedIds(timeline).size(),1);
        QVERIFY(timeline->hasActiveFocus()); // keyboard shortcuts keep working after a menu action
        chooseEditItem("editCopyItem"); QVERIFY(tracks.hasClipboard()); QCOMPARE(tracks.clips().size(),1);
        controller.seekBeat(8); io->process();
        chooseEditItem("editPasteItem"); QCOMPARE(tracks.clips().size(),2);
        // The copy lands at the playhead (beat 8 at 120 BPM and 48 kHz = 192000 samples).
        const auto& pasted=tracks.project().findTrack(track)->audioClips;
        QVERIFY(std::any_of(pasted.begin(),pasted.end(),[](const auto& c) { return c.start==qint64(192000); }));
        chooseEditItem("editDuplicateItem"); QCOMPARE(tracks.clips().size(),3);
        chooseEditItem("editDeleteItem"); QCOMPARE(tracks.clips().size(),2);
        chooseEditItem("editSelectAllItem"); QCOMPARE(selectedIds(timeline).size(),2);
        chooseEditItem("editCutItem"); QCOMPARE(tracks.clips().size(),0); QCOMPARE(tracks.clipboardClipCount(),2);
        chooseEditItem("editUndoItem"); QCOMPARE(tracks.clips().size(),2);
        controller.shutdownSession();
    }
    void timeline_bpm_edit_updates_grid_and_preserves_recorded_audio_timing() {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller->attachProject(&tracks); controller->setMeter(4,4);
        const int track=tracks.addAudioTrack();
        auto audio=std::make_shared<AudioData>(); audio->path="tempo-wave.wav"; audio->frames=96000;
        audio->channels={std::vector<float>(96000,0.25f)}; audio->buildWaveform();
        QVERIFY(tracks.attachTake(track,48000,audio));
        const auto original=tracks.project().findTrack(track)->audioClips[0];
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true);
        QCoreApplication::processEvents();
        const QString blockName="clipBlock"+QString::number(original.id);
        auto* block=visualItem(window->contentItem(),blockName); QVERIFY(block);
        const double originalX=block->x(), originalWidth=block->width();
        QCOMPARE(timeline->property("barWidth").toDouble(),192.0);
        click(field); QTest::keyClick(window,Qt::Key_6); QTest::keyClick(window,Qt::Key_0); QTest::keyClick(window,Qt::Key_Return);
        QCOMPARE(controller->bpm(),60.0);
        QCOMPARE(timeline->property("barWidth").toDouble(),384.0);
        QCoreApplication::processEvents();
        block=visualItem(window->contentItem(),blockName); QVERIFY(block);
        QCOMPARE(block->x(),originalX); QCOMPARE(block->width(),originalWidth);
        QCOMPARE(tracks.clips()[0].toMap()["startBeat"].toDouble(),1.0);
        QCOMPARE(tracks.clips()[0].toMap()["lengthBeats"].toDouble(),2.0);
        const auto& unchanged=tracks.project().findTrack(track)->audioClips[0];
        QCOMPARE(unchanged.start,original.start); QCOMPARE(unchanged.length,original.length);
        QCOMPARE(unchanged.sourceOffset,original.sourceOffset);
        QCOMPARE(tracks.playbackSnapshot()->keepAudio[0].get(),audio.get());
        // At 60 BPM a 105-pixel drag snaps to one second, not the old half-second beat.
        const QPoint start=block->mapToScene(QPointF(30,30)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        QTest::mouseMove(window,start+QPoint(105,0));
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,start+QPoint(105,0));
        QCOMPARE(tracks.project().findTrack(track)->audioClips[0].start,qint64(96000));
        QVERIFY(tracks.undo()); QCOMPARE(tracks.project().findTrack(track)->audioClips[0].start,original.start);
        timeline->setVisible(false); timeline->setProperty("trackModel",QVariant());
    }
    void timeline_tempo_grid_updates_after_zoom_with_decimal_bpm_and_meter() {
        auto* controller=EngineController::instance(); controller->setMeter(4,4);
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        // Exercise the same zoom handler as the mouse wheel before changing tempo.
        QVERIFY(QMetaObject::invokeMethod(timeline,"zoomAtCursor",Q_ARG(QVariant,2.0),Q_ARG(QVariant,0.0)));
        QCOMPARE(timeline->property("beatWidth").toDouble(),96.0);
        controller->setBpm(137.5);
        QVERIFY(std::abs(timeline->property("beatWidth").toDouble()-192.0*60/137.5)<1e-8);
        auto* plus=window->findChild<QQuickItem*>("bpmIncrease"); QVERIFY(plus); click(plus);
        QCOMPARE(controller->bpm(),138.5);
        QVERIFY(std::abs(timeline->property("barWidth").toDouble()-4*192.0*60/138.5)<1e-8);
        controller->setMeter(7,8);
        QVERIFY(std::abs(timeline->property("barWidth").toDouble()-3.5*192.0*60/138.5)<1e-8);
        for(double bpm : {0.01,10000.0}) {
            controller->setBpm(bpm);
            QVERIFY(std::isfinite(timeline->property("barWidth").toDouble()));
            QVERIFY(timeline->property("barWidth").toDouble()>0);
            QVERIFY(timeline->property("visibleBeatCount").toInt()<150);
        }
        QVERIFY(QMetaObject::invokeMethod(timeline,"zoomAtCursor",Q_ARG(QVariant,0.5),Q_ARG(QVariant,0.0)));
        controller->setMeter(4,4); controller->setBpm(120);
        QCOMPARE(timeline->property("beatWidth").toDouble(),48.0);
    }
    void timeline_drag_selects_snaps_and_undoes_as_one_edit() {
        TrackListWrapper tracks; const int track=tracks.addAudioTrack(); const int clip=tracks.addClipAtBeat(track,1,4);
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true);
        QCoreApplication::processEvents();
        auto* block=visualItem(window->contentItem(),"clipBlock"+QString::number(clip)); QVERIFY(block);
        const QPoint start=block->mapToScene(QPointF(30,30)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        QTest::mouseMove(window,start+QPoint(105,0));
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,start+QPoint(105,0));
        QCOMPARE(timeline->property("selectedClipId").toInt(),clip);
        QCOMPARE(tracks.project().findTrack(track)->audioClips[0].start,qint64(72000));
        QVERIFY(tracks.undo()); QCOMPARE(tracks.project().findTrack(track)->audioClips[0].start,qint64(24000));
        timeline->setVisible(false); timeline->setProperty("trackModel",QVariant());
    }
    void recorded_block_displays_waveform_peaks() {
        TrackListWrapper tracks; const int track=tracks.addAudioTrack();
        auto audio=std::make_shared<AudioData>(); audio->path="ui-wave.wav"; audio->frames=48000;
        audio->channels={std::vector<float>(48000)};
        for(int i=0;i<48000;++i)audio->channels[0][i]=float(std::sin(i*0.07)*0.7);
        audio->buildWaveform(); QVERIFY(tracks.attachTake(track,0,audio));
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true);
        QCoreApplication::processEvents();
        const int clip=tracks.project().findTrack(track)->audioClips[0].id;
        auto* waveform=visualItem(window->contentItem(),"clipWaveform"+QString::number(clip)); QVERIFY(waveform);
        QVERIFY(waveform->width()>0); QVERIFY(waveform->height()>0);
        QVERIFY(QMetaObject::invokeMethod(waveform,"requestPaint")); QTest::qWait(30);
        QVERIFY(window->grabWindow().save("/tmp/projectdaw-waveform.png"));
        timeline->setVisible(false); timeline->setProperty("trackModel",QVariant());
    }
    void scrolling_a_long_clip_reuses_its_waveform_and_keeps_it_covering_the_view() {
        TrackListWrapper tracks; const int track=tracks.addAudioTrack();
        auto audio=std::make_shared<AudioData>(); audio->path="ui-long-wave.wav"; audio->frames=48000*60;
        audio->channels={std::vector<float>(audio->frames)};
        for(qint64 i=0;i<audio->frames;++i)audio->channels[0][i]=float(std::sin(i*0.01)*0.7);
        audio->buildWaveform(); QVERIFY(tracks.attachTake(track,0,audio));
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        const auto restore=qScopeGuard([&] { timeline->setVisible(false); timeline->setProperty("trackModel",QVariant()); });
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true);
        QCoreApplication::processEvents();
        const int clip=tracks.project().findTrack(track)->audioClips[0].id;
        auto* scroll=visualItem(timeline,"timelineScroll"); QVERIFY(scroll);
        auto* waveform=visualItem(timeline,"clipWaveform"+QString::number(clip)); QVERIFY(waveform);
        auto* block=waveform->parentItem(); QVERIFY(block);
        QVERIFY2(block->width()>scroll->width()*3,qPrintable(QString("clip only %1 px").arg(block->width())));
        const auto covered=[&] {
            const QRectF view=scroll->mapRectToScene(QRectF(0,0,scroll->width(),scroll->height()));
            const QRectF clipRect=block->mapRectToScene(QRectF(0,0,block->width(),block->height()));
            const QRectF wave=waveform->mapRectToScene(QRectF(0,0,waveform->width(),waveform->height()));
            const qreal left=std::max(view.left(),clipRect.left()), right=std::min(view.right(),clipRect.right());
            return wave.left()<=left+0.5 && wave.right()>=right-0.5;
        };
        // Plan 5.3: scrolling 300 px one pixel at a time fetches new peaks at most once,
        // and the waveform always spans the visible part of the clip.
        scroll->setProperty("contentX",200); QCoreApplication::processEvents(); QVERIFY(covered());
        QSignalSpy fetched(waveform,SIGNAL(peaksChanged()));
        for(int x=201;x<=500;++x) {
            scroll->setProperty("contentX",x); QCoreApplication::processEvents();
            QVERIFY2(covered(),qPrintable(QString("gap at contentX %1").arg(x)));
        }
        QVERIFY2(fetched.count()<=1,qPrintable(QString("%1 waveform fetches").arg(fetched.count())));
        // Far along the clip, still covered, still one bin per pixel.
        scroll->setProperty("contentX",block->width()-scroll->width()/2); QCoreApplication::processEvents();
        QVERIFY(covered());
        QCOMPARE(waveform->property("peaks").toList().size(),qsizetype(std::ceil(waveform->width())));
    }
    void timeline_left_and_right_handles_trim_without_moving_source() {
        TrackListWrapper tracks; const int track=tracks.addAudioTrack(); const int clip=tracks.addClipAtBeat(track,1,4);
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true); QCoreApplication::processEvents();
        auto drag=[&](const QString& name,int delta) {
            auto* handle=visualItem(window->contentItem(),name+QString::number(clip)); QVERIFY(handle);
            const QPoint start=handle->mapToScene(QPointF(4,40)).toPoint();
            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start); QTest::mouseMove(window,start+QPoint(delta,0));
            QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,start+QPoint(delta,0)); QCoreApplication::processEvents();
        };
        drag("clipLeftTrim",48); auto c=tracks.project().findTrack(track)->audioClips[0];
        QCOMPARE(c.start,qint64(48000)); QCOMPARE(c.length,qint64(72000)); QCOMPARE(c.sourceOffset,qint64(24000));
        drag("clipRightTrim",-48); QCOMPARE(tracks.project().findTrack(track)->audioClips[0].length,qint64(48000));
        QVERIFY(tracks.undo()); QVERIFY(tracks.undo()); QCOMPARE(tracks.project().findTrack(track)->audioClips[0].start,qint64(24000));
        timeline->setVisible(false); timeline->setProperty("trackModel",QVariant());
    }
    void monitor_buttons_are_independent_of_record_arm_and_capture() {
        auto device=std::make_unique<MultiInputUiDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); controller.setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller.attachProject(&tracks);
        const int first=tracks.addAudioTrack(),second=tracks.addVocalTrack();
        QVERIFY(tracks.setTrackInputPort(second,"test:mic"));
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        const auto restore=qScopeGuard([&] { list->setVisible(false); list->setProperty("trackModel",QVariant()); });
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true);
        QTest::qWait(30);
        auto* arm=visualItem(list,"trackRecordButton"+QString::number(first));
        auto* monitorA=visualItem(list,"trackMonitorButton"+QString::number(first));
        auto* monitorB=visualItem(list,"trackMonitorButton"+QString::number(second));
        QVERIFY(arm); QVERIFY(monitorA); QVERIFY(monitorB);
        click(arm); QVERIFY(tracks.project().findTrack(first)->recordArmed);
        QVERIFY(!tracks.project().findTrack(first)->monitorEnabled);
        click(monitorB); QVERIFY(!tracks.project().findTrack(second)->recordArmed);
        io->inputA=0.125f; io->inputB=0.25f;
        for(int i=0;i<200;++i)io->process();
        QVERIFY(std::abs(io->outputSample-0.25f)<1e-5);
        controller.transportAction("record"); io->process();
        QVERIFY(controller.recording()); QCOMPARE(controller.recordingTracks().size(),1);
        QVERIFY(std::abs(io->outputSample-0.25f)<1e-5);
        click(monitorA); io->process();
        QVERIFY(std::abs(io->outputSample-0.375f)<1e-5);
        click(monitorB); io->process();
        QVERIFY(std::abs(io->outputSample-0.125f)<1e-5);
        click(monitorA); io->process();
        QCOMPARE(io->outputSample,0.0f); QVERIFY(controller.recording());
        QVERIFY(tracks.project().findTrack(first)->recordArmed);
        controller.transportAction("stop"); io->process(); QTRY_VERIFY(!controller.takeBusy());
        QCOMPARE(tracks.clips().size(),1);
        auto snapshot=tracks.playbackSnapshot(); QVERIFY(!snapshot->keepAudio.empty());
        QCOMPARE(snapshot->keepAudio.front()->channels[0][64],0.125f);
        click(monitorA); click(arm); io->process();
        QVERIFY(tracks.project().findTrack(first)->monitorEnabled);
        QVERIFY(!tracks.project().findTrack(first)->recordArmed);
        QVERIFY(std::abs(io->outputSample-0.125f)<1e-5);
        TrackListWrapper reopened; QString error;
        QVERIFY2(reopened.restoreSessionState(tracks.sessionState(),error),qPrintable(error));
        QVERIFY(!reopened.project().tracks()[0]->monitorEnabled);
        QVERIFY(!reopened.project().tracks()[1]->monitorEnabled);
        QVERIFY(!reopened.project().tracks()[0]->recordArmed);
        controller.shutdownSession();
    }
    void dragging_track_headers_reorders_mixer_lanes_and_saved_order() {
        TrackListWrapper tracks;
        QAbstractItemModelTester modelCheck(&tracks,QAbstractItemModelTester::FailureReportingMode::QtTest);
        const int first=tracks.addAudioTrack(),removed=tracks.addAudioTrack();
        QVERIFY(tracks.removeTrack(removed));
        const int second=tracks.addVocalTrack(),third=tracks.addAudioTrack();
        QVERIFY(tracks.setVolume(first,0.5)); QVERIFY(tracks.setMuted(second,true));
        const int clip=tracks.addClipAtBeat(first,0,4); QVERIFY(clip>=0);
        auto* list=window->findChild<QQuickItem*>("testTrackList");
        auto* mixer=window->findChild<QQuickItem*>("testMixer"); QVERIFY(list); QVERIFY(mixer);
        const auto restore=qScopeGuard([&] {
            list->setVisible(false); list->setProperty("trackModel",QVariant());
            mixer->setVisible(false); mixer->setProperty("trackModel",QVariant());
        });
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true);
        mixer->setProperty("trackModel",QVariant::fromValue(&tracks)); mixer->setVisible(true);
        QTest::qWait(30);
        auto* firstStrip=visualItem(mixer,"mixerStrip"+QString::number(first)); QVERIFY(firstStrip);
        auto* secondStrip=visualItem(mixer,"mixerStrip"+QString::number(second)); QVERIFY(secondStrip);
        auto* thirdStrip=visualItem(mixer,"mixerStrip"+QString::number(third)); QVERIFY(thirdStrip);
        QSignalSpy changes(&tracks,&TrackListWrapper::projectChanged);
        QSignalSpy countChanges(&tracks,&TrackListWrapper::countChanged);
        const auto drag=[&](int id,int rows) {
            auto* header=visualItem(list,"trackHeader"+QString::number(id)); QVERIFY(header);
            const QPoint start=header->mapToScene(QPointF(30,header->height()/2)).toPoint();
            const QPoint end=start+QPoint(0,rows*list->property("laneHeight").toInt());
            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
            QTest::mouseMove(window,end,20);
            QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,end);
        };
        drag(first,2);
        QCOMPARE(tracks.trackIdAt(0),second); QCOMPARE(tracks.trackIdAt(1),third); QCOMPARE(tracks.trackIdAt(2),first);
        QTRY_VERIFY(secondStrip->x()<thirdStrip->x() && thirdStrip->x()<firstStrip->x());
        QCOMPARE(tracks.clips().first().toMap()["laneIndex"].toInt(),2);
        QCOMPARE(changes.count(),1); QCOMPARE(countChanges.count(),0);
        drag(first,-2);
        QCOMPARE(tracks.trackIdAt(0),first); QCOMPARE(tracks.trackIdAt(1),second); QCOMPARE(tracks.trackIdAt(2),third);
        QTRY_VERIFY(firstStrip->x()<secondStrip->x() && secondStrip->x()<thirdStrip->x());
        QCOMPARE(tracks.clips().first().toMap()["laneIndex"].toInt(),0);
        QCOMPARE(tracks.project().findTrack(first)->volume,0.5f); QVERIFY(tracks.project().findTrack(second)->muted);
        QVERIFY(!tracks.moveTrack(-1,0)); QVERIFY(!tracks.moveTrack(first,-1)); QVERIFY(!tracks.moveTrack(first,tracks.count()));
        QVERIFY(tracks.moveTrack(first,0)); QCOMPARE(changes.count(),2);
        drag(third,-1);
        TrackListWrapper reopened; QString error;
        QVERIFY2(reopened.restoreSessionState(tracks.sessionState(),error),qPrintable(error));
        for(int row=0;row<tracks.count();++row)
            QCOMPARE(reopened.data(reopened.index(row),TrackListWrapper::NameRole),
                     tracks.data(tracks.index(row),TrackListWrapper::NameRole));
    }
    void mixer_strip_width_is_saved_per_track_once_per_drag() {
        TrackListWrapper tracks; const int first=tracks.addAudioTrack(), second=tracks.addVocalTrack();
        QVERIFY(tracks.setStripWidth(second,180.0));                // e.g. loaded from a project
        auto* mixer=window->findChild<QQuickItem*>("testMixer"); QVERIFY(mixer);
        const auto restore=qScopeGuard([&] { mixer->setVisible(false); mixer->setProperty("trackModel",QVariant()); });
        mixer->setProperty("trackModel",QVariant::fromValue(&tracks)); mixer->setVisible(true);
        QTRY_COMPARE(mixer->property("stripCount").toInt(),2);
        auto* firstStrip=visualItem(mixer,"mixerStrip"+QString::number(first));
        auto* secondStrip=visualItem(mixer,"mixerStrip"+QString::number(second));
        QVERIFY(firstStrip); QVERIFY(secondStrip);
        QCOMPARE(firstStrip->width(),108.0); QCOMPARE(secondStrip->width(),180.0);
        // Drag the second strip wider: live during the drag, stored once on release.
        QSignalSpy changes(&tracks,&QAbstractItemModel::dataChanged);
        auto* handle=visualItem(mixer,"mixerResize"+QString::number(second)); QVERIFY(handle);
        const QPoint start=handle->mapToScene(QPointF(3,30)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        for(int dx:{10,20,30}) QTest::mouseMove(window,start+QPoint(dx,0));
        QCOMPARE(secondStrip->width(),210.0);
        QCOMPARE(tracks.project().findTrack(second)->stripWidth,180.0f);  // not stored mid-drag
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,start+QPoint(30,0));
        QCOMPARE(tracks.project().findTrack(second)->stripWidth,210.0f);
        QCOMPARE(changes.count(),1);
        QCOMPARE(secondStrip->width(),210.0);
        QCOMPARE(tracks.project().findTrack(first)->stripWidth,108.0f);
        // Model changes (project load, session restore) resize the strip.
        QVERIFY(tracks.setStripWidth(first,120.0)); QCOMPARE(firstStrip->width(),120.0);
        // Session state keeps the widths; old sessions restore the default.
        QJsonObject state=tracks.sessionState();
        TrackListWrapper restored; QString error;
        QVERIFY2(restored.restoreSessionState(state,error),qPrintable(error));
        QCOMPARE(restored.project().tracks()[1]->stripWidth,210.0f);
        auto list=state["tracks"].toArray(); auto track=list[1].toObject(); track.remove("stripWidth"); list[1]=track;
        state["tracks"]=list;
        QVERIFY2(restored.restoreSessionState(state,error),qPrintable(error));
        QCOMPARE(restored.project().tracks()[1]->stripWidth,108.0f);
        track["stripWidth"]=999; list[1]=track; state["tracks"]=list;
        QVERIFY(!restored.restoreSessionState(state,error));
    }
    void mixer_tracks_follow_add_remove_and_project_replacement() {
        TrackListWrapper tracks;
        auto* mixer=window->findChild<QQuickItem*>("testMixer"); QVERIFY(mixer);
        mixer->setProperty("trackModel",QVariant::fromValue(&tracks)); mixer->setVisible(true);
        QCOMPARE(mixer->property("stripCount").toInt(),0);
        const int first=tracks.addAudioTrack(), second=tracks.addVocalTrack();
        QTRY_COMPARE(mixer->property("stripCount").toInt(),2);
        auto* label=visualItem(mixer,"mixerName"+QString::number(second)); QVERIFY(label);
        QCOMPARE(label->property("text").toString(),QString("Vocal 1"));
        auto* handle=visualItem(mixer,"mixerResize"+QString::number(second)); QVERIFY(handle);
        const QPoint start=handle->mapToScene(QPointF(3,30)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        QTest::mouseMove(window,start+QPoint(40,0));
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,start+QPoint(40,0));
        auto* remaining=visualItem(mixer,"mixerStrip"+QString::number(second)); QVERIFY(remaining);
        QCOMPARE(remaining->width(),148.0);
        QVERIFY(tracks.removeTrack(first)); QTRY_COMPARE(mixer->property("stripCount").toInt(),1);
        QVERIFY(!visualItem(mixer,"mixerStrip"+QString::number(first)));
        QCOMPARE(visualItem(mixer,"mixerStrip"+QString::number(second)),remaining);
        QCOMPARE(remaining->width(),148.0);
        auto* mute=visualItem(mixer,"mixerMute"+QString::number(second)); QVERIFY(mute); click(mute);
        QVERIFY(tracks.project().findTrack(second)->muted);
        for(int i=0;i<12;++i)tracks.addAudioTrack();
        QTRY_COMPARE(mixer->property("stripCount").toInt(),13);
        auto* scroll=mixer->findChild<QQuickItem*>("mixerScroll"); QVERIFY(scroll);
        QTRY_VERIFY(scroll->property("contentWidth").toDouble()>mixer->width());
        // New/Open replace the contents of the same model object.
        TrackListWrapper replacement; const int guitar=replacement.addInstrumentTrackOfType("Electric Guitar");
        replacement.setVolume(guitar,0.5); replacement.setSolo(guitar,true);
        tracks.adoptDocument(replacement); QTRY_COMPARE(mixer->property("stripCount").toInt(),1);
        // Track volume is not in the mixer (it lives in the track list); the strip shows clip gain.
        QVERIFY(!visualItem(mixer,"mixerVolume"+QString::number(guitar)));
        QVERIFY(visualItem(mixer,"mixerClipGain"+QString::number(guitar)));
        auto* solo=visualItem(mixer,"mixerSolo"+QString::number(guitar)); QVERIFY(solo);
        QVERIFY(solo->property("active").toBool());
        QTRY_COMPARE(scroll->property("contentWidth").toDouble(),108.0);
        TrackListWrapper empty; tracks.adoptDocument(empty); QTRY_COMPARE(mixer->property("stripCount").toInt(),0);
        QTRY_COMPARE(scroll->property("contentWidth").toDouble(),0.0);
        mixer->setVisible(false); mixer->setProperty("trackModel",QVariant());
    }
    void mixer_controls_update_shared_track_state_and_follow_external_changes() {
        TrackListWrapper tracks; const int first=tracks.addAudioTrack(), second=tracks.addVocalTrack();
        auto* mixer=window->findChild<QQuickItem*>("testMixer"); QVERIFY(mixer);
        mixer->setProperty("trackModel",QVariant::fromValue(&tracks)); mixer->setVisible(true);
        QCoreApplication::processEvents();
        auto* mute=visualItem(mixer,"mixerMute"+QString::number(first));
        auto* solo=visualItem(mixer,"mixerSolo"+QString::number(first));
        QVERIFY(mute); QVERIFY(solo);
        QTest::qWait(30); // Allow the newly visible ColumnLayout to place its controls.
        click(mute); click(solo);
        QVERIFY(tracks.project().findTrack(first)->muted); QVERIFY(tracks.project().findTrack(first)->solo);
        QVERIFY(!tracks.project().findTrack(second)->muted); QVERIFY(!tracks.project().findTrack(second)->solo);
        tracks.setMuted(first,false); tracks.setSolo(first,false);
        QVERIFY(!mute->property("active").toBool()); QVERIFY(!solo->property("active").toBool());
        mixer->setVisible(false); mixer->setProperty("trackModel",QVariant());
    }
    void mixer_pan_knob_sets_track_pan_resets_to_centre_and_is_saved() {
        TrackListWrapper tracks; const int first=tracks.addAudioTrack(), second=tracks.addVocalTrack();
        auto* mixer=window->findChild<QQuickItem*>("testMixer"); QVERIFY(mixer);
        const auto restore=qScopeGuard([&] { mixer->setVisible(false); mixer->setProperty("trackModel",QVariant()); });
        mixer->setProperty("trackModel",QVariant::fromValue(&tracks)); mixer->setVisible(true);
        QTest::qWait(30);
        const QString id=QString::number(first);
        auto* pan=visualItem(mixer,"mixerPan"+id); QVERIFY(pan);
        auto* name=visualItem(mixer,"mixerName"+id); QVERIFY(name);
        const auto readout=[&] { return pan->property("readout").toString(); };
        QCOMPARE(pan->property("value").toDouble(),0.0); QCOMPARE(readout(),QString("C"));
        // The knob sits in the name row: it does not take height from the fader.
        QVERIFY(std::abs(pan->mapToScene(QPointF(0,pan->height()/2)).y()-name->mapToScene(QPointF(0,name->height()/2)).y())<4);
        // Drag down: only this track pans left, the readout follows.
        const QPoint centre=pan->mapToScene(QPointF(pan->width()/2,pan->height()/2)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,centre);
        for(int dy:{10,20,40,60}) QTest::mouseMove(window,centre+QPoint(0,dy),10);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,centre+QPoint(0,60));
        const float panned=tracks.project().findTrack(first)->pan;
        QVERIFY2(panned<-0.2f,qPrintable(QString::number(panned)));
        QCOMPARE(tracks.project().findTrack(second)->pan,0.0f);
        QVERIFY(readout().startsWith("L"));
        // Double-click returns to centre.
        QTest::mouseDClick(window,Qt::LeftButton,Qt::NoModifier,centre,10);
        QTRY_COMPARE(tracks.project().findTrack(first)->pan,0.0f);
        QCOMPARE(readout(),QString("C"));
        // External changes turn the knob; values are clamped.
        QVERIFY(tracks.setPan(first,0.4)); QCOMPARE(readout(),QString("R40"));
        QVERIFY(qAbs(pan->property("value").toDouble()-0.4)<1e-6);
        QVERIFY(tracks.setPan(first,9.0)); QCOMPARE(tracks.project().findTrack(first)->pan,1.0f);
        // Session state keeps pan; sessions saved before pan existed restore centred.
        QJsonObject state=tracks.sessionState();
        TrackListWrapper restored; QString error;
        QVERIFY2(restored.restoreSessionState(state,error),qPrintable(error));
        QCOMPARE(restored.project().tracks()[0]->pan,1.0f);
        auto list=state["tracks"].toArray(); auto track=list[0].toObject(); track.remove("pan"); list[0]=track;
        state["tracks"]=list;
        QVERIFY2(restored.restoreSessionState(state,error),qPrintable(error));
        QCOMPARE(restored.project().tracks()[0]->pan,0.0f);
        track["pan"]=2.0; list[0]=track; state["tracks"]=list;
        QVERIFY(!restored.restoreSessionState(state,error));
    }
    void volume_faders_double_click_restores_unity_gain_data() {
        // Track volume lives only in the track list (the mixer strip shows clip gain).
        QTest::addColumn<double>("initialGain");
        QTest::newRow("track silent") << 0.0;
        QTest::newRow("track attenuated") << 0.35;
        QTest::newRow("track boosted") << 1.8;
    }
    void volume_faders_double_click_restores_unity_gain() {
        QFETCH(double,initialGain);
        TrackListWrapper tracks; const int first=tracks.addAudioTrack(),second=tracks.addVocalTrack();
        QVERIFY(tracks.setVolume(first,initialGain)); QVERIFY(tracks.setVolume(second,0.5));
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        const auto restore=qScopeGuard([&] { list->setVisible(false); list->setProperty("trackModel",QVariant()); });
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true);
        QTest::qWait(30);
        auto* fader=visualItem(list,"trackVolume"+QString::number(first)); QVERIFY(fader);
        const QPoint resetPoint=fader->mapToScene(QPointF(fader->width()*0.2,fader->height()/2)).toPoint();
        QTest::mouseDClick(window,Qt::LeftButton,Qt::NoModifier,resetPoint,10);
        QTRY_COMPARE(tracks.project().findTrack(first)->volume,1.0f);
        QCOMPARE(fader->property("value").toDouble(),1.0);
        QCOMPARE(tracks.project().findTrack(second)->volume,0.5f);
        // The double-click handler must leave ordinary dragging usable.
        const QPoint start=fader->mapToScene(QPointF(fader->width()/2,fader->height()/2)).toPoint();
        const QPoint end=fader->mapToScene(QPointF(fader->width()*0.9,fader->height()/2)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        QTest::mouseMove(window,end,20);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,end);
        QVERIFY(tracks.project().findTrack(first)->volume>1.1f);
        QVERIFY(tracks.setVolume(first,0.5));
        QCOMPARE(fader->property("value").toDouble(),0.5);
    }
    void mixer_split_meters_show_independent_channels_beside_the_vertical_fader() {
        TrackListWrapper tracks; const int a=tracks.addAudioTrack(), b=tracks.addVocalTrack();
        auto* mixer=window->findChild<QQuickItem*>("testMixer"); QVERIFY(mixer);
        mixer->setProperty("trackModel",QVariant::fromValue(&tracks)); mixer->setVisible(true);
        QVariantMap levels{{QString::number(a),QVariantMap{{"inputLeftDb",-6.0},{"inputRightDb",-18.0},
            {"outputLeftDb",1.0},{"outputRightDb",-12.0},{"outputLeftClip",true}}}};
        // Replace the binding to EngineController.trackLevels; a plain setProperty() keeps it,
        // so live engine level updates would overwrite these test values.
        QQmlProperty::write(mixer,"levels",levels); QTest::qWait(30);
        auto* input=visualItem(mixer,"mixerInput"+QString::number(a));
        auto* output=visualItem(mixer,"mixerOutput"+QString::number(a));
        auto* fader=visualItem(mixer,"mixerClipGain"+QString::number(a));
        auto* silent=visualItem(mixer,"mixerInput"+QString::number(b));
        QVERIFY(input); QVERIFY(output); QVERIFY(fader); QVERIFY(silent);
        QCOMPARE(input->property("leftDb").toDouble(),-6.0); QCOMPARE(input->property("rightDb").toDouble(),-18.0);
        QVERIFY(output->property("leftClipped").toBool()); QVERIFY(!output->property("rightClipped").toBool());
        QCOMPARE(silent->property("leftDb").toDouble(),-120.0);
        QVERIFY(input->height()>input->width()); QVERIFY(output->height()>output->width());
        QCOMPARE(fader->property("orientation").toInt(),int(Qt::Vertical));
        QVERIFY(fader->x()<input->x()); QVERIFY(input->x()<output->x());
        QVERIFY(window->grabWindow().save("/tmp/chaosmaker-mixer-meters.png"));
        mixer->setProperty("levels",QVariantMap()); mixer->setVisible(false); mixer->setProperty("trackModel",QVariant());
    }
    void timeline_shows_each_recording_lane_and_disarming_one_keeps_the_other_active() {
        auto device=std::make_unique<MultiInputUiDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); controller.setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller.attachProject(&tracks);
        const int a=tracks.addAudioTrack(),b=tracks.addVocalTrack();
        QVERIFY(tracks.setTrackInputPort(b,"test:mic"));
        QVERIFY(tracks.setRecordArmed(a,true)); QVERIFY(tracks.setRecordArmed(b,true));
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        timeline->setProperty("transport",QVariant::fromValue(&controller));
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true);
        controller.transportAction("record"); io->process();
        QTRY_VERIFY(visualItem(timeline,"recordingPreview"+QString::number(a)));
        auto* first=visualItem(timeline,"recordingPreview"+QString::number(a));
        auto* second=visualItem(timeline,"recordingPreview"+QString::number(b)); QVERIFY(second);
        QCOMPARE(second->y()-first->y(),timeline->property("laneHeight").toDouble());
        QVERIFY(tracks.setRecordArmed(a,false)); io->process();
        QTRY_VERIFY(!visualItem(timeline,"recordingPreview"+QString::number(a)));
        QVERIFY(visualItem(timeline,"recordingPreview"+QString::number(b))); QVERIFY(controller.recording());
        controller.transportAction("stop"); io->process(); QTRY_VERIFY(!controller.takeBusy());
        QCOMPARE(tracks.clips().size(),2);
        timeline->setVisible(false); timeline->setProperty("trackModel",QVariant());
        timeline->setProperty("transport",QVariant::fromValue(EngineController::instance()));
        controller.shutdownSession();
    }
    void open_recent_lists_saved_projects_and_opens_them() {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller->attachProject(&tracks); tracks.addAudioTrack();
        auto* recent=window->findChild<QObject*>("recentProjects"); QVERIFY(recent);
        QMetaObject::invokeMethod(recent,"clear");
        const auto restore=qScopeGuard([&] { QMetaObject::invokeMethod(recent,"clear"); controller->newProject(); });
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString first=dir.filePath("first.chaos"), second=dir.filePath("second.chaos");
        QVERIFY(controller->saveProject(QUrl::fromLocalFile(first)));
        QVERIFY(controller->saveProject(QUrl::fromLocalFile(second)));
        // Saved projects are listed newest first.
        QCOMPARE(recent->property("paths").toStringList(),QStringList({second,first}));
        QCOMPARE(recent->property("labels").toStringList(),QStringList({"second.chaos","first.chaos"}));

        auto* fileButton=visualItem(window->contentItem(),"fileMenuButton"); QVERIFY(fileButton);
        auto* fileMenu=window->findChild<QObject*>("fileMenu"); QVERIFY(fileMenu);
        auto* recentMenu=window->findChild<QObject*>("fileOpenRecentMenu"); QVERIFY(recentMenu);
        const auto chooseRecent=[&](const char* name) {
            click(fileButton); QTRY_VERIFY(fileMenu->property("visible").toBool());
            QVERIFY(recentMenu->property("enabled").toBool());
            QMetaObject::invokeMethod(recentMenu,"open"); QTRY_VERIFY(recentMenu->property("visible").toBool());
            QQuickItem* entry=nullptr; QTRY_VERIFY((entry=visualItem(window->contentItem(),name)));
            click(entry); QTRY_VERIFY(!fileMenu->property("visible").toBool());
        };
        chooseRecent("recentProject_1");
        QCOMPARE(controller->projectFile(),QUrl::fromLocalFile(first));
        QCOMPARE(recent->property("paths").toStringList(),QStringList({first,second}));

        // Unsaved changes: ask first; cancelling keeps the current project.
        tracks.addClipAtBeat(tracks.trackIdAt(0),0,4); QTRY_VERIFY(controller->projectDirty());
        chooseRecent("recentProject_1");
        auto* dialog=window->findChild<QObject*>("unsavedProjectChanges"); QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        verifyThemedDialog(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog,"reject"));
        QCOMPARE(controller->projectFile(),QUrl::fromLocalFile(first));

        // A deleted project leaves the list when the File menu opens.
        QVERIFY(QFile::remove(second));
        click(fileButton); QTRY_VERIFY(fileMenu->property("visible").toBool());
        QCOMPARE(recent->property("paths").toStringList(),QStringList({first}));
        QMetaObject::invokeMethod(fileMenu,"close"); QTRY_VERIFY(!fileMenu->property("visible").toBool());

        // Clear Recent empties the list and disables the submenu.
        click(fileButton); QTRY_VERIFY(fileMenu->property("visible").toBool());
        QMetaObject::invokeMethod(recentMenu,"open"); QTRY_VERIFY(recentMenu->property("visible").toBool());
        QQuickItem* clear=nullptr; QTRY_VERIFY((clear=visualItem(window->contentItem(),"clearRecentProjects")));
        click(clear); QTRY_VERIFY(!fileMenu->property("visible").toBool());
        QVERIFY(recent->property("empty").toBool()); QVERIFY(!recentMenu->property("enabled").toBool());
        // The list is kept in the (test) app settings.
        QCOMPARE(QSettings().value("session/recentProjects").toStringList(),QStringList());
    }
    void new_project_prompts_before_discarding_unsaved_clips() {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller->attachProject(&tracks); const int track=tracks.addAudioTrack(); tracks.addClipAtBeat(track,0,4);
        auto* top=window->findChild<QObject*>("testTopBar"); QVERIFY(top); top->setProperty("trackModel",QVariant::fromValue(&tracks));
        // Use the real File > New path (a mouse click cannot reliably hit the 1x1 test hook).
        auto* fileButton=visualItem(window->contentItem(),"fileMenuButton"); QVERIFY(fileButton); click(fileButton);
        auto* fileMenu=window->findChild<QObject*>("fileMenu"); QVERIFY(fileMenu); QTRY_VERIFY(fileMenu->property("visible").toBool());
        auto* newItem=visualItem(window->contentItem(),"fileNewItem"); QVERIFY(newItem); click(newItem);
        auto* dialog=window->findChild<QObject*>("unsavedProjectChanges"); QVERIFY(dialog); QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog,"reject")); QCOMPARE(tracks.clips().size(),1);
        QVERIFY(controller->projectDirty()); top->setProperty("trackModel",QVariant());
    }
    void ctrl_shift_s_opens_save_as() {
        auto* bridge=window->findChild<QObject*>("projectActionsBridge"); QVERIFY(bridge);
        QSignalSpy saveDialog(bridge,SIGNAL(showSaveDialog())); QVERIFY(saveDialog.isValid());
        window->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(window));
        QTest::keyClick(window,Qt::Key_S,Qt::ControlModifier|Qt::ShiftModifier);
        QTRY_COMPARE(saveDialog.count(),1);
        // Close the file dialog again so later tests start clean.
        auto* dialog=window->findChild<QObject*>("saveProjectDialog"); QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog,"reject"));
    }
    void transport_keys_fire_once_while_quick_settings_is_open() {
        window->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(window)); // keys need the active window
        QSignalSpy actions(bar,SIGNAL(transportAction(QString))); QVERIFY(actions.isValid());
        auto* button=visualItem(window->contentItem(),"quickSettingsButton"); QVERIFY(button);
        auto* popup=window->findChild<QObject*>("quickSettingsPopup"); QVERIFY(popup);
        const auto restore=qScopeGuard([&] { QMetaObject::invokeMethod(popup,"close"); });
        click(button); QTRY_VERIFY(popup->property("visible").toBool());
        QTest::keyClick(window,Qt::Key_Space);
        QCOMPARE(actions.count(),1);
        QTest::keyClick(window,Qt::Key_R);
        QCOMPARE(actions.count(),2);
        QCOMPARE(actions.at(1).first().toString(),QString("record"));
    }
    void timeline_clip_keys_work_with_a_piano_roll_in_the_window_data() {
        QTest::addColumn<bool>("pianoRollVisible");
        QTest::newRow("piano roll hidden") << false;
        QTest::newRow("piano roll docked") << true;
    }
    void timeline_clip_keys_work_with_a_piano_roll_in_the_window() {
        QFETCH(bool,pianoRollVisible);
        window->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(window)); // keys need the active window
        TrackListWrapper tracks; const int track=tracks.addInstrumentTrack();
        QVERIFY(tracks.addClipAtBeat(track,0,4)>0);
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        auto* roll=window->findChild<QQuickItem*>("testPianoRoll"); QVERIFY(roll);
        const auto restore=qScopeGuard([&] {
            timeline->setVisible(false); timeline->setProperty("trackModel",QVariant()); roll->setVisible(false);
        });
        roll->setVisible(pianoRollVisible);
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true);
        timeline->forceActiveFocus(); QVERIFY(timeline->hasActiveFocus());
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);      // select all clips
        QCOMPARE(selectedIds(timeline).size(),1);
        QTest::keyClick(window,Qt::Key_Escape);                     // deselect
        QCOMPARE(selectedIds(timeline).size(),0);
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
        QTest::keyClick(window,Qt::Key_Delete);                     // delete the clip
        QCOMPARE(tracks.clips().size(),0);
    }
    void piano_roll_keys_edit_notes_only_when_it_has_focus() {
        window->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(window)); // keys need the active window
        TrackListWrapper tracks; const int track=tracks.addInstrumentTrack();
        QVERIFY(tracks.addClipAtBeat(track,0,4)>0);
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        auto* roll=window->findChild<QQuickItem*>("testPianoRoll"); QVERIFY(roll);
        auto* notes=roll->findChild<QObject*>("pianoRollBackend"); QVERIFY(notes);
        const auto count=[&] { return notes->property("count").toInt(); };
        const auto restore=qScopeGuard([&] {
            QMetaObject::invokeMethod(notes,"clear");
            timeline->setVisible(false); timeline->setProperty("trackModel",QVariant()); roll->setVisible(false);
        });
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true);
        roll->setVisible(true);
        int id=-1;
        for(int pitch:{60,64})QVERIFY(QMetaObject::invokeMethod(notes,"addNote",Q_RETURN_ARG(int,id),Q_ARG(int,pitch),Q_ARG(double,0.0),Q_ARG(double,1.0),Q_ARG(int,100)));
        QCOMPARE(count(),2);
        // Focus in the timeline: the piano roll's keys stay out of the way.
        timeline->forceActiveFocus();
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); QTest::keyClick(window,Qt::Key_Delete);
        QCOMPARE(count(),2); QCOMPARE(tracks.clips().size(),0);
        // Focus in the piano roll: its keys select and delete notes.
        roll->forceActiveFocus(); QVERIFY(roll->hasActiveFocus());
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
        QCOMPARE(notes->property("selectedCount").toInt(),2);
        QTest::keyClick(window,Qt::Key_Escape);
        QCOMPARE(notes->property("selectedCount").toInt(),0);
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); QTest::keyClick(window,Qt::Key_Delete);
        QCOMPARE(count(),0);
    }
    void piano_roll_undo_redo_buttons_use_the_project_history() {
        TrackListWrapper tracks; const int track=tracks.addInstrumentTrack();
        const int clip=tracks.addClipAtBeat(track,0,4); QVERIFY(clip>0);
        auto* roll=window->findChild<QQuickItem*>("testPianoRoll"); QVERIFY(roll);
        auto* notes=roll->findChild<QObject*>("pianoRollBackend"); QVERIFY(notes);
        const auto restore=qScopeGuard([&] {
            roll->setVisible(false); roll->setProperty("trackModel",QVariant::fromValue<QObject*>(nullptr));
            roll->setProperty("trackId",-1); roll->setProperty("clipId",-1);
        });
        roll->setProperty("trackModel",QVariant::fromValue(&tracks));
        roll->setProperty("trackId",track); roll->setProperty("clipId",clip); roll->setVisible(true);
        auto* undo=visualItem(roll,"pianoRollUndo"); auto* redo=visualItem(roll,"pianoRollRedo");
        QVERIFY(undo); QVERIFY(redo);
        // Only the clip creation is in the history so far; nothing to redo.
        QVERIFY(!redo->isEnabled());
        int id=-1;
        QVERIFY(QMetaObject::invokeMethod(notes,"addNote",Q_RETURN_ARG(int,id),Q_ARG(int,60),Q_ARG(double,0.0),Q_ARG(double,1.0),Q_ARG(int,100)));
        QVERIFY(id>0); QCOMPARE(notes->property("count").toInt(),1); QVERIFY(undo->isEnabled());
        click(undo); QTRY_COMPARE(notes->property("count").toInt(),0);
        QVERIFY(redo->isEnabled());
        click(redo); QTRY_COMPARE(notes->property("count").toInt(),1);
        QVERIFY(!redo->isEnabled());
    }
    void piano_roll_snap_button_switches_snapping_but_keeps_the_grid() {
        auto* roll=window->findChild<QQuickItem*>("testPianoRoll"); QVERIFY(roll);
        auto* notes=roll->findChild<QObject*>("pianoRollBackend"); QVERIFY(notes);
        auto* grid=visualItem(roll,"pianoRollGrid"); QVERIFY(grid);
        auto* snap=visualItem(roll,"pianoRollSnap"); QVERIFY(snap);
        const auto restore=qScopeGuard([&] {
            QMetaObject::invokeMethod(notes,"clear"); roll->setProperty("snapEnabled",true); roll->setVisible(false);
        });
        roll->setVisible(true); QTest::qWait(30); // let the grid scroll to middle C
        const double ppb=roll->property("pixelsPerBeat").toDouble();
        const double gridLines=roll->property("gridLineBeats").toDouble();
        // Draw with the pencil at beat 1.3 (between 1/16 grid lines), on a visible row.
        const auto drawAt=[&](double beat) {
            auto* flick=grid->parentItem()->parentItem(); // contentItem -> Flickable
            const double y=flick->property("contentY").toDouble()+40;
            QTest::mouseClick(window,Qt::LeftButton,{},grid->mapToScene(QPointF(beat*ppb,y)).toPoint());
            const auto list=notes->property("notes").toList();
            return list.isEmpty() ? -1.0 : list.last().toMap().value("startBeat").toDouble();
        };
        QVERIFY(roll->property("snapEnabled").toBool());
        QCOMPARE(drawAt(1.3),1.25);                                 // snapped to 1/16
        QMetaObject::invokeMethod(notes,"clear");
        click(snap); QVERIFY(!roll->property("snapEnabled").toBool());
        const double free=drawAt(1.3);
        QVERIFY(std::abs(free-1.3)<1.0/ppb);                        // placed where clicked
        QCOMPARE(roll->property("gridLineBeats").toDouble(),gridLines); // grid lines unchanged
        click(snap); QVERIFY(roll->property("snapEnabled").toBool());
    }
    void piano_roll_quantize_button_and_q_key_snap_note_starts() {
        window->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(window)); // keys need the active window
        auto* roll=window->findChild<QQuickItem*>("testPianoRoll"); QVERIFY(roll);
        auto* notes=roll->findChild<QObject*>("pianoRollBackend"); QVERIFY(notes);
        auto* quantize=visualItem(roll,"pianoRollQuantize"); QVERIFY(quantize);
        const auto restore=qScopeGuard([&] { QMetaObject::invokeMethod(notes,"clear"); roll->setVisible(false); });
        roll->setVisible(true);
        const auto starts=[&] {
            QList<double> result;
            for(const auto& n:notes->property("notes").toList())result.push_back(n.toMap().value("startBeat").toDouble());
            return result;
        };
        const auto add=[&](double start) {
            int id=-1;
            QVERIFY(QMetaObject::invokeMethod(notes,"addNote",Q_RETURN_ARG(int,id),Q_ARG(int,60),Q_ARG(double,start),Q_ARG(double,0.5),Q_ARG(int,100)));
        };
        QVERIFY(!quantize->isEnabled());                            // no notes yet
        add(0.3); add(1.13); QVERIFY(quantize->isEnabled());
        click(quantize);                                            // default Grid is 1/16 = 0.25 beat
        QCOMPARE(starts(),(QList<double>{0.25,1.25}));
        // The Q key works while the piano roll has focus
        QMetaObject::invokeMethod(notes,"clear"); add(0.6);
        roll->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Q);
        QCOMPARE(starts(),(QList<double>{0.5}));
        // ...and not while it lacks focus
        QMetaObject::invokeMethod(notes,"clear"); add(0.6);
        auto* other=window->findChild<QQuickItem*>("otherFocus"); QVERIFY(other); other->forceActiveFocus();
        QTest::keyClick(window,Qt::Key_Q);
        QCOMPARE(starts(),(QList<double>{0.6}));
    }
    void piano_roll_footer_buttons_drive_the_transport() {
        auto device=std::make_unique<MultiInputUiDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); controller.setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller.attachProject(&tracks); tracks.addAudioTrack();
        auto* roll=window->findChild<QQuickItem*>("testPianoRoll"); QVERIFY(roll);
        const auto restore=qScopeGuard([&] {
            roll->setVisible(false); roll->setProperty("transport",QVariant::fromValue(EngineController::instance()));
        });
        roll->setProperty("transport",QVariant::fromValue(&controller)); roll->setVisible(true);
        const auto button=[&](const char* command) {
            return visualItem(roll,QStringLiteral("pianoRollTransport_")+command);
        };
        for(auto* command:{"rewind","play","pause","stop","forward"})QVERIFY(button(command));
        QVERIFY(!visualItem(roll,"pianoRollTransport_zoom"));      // dead zoom-tool button removed
        click(button("play")); io->process();
        QVERIFY(controller.playing());
        for(int i=0;i<20;++i)io->process();
        click(button("pause")); io->process();
        QVERIFY(!controller.playing()); QVERIFY(controller.positionSeconds()>0);
        click(button("rewind")); io->process();
        QTRY_COMPARE(controller.positionSeconds(),0.0);
        click(button("forward")); io->process();
        QTRY_VERIFY(controller.positionBeats()>0);
        click(button("play")); io->process(); QVERIFY(controller.playing());
        click(button("stop")); io->process(); QVERIFY(!controller.playing());
        controller.shutdownSession();
    }
    void piano_roll_names_the_drum_kit_parts_on_a_drums_track() {
        TrackListWrapper tracks;
        const int drums=tracks.addInstrumentTrackOfType("Drums"), piano=tracks.addInstrumentTrackOfType("Piano");
        const int drumClip=tracks.addClipAtBeat(drums,0,4), pianoClip=tracks.addClipAtBeat(piano,0,4);
        QVERIFY(drumClip>0 && pianoClip>0);
        auto* roll=window->findChild<QQuickItem*>("testPianoRoll"); QVERIFY(roll);
        auto* backend=roll->findChild<QObject*>("pianoRollBackend"); QVERIFY(backend);
        const auto restore=qScopeGuard([&] {
            roll->setVisible(false); roll->setProperty("clipId",-1); roll->setProperty("trackId",-1);
            roll->setProperty("trackModel",QVariant::fromValue(static_cast<TrackListWrapper*>(nullptr)));
        });
        roll->setProperty("trackModel",QVariant::fromValue(&tracks));
        roll->setProperty("trackId",drums); roll->setProperty("clipId",drumClip);
        roll->setVisible(true);
        QTRY_VERIFY(backend->property("drumMode").toBool());
        auto* keys=visualItem(roll,"pianoKeys"); QVERIFY(keys);
        QCOMPARE(keys->width(),90.0);                               // the same keys as any track
        const auto label=[&](int note) { return visualItem(roll,"drumKeyLabel"+QString::number(note)); };
        QVERIFY(label(60)); QTRY_VERIFY(label(60)->isVisible());
        QCOMPARE(label(60)->property("text").toString(),QString("Kick"));
        QCOMPARE(label(62)->property("text").toString(),QString("Snare"));
        QCOMPARE(label(66)->property("text").toString(),QString("Hi-hat closed"));
        QCOMPARE(label(70)->property("text").toString(),QString("Hi-hat open"));
        QCOMPARE(label(55)->property("text").toString(),QString("Hi-hat tight"));
        QVERIFY(!label(73)->isVisible());                          // no cymbals in this kit
        QVERIFY(label(66)->width()<54);                            // a black key's part sits inside the black key
        // Keys the kit does not play keep their note names (D#4 between snare and snare edge).
        bool noteNameShown=false;
        const std::function<void(QQuickItem*)> scan=[&](QQuickItem* item) {
            if (item->isVisible() && item->property("text").toString()=="D#4") noteNameShown=true;
            for (auto* child : item->childItems()) scan(child);
        };
        scan(keys);
        QVERIFY(noteNameShown);
        QTest::qWait(50);
        QVERIFY(window->grabWindow().save("/tmp/chaosmaker-drum-piano-roll.png"));

        roll->setProperty("trackId",piano); roll->setProperty("clipId",pianoClip);   // a piano clip: as before
        QTRY_VERIFY(!backend->property("drumMode").toBool());
        QCOMPARE(keys->width(),90.0);
        QVERIFY(!label(60)->isVisible());                           // no part names on other instruments
    }
    void piano_roll_names_the_electronic_kit_parts() {
        TrackListWrapper tracks;
        const int electro=tracks.addInstrumentTrackOfType("Electronic Drums"), drums=tracks.addInstrumentTrackOfType("Drums");
        const int electroClip=tracks.addClipAtBeat(electro,0,4), drumClip=tracks.addClipAtBeat(drums,0,4);
        QVERIFY(electroClip>0 && drumClip>0);
        auto* roll=window->findChild<QQuickItem*>("testPianoRoll"); QVERIFY(roll);
        auto* backend=roll->findChild<QObject*>("pianoRollBackend"); QVERIFY(backend);
        const auto restore=qScopeGuard([&] {
            roll->setVisible(false); roll->setProperty("clipId",-1); roll->setProperty("trackId",-1);
            roll->setProperty("trackModel",QVariant::fromValue(static_cast<TrackListWrapper*>(nullptr)));
        });
        roll->setProperty("trackModel",QVariant::fromValue(&tracks));
        roll->setProperty("trackId",electro); roll->setProperty("clipId",electroClip);
        roll->setVisible(true);
        QTRY_VERIFY(backend->property("drumMode").toBool());
        const auto label=[&](int note) { return visualItem(roll,"drumKeyLabel"+QString::number(note)); };
        QVERIFY(label(60)); QTRY_VERIFY(label(60)->isVisible());
        QCOMPARE(label(60)->property("text").toString(),QString("Kick"));
        QCOMPARE(label(63)->property("text").toString(),QString("Clap"));
        QCOMPARE(label(73)->property("text").toString(),QString("Crash"));
        QCOMPARE(label(75)->property("text").toString(),QString("Ride"));
        QVERIFY(label(73)->isVisible());
        // Straight to the acoustic kit (both are drum tracks): its own names, no clap or crash.
        roll->setProperty("trackId",drums); roll->setProperty("clipId",drumClip);
        QTRY_VERIFY(!label(73)->isVisible());
        QVERIFY(!label(63)->isVisible());
        QCOMPARE(label(60)->property("text").toString(),QString("Kick"));
        QCOMPARE(label(64)->property("text").toString(),QString("Snare edge"));
    }
    void piano_roll_velocity_lane_paints_remembers_and_follows_the_clip() {
        TrackListWrapper tracks;
        const int track=tracks.addInstrumentTrack();
        const int clip=tracks.addClipAtBeat(track,0,4); QVERIFY(clip>0);
        auto* roll=window->findChild<QQuickItem*>("testPianoRoll"); QVERIFY(roll);
        auto* backend=roll->findChild<QObject*>("pianoRollBackend"); QVERIFY(backend);
        const auto restore=qScopeGuard([&] {
            roll->setVisible(false); roll->setProperty("clipId",-1); roll->setProperty("trackId",-1);
            roll->setProperty("trackModel",QVariant::fromValue(static_cast<TrackListWrapper*>(nullptr)));
        });
        roll->setProperty("trackModel",QVariant::fromValue(&tracks));
        roll->setProperty("trackId",track); roll->setProperty("clipId",clip);
        roll->setVisible(true); QTest::qWait(30);
        const auto add=[&](int pitch,double start,int velocity) {
            int id=-1;
            QMetaObject::invokeMethod(backend,"addNote",Q_RETURN_ARG(int,id),Q_ARG(int,pitch),Q_ARG(double,start),Q_ARG(double,0.5),Q_ARG(int,velocity));
            return id;
        };
        const auto velocity=[&](int id) {
            for(const auto& n:backend->property("notes").toList()) if(n.toMap().value("id").toInt()==id) return n.toMap().value("velocity").toInt();
            return -1;
        };
        const int a=add(60,0.0,100), b=add(62,1.0,100), c=add(64,2.0,100);
        QTRY_VERIFY(visualItem(roll,"velocityPin"+QString::number(c)));

        // D2: the pin of the note on beat 0 shows whole inside the lane.
        auto* area=visualItem(roll,"velocityStalkArea"); QVERIFY(area);
        auto* pin=visualItem(roll,"velocityPin"+QString::number(a)); QVERIFY(pin);
        QVERIFY(pin->mapToItem(area,QPointF(0,0)).x()>=-0.01);

        // D3: a drag from empty lane space sweeps across the stalks, low on the left, high on
        // the right; one undo puts every velocity back.
        const double ppb=roll->property("pixelsPerBeat").toDouble();
        const auto lanePoint=[&](double beat,double fraction) {
            return area->mapToScene(QPointF(beat*ppb+2,area->height()*fraction)).toPoint();
        };
        const QPoint start=area->mapToScene(QPointF(0.5*ppb,area->height()*0.85)).toPoint();   // between a and b: empty
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        for(int i=1;i<=4;++i) QTest::mouseMove(window,start+(lanePoint(2.0,0.2)-start)*i/4);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,lanePoint(2.0,0.2));
        QTRY_VERIFY(velocity(c)>90);
        QVERIFY(velocity(b)<velocity(c)); QVERIFY(velocity(b)!=100);
        QCOMPARE(velocity(a),100);                                    // the sweep started right of a
        QVERIFY(tracks.undo());
        QTRY_COMPARE(velocity(b),100); QCOMPARE(velocity(c),100);
        QVERIFY(tracks.redo());
        QTRY_VERIFY(velocity(c)>90);

        // D1: a drawn note takes the last velocity set (c's, the sweep's last stalk).
        const int remembered=backend->property("newNoteVelocity").toInt();
        QCOMPARE(remembered,velocity(c));
        auto* grid=visualItem(roll,"pianoRollGrid"); QVERIFY(grid);
        auto* flick=grid->parentItem()->parentItem();
        QTest::mouseClick(window,Qt::LeftButton,{},grid->mapToScene(QPointF(3.1*ppb,flick->property("contentY").toDouble()+40)).toPoint());
        QTRY_COMPARE(backend->property("count").toInt(),4);
        QCOMPARE(backend->property("notes").toList().last().toMap().value("velocity").toInt(),remembered);

        // D4: resizing the clip on the timeline moves the open piano roll's clip end.
        QCOMPARE(roll->property("shownClipLengthBeats").toDouble(),4.0);
        QVERIFY(tracks.resizeClipAtBeat(track,clip,8.0));
        QTRY_COMPARE(roll->property("shownClipLengthBeats").toDouble(),8.0);
        QVERIFY(tracks.undo());
        QTRY_COMPARE(roll->property("shownClipLengthBeats").toDouble(),4.0);
    }
    void piano_roll_shows_no_placeholder_controls() {
        auto* roll=window->findChild<QQuickItem*>("testPianoRoll"); QVERIFY(roll);
        const auto restore=qScopeGuard([&] { roll->setVisible(false); });
        roll->setVisible(true);
        QStringList texts;
        std::function<void(QQuickItem*)> collect=[&](QQuickItem* item) {
            const auto text=item->property("text");
            if(text.isValid() && item->isVisible())texts<<text.toString();
            for(auto* child:item->childItems())collect(child);
        };
        auto* laneLabel=visualItem(roll,"velocityLaneLabel"); QVERIFY(laneLabel);
        collect(laneLabel);
        QCOMPARE(texts,QStringList{"Velocity"});                    // plain label, no "▾" or "+"
        texts.clear(); collect(roll);
        for(const auto& fake:{"Track 1","All channels","Key snap","Pitch","Channel","File","Navigate","Contents"})
            QVERIFY2(!texts.contains(QString::fromLatin1(fake)),fake);
    }
    void piano_roll_pointer_drag_on_empty_grid_box_selects_notes() {
        auto* roll=window->findChild<QQuickItem*>("testPianoRoll"); QVERIFY(roll);
        auto* notes=roll->findChild<QObject*>("pianoRollBackend"); QVERIFY(notes);
        auto* grid=visualItem(roll,"pianoRollGrid"); QVERIFY(grid);
        auto* box=visualItem(roll,"pianoRollSelectionBox"); QVERIFY(box);
        const auto restore=qScopeGuard([&] {
            QMetaObject::invokeMethod(notes,"clear"); roll->setProperty("activeTool","pencil"); roll->setVisible(false);
        });
        roll->setVisible(true); QTest::qWait(30);
        roll->setProperty("activeTool","pointer");
        const double ppb=roll->property("pixelsPerBeat").toDouble();
        const double kh=roll->property("keyHeight").toDouble();
        auto* flick=grid->parentItem()->parentItem();
        const int top=127-int(std::floor((flick->property("contentY").toDouble()+20)/kh)); // a visible row
        const auto add=[&](int pitch,double start,double length) {
            int id=-1;
            QMetaObject::invokeMethod(notes,"addNote",Q_RETURN_ARG(int,id),Q_ARG(int,pitch),Q_ARG(double,start),Q_ARG(double,length),Q_ARG(int,100));
            return id;
        };
        const int a=add(top-1,0.5,1.0), b=add(top-2,1.0,0.5), c=add(top-1,3.0,1.0), d=add(top-6,1.0,1.0);
        const auto at=[&](double beat,int pitch,double dy) {
            return grid->mapToScene(QPointF(beat*ppb,(127-pitch)*kh+dy)).toPoint();
        };
        const auto selected=[&] {
            QSet<int> ids;
            for(const auto& n:notes->property("notes").toList())
                if(n.toMap().value("selected").toBool())ids.insert(n.toMap().value("id").toInt());
            return ids;
        };
        const auto drag=[&](QPoint from,QPoint to,Qt::KeyboardModifiers modifiers) {
            QTest::mousePress(window,Qt::LeftButton,modifiers,from);
            QTest::mouseMove(window,(from+to)/2,10); QTest::mouseMove(window,to,10);
            const bool shown=box->isVisible();
            QTest::mouseRelease(window,Qt::LeftButton,modifiers,to);
            return shown;
        };
        // Box over the first two notes: touching a note is enough (b only partly covered).
        QVERIFY(drag(at(0.25,top-1,2),at(1.2,top-2,kh-2),Qt::NoModifier));
        QCOMPARE(selected(),(QSet<int>{a,b}));
        QVERIFY(!box->isVisible());
        QCOMPARE(notes->property("count").toInt(),4);              // pointer never draws notes
        // Shift adds; a new plain box replaces.
        drag(at(2.8,top-1,2),at(3.5,top-1,kh-2),Qt::ShiftModifier);
        QCOMPARE(selected(),(QSet<int>{a,b,c}));
        drag(at(0.8,top-5,2),at(1.5,top-6,kh-2),Qt::NoModifier);
        QCOMPARE(selected(),(QSet<int>{d}));
        // A plain click on empty grid clears the selection.
        QTest::mouseClick(window,Qt::LeftButton,{},at(6.0,top-3,kh/2));
        QVERIFY(selected().isEmpty());
    }
    // The mouse-driven piano roll harness, kept as a test (D7): move, resize and erase by
    // mouse; an edit leaves the other notes' blocks in place (D6, the note list model).
    void piano_roll_mouse_moves_resizes_and_erases_notes() {
        auto* roll=window->findChild<QQuickItem*>("testPianoRoll"); QVERIFY(roll);
        auto* notes=roll->findChild<QObject*>("pianoRollBackend"); QVERIFY(notes);
        auto* grid=visualItem(roll,"pianoRollGrid"); QVERIFY(grid);
        const auto restore=qScopeGuard([&] { QMetaObject::invokeMethod(notes,"clear"); roll->setVisible(false); });
        roll->setVisible(true); QTest::qWait(30);
        roll->setProperty("activeTool","pencil");
        const double ppb=roll->property("pixelsPerBeat").toDouble();
        const double kh=roll->property("keyHeight").toDouble();
        auto* flick=grid->parentItem()->parentItem();
        const int top=127-int(std::floor((flick->property("contentY").toDouble()+20)/kh)); // a visible row
        const auto add=[&](int pitch,double start,double length) {
            int id=-1;
            QMetaObject::invokeMethod(notes,"addNote",Q_RETURN_ARG(int,id),Q_ARG(int,pitch),Q_ARG(double,start),Q_ARG(double,length),Q_ARG(int,100));
            return id;
        };
        const auto note=[&](int id) {
            for(const auto& n:notes->property("notes").toList())
                if(n.toMap().value("id").toInt()==id)return n.toMap();
            return QVariantMap{};
        };
        const std::function<QQuickItem*(QQuickItem*,int)> blockIn=[&](QQuickItem* item,int id) -> QQuickItem* {
            if(item->property("notePitch").isValid() && item->property("noteId").toInt()==id)return item;
            for(auto* child:item->childItems())
                if(auto* found=blockIn(child,id))return found;
            return nullptr;
        };
        const auto block=[&](int id) { return blockIn(roll,id); };
        const auto at=[&](double beat,int pitch) {
            return grid->mapToScene(QPointF(beat*ppb,(127-pitch)*kh+kh/2)).toPoint();
        };
        const auto drag=[&](QPoint from,QPoint to) {
            QTest::mousePress(window,Qt::LeftButton,{},from);
            QTest::mouseMove(window,(from+to)/2,10); QTest::mouseMove(window,to,10);
            QTest::mouseRelease(window,Qt::LeftButton,{},to,10);
        };
        const int a=add(top-1,1.0,1.0), b=add(top-6,4.0,1.0);
        QTest::qWait(10);
        QPointer<QQuickItem> bBlock=block(b); QVERIFY(bBlock);

        // Move: drag the body one beat right and two rows down
        drag(at(1.3,top-1),at(2.3,top-3));
        QCOMPARE(note(a).value("startBeat").toDouble(),2.0);
        QCOMPARE(note(a).value("note").toInt(),top-3);
        QVERIFY(note(a).value("selected").toBool());
        QVERIFY(bBlock && block(b)==bBlock);                       // b's block was not rebuilt

        // Resize: drag the right edge one beat further
        drag(at(3.0,top-3)-QPoint(3,0),at(4.0,top-3)-QPoint(3,0));
        QCOMPARE(note(a).value("lengthBeats").toDouble(),2.0);
        QCOMPARE(note(a).value("startBeat").toDouble(),2.0);
        QVERIFY(bBlock && block(b)==bBlock);

        // Erase: right-click a note's body
        QTest::mouseClick(window,Qt::RightButton,{},at(4.4,top-6));
        QCOMPARE(notes->property("count").toInt(),1);
        QVERIFY(note(b).isEmpty() && !note(a).isEmpty());
    }
    void piano_roll_ctrl_wheel_zooms_time_and_rows_around_the_cursor() {
        auto* roll=window->findChild<QQuickItem*>("testPianoRoll"); QVERIFY(roll);
        auto* grid=visualItem(roll,"pianoRollGrid"); QVERIFY(grid);
        auto* taller=visualItem(roll,"pianoRollRowsTaller"); auto* shorter=visualItem(roll,"pianoRollRowsShorter");
        QVERIFY(taller); QVERIFY(shorter);
        const auto restore=qScopeGuard([&] {
            roll->setProperty("pixelsPerBeat",96.0); roll->setProperty("keyHeight",18.0); roll->setVisible(false);
        });
        roll->setVisible(true); QTest::qWait(30);
        auto* flick=grid->parentItem()->parentItem();
        const double dockMin=roll->property("minimumDockHeight").toDouble();
        const auto ppb=[&] { return roll->property("pixelsPerBeat").toDouble(); };
        const auto kh=[&] { return roll->property("keyHeight").toDouble(); };
        const QPointF viewportPt(300,120);
        const auto wheelAt=[&](QPointF scenePt,int angle,Qt::KeyboardModifiers modifiers) {
            QWheelEvent event(scenePt,window->mapToGlobal(scenePt.toPoint()),QPoint(),QPoint(0,angle),Qt::NoButton,modifiers,Qt::NoScrollPhase,false);
            QCoreApplication::sendEvent(window,&event); QCoreApplication::processEvents();
        };
        const QPointF gridPt=flick->mapToScene(viewportPt);
        // Content position under the cursor, in beats and rows.
        const auto beatUnder=[&] { return (flick->property("contentX").toDouble()+viewportPt.x())/ppb(); };
        const auto rowUnder=[&] { return (flick->property("contentY").toDouble()+viewportPt.y())/kh(); };
        // Plain wheel scrolls pitch only.
        const double y0=flick->property("contentY").toDouble();
        wheelAt(gridPt,-120,Qt::NoModifier);
        QVERIFY(flick->property("contentY").toDouble()>y0);
        QCOMPARE(ppb(),96.0); QCOMPARE(kh(),18.0);
        // Ctrl+wheel zooms time around the cursor; rows unchanged.
        flick->setProperty("contentX",200.0);
        const double beat=beatUnder();
        wheelAt(gridPt,120,Qt::ControlModifier);
        QVERIFY(ppb()>96.0); QCOMPARE(kh(),18.0);
        QVERIFY(std::abs(beatUnder()-beat)<0.01);
        for(int i=0;i<40;++i)wheelAt(gridPt,120,Qt::ControlModifier);
        QCOMPARE(ppb(),roll->property("maxPixelsPerBeat").toDouble());
        for(int i=0;i<40;++i)wheelAt(gridPt,-120,Qt::ControlModifier);
        QCOMPARE(ppb(),roll->property("minPixelsPerBeat").toDouble());
        // Ctrl+Shift+wheel changes the row height around the cursor; time unchanged.
        const double timeZoom=ppb(); const double row=rowUnder();
        wheelAt(gridPt,120,Qt::ControlModifier|Qt::ShiftModifier);
        QVERIFY(kh()>18.0); QCOMPARE(ppb(),timeZoom);
        QVERIFY(std::abs(rowUnder()-row)<0.01);
        // Ctrl+wheel over the keys also changes rows; limits hold.
        auto* keysArea=flick->parentItem(); // RowLayout: keys flickable is its first child
        QQuickItem* keys=nullptr;
        for(auto* child:keysArea->childItems()) if(child!=flick && child->width()>0 && child->width()<=90) keys=child;
        QVERIFY(keys);
        const QPointF keyPt=keys->mapToScene(QPointF(45,120));
        const double beforeKeyStep=kh();
        wheelAt(keyPt,120,Qt::ControlModifier);
        QVERIFY(std::abs(kh()-beforeKeyStep*1.1)<1e-9);             // one step, applied once
        for(int i=0;i<30;++i)wheelAt(keyPt,-120,Qt::ControlModifier);
        QCOMPARE(kh(),roll->property("minKeyHeight").toDouble()); QCOMPARE(ppb(),timeZoom);
        for(int i=0;i<30;++i)wheelAt(keyPt,120,Qt::ControlModifier);
        QCOMPARE(kh(),roll->property("maxKeyHeight").toDouble());
        // Footer row buttons; the dock's minimum height never follows the zoom.
        click(shorter); QVERIFY(kh()<roll->property("maxKeyHeight").toDouble());
        const double afterShorter=kh(); click(taller); QVERIFY(kh()>afterShorter);
        QCOMPARE(roll->property("minimumDockHeight").toDouble(),dockMin);
    }
    void piano_roll_notes_box_sets_the_length_of_drawn_notes() {
        auto* roll=window->findChild<QQuickItem*>("testPianoRoll"); QVERIFY(roll);
        auto* notes=roll->findChild<QObject*>("pianoRollBackend"); QVERIFY(notes);
        auto* grid=visualItem(roll,"pianoRollGrid"); QVERIFY(grid);
        auto* lengthBox=visualItem(roll,"pianoRollNoteLength"); QVERIFY(lengthBox);
        const auto restore=qScopeGuard([&] {
            QMetaObject::invokeMethod(notes,"clear"); lengthBox->setProperty("currentIndex",0); roll->setVisible(false);
        });
        roll->setVisible(true); QTest::qWait(30);
        const double ppb=roll->property("pixelsPerBeat").toDouble();
        const auto drawAt=[&](double beat) {
            auto* flick=grid->parentItem()->parentItem();
            const double y=flick->property("contentY").toDouble()+40;
            QTest::mouseClick(window,Qt::LeftButton,{},grid->mapToScene(QPointF(beat*ppb,y)).toPoint());
            return notes->property("notes").toList().last().toMap().value("lengthBeats").toDouble();
        };
        QCOMPARE(drawAt(0.1),0.25);                                 // "Grid" = 1/16 grid
        lengthBox->setProperty("currentIndex",3);                   // "1/4"
        QCOMPARE(roll->property("noteLengthMode").toString(),QString("1/4"));
        QCOMPARE(drawAt(2.1),1.0);
        lengthBox->setProperty("currentIndex",2);                   // "1/8"
        QCOMPARE(drawAt(4.1),0.5);
    }
    void piano_roll_triplet_and_dotted_grids_snap_per_bar() {
        auto* roll=window->findChild<QQuickItem*>("testPianoRoll"); QVERIFY(roll);
        auto* notes=roll->findChild<QObject*>("pianoRollBackend"); QVERIFY(notes);
        auto* grid=visualItem(roll,"pianoRollGrid"); QVERIFY(grid);
        auto* style=visualItem(roll,"pianoRollGridStyle"); QVERIFY(style);
        const auto restore=qScopeGuard([&] {
            QMetaObject::invokeMethod(notes,"clear"); style->setProperty("currentIndex",0); roll->setVisible(false);
        });
        roll->setVisible(true); QTest::qWait(30);
        QCOMPARE(style->property("count").toInt(),3);               // straight, triplet, dotted (no swing)
        const double ppb=roll->property("pixelsPerBeat").toDouble();
        const auto drawAt=[&](double beat) {
            auto* flick=grid->parentItem()->parentItem();
            const double y=flick->property("contentY").toDouble()+40;
            QTest::mouseClick(window,Qt::LeftButton,{},grid->mapToScene(QPointF(beat*ppb,y)).toPoint());
            return notes->property("notes").toList().last().toMap().value("startBeat").toDouble();
        };
        style->setProperty("currentIndex",1);                       // triplet 1/16 = 1/6 beat
        QCOMPARE(roll->property("snapStyle").toString(),QString("triplet"));
        QVERIFY(std::abs(drawAt(0.4)-1.0/3.0)<1e-6);
        QVERIFY(std::abs(roll->property("gridLineBeats").toDouble()-1.0/6.0)<1e-6);
        style->setProperty("currentIndex",2);                       // dotted 1/16 = 0.375 beat
        QVERIFY(std::abs(drawAt(3.9)-3.75)<1e-6);
        QVERIFY(std::abs(drawAt(4.2)-4.0)<1e-6);                    // grid restarts at bar 2 (past the 3.75 note)
    }
    void piano_roll_colour_box_switches_between_velocity_and_track_colour() {
        TrackListWrapper tracks; const int track=tracks.addInstrumentTrack();
        const int clip=tracks.addClipAtBeat(track,0,4); QVERIFY(clip>0);
        QVERIFY(tracks.setTrackColour(track,"#d97c73"));
        auto* roll=window->findChild<QQuickItem*>("testPianoRoll"); QVERIFY(roll);
        auto* mode=visualItem(roll,"pianoRollColorMode"); QVERIFY(mode);
        const auto restore=qScopeGuard([&] {
            mode->setProperty("currentIndex",1); roll->setVisible(false);
            roll->setProperty("trackModel",QVariant::fromValue<QObject*>(nullptr));
            roll->setProperty("trackId",-1); roll->setProperty("clipId",-1);
        });
        roll->setProperty("trackModel",QVariant::fromValue(&tracks));
        roll->setProperty("trackId",track); roll->setProperty("clipId",clip); roll->setVisible(true);
        QCOMPARE(mode->property("count").toInt(),2);                // Velocity, Track
        const auto fill=[&](int velocity) {
            QVariant colour;
            QMetaObject::invokeMethod(roll,"noteFillColor",Q_RETURN_ARG(QVariant,colour),Q_ARG(QVariant,velocity));
            return colour.value<QColor>();
        };
        const QColor track127=QColor("#d97c73").lighter(110);
        const auto near=[](QColor a,QColor b) {
            return std::abs(a.redF()-b.redF())<0.02 && std::abs(a.greenF()-b.greenF())<0.02 && std::abs(a.blueF()-b.blueF())<0.02;
        };
        // Track colour is the default: loud note = track colour, soft notes darker, and the
        // outline follows the track too.
        QCOMPARE(roll->property("colorMode").toString(),QString("Track"));
        QCOMPARE(mode->property("currentText").toString(),QString("Track"));
        QVERIFY(near(fill(127),track127));
        QVERIFY(fill(10).lightnessF()<fill(127).lightnessF());
        QVERIFY(near(roll->property("noteOutlineColour").value<QColor>(),QColor("#d97c73").lighter(145)));
        // Velocity mode: blue/teal scale and the cyan outline.
        mode->setProperty("currentIndex",0);
        QCOMPARE(roll->property("colorMode").toString(),QString("Velocity"));
        QVERIFY(!near(fill(127),track127));
        QVERIFY(near(roll->property("noteOutlineColour").value<QColor>(),QColor("#5bc0de")));
        // Back to Track; an unbound piano roll (no track colour) keeps the blue scale.
        mode->setProperty("currentIndex",1);
        QVERIFY(near(fill(127),track127));
        roll->setProperty("trackModel",QVariant::fromValue<QObject*>(nullptr));
        roll->setProperty("trackId",-1); roll->setProperty("clipId",-1);
        QVERIFY(!roll->property("useTrackColour").toBool());
        QVERIFY(near(roll->property("noteOutlineColour").value<QColor>(),QColor("#5bc0de")));
    }
    void removing_a_track_with_clips_asks_first() {
        TrackListWrapper tracks;
        const int empty=tracks.addAudioTrack();
        const int full=tracks.addInstrumentTrack(); QVERIFY(tracks.addClipAtBeat(full,0,4)>0);
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(list);
        const auto restore=qScopeGuard([&] { list->setVisible(false); list->setProperty("trackModel",QVariant()); });
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(true);
        QCoreApplication::processEvents();
        auto* dialog=window->findChild<QObject*>("removeTrackDialog"); QVERIFY(dialog);
        // Empty track: removed straight away, no dialog
        auto* removeEmpty=visualItem(list,"trackRemoveButton"+QString::number(empty)); QVERIFY(removeEmpty);
        click(removeEmpty);
        QTRY_COMPARE(tracks.count(),1); QVERIFY(!dialog->property("visible").toBool());
        // Track with a clip: asks; Cancel keeps it
        auto* removeFull=visualItem(list,"trackRemoveButton"+QString::number(full)); QVERIFY(removeFull);
        click(removeFull);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(dialog->property("clipCount").toInt(),1);
        verifyThemedDialog(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog,"reject"));
        QTRY_VERIFY(!dialog->property("visible").toBool()); QCOMPARE(tracks.count(),1);
        // ...Delete removes it; the dialog says it can be undone
        click(removeFull); QTRY_VERIFY(dialog->property("visible").toBool());
        auto* message=dialog->property("contentItem").value<QQuickItem*>(); QVERIFY(message);
        QVERIFY(message->property("text").toString().contains("Undo (Ctrl+Z)"));
        QVERIFY(QMetaObject::invokeMethod(dialog,"accept"));
        QTRY_COMPARE(tracks.count(),0);
        // Undo brings the track (and its card) back
        QVERIFY(tracks.undo());
        QTRY_COMPARE(tracks.count(),1); QCOMPARE(tracks.trackIdAt(0),full);
        QTRY_VERIFY(visualItem(list,"trackRemoveButton"+QString::number(full)));
        QCOMPARE(tracks.clips().size(),1);
    }
    void mixer_clip_gain_fader_follows_the_target_clip() {
        TrackListWrapper tracks;
        const int empty=tracks.addAudioTrack();
        const int withClip=tracks.addInstrumentTrack(); const int clip=tracks.addClipAtBeat(withClip,0,4); QVERIFY(clip>0);
        auto* mixer=window->findChild<QQuickItem*>("testMixer"); QVERIFY(mixer);
        const auto restore=qScopeGuard([&] { mixer->setVisible(false); mixer->setProperty("trackModel",QVariant()); });
        mixer->setProperty("trackModel",QVariant::fromValue(&tracks)); mixer->setVisible(true);
        QTest::qWait(30);
        auto* none=visualItem(mixer,"mixerClipGain"+QString::number(empty)); QVERIFY(none);
        auto* gain=visualItem(mixer,"mixerClipGain"+QString::number(withClip)); QVERIFY(gain);
        QVERIFY(!none->isEnabled()); QVERIFY(gain->isEnabled());
        QVERIFY(tracks.setClipGain(withClip,clip,-6.0));
        QTRY_COMPARE(gain->property("value").toDouble(),-6.0);
        // Clip gain is the strip's vertical fader; there is no track volume fader in the mixer.
        QCOMPARE(gain->property("orientation").toInt(),int(Qt::Vertical));
        QVERIFY(!visualItem(mixer,"mixerVolume"+QString::number(withClip)));
        // Double-click resets clip gain to 0 dB
        QTest::mouseDClick(window,Qt::LeftButton,Qt::NoModifier,gain->mapToScene(QPointF(gain->width()/2,gain->height()/2)).toPoint(),10);
        QTRY_COMPARE(tracks.project().findTrack(withClip)->audioClips[0].gainDb,0.0f);
        // Selecting the track's second clip moves the fader to that clip
        const int second=tracks.addClipAtBeat(withClip,8,4); QVERIFY(second>0);
        QVERIFY(tracks.setClipGain(withClip,second,4.5));
        QCOMPARE(gain->property("value").toDouble(),0.0);              // still the first clip
        tracks.selectClip(withClip,second);
        QTRY_COMPARE(gain->property("value").toDouble(),4.5);
    }
    void mixer_clip_gain_does_not_rebuild_the_timeline() {
        TrackListWrapper tracks; const int track=tracks.addInstrumentTrack();
        const int clip=tracks.addClipAtBeat(track,0,4); QVERIFY(clip>0);
        auto* mixer=window->findChild<QQuickItem*>("testMixer"); QVERIFY(mixer);
        auto* timeline=window->findChild<QQuickItem*>("testTimeline"); QVERIFY(timeline);
        const auto restore=qScopeGuard([&] {
            mixer->setVisible(false); mixer->setProperty("trackModel",QVariant());
            timeline->setVisible(false); timeline->setProperty("trackModel",QVariant());
        });
        timeline->setProperty("trackModel",QVariant::fromValue(&tracks)); timeline->setVisible(true);
        mixer->setProperty("trackModel",QVariant::fromValue(&tracks)); mixer->setVisible(true);
        QTest::qWait(30);
        QPointer<QQuickItem> block=visualItem(timeline,"clipBlock"+QString::number(clip)); QVERIFY(block);
        auto* fader=visualItem(mixer,"mixerClipGain"+QString::number(track)); QVERIFY(fader);
        // Drag the clip gain fader: the clip block must be the same object afterwards (not rebuilt)
        const QPoint start=fader->mapToScene(QPointF(fader->width()/2,fader->height()/2)).toPoint();
        const QPoint end=fader->mapToScene(QPointF(fader->width()/2,fader->height()*0.8)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        QTest::mouseMove(window,end,20);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,end);
        QVERIFY(tracks.project().findTrack(track)->audioClips[0].gainDb<0.0f);
        QVERIFY(block);
        QCOMPARE(visualItem(timeline,"clipBlock"+QString::number(clip)),block.data());
        auto* label=visualItem(mixer,"mixerClipLabel"+QString::number(track)); QVERIFY(label);
        QCOMPARE(label->property("text").toString(),QString("CLIP"));
        QVERIFY(label->mapToScene(QPointF(0,0)).y()>=fader->mapToScene(QPointF(0,fader->height())).y()-1); // under the fader
        QVERIFY(std::abs(label->mapToScene(QPointF(label->width()/2,0)).x()
                         -fader->mapToScene(QPointF(fader->width()/2,0)).x())<3); // centred on it
    }
    void mixer_clip_gain_drag_is_one_undo_step() {
        TrackListWrapper tracks; const int track=tracks.addInstrumentTrack();
        const int clip=tracks.addClipAtBeat(track,0,4); QVERIFY(clip>0);
        auto* mixer=window->findChild<QQuickItem*>("testMixer"); QVERIFY(mixer);
        const auto restore=qScopeGuard([&] { mixer->setVisible(false); mixer->setProperty("trackModel",QVariant()); });
        mixer->setProperty("trackModel",QVariant::fromValue(&tracks)); mixer->setVisible(true);
        QTest::qWait(30);
        auto* fader=visualItem(mixer,"mixerClipGain"+QString::number(track)); QVERIFY(fader);
        const auto gain=[&] { return tracks.project().findTrack(track)->audioClips[0].gainDb; };
        // Drag in several steps; the gain follows live during the drag
        const auto at=[&](double f) { return fader->mapToScene(QPointF(fader->width()/2,fader->height()*f)).toPoint(); };
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,at(0.5));
        float previous=gain();
        int changes=0;
        for(double f:{0.6,0.7,0.8,0.9}) {
            QTest::mouseMove(window,at(f),10);
            if(gain()!=previous){ ++changes; previous=gain(); }
        }
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,at(0.9));
        QVERIFY(changes>=3); QVERIFY(gain()<-1.0f);
        // One undo returns to the gain from before the drag
        QVERIFY(tracks.undo()); QCOMPARE(gain(),0.0f);
    }
    void master_scale_labels_sit_at_their_meter_levels() {
        auto* mixer=window->findChild<QQuickItem*>("testMixer"); QVERIFY(mixer);
        const auto restore=qScopeGuard([&] { mixer->setVisible(false); });
        mixer->setVisible(true); QTest::qWait(30);
        auto* meter=visualItem(mixer,"mixerMasterMeter"); QVERIFY(meter);
        auto* bar=meter->property("barItem").value<QQuickItem*>(); QVERIFY(bar); QVERIFY(bar->height()>40);
        const double top=bar->mapToScene(QPointF(0,0)).y(), height=bar->height();
        // The meter is linear in dB from -60 (empty) to 0 (full)
        for(const auto& [db,fraction]:std::initializer_list<std::pair<int,double>>{{0,1.0},{-6,0.9},{-12,0.8},{-24,0.6},{-48,0.2},{-60,0.0}}) {
            auto* label=visualItem(mixer,"mixerMasterScale"+QString::number(db)); QVERIFY2(label,qPrintable(QString::number(db)));
            const double centre=label->mapToScene(QPointF(0,label->height()/2)).y();
            QVERIFY2(std::abs(centre-(top+(1-fraction)*height))<1.5,qPrintable(QString::number(db)));
        }
        QVERIFY(!visualItem(mixer,"mixerMasterScale-90"));
    }
    void piano_roll_ruler_and_grid_follow_the_meter() {
        TrackListWrapper tracks; const int track=tracks.addInstrumentTrack();
        const int clip=tracks.addClipAtBeat(track,0,4); QVERIFY(clip>0);
        auto* roll=window->findChild<QQuickItem*>("testPianoRoll"); QVERIFY(roll);
        const auto restore=qScopeGuard([&] {
            roll->setVisible(false); roll->setProperty("trackModel",QVariant::fromValue<QObject*>(nullptr));
            roll->setProperty("trackId",-1); roll->setProperty("clipId",-1);
        });
        roll->setProperty("trackModel",QVariant::fromValue(&tracks));
        roll->setProperty("trackId",track); roll->setProperty("clipId",clip); roll->setVisible(true);
        QTest::qWait(30);
        const auto texts=[&] {
            QStringList result;
            std::function<void(QQuickItem*)> collect=[&](QQuickItem* item) {
                const auto text=item->property("text");
                if(text.isValid() && item->isVisible())result<<text.toString();
                for(auto* child:item->childItems())collect(child);
            };
            collect(roll); return result;
        };
        // 4/4: bar 2 starts after 4 beats, beats are labelled bar.beat
        QVERIFY(texts().contains("1.2")); QVERIFY(texts().contains("2")); QVERIFY(!texts().contains("1.5"));
        QCOMPARE(roll->property("beatsInBar").toInt(),4);
        // 6/8: six counted beats per bar
        tracks.setTempo(tracks.project().tempo().bpm(),6,8);
        QTRY_COMPARE(roll->property("beatsInBar").toInt(),6);
        QTRY_VERIFY(texts().contains("1.6"));
        QCOMPARE(roll->property("gridLinesPerBar").toInt()*roll->property("gridLineBeats").toDouble(),3.0);
    }
    void piano_roll_keys_and_notes_are_named_by_the_backend() {
        auto* roll=window->findChild<QQuickItem*>("testPianoRoll"); QVERIFY(roll);
        auto* notes=roll->findChild<QObject*>("pianoRollBackend"); QVERIFY(notes);
        const auto restore=qScopeGuard([&] { QMetaObject::invokeMethod(notes,"clear"); roll->setVisible(false); });
        roll->setVisible(true); QTest::qWait(30);
        QStringList texts;
        std::function<void(QQuickItem*)> collect=[&](QQuickItem* item) {
            const auto text=item->property("text");
            if(text.isValid() && item->isVisible())texts<<text.toString();
            for(auto* child:item->childItems())collect(child);
        };
        int id=-1;
        QVERIFY(QMetaObject::invokeMethod(notes,"addNote",Q_RETURN_ARG(int,id),Q_ARG(int,66),Q_ARG(double,0.0),Q_ARG(double,1.0),Q_ARG(int,100)));
        QTest::qWait(20);
        collect(roll);
        QVERIFY(texts.contains("C4"));                               // white key label
        QVERIFY(texts.contains("C#4"));                              // black key label
        QVERIFY(texts.contains("F#4"));                              // the note block's name
        QCOMPARE(roll->property("currentNoteText").toString().isEmpty(),false);
    }
    void transport_clock_shows_the_position_from_the_backend() {
        auto device=std::make_unique<MultiInputUiDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); controller.setRestoreSessionOnStartup(false);
        TrackListWrapper tracks; controller.attachProject(&tracks); tracks.addAudioTrack();
        const auto restore=qScopeGuard([&] { window->setProperty("transport",QVariant::fromValue(EngineController::instance())); });
        window->setProperty("transport",QVariant::fromValue(&controller));
        auto* clock=visualItem(window->contentItem(),"transportClock"); QVERIFY(clock);
        QCOMPARE(clock->property("text").toString(),QString("00:00:00.00"));
        controller.transportAction("play"); for(int i=0;i<200;++i)io->process();
        controller.transportAction("pause"); io->process();
        const double seconds=controller.positionSeconds(); QVERIFY(seconds>0);
        const qint64 cs=qint64(std::floor(seconds*100+1e-6)); // hundredths (plan 6.5)
        const QString expected=QString("%1:%2:%3.%4").arg(cs/360000,2,10,QChar('0')).arg(cs/6000%60,2,10,QChar('0'))
            .arg(cs/100%60,2,10,QChar('0')).arg(cs%100,2,10,QChar('0'));
        QTRY_COMPARE(clock->property("text").toString(),expected);
        controller.shutdownSession();
    }
    void restore_session_setting_has_one_owner() {
        auto* controller=EngineController::instance();
        const bool original=controller->restoreSessionOnStartup();
        auto* bridge=window->findChild<QObject*>("appSettingsBridge"); QVERIFY(bridge);
        const QString presentation=bridge->property("pianoRollPresentation").toString();
        const auto restore=qScopeGuard([&] {
            bridge->setProperty("pianoRollPresentation",presentation); controller->setRestoreSessionOnStartup(original);
        });
        // Changing another preference must not overwrite the restore setting
        controller->setRestoreSessionOnStartup(true);
        bridge->setProperty("pianoRollPresentation",presentation=="window" ? "dock" : "window");
        QVERIFY(controller->restoreSessionOnStartup());
        controller->setRestoreSessionOnStartup(false);
        bridge->setProperty("pianoRollPresentation",presentation);
        QVERIFY(!controller->restoreSessionOnStartup());
        QVERIFY(!bridge->property("restoreSessionOnStartup").isValid()); // no second copy
        // The menu tick follows the owner, also after being clicked
        auto* settings=visualItem(window->contentItem(),"settingsButton"); QVERIFY(settings); click(settings);
        QQuickItem* option=nullptr;
        QTRY_VERIFY((option=visualItem(window->contentItem(),"restoreSessionSetting")));
        QVERIFY(!option->property("checked").toBool());
        click(option); QTRY_VERIFY(controller->restoreSessionOnStartup());
        controller->setRestoreSessionOnStartup(false);
        QTRY_VERIFY(!option->property("checked").toBool());
        controller->setRestoreSessionOnStartup(true);
        QTRY_VERIFY(option->property("checked").toBool());
        auto* menu=window->findChild<QObject*>("settingsMenu");
        if(menu)QMetaObject::invokeMethod(menu,"close");
        QTest::keyClick(window,Qt::Key_Escape);
    }
    void unavailable_audio_does_not_simulate_playback() {
        auto* play=visualItem(window->contentItem(),"transport_play"); QVERIFY(play); click(play);
        QVERIFY(!EngineController::instance()->playing());
        QVERIFY(EngineController::instance()->transportMessage().contains("unavailable"));
    }
private:
    QTemporaryDir storage{QStringLiteral("/tmp/chaosmaker-ui-XXXXXX")};
    static QVariantList selectedIds(QObject* timeline) {
        const auto value = timeline->property("selectedClipIds");
        if (value.metaType() == QMetaType::fromType<QJSValue>())
            return value.value<QJSValue>().toVariant().toList();
        return value.toList();
    }
    static QObject* visualObjectIn(QQuickItem* root,const QString& name) {
        if(root->objectName()==name)return root;
        if(auto* object=root->findChild<QObject*>(name))return object;
        for(auto* child:root->childItems())if(auto* result=visualObjectIn(child,name))return result;
        return nullptr;
    }
    static QQuickItem* visualItemIn(QQuickItem* root,const QString& name) {
        if(root->objectName()==name)return root;
        for(auto* child:root->childItems())if(auto* result=visualItemIn(child,name))return result;
        return nullptr;
    }
    // The app's other open windows (the FX panel floats in one of its own), for a search
    // that starts at a window's content item and finds nothing there.
    static QList<QQuickItem*> otherWindowRoots(QQuickItem* root) {
        QList<QQuickItem*> roots;
        if(!root->window()||root!=root->window()->contentItem())return roots;
        for(auto* top:QGuiApplication::topLevelWindows())
            if(auto* quick=qobject_cast<QQuickWindow*>(top);quick&&quick!=root->window()&&quick->isVisible())
                roots.append(quick->contentItem());
        return roots;
    }
    static QObject* visualObject(QQuickItem* root,const QString& name) {
        if(auto* found=visualObjectIn(root,name))return found;
        for(auto* other:otherWindowRoots(root))if(auto* found=visualObjectIn(other,name))return found;
        return nullptr;
    }
    static QQuickItem* visualItem(QQuickItem* root,const QString& name) {
        if(auto* found=visualItemIn(root,name))return found;
        for(auto* other:otherWindowRoots(root))if(auto* found=visualItemIn(other,name))return found;
        return nullptr;
    }
    void chooseEditItem(const char* name) {
        auto* button=visualItem(window->contentItem(),"editMenuButton"); QVERIFY(button);
        auto* menu=window->findChild<QObject*>("editMenu"); QVERIFY(menu);
        click(button); QTRY_VERIFY(menu->property("visible").toBool());
        auto* entry=visualItem(window->contentItem(),name); QVERIFY2(entry,name); QVERIFY2(entry->isEnabled(),name);
        click(entry); QTRY_VERIFY(!menu->property("visible").toBool());
    }
    void openQuickSettings() {
        auto* popup=window->findChild<QObject*>("quickSettingsPopup"); QVERIFY(popup);
        if(!popup->property("visible").toBool()) {
            auto* button=visualItem(window->contentItem(),"quickSettingsButton"); QVERIFY(button); click(button);
        }
        QTRY_VERIFY(popup->property("visible").toBool()); QTest::qWait(20);
    }
    // The open FX panel's own window (it floats apart from the main one); the main window
    // when no panel is open. Keys and drags meant for the panel go here.
    QWindow* fxWindow() {
        for(auto* top:QGuiApplication::topLevelWindows())
            if(auto* quick=qobject_cast<QQuickWindow*>(top);quick&&quick!=window&&quick->isVisible()
               &&quick->objectName().startsWith("trackEffectsPanel"))
                return quick;
        return window;
    }
    // Clicks the item's middle in whichever window shows it (the FX panel has its own).
    void click(QQuickItem* item) {
        QWindow* target=item->window()?static_cast<QWindow*>(item->window()):static_cast<QWindow*>(window);
        QTest::mouseClick(target,Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());
    }
    // Adds an effect through the open FX panel's "+ Add FX" picker, as a user would.
    void addEffectFromPicker(const QString& type) {
        auto* add=visualItem(window->contentItem(),"addEffectButton"); QVERIFY(add); QVERIFY(add->isEnabled()); click(add);
        QQuickItem* choice=nullptr;
        QTRY_VERIFY((choice=visualItem(window->contentItem(),"addEffect_"+type)) && choice->isVisible());
        // The picker's rows are rebuilt when the track or chain changes; let the list lay out
        // (a frame) and look the row up again, or the click can land on a row's old position.
        QTest::qWait(30);
        QTRY_VERIFY((choice=visualItem(window->contentItem(),"addEffect_"+type)) && choice->isVisible());
        click(choice);
        QTRY_VERIFY(visualItem(window->contentItem(),"selectEffect_"+type));
    }
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window=nullptr;QQuickItem* field=nullptr;QObject* bar=nullptr;
};
QTEST_MAIN(TransportUiTest)
#include "transport_qtcheck.moc"
