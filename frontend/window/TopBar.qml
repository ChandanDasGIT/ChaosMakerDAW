import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../session"
import "../audiodevice"
import "../settings"

Rectangle {
    id: root
    property var trackModel: null
    signal closeRequested()

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
            trackModel: root.trackModel
            onCloseRequested: root.closeRequested()
        }

        Repeater {
            model: ["↶", "↷"]
            delegate: ToolButton {
                text: modelData
                objectName: modelData === "↶" ? "undoClipEdit" : "redoClipEdit"
                enabled: modelData === "↶" ? !!root.trackModel && root.trackModel.canUndo
                         : !!root.trackModel && root.trackModel.canRedo
                onClicked: {
                    if (modelData === "↶") root.trackModel.undo()
                    else root.trackModel.redo()
                }
                ToolTip.visible: hovered
                ToolTip.text: modelData === "↶" ? "Undo clip edit (Ctrl+Z)" : "Redo clip edit (Ctrl+Shift+Z)"
                implicitHeight: 32
                contentItem: Text {
                    text: parent.text
                    color: parent.enabled ? Theme.text : Theme.textMuted
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 4
                    color: parent.down ? "#383838" : "transparent"
                }
            }
        }
        AppSettings {
            id: appSettings
        }
        Item { Layout.fillWidth: true }
        DeviceSettings {
            id: deviceSettings
        }
    }
}
