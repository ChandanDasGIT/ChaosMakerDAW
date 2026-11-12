import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../common"

// Options menu: Tap tempo… (restMap G17) opens the tap tempo popup. The tap itself lives with
// the BPM field in the transport bar; the popup only asks for taps.
ToolButton {
    id: root
    property real bpm: 120                 // the project's BPM, shown in the popup
    property int tapCount: 0               // taps in the current count (0 = none yet)
    signal tapTempoRequested()
    signal bpmEdited(real value)           // the popup's "Back to …"

    TapTempoDialog {
        id: tapDialog
        bpm: root.bpm
        tapCount: root.tapCount
        onTapRequested: root.tapTempoRequested()
        onBpmEdited: value => root.bpmEdited(value)
    }

    objectName: "optionsMenuButton"
    text: "Options"
    implicitHeight: 32
    checked: optionsMenu.visible
    onClicked: optionsMenu.visible ? optionsMenu.close() : optionsMenu.open()
    contentItem: RowLayout {
        spacing: 4
        Text {
            text: "Options"
            color: root.down || root.checked ? Theme.accent : Theme.text
            font.pixelSize: Theme.bodySize
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            text: Icons.menuArrow
            color: root.down || root.checked ? Theme.accent : Theme.textMuted
            font.pixelSize: 10
            verticalAlignment: Text.AlignVCenter
        }
    }
    background: Rectangle {
        radius: 4
        color: root.down || root.checked ? Theme.controlBgActive : root.hovered ? Theme.controlBgHover : "transparent"
        border.color: root.checked ? Theme.divider : "transparent"
    }

    ThemedMenu {
        id: optionsMenu
        objectName: "optionsMenu"
        y: parent.height + 4
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent   // the button toggles it
        width: 220

        ThemedMenuItem {
            objectName: "optionsTapTempo"
            text: "Tap tempo…"
            shortcutText: Shortcuts.texts["transport.tapTempo"] || ""
            onTriggered: tapDialog.open()
        }
    }
}
