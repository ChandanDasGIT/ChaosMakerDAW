import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../session"
import "../settings"

Rectangle {
    id: root
    property var trackModel: null
    property var timeline: null
    property bool mixerVisible: true
    property bool mixerShown: true
    property bool pianoRollShown: false
    property bool pianoRollAvailable: false
    readonly property alias appSettings: appSettings
    signal closeRequested()
    signal toggleMixerRequested()
    signal togglePianoRollRequested()

    function requestProjectAction(action) {
        projectActions.requestAction(action)
    }

    Shortcut { sequences: [StandardKey.Undo]; enabled: !!root.trackModel && root.trackModel.canUndo; onActivated: root.trackModel.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: !!root.trackModel && root.trackModel.canRedo; onActivated: root.trackModel.redo() }

    height: 48
    color: Theme.panelBg
    border.color: Theme.divider
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 6

        ProjectActions {
            id: projectActions
            trackModel: root.trackModel
            onCloseRequested: root.closeRequested()
        }

        EditMenu {
            id: editMenu
            trackModel: root.trackModel
            timeline: root.timeline
        }

        ViewMenu {
            id: viewMenu
            mixerShown: root.mixerShown
            pianoRollShown: root.pianoRollShown
            pianoRollAvailable: root.pianoRollAvailable
            onToggleMixerRequested: root.toggleMixerRequested()
            onTogglePianoRollRequested: root.togglePianoRollRequested()
        }

        AppSettings {
            id: appSettings
        }
        ToolButton {
            id: mixerToggleBtn
            objectName: "toggleMixerButton"
            implicitHeight: 32
            onClicked: root.toggleMixerRequested()
            ToolTip.visible: hovered
            ToolTip.text: "Toggle bottom mixer panel (M)"
            contentItem: RowLayout {
                spacing: 6
                Rectangle {
                    width: 7; height: 7; radius: 3.5
                    color: root.mixerVisible ? Theme.accent : Theme.textMuted
                }
                Text {
                    text: "Mixer"
                    color: root.mixerVisible ? Theme.text : Theme.textMuted
                    font.pixelSize: Theme.bodySize
                    font.bold: root.mixerVisible
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            background: Rectangle {
                radius: 4
                color: parent.down ? Theme.controlBgActive : parent.hovered ? Theme.controlBgHover : root.mixerVisible ? Theme.cardBgAlt : "transparent"
                border.color: root.mixerVisible ? Theme.divider : "transparent"
            }
        }
        Item { Layout.fillWidth: true }
    }
}
