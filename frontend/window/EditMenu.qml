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
    implicitHeight: 30
    checked: editMenu.visible
    onClicked: editMenu.visible ? editMenu.close() : editMenu.open()
    leftPadding: 10
    rightPadding: 10
    contentItem: RowLayout {
        spacing: 4
        Text {
            text: "Edit"
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

    // Opened from the menu below (restMap G16).
    function openUndoHistory() { undoHistory.open() }
    UndoHistory {
        id: undoHistory
        trackModel: root.trackModel
    }

    ThemedMenu {
        id: editMenu
        objectName: "editMenu"
        y: parent.height + 4
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent   // the button toggles it
        width: 200

        ThemedMenuItem {
            objectName: "editUndoItem"
            text: "Undo"
            shortcutText: Shortcuts.texts["edit.undo"]
            enabled: !!root.trackModel && root.trackModel.canUndo
            onTriggered: root.trackModel.undo()
        }
        ThemedMenuItem {
            objectName: "editRedoItem"
            text: "Redo"
            shortcutText: Shortcuts.texts["edit.redo"]
            enabled: !!root.trackModel && root.trackModel.canRedo
            onTriggered: root.trackModel.redo()
        }
        ThemedMenuItem {
            objectName: "editUndoHistoryItem"
            text: "Undo History…"
            shortcutText: Shortcuts.texts["edit.history"]
            enabled: !!root.trackModel
            onTriggered: undoHistory.open()
        }

        ThemedMenuSeparator {}
        ThemedMenuItem {
            objectName: "editEffectsOnSelectionItem"
            text: "Effects on selection…"
            enabled: root.hasTimeline && root.timeline.menuCanEffectsOnSelection
            onTriggered: root.timeline.effectsOnSelection(-1)
        }
        ThemedMenuSeparator {}

        ThemedMenuItem {
            objectName: "editCutItem"
            text: "Cut"
            shortcutText: Shortcuts.texts["edit.cut"]
            enabled: root.hasTimeline && root.timeline.menuCanCut
            onTriggered: root.runOnTimeline(() => root.timeline.cutSelection())
        }
        ThemedMenuItem {
            objectName: "editCopyItem"
            text: "Copy"
            shortcutText: Shortcuts.texts["edit.copy"]
            enabled: root.hasTimeline && root.timeline.menuCanCopy
            onTriggered: root.runOnTimeline(() => root.timeline.copySelection())
        }
        ThemedMenuItem {
            objectName: "editPasteItem"
            text: "Paste"
            shortcutText: Shortcuts.texts["edit.paste"]
            enabled: root.hasTimeline && root.timeline.menuCanPaste
            onTriggered: root.runOnTimeline(() => root.timeline.pasteAtPlayhead())
        }
        ThemedMenuItem {
            objectName: "editDuplicateItem"
            text: "Duplicate"
            shortcutText: Shortcuts.texts["edit.duplicate"]
            enabled: root.hasTimeline && root.timeline.menuCanDuplicate
            onTriggered: root.runOnTimeline(() => root.timeline.duplicateSelection())
        }
        ThemedMenuItem {
            objectName: "editDeleteItem"
            text: "Delete"
            shortcutText: Shortcuts.texts["edit.delete"]
            enabled: root.hasTimeline && root.timeline.menuCanDelete
            onTriggered: root.runOnTimeline(() => root.timeline.deleteSelection())
        }

        ThemedMenuItem {
            objectName: "editRippleItem"
            text: "Ripple editing"
            checkable: true
            boxed: true                              // white box: visible when off as well
            checked: root.hasTimeline && root.timeline.ripple
            enabled: root.hasTimeline
            onTriggered: root.timeline.rippleEdited(!root.timeline.ripple)
            Binding on checked { value: root.hasTimeline && root.timeline.ripple }
        }

        ThemedMenuSeparator {}

        ThemedMenuItem {
            objectName: "editSelectAllItem"
            text: "Select All Clips"
            shortcutText: Shortcuts.texts["edit.selectAll"]
            enabled: root.hasTimeline && root.timeline.menuCanSelectAll
            onTriggered: root.runOnTimeline(() => root.timeline.selectAllClips())
        }
    }
}
