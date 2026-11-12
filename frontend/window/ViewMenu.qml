import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../common"

// View menu: which panels are on screen. The window owns the state; this menu only
// shows it and asks for changes.
ToolButton {
    id: root
    property bool mixerShown: true
    property bool pianoRollShown: false
    property bool pianoRollAvailable: false
    signal toggleMixerRequested()
    signal togglePianoRollRequested()

    objectName: "viewMenuButton"
    text: "View"
    implicitHeight: 32
    checked: viewMenu.visible
    onClicked: viewMenu.visible ? viewMenu.close() : viewMenu.open()
    contentItem: RowLayout {
        spacing: 4
        Text {
            text: "View"
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
        id: viewMenu
        objectName: "viewMenu"
        y: parent.height + 4
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent   // the button toggles it
        width: 200

        ThemedMenuItem {
            objectName: "viewMixerItem"
            text: "Mixer"
            shortcutText: Shortcuts.texts["view.mixer"]
            checkable: true
            // The tick follows the window state, also after a click.
            Binding on checked { value: root.mixerShown }
            onTriggered: root.toggleMixerRequested()
        }
        ThemedMenuItem {
            objectName: "viewPianoRollItem"
            text: "Piano Roll"
            checkable: true
            enabled: root.pianoRollAvailable || root.pianoRollShown
            Binding on checked { value: root.pianoRollShown }
            onTriggered: root.togglePianoRollRequested()
            ToolTip.visible: hovered && !enabled
            ToolTip.text: "Open a MIDI clip in the piano roll first"
        }
    }
}
