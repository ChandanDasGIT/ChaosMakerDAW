import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../common"
import "../tools"

// Tools menu: Tap tempo… (restMap G17) opens the tap tempo popup; the analyzers (G19-G22) live
// here too. The tap itself lives with
// the BPM field in the transport bar; the popup only asks for taps.
ToolButton {
    id: root
    property real bpm: 120                 // the project's BPM, shown in the popup
    property int tapCount: 0               // taps in the current count (0 = none yet)
    property var trackModel: null          // the project's tracks: the analyzers read its selected clip
    signal tapTempoRequested()
    signal bpmEdited(real value)           // the popup's "Back to …"

    TapTempoDialog {
        id: tapDialog
        bpm: root.bpm
        tapCount: root.tapCount
        onTapRequested: root.tapTempoRequested()
        onBpmEdited: value => root.bpmEdited(value)
    }
    DrumAnalyzerDialog {
        id: drumAnalyzer
        trackModel: root.trackModel
    }

    objectName: "toolsMenuButton"
    text: "Tools"
    implicitHeight: 32
    checked: toolsMenu.visible
    onClicked: toolsMenu.visible ? toolsMenu.close() : toolsMenu.open()
    contentItem: RowLayout {
        spacing: 4
        Text {
            text: "Tools"
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
        id: toolsMenu
        objectName: "toolsMenu"
        y: parent.height + 4
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent   // the button toggles it
        width: 240

        ThemedMenuItem {
            objectName: "toolsTapTempo"
            text: "Tap tempo…"
            shortcutText: Shortcuts.texts["transport.tapTempo"] || ""
            onTriggered: tapDialog.open()
        }
        ThemedMenuItem {
            objectName: "toolsDrumAnalyzer"
            text: "Drum pattern analyzer…"
            onTriggered: drumAnalyzer.open()
        }
    }
}
