import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../common"

// Edit menu: undo history of the project, and clip edits on the timeline.
ToolButton {
    id: root
    property var trackModel: null
    property var timeline: null
    readonly property bool hasTimeline: !!root.timeline

    // Run a timeline action, then give the timeline keyboard focus back so the
    // matching shortcuts keep working afterwards.
    function runOnTimeline(action) {
        if (!root.hasTimeline) return
        action()
        root.timeline.forceActiveFocus()
    }

    objectName: "editMenuButton"
    text: "Edit"
    implicitHeight: 32
    checkable: true
    checked: editMenu.visible
    onClicked: editMenu.visible ? editMenu.close() : editMenu.open()
    contentItem: RowLayout {
        spacing: 4
        Text {
            text: "Edit"
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
        id: editMenu
        objectName: "editMenu"
        y: parent.height + 4
        width: 200

        ThemedMenuItem {
            objectName: "editUndoItem"
            text: "Undo"
            shortcutText: "Ctrl+Z"
            enabled: !!root.trackModel && root.trackModel.canUndo
            onTriggered: root.trackModel.undo()
        }
        ThemedMenuItem {
            objectName: "editRedoItem"
            text: "Redo"
            shortcutText: "Ctrl+Shift+Z"
            enabled: !!root.trackModel && root.trackModel.canRedo
            onTriggered: root.trackModel.redo()
        }

        ThemedMenuSeparator {}

        ThemedMenuItem {
            objectName: "editCutItem"
            text: "Cut"
            shortcutText: "Ctrl+X"
            enabled: root.hasTimeline && root.timeline.menuCanCut
            onTriggered: root.runOnTimeline(() => root.timeline.cutSelection())
        }
        ThemedMenuItem {
            objectName: "editCopyItem"
            text: "Copy"
            shortcutText: "Ctrl+C"
            enabled: root.hasTimeline && root.timeline.menuCanCopy
            onTriggered: root.runOnTimeline(() => root.timeline.copySelection())
        }
        ThemedMenuItem {
            objectName: "editPasteItem"
            text: "Paste"
            shortcutText: "Ctrl+V"
            enabled: root.hasTimeline && root.timeline.menuCanPaste
            onTriggered: root.runOnTimeline(() => root.timeline.pasteAtPlayhead())
        }
        ThemedMenuItem {
            objectName: "editDuplicateItem"
            text: "Duplicate"
            shortcutText: "Ctrl+D"
            enabled: root.hasTimeline && root.timeline.menuCanDuplicate
            onTriggered: root.runOnTimeline(() => root.timeline.duplicateSelection())
        }
        ThemedMenuItem {
            objectName: "editDeleteItem"
            text: "Delete"
            shortcutText: "Del"
            enabled: root.hasTimeline && root.timeline.menuCanDelete
            onTriggered: root.runOnTimeline(() => root.timeline.deleteSelection())
        }

        ThemedMenuSeparator {}

        ThemedMenuItem {
            objectName: "editSelectAllItem"
            text: "Select All Clips"
            shortcutText: "Ctrl+A"
            enabled: root.hasTimeline && root.timeline.menuCanSelectAll
            onTriggered: root.runOnTimeline(() => root.timeline.selectAllClips())
        }
    }
}
