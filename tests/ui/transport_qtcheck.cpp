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
    void cleanupTestCase() { engine.reset(); }
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
        QCOMPARE(model->count(),5);
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
    void unavailable_audio_does_not_simulate_playback() {
        auto* play=visualItem(window->contentItem(),"transport_play"); QVERIFY(play); click(play);
        QVERIFY(!EngineController::instance()->playing());
        QVERIFY(EngineController::instance()->transportMessage().contains("unavailable"));
    }
private:
    QTemporaryDir storage{QStringLiteral("/tmp/chaosmaker-ui-XXXXXX")};
    static QQuickItem* visualItem(QQuickItem* root,const QString& name) {
        if(root->objectName()==name)return root;
        for(auto* child:root->childItems())if(auto* result=visualItem(child,name))return result;
        return nullptr;
    }
    void click(QQuickItem* item) {
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());
    }
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window=nullptr;QQuickItem* field=nullptr;QObject* bar=nullptr;
};
QTEST_MAIN(TransportUiTest)
#include "transport_qtcheck.moc"
