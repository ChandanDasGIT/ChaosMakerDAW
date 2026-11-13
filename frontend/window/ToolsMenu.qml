import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../common"
import "../tools"

// Tools menu: Tap tempo… (restMap G17) opens the tap tempo window; the analyzers (G20-G22) live
// here too. Every tool is a window of its own (restMap U7). The tap itself lives with
// the BPM field in the transport bar; the window only asks for taps.
ToolButton {
    id: root
    property real bpm: 120                 // the project's BPM, shown in the tap window
    property int tapCount: 0               // taps in the current count (0 = none yet)
    property var trackModel: null          // the project's tracks: the analyzers read its selected clip
    signal tapTempoRequested()
    signal bpmEdited(real value)           // the tap window's "Back to …"

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
    ChordAnalyzerDialog {
        id: chordAnalyzer
        trackModel: root.trackModel
    }

    objectName: "toolsMenuButton"
    text: "Tools"
    implicitHeight: 30
    checked: toolsMenu.visible
    onClicked: toolsMenu.visible ? toolsMenu.close() : toolsMenu.open()
    leftPadding: 10
    rightPadding: 10
    contentItem: RowLayout {
        spacing: 4
        Text {
            text: "Tools"
            color: root.down || root.checked ? Theme.accent : Theme.text
            font.pixelSize: Theme.bodySize
            verticalAlignment: Text.AlignVCenter
        }
    }
    background: Rectangle {
        radius: Theme.controlRadius
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
        ThemedMenuItem {
            objectName: "toolsChordAnalyzer"
            text: "Chord analyzer…"
            onTriggered: chordAnalyzer.open()
        }
    }
}
