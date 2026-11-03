import QtQuick
import QtQuick.Controls

// Standalone host; track FX uses the same editor inside its persistent workspace.
Popup {
    id: root
    objectName: "guitarSuitePanel"
    property string trackName: ""
    property var effectsModel: EngineController.guitarSuite
    parent: Overlay.overlay
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    width: Math.min(920, parent.width - 32)
    height: Math.min(580, parent.height - 32)
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 0
    background: Rectangle { radius: 9; color: "#25272c"; border.color: "#4a4d56" }
    contentItem: GuitarSuiteEditor {
        trackName: root.trackName
        effectsModel: root.effectsModel
    }
}
