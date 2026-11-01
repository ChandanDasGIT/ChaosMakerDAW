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
    TopBar { width: parent.width }
    GuitarSuitePanel { trackName: "Transport interaction test" }
    Button {
        objectName: "otherFocus"
        y: 52
        text: "Other control"
        onClicked: forceActiveFocus()
    }
}
