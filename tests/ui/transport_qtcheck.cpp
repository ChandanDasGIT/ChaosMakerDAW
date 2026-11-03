#include <QtTest>
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/pluginWrap/guitarsuitemodel.h"
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QPointer>
#include <QQuickWindow>
#include <memory>
#include <QSettings>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QAbstractItemModelTester>
#include <QJSValue>
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

class TransportUiTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QVERIFY(storage.isValid());
        qputenv("XDG_DATA_HOME", storage.path().toUtf8());
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
        QVERIFY(!popup->property("visible").toBool()); QVERIFY(!field->isVisible());
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
        click(button); QTRY_VERIFY(!popup->property("visible").toBool());
        QVERIFY(!button->property("checked").toBool());
        click(button); QTRY_VERIFY(popup->property("visible").toBool()); QVERIFY(button->property("checked").toBool());
        const QSize original=window->size(); window->resize(1000,800); QTest::qWait(30); verifyCenter();
        window->resize(original); QTest::qWait(30); verifyCenter();
        QVERIFY(field->isVisible());
        auto* numerator=visualItem(window->contentItem(),"meterNumerator");
        auto* denominator=visualItem(window->contentItem(),"meterDenominator");
        QVERIFY(numerator); QVERIFY(denominator);
        QVERIFY(visualItem(window->contentItem(),"metronomeToggle"));
        QVERIFY(visualItem(window->contentItem(),"metronomeVolume"));
        QVERIFY(visualItem(window->contentItem(),"countInToggle"));
        click(numerator); QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
        QTest::keyClick(window,Qt::Key_3); QTest::keyClick(window,Qt::Key_Return);
        QCOMPARE(controller->numerator(),3);
        click(denominator); QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
        QTest::keyClick(window,Qt::Key_8); QTest::keyClick(window,Qt::Key_Return);
        QCOMPARE(controller->denominator(),8);
        click(field); QTest::qWait(10); QTest::keyClick(window,Qt::Key_9); QTest::keyClick(window,Qt::Key_5);
        auto* close=visualItem(window->contentItem(),"closeQuickSettings"); QVERIFY(close); click(close);
        QTRY_VERIFY(!popup->property("visible").toBool()); QCOMPARE(controller->bpm(),95.0);
        QVERIFY(!button->property("checked").toBool());
        click(button); QTRY_VERIFY(popup->property("visible").toBool()); QCOMPARE(field->property("text").toString(),QString("95"));
        QTest::keyClick(window,Qt::Key_Escape); QTRY_VERIFY(!popup->property("visible").toBool());
        QVERIFY(!button->property("checked").toBool());
        click(button); QTRY_VERIFY(popup->property("visible").toBool());
        auto* other=window->findChild<QQuickItem*>("otherFocus"); QVERIFY(other); click(other);
        QTRY_VERIFY(!popup->property("visible").toBool()); QVERIFY(!field->isVisible());
        QVERIFY(!button->property("checked").toBool());
        QVERIFY(other->hasActiveFocus());
        controller->setMeter(4,4);
    }
    void recording_count_in_uses_one_bar_and_can_be_cancelled() {
        auto device=std::make_unique<MultiInputUiDevice>(); auto* io=device.get();
        EngineController controller(std::move(device),false); controller.setRestoreSessionOnStartup(false);
        TrackListModel tracks; controller.attachProject(&tracks); const int track=tracks.addAudioTrack();
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
        TrackListModel tracks; controller.attachProject(&tracks);
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
        TrackListModel tracks; controller.attachProject(&tracks);
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
        TrackListModel tracks; controller.attachProject(&tracks);
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
        TrackListModel tracks; controller.attachProject(&tracks);
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
    void factory_preset_picker_applies_djent_and_jazz() {
        auto* model=qobject_cast<GuitarSuiteModel*>(EngineController::instance()->guitarSuite());QVERIFY(model);
        auto* board=window->findChild<QObject*>("guitarSuitePanel");QVERIFY(board);
        QVERIFY(QMetaObject::invokeMethod(board,"open"));QTRY_VERIFY(board->property("visible").toBool());
        auto* picker=visualItem(window->contentItem(),"guitarFactoryPreset");
        auto* apply=visualItem(window->contentItem(),"applyGuitarFactoryPreset");QVERIFY(picker);QVERIFY(apply);
        QCOMPARE(picker->property("count").toInt(),2);
        picker->setProperty("currentIndex",0);click(apply);
        QCOMPARE(model->count(),7);
        QCOMPARE(model->data(model->index(1),GuitarSuiteModel::PedalTypeRole).toString(),QString("pedal.distortion"));
        picker->setProperty("currentIndex",1);click(apply);
        QCOMPARE(model->count(),5);
        QCOMPARE(model->data(model->index(0),GuitarSuiteModel::PedalTypeRole).toString(),QString("pedal.compressor"));
        QCOMPARE(model->data(model->index(4),GuitarSuiteModel::PedalTypeRole).toString(),QString("pedal.reverb"));
        QVERIFY(QMetaObject::invokeMethod(board,"close"));
        while(model->count())QVERIFY(model->removePedal(model->data(model->index(0),GuitarSuiteModel::PedalIdRole).toInt()));
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
    void startup_setting_saves_and_restores_session_stopped() {
        auto* controller=EngineController::instance();
        controller->setRestoreSessionOnStartup(false);
        TrackListModel first; controller->attachProject(&first);
        const int track=first.addInstrumentTrackOfType("Electric Guitar");
        first.setVolume(track,0.42); first.setRecordArmed(track,true);
        controller->setBpm(88); controller->setMeter(3,4);
        auto* settings=visualItem(window->contentItem(),"settingsButton"); QVERIFY(settings); click(settings);
        QQuickItem* option=nullptr;
        QTRY_VERIFY((option=visualItem(window->contentItem(),"restoreSessionSetting")));
        click(option); QTRY_VERIFY(controller->restoreSessionOnStartup());
        QVERIFY(controller->saveSession());
        TrackListModel second; controller->attachProject(&second);
        QCOMPARE(second.count(),1); QCOMPARE(controller->bpm(),88.0); QCOMPARE(controller->numerator(),3);
        QVERIFY(!controller->playing()); QVERIFY(!controller->recording()); QVERIFY(!second.cleanMonitorArmed());
        QCOMPARE(second.data(second.index(0),TrackListModel::VolumeRole).toFloat(),0.42f);
        controller->setRestoreSessionOnStartup(false);
    }
    void track_effects_switch_in_place_and_keep_edits_on_the_selected_track() {
        auto* controller=EngineController::instance();
        controller->setRestoreSessionOnStartup(false);
        TrackListModel tracks; controller->attachProject(&tracks);
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
        auto* pedals=visualItem(window->contentItem(),"selectEffect_pedals"); QVERIFY(pedals); click(pedals);
        QTRY_COMPARE(panel->property("selectedEffect").toString(),QString("pedals"));
        QPointer<QQuickItem> dial=visualItem(window->contentItem(),"pedal.parametriceq.lowfreq"); QVERIFY(dial);
        const int id=guitarFx->data(guitarFx->index(0),GuitarSuiteModel::PedalIdRole).toInt();
        QVERIFY(guitarFx->setParameter(id,"lowfreq",300)); QTRY_COMPARE(dial->property("value").toDouble(),300.0);
        auto* eq=visualItem(window->contentItem(),"selectEffect_eq"); QVERIFY(eq); click(eq);
        QTRY_COMPARE(panel->property("selectedEffect").toString(),QString("eq"));
        QVERIFY(panel->property("visible").toBool()); QVERIFY(tracks.setEqBand(guitar,5,4));
        auto* band=visualItem(window->contentItem(),"trackEqBand5"); QVERIFY(band);
        QTRY_COMPARE(band->property("value").toDouble(),4.0);
        click(pedals); QVERIFY(dial); QTRY_COMPARE(dial->property("value").toDouble(),300.0);
        QCOMPARE(vocalFx->count(),0); QCOMPARE(tracks.project().findTrack(vocal)->eqGainDb[5],0.0f);
        auto* search=visualItem(window->contentItem(),"effectSearch"); QVERIFY(search);
        search->setProperty("text","guitar"); QTRY_VERIFY(!eq->isVisible()); QVERIFY(pedals->isVisible());
        search->setProperty("text",""); QTRY_VERIFY(eq->isVisible());
        // The FX workspace stays open while editing the transport.
        click(field); QTest::keyClick(window,Qt::Key_9); QTest::keyClick(window,Qt::Key_1); QTest::keyClick(window,Qt::Key_Return);
        QCOMPARE(controller->bpm(),91.0); QVERIFY(panel->property("visible").toBool());
        window->grabWindow().save("/tmp/projectdaw-track-effects.png");
        QPointer<QObject> guardedPanel=panel;
        QVERIFY(tracks.removeTrack(guitar));
        QTRY_VERIFY(guardedPanel.isNull());
        list->setVisible(false); list->setProperty("trackModel",QVariant());
    }
    void vocal_effects_offer_eq_and_recording_controls_without_guitarsuite() {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListModel tracks; controller->attachProject(&tracks); const int vocal=tracks.addVocalTrack();
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
        QCOMPARE(panel->property("selectedEffect").toString(),QString("eq"));
        QVERIFY(!visualItem(window->contentItem(),"selectEffect_pedals"));
        QVERIFY(!visualObject(window->contentItem(),"trackGuitarSuiteEditor"+QString::number(vocal)));
        auto* band=visualItem(window->contentItem(),"trackEqBand5"); QVERIFY(band); QVERIFY(band->isVisible());
        band->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Up);
        QCOMPARE(tracks.project().findTrack(vocal)->eqGainDb[5],0.5f);
        auto* mode=visualItem(window->contentItem(),"trackRecordingMode"); QVERIFY(mode);
        QVERIFY(mode->isVisible()); QVERIFY(mode->isEnabled());
        mode->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Down);
        QTRY_VERIFY(tracks.project().findTrack(vocal)->recordDry);
        QTest::keyClick(window,Qt::Key_Up); QTRY_VERIFY(!tracks.project().findTrack(vocal)->recordDry);
        // Unsupported navigation cannot reveal the pedal page, including after reopen.
        panel->setProperty("selectedEffect","pedals");
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
    void double_click_empty_track_area_adds_vocals_without_interfering_with_renaming() {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListModel tracks; controller->attachProject(&tracks);
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
    void track_names_edit_inline_commit_cancel_and_update_mixer_and_fx() {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListModel tracks; controller->attachProject(&tracks);
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
    void track_colour_picker_updates_cards_timeline_and_mixer_and_cancels_custom_edits() {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListModel tracks; controller->attachProject(&tracks);
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
        QVERIFY(window->grabWindow().save("/tmp/chaosmaker-track-colours.png"));
        for(auto* view:{list,mixer,timeline}) { view->setVisible(false); view->setProperty("trackModel",QVariant()); }
        list->setHeight(650); timeline->setX(0); timeline->setWidth(1000);
    }
    void adjustable_snap_grid_applies_to_group_edits_and_ranges_data() {
        QTest::addColumn<int>("mode"); QTest::addColumn<int>("numerator"); QTest::addColumn<int>("denominator");
        for(int mode=0;mode<7;++mode)QTest::newRow(qPrintable(QString("4/4 mode %1").arg(mode))) << mode << 4 << 4;
        for(int mode:{0,1,3,6})QTest::newRow(qPrintable(QString("3/8 mode %1").arg(mode))) << mode << 3 << 8;
    }
    void adjustable_snap_grid_applies_to_group_edits_and_ranges() {
        QFETCH(int,mode); QFETCH(int,numerator); QFETCH(int,denominator);
        auto* controller=EngineController::instance(); controller->setMeter(numerator,denominator);
        TrackListModel tracks; const int a=tracks.addAudioTrack(),b=tracks.addAudioTrack();
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
        TrackListModel tracks; const int track=tracks.addAudioTrack(),clip=tracks.addClipAtBeat(track,2,1);
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
        TrackListModel tracks; controller.attachProject(&tracks);
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
        QVERIFY(scroll->property("contentX").toDouble()>trackpadX);
        QCOMPARE(timeline->property("pixelsPerSecond").toDouble(),zoom);
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
        TrackListModel tracks; controller.attachProject(&tracks);
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
        TrackListModel tracks; controller.attachProject(&tracks); const int track=tracks.addAudioTrack();
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
        QCOMPARE(timeline->property("selectedClipIds").value<QJSValue>().toVariant().toList().size(),0);
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
        TrackListModel tracks; const int a=tracks.addAudioTrack(),b=tracks.addAudioTrack(),c=tracks.addAudioTrack();
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
        QCOMPARE(timeline->property("selectedClipIds").value<QJSValue>().toVariant().toList().size(),2);
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
        TrackListModel tracks; const int a=tracks.addAudioTrack(),b=tracks.addAudioTrack();
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
        QCOMPARE(timeline->property("selectedClipIds").value<QJSValue>().toVariant().toList().size(),2);
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
        QCOMPARE(timeline->property("selectedClipIds").value<QJSValue>().toVariant().toList().size(),3);
    }
    void undo_redo_buttons_restore_deleted_clips() {
        TrackListModel tracks;
        auto* top=window->findChild<QObject*>("testTopBar"); QVERIFY(top);
        top->setProperty("trackModel",QVariant::fromValue(&tracks));
        const int track=tracks.addAudioTrack(); const int clip=tracks.addClipAtBeat(track,0,4);
        QVERIFY(tracks.removeAudioClip(track,clip));
        auto* undo=visualItem(window->contentItem(),"undoClipEdit");
        auto* redo=visualItem(window->contentItem(),"redoClipEdit"); QVERIFY(undo); QVERIFY(redo);
        QVERIFY(undo->isEnabled()); click(undo); QCOMPARE(tracks.clips().size(),1);
        QVERIFY(redo->isEnabled()); click(redo); QCOMPARE(tracks.clips().size(),0);
        top->setProperty("trackModel",QVariant());
    }
    void timeline_bpm_edit_updates_grid_and_preserves_recorded_audio_timing() {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListModel tracks; controller->attachProject(&tracks); controller->setMeter(4,4);
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
        TrackListModel tracks; const int track=tracks.addAudioTrack(); const int clip=tracks.addClipAtBeat(track,1,4);
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
        TrackListModel tracks; const int track=tracks.addAudioTrack();
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
    void timeline_left_and_right_handles_trim_without_moving_source() {
        TrackListModel tracks; const int track=tracks.addAudioTrack(); const int clip=tracks.addClipAtBeat(track,1,4);
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
        TrackListModel tracks; controller.attachProject(&tracks);
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
        TrackListModel reopened; QString error;
        QVERIFY2(reopened.restoreSessionState(tracks.sessionState(),error),qPrintable(error));
        QVERIFY(!reopened.project().tracks()[0]->monitorEnabled);
        QVERIFY(!reopened.project().tracks()[1]->monitorEnabled);
        QVERIFY(!reopened.project().tracks()[0]->recordArmed);
        controller.shutdownSession();
    }
    void dragging_track_headers_reorders_mixer_lanes_and_saved_order() {
        TrackListModel tracks;
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
        QSignalSpy changes(&tracks,&TrackListModel::projectChanged);
        QSignalSpy countChanges(&tracks,&TrackListModel::countChanged);
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
        TrackListModel reopened; QString error;
        QVERIFY2(reopened.restoreSessionState(tracks.sessionState(),error),qPrintable(error));
        for(int row=0;row<tracks.count();++row)
            QCOMPARE(reopened.data(reopened.index(row),TrackListModel::NameRole),
                     tracks.data(tracks.index(row),TrackListModel::NameRole));
    }
    void mixer_tracks_follow_add_remove_and_project_replacement() {
        TrackListModel tracks;
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
        TrackListModel replacement; const int guitar=replacement.addInstrumentTrackOfType("Electric Guitar");
        replacement.setVolume(guitar,0.5); replacement.setSolo(guitar,true);
        tracks.adoptDocument(replacement); QTRY_COMPARE(mixer->property("stripCount").toInt(),1);
        auto* slider=visualItem(mixer,"mixerVolume"+QString::number(guitar)); QVERIFY(slider);
        QCOMPARE(slider->property("value").toDouble(),0.5);
        auto* solo=visualItem(mixer,"mixerSolo"+QString::number(guitar)); QVERIFY(solo);
        QVERIFY(solo->property("active").toBool());
        QTRY_COMPARE(scroll->property("contentWidth").toDouble(),108.0);
        TrackListModel empty; tracks.adoptDocument(empty); QTRY_COMPARE(mixer->property("stripCount").toInt(),0);
        QTRY_COMPARE(scroll->property("contentWidth").toDouble(),0.0);
        mixer->setVisible(false); mixer->setProperty("trackModel",QVariant());
    }
    void mixer_controls_update_shared_track_state_and_follow_external_changes() {
        TrackListModel tracks; const int first=tracks.addAudioTrack(), second=tracks.addVocalTrack();
        auto* mixer=window->findChild<QQuickItem*>("testMixer"); QVERIFY(mixer);
        mixer->setProperty("trackModel",QVariant::fromValue(&tracks)); mixer->setVisible(true);
        QCoreApplication::processEvents();
        auto* mute=visualItem(mixer,"mixerMute"+QString::number(first));
        auto* solo=visualItem(mixer,"mixerSolo"+QString::number(first));
        auto* slider=visualItem(mixer,"mixerVolume"+QString::number(first));
        QVERIFY(mute); QVERIFY(solo); QVERIFY(slider);
        QTest::qWait(30); // Allow the newly visible ColumnLayout to place its controls.
        click(mute); click(solo);
        QVERIFY(tracks.project().findTrack(first)->muted); QVERIFY(tracks.project().findTrack(first)->solo);
        QVERIFY(!tracks.project().findTrack(second)->muted); QVERIFY(!tracks.project().findTrack(second)->solo);
        tracks.setMuted(first,false); tracks.setSolo(first,false); tracks.setVolume(first,0.75);
        QVERIFY(!mute->property("active").toBool()); QVERIFY(!solo->property("active").toBool());
        QCOMPARE(slider->property("value").toDouble(),0.75);
        slider->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Up);
        QVERIFY(std::abs(tracks.project().findTrack(first)->volume-0.76f)<1e-6);
        QCOMPARE(tracks.project().findTrack(second)->volume,1.0f);
        // A user movement must not detach later model-to-slider updates.
        tracks.setVolume(first,0.5); QCOMPARE(slider->property("value").toDouble(),0.5);
        tracks.setVolume(first,2); QCOMPARE(slider->property("value").toDouble(),2.0);
        tracks.setVolume(first,0); QCOMPARE(slider->property("value").toDouble(),0.0);
        mixer->setVisible(false); mixer->setProperty("trackModel",QVariant());
    }
    void volume_faders_double_click_restores_unity_gain_data() {
        QTest::addColumn<bool>("useMixer");
        QTest::addColumn<double>("initialGain");
        QTest::newRow("mixer silent") << true << 0.0;
        QTest::newRow("mixer attenuated") << true << 0.35;
        QTest::newRow("mixer boosted") << true << 1.8;
        QTest::newRow("track silent") << false << 0.0;
        QTest::newRow("track attenuated") << false << 0.35;
        QTest::newRow("track boosted") << false << 1.8;
    }
    void volume_faders_double_click_restores_unity_gain() {
        QFETCH(bool,useMixer); QFETCH(double,initialGain);
        TrackListModel tracks; const int first=tracks.addAudioTrack(),second=tracks.addVocalTrack();
        QVERIFY(tracks.setVolume(first,initialGain)); QVERIFY(tracks.setVolume(second,0.5));
        auto* mixer=window->findChild<QQuickItem*>("testMixer");
        auto* list=window->findChild<QQuickItem*>("testTrackList"); QVERIFY(mixer); QVERIFY(list);
        const auto restore=qScopeGuard([&] {
            mixer->setVisible(false); mixer->setProperty("trackModel",QVariant());
            list->setVisible(false); list->setProperty("trackModel",QVariant());
        });
        mixer->setProperty("trackModel",QVariant::fromValue(&tracks)); mixer->setVisible(useMixer);
        list->setProperty("trackModel",QVariant::fromValue(&tracks)); list->setVisible(!useMixer);
        QTest::qWait(30);
        auto* mixerFader=visualItem(mixer,"mixerVolume"+QString::number(first));
        auto* trackFader=visualItem(list,"trackVolume"+QString::number(first));
        QVERIFY(mixerFader); QVERIFY(trackFader);
        auto* fader=useMixer ? mixerFader : trackFader;
        const QPoint resetPoint=fader->mapToScene(useMixer
            ? QPointF(fader->width()/2,fader->height()*0.2)
            : QPointF(fader->width()*0.2,fader->height()/2)).toPoint();
        QTest::mouseDClick(window,Qt::LeftButton,Qt::NoModifier,resetPoint,10);
        QTRY_COMPARE(tracks.project().findTrack(first)->volume,1.0f);
        QCOMPARE(mixerFader->property("value").toDouble(),1.0);
        QCOMPARE(trackFader->property("value").toDouble(),1.0);
        QCOMPARE(tracks.project().findTrack(second)->volume,0.5f);
        // The double-click handler must leave ordinary dragging usable.
        const QPoint start=fader->mapToScene(QPointF(fader->width()/2,fader->height()/2)).toPoint();
        const QPoint end=fader->mapToScene(useMixer
            ? QPointF(fader->width()/2,fader->height()*0.1)
            : QPointF(fader->width()*0.9,fader->height()/2)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        QTest::mouseMove(window,end,20);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,end);
        QVERIFY(tracks.project().findTrack(first)->volume>1.1f);
        QVERIFY(tracks.setVolume(first,0.5));
        QCOMPARE(mixerFader->property("value").toDouble(),0.5);
        QCOMPARE(trackFader->property("value").toDouble(),0.5);
    }
    void mixer_split_meters_show_independent_channels_beside_the_vertical_fader() {
        TrackListModel tracks; const int a=tracks.addAudioTrack(), b=tracks.addVocalTrack();
        auto* mixer=window->findChild<QQuickItem*>("testMixer"); QVERIFY(mixer);
        mixer->setProperty("trackModel",QVariant::fromValue(&tracks)); mixer->setVisible(true);
        QVariantMap levels{{QString::number(a),QVariantMap{{"inputLeftDb",-6.0},{"inputRightDb",-18.0},
            {"outputLeftDb",1.0},{"outputRightDb",-12.0},{"outputLeftClip",true}}}};
        mixer->setProperty("levels",levels); QTest::qWait(30);
        auto* input=visualItem(mixer,"mixerInput"+QString::number(a));
        auto* output=visualItem(mixer,"mixerOutput"+QString::number(a));
        auto* fader=visualItem(mixer,"mixerVolume"+QString::number(a));
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
        TrackListModel tracks; controller.attachProject(&tracks);
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
    void new_project_prompts_before_discarding_unsaved_clips() {
        auto* controller=EngineController::instance(); controller->setRestoreSessionOnStartup(false);
        TrackListModel tracks; controller->attachProject(&tracks); const int track=tracks.addAudioTrack(); tracks.addClipAtBeat(track,0,4);
        auto* top=window->findChild<QObject*>("testTopBar"); QVERIFY(top); top->setProperty("trackModel",QVariant::fromValue(&tracks));
        auto* button=visualItem(window->contentItem(),"projectNew"); QVERIFY(button); click(button);
        auto* dialog=window->findChild<QObject*>("unsavedProjectChanges"); QVERIFY(dialog); QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog,"reject")); QCOMPARE(tracks.clips().size(),1);
        QVERIFY(controller->projectDirty()); top->setProperty("trackModel",QVariant());
    }
    void unavailable_audio_does_not_simulate_playback() {
        auto* play=visualItem(window->contentItem(),"transport_play"); QVERIFY(play); click(play);
        QVERIFY(!EngineController::instance()->playing());
        QVERIFY(EngineController::instance()->transportMessage().contains("unavailable"));
    }
private:
    QTemporaryDir storage{QStringLiteral("/tmp/chaosmaker-ui-XXXXXX")};
    static QObject* visualObject(QQuickItem* root,const QString& name) {
        if(root->objectName()==name)return root;
        if(auto* object=root->findChild<QObject*>(name))return object;
        for(auto* child:root->childItems())if(auto* result=visualObject(child,name))return result;
        return nullptr;
    }
    static QQuickItem* visualItem(QQuickItem* root,const QString& name) {
        if(root->objectName()==name)return root;
        for(auto* child:root->childItems())if(auto* result=visualItem(child,name))return result;
        return nullptr;
    }
    void openQuickSettings() {
        auto* popup=window->findChild<QObject*>("quickSettingsPopup"); QVERIFY(popup);
        if(!popup->property("visible").toBool()) {
            auto* button=visualItem(window->contentItem(),"quickSettingsButton"); QVERIFY(button); click(button);
        }
        QTRY_VERIFY(popup->property("visible").toBool()); QTest::qWait(20);
    }
    void click(QQuickItem* item) {
        if(item==field || item->objectName()=="bpmIncrease" || item->objectName()=="bpmDecrease")openQuickSettings();
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());
    }
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window=nullptr;QQuickItem* field=nullptr;QObject* bar=nullptr;
};
QTEST_MAIN(TransportUiTest)
#include "transport_qtcheck.moc"
