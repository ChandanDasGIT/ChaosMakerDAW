import QtQuick
import QtQuick.Controls
import TransportTests
Window {
    width: 1100; height: 900; visible: true
    TransportBar {
        objectName: "transport"
        width: parent.width
        anchors.bottom: parent.bottom
        bpm: EngineController.bpm
        onTransportAction: action => EngineController.transportAction(action)
        onBpmEdited: value => EngineController.bpm = value
    }
    TopBar { objectName: "testTopBar"; width: parent.width }
    MixerPanel {
        objectName: "testMixer"
        x: 0; y: 650; width: parent.width; height: 166
        visible: false
    }
    GuitarSuitePanel { trackName: "Transport interaction test" }
    TrackList {
        objectName: "testTrackList"
        x: 0; y: 90; width: 340; height: 650
        visible: false
    }
    Timeline {
        objectName: "testTimeline"
        bpm: EngineController.bpm
        timeSignature: EngineController.numerator
        timeSignatureDenominator: EngineController.denominator
        x: 0; y: 100; width: 1000; height: 500
        laneCount: trackModel ? trackModel.count : 0
        visible: false
    }
    Button {
        objectName: "otherFocus"
        y: 52
        text: "Other control"
        onClicked: forceActiveFocus()
    }
}
