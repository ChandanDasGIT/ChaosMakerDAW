import QtQuick
import QtQuick.Controls
import TransportTests
Window {
    id: harness
    width: 1100; height: 900; visible: true
    property var transport: EngineController
    TransportBar {
        id: testTransportBar
        snapMode: testTimeline.snapMode
        onSnapModeEdited: value => testTimeline.snapMode = value
        ripple: testTimeline.ripple
        onRippleEdited: on => testTimeline.rippleEdited(on)
        transport: harness.transport
        objectName: "transport"
        width: parent.width
        anchors.bottom: parent.bottom
        bpm: EngineController.bpm
        timeSignature: EngineController.numerator
        timeSignatureDenominator: EngineController.denominator
        onNumeratorEdited: value => EngineController.setMeter(value, EngineController.denominator)
        onDenominatorEdited: value => EngineController.setMeter(EngineController.numerator, value)
        playing: transport.playing
        recording: transport.recording
        onTransportAction: action => transport.transportAction(action)
        onBpmEdited: value => EngineController.bpm = value
    }
    TopBar {
        id: testTopBar
        objectName: "testTopBar"; width: parent.width; timeline: testTimeline
        bpm: testTransportBar.bpm; tapCount: testTransportBar.tapCount
        onTapTempoRequested: testTransportBar.tapTempo()
        onBpmEdited: value => EngineController.bpm = value
        onExportRequested: testExportDialog.open()
    }
    ExportDialog {
        id: testExportDialog
        hasTimeSelection: testTimeline.hasTimeSelection
        selectionStartBeat: testTimeline.rangeStartBeat
        selectionEndBeat: testTimeline.rangeEndBeat
    }
    MixerPanel {
        objectName: "testMixer"
        x: 0; y: 650; width: parent.width; height: 166
        visible: false
    }
    GuitarSuitePanel { trackName: "Transport interaction test" }
    StatusStrip {
        objectName: "testStatusStrip"
        x: 0; y: 600; width: parent.width
        visible: false
    }
    TrackList {
        objectName: "testTrackList"
        x: 0; y: 90; width: 340; height: 650
        visible: false
    }
    Timeline {
        id: testTimeline
        objectName: "testTimeline"
        bpm: EngineController.bpm
        timeSignature: EngineController.numerator
        timeSignatureDenominator: EngineController.denominator
        x: 0; y: 100; width: 1000; height: 500
        laneCount: trackModel ? trackModel.count : 0
        visible: false
        onRippleEdited: on => ripple = on   // the app saves it in its settings (Main.qml)
    }
    // Like the app's docked piano roll: always present in the window, hidden by default.
    PianoRollView {
        objectName: "testPianoRoll"
        x: 0; y: 100; width: 1000; height: 500
        visible: false
    }
    Button {
        objectName: "otherFocus"
        y: 52
        text: "Other control"
        onClicked: forceActiveFocus()
    }
}
