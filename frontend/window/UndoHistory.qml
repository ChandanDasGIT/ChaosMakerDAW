import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../common"

// The undo history (restMap G16): every step, oldest first, under a "Start" row. The current
// place is highlighted; undone steps are greyed. Clicking a row undoes / redoes up to it.
Popup {
    id: root
    property var trackModel: null
    readonly property var steps: !!root.trackModel ? root.trackModel.undoHistory : []
    readonly property int position: !!root.trackModel ? root.trackModel.undoPosition : 0
    // Recording locks edits: the list stays readable but cannot be clicked.
    readonly property bool canJump: !!root.trackModel && (root.trackModel.canUndo || root.trackModel.canRedo)

    objectName: "undoHistoryPopup"
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: 320
    padding: 16
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: list.positionViewAtIndex(root.position, ListView.Contain)
    background: Rectangle {
        color: Theme.panelBg
        border.color: Theme.divider
        border.width: 1
        radius: Theme.panelRadius
    }

    contentItem: ColumnLayout {
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "Undo History"
                color: Theme.text
                font.pixelSize: Theme.bodySize + 2
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            CloseButton {
                Accessible.name: "Close undo history"
                implicitWidth: 28
                implicitHeight: 28
                glyphSize: 18
                onClicked: root.close()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.divider
        }

        ListView {
            id: list
            objectName: "undoHistoryList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 360)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            // Row 0 is the start (nothing done); row i is after step i.
            model: root.steps.length + 1
            ScrollBar.vertical: ScrollBar { policy: list.contentHeight > list.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

            delegate: ItemDelegate {
                id: row
                required property int index
                readonly property bool current: row.index === root.position
                readonly property bool undone: row.index > root.position
                objectName: "undoHistoryRow" + row.index
                width: ListView.view.width
                height: 28
                enabled: root.canJump
                hoverEnabled: true
                onClicked: if (!row.current) root.trackModel.jumpToHistory(row.index)
                background: Rectangle {
                    radius: Theme.controlRadius
                    color: row.current ? Theme.selectionBg : row.hovered ? Theme.controlBgHover : "transparent"
                }
                contentItem: Text {
                    text: row.index === 0 ? "Start" : root.steps[row.index - 1]
                    color: row.current ? Theme.textStrong : row.undone || !root.canJump ? Theme.textDisabled : Theme.text
                    font.pixelSize: Theme.bodySize
                    font.italic: row.index === 0
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.steps.length === 0
            text: "Nothing to undo yet."
            color: Theme.textMuted
            font.pixelSize: Theme.bodySize
        }
    }
}
