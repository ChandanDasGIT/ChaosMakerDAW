import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import ".."
import "../common"

RowLayout {
    id: root
    spacing: 6

    property var trackModel: null
    signal closeRequested()

    function requestAction(action) {
        if (action === "New" || action === "Open" || action === "Close") {
            actionsBridge.requestAction(action, EngineController.projectDirty)
        } else if (action === "Save") {
            actionsBridge.requestSave(EngineController.projectFile.toString().length > 0)
        } else if (action === "Save As") {
            actionsBridge.requestSaveAs()
        }
    }

    RecentProjectsWrapper {
        id: recentProjects
        objectName: "recentProjects"
    }

    ProjectActionsWrapper {
        id: actionsBridge
        objectName: "projectActionsBridge"
        onShowUnsavedConfirmation: unsavedDialog.open()
        onShowOpenDialog: openProjectDialog.open()
        onShowSaveDialog: saveProjectDialog.open()
        onExecuteNew: EngineController.newProject()
        onExecuteClose: root.closeRequested()
        // A recent project that cannot be opened (moved or deleted) leaves the list.
        onExecuteOpenFile: file => { if (!EngineController.openProject(file)) recentProjects.refresh() }
        onDirectSaveRequested: {
            if (EngineController.saveProject() && actionsBridge.resumeAfterSave) {
                actionsBridge.onSaveCompleted()
            }
        }
    }

    Shortcut {
        sequences: [StandardKey.Save]
        enabled: !EngineController.takeBusy && !EngineController.projectLoading
        onActivated: actionsBridge.requestSave(EngineController.projectFile.toString().length > 0)
    }

    // Literal sequence so it always matches the "Ctrl+Shift+S" shown in the File menu
    // (StandardKey.SaveAs is empty on some desktops).
    Shortcut {
        sequence: "Ctrl+Shift+S"
        enabled: !EngineController.takeBusy && !EngineController.projectLoading
        onActivated: actionsBridge.requestSaveAs()
    }

    Shortcut {
        sequences: [StandardKey.New]
        enabled: !EngineController.takeBusy && !EngineController.projectLoading
        onActivated: actionsBridge.requestAction("New", EngineController.projectDirty)
    }

    Shortcut {
        sequences: [StandardKey.Open]
        enabled: !EngineController.takeBusy && !EngineController.projectLoading
        onActivated: actionsBridge.requestAction("Open", EngineController.projectDirty)
    }

    FileDialog {

        // Inside the main window: a separate dialog window keeps app focus after closing,

        // which left the app without keyboard input (plan item 1.11).

        popupType: Popup.Item
        id: saveProjectDialog
        objectName: "saveProjectDialog"
        title: "Save project"
        fileMode: FileDialog.SaveFile
        nameFilters: ["ChaosMaker projects (*.chaos)"]
        defaultSuffix: "chaos"
        onAccepted: {
            if (EngineController.saveProject(selectedFile)) {
                actionsBridge.onSaveCompleted()
            } else {
                actionsBridge.onSaveCancelled()
            }
        }
        onRejected: actionsBridge.onSaveCancelled()
    }

    FileDialog {

        // Inside the main window: a separate dialog window keeps app focus after closing,

        // which left the app without keyboard input (plan item 1.11).

        popupType: Popup.Item
        id: openProjectDialog
        title: "Open project"
        fileMode: FileDialog.OpenFile
        nameFilters: ["ChaosMaker projects (*.chaos)", "JSON sessions (*.json)"]
        onAccepted: EngineController.openProject(selectedFile)
    }

    Dialog {
        id: unsavedDialog
        objectName: "unsavedProjectChanges"
        parent: Overlay.overlay
        anchors.centerIn: parent
        title: "Save changes to this project?"
        implicitWidth: 420
        width: Math.min(420, parent.width - 32)
        modal: true
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        background: Rectangle { color: Theme.panelBg; border.color: Theme.divider; radius: 8 }
        palette.windowText: Theme.text
        palette.text: Theme.text
        contentItem: Text { text: "Save your changes before continuing."; color: Theme.text; padding: 16 }
        onAccepted: actionsBridge.onUnsavedSave(EngineController.projectFile.toString().length > 0)
        onDiscarded: actionsBridge.onUnsavedDiscard()
        onRejected: actionsBridge.onUnsavedCancel()
    }

    ToolButton {
        id: fileMenuButton
        objectName: "fileMenuButton"
        text: "File"
        implicitHeight: 32
        checkable: true
        checked: fileMenu.visible
        onClicked: fileMenu.visible ? fileMenu.close() : fileMenu.open()
        contentItem: RowLayout {
            spacing: 4
            Text {
                text: "File"
                color: fileMenuButton.down || fileMenuButton.checked ? Theme.accent : Theme.text
                font.pixelSize: Theme.bodySize
                font.bold: true
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                text: Icons.menuArrow
                color: fileMenuButton.down || fileMenuButton.checked ? Theme.accent : Theme.textMuted
                font.pixelSize: 10
                verticalAlignment: Text.AlignVCenter
            }
        }
        background: Rectangle {
            radius: 4
            color: fileMenuButton.down || fileMenuButton.checked ? Theme.controlBgActive : fileMenuButton.hovered ? Theme.controlBgHover : "transparent"
            border.color: fileMenuButton.checked ? Theme.divider : "transparent"
        }

        Menu {
            id: fileMenu
            objectName: "fileMenu"
            y: parent.height + 4
            width: 190
            padding: 4
            // Styles the "Open Recent" submenu entry like the other items.
            delegate: ThemedMenuItem {}
            onAboutToShow: recentProjects.refresh()
            background: Rectangle {
                color: Theme.panelBg
                border.color: Theme.divider
                radius: 6
            }

            MenuItem {
                id: newAction
                objectName: "fileNewItem"
                text: "New"
                enabled: !EngineController.takeBusy && !EngineController.projectLoading
                implicitHeight: 30
                leftPadding: Theme.margin
                rightPadding: Theme.margin
                contentItem: RowLayout {
                    Text {
                        text: newAction.text
                        color: !newAction.enabled ? Theme.textMuted : newAction.highlighted ? Theme.accent : Theme.text
                        font.pixelSize: Theme.bodySize
                        Layout.fillWidth: true
                    }
                    Text {
                        text: "Ctrl+N"
                        color: Theme.textMuted
                        font.pixelSize: Theme.captionSize
                        font.family: Theme.monospaceFontFamily
                    }
                }
                background: Rectangle {
                    color: newAction.highlighted ? Theme.tooltipBg : "transparent"
                    radius: 4
                }
                onTriggered: actionsBridge.requestAction("New", EngineController.projectDirty)
            }

            MenuItem {
                id: openAction
                objectName: "fileOpenItem"
                text: "Open..."
                enabled: !EngineController.takeBusy && !EngineController.projectLoading
                implicitHeight: 30
                leftPadding: Theme.margin
                rightPadding: Theme.margin
                contentItem: RowLayout {
                    Text {
                        text: openAction.text
                        color: !openAction.enabled ? Theme.textMuted : openAction.highlighted ? Theme.accent : Theme.text
                        font.pixelSize: Theme.bodySize
                        Layout.fillWidth: true
                    }
                    Text {
                        text: "Ctrl+O"
                        color: Theme.textMuted
                        font.pixelSize: Theme.captionSize
                        font.family: Theme.monospaceFontFamily
                    }
                }
                background: Rectangle {
                    color: openAction.highlighted ? Theme.tooltipBg : "transparent"
                    radius: 4
                }
                onTriggered: actionsBridge.requestAction("Open", EngineController.projectDirty)
            }

            Menu {
                id: recentMenu
                objectName: "fileOpenRecentMenu"
                title: "Open Recent"
                enabled: !recentProjects.empty && !EngineController.takeBusy && !EngineController.projectLoading
                width: 300
                padding: 4
                background: Rectangle {
                    color: Theme.panelBg
                    border.color: Theme.divider
                    radius: 6
                }

                Instantiator {
                    model: recentProjects.paths
                    delegate: ThemedMenuItem {
                        required property int index
                        required property string modelData
                        objectName: "recentProject_" + index
                        text: recentProjects.labels[index] || ""
                        ToolTip.visible: hovered
                        ToolTip.text: modelData
                        ToolTip.delay: 450
                        onTriggered: {
                            fileMenu.close()
                            actionsBridge.requestOpenFile(modelData, EngineController.projectDirty)
                        }
                    }
                    onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => recentMenu.removeItem(object)
                }

                MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.divider } }

                ThemedMenuItem {
                    objectName: "clearRecentProjects"
                    text: "Clear Recent"
                    onTriggered: recentProjects.clear()
                }
            }

            MenuItem {
                id: saveAction
                objectName: "fileSaveItem"
                text: "Save"
                enabled: !EngineController.takeBusy && !EngineController.projectLoading
                implicitHeight: 30
                leftPadding: Theme.margin
                rightPadding: Theme.margin
                contentItem: RowLayout {
                    Text {
                        text: saveAction.text
                        color: !saveAction.enabled ? Theme.textMuted : saveAction.highlighted ? Theme.accent : Theme.text
                        font.pixelSize: Theme.bodySize
                        Layout.fillWidth: true
                    }
                    Text {
                        text: "Ctrl+S"
                        color: Theme.textMuted
                        font.pixelSize: Theme.captionSize
                        font.family: Theme.monospaceFontFamily
                    }
                }
                background: Rectangle {
                    color: saveAction.highlighted ? Theme.tooltipBg : "transparent"
                    radius: 4
                }
                onTriggered: actionsBridge.requestSave(EngineController.projectFile.toString().length > 0)
            }

            MenuItem {
                id: saveAsAction
                objectName: "fileSaveAsItem"
                text: "Save As..."
                enabled: !EngineController.takeBusy && !EngineController.projectLoading
                implicitHeight: 30
                leftPadding: Theme.margin
                rightPadding: Theme.margin
                contentItem: RowLayout {
                    Text {
                        text: saveAsAction.text
                        color: !saveAsAction.enabled ? Theme.textMuted : saveAsAction.highlighted ? Theme.accent : Theme.text
                        font.pixelSize: Theme.bodySize
                        Layout.fillWidth: true
                    }
                    Text {
                        text: "Ctrl+Shift+S"
                        color: Theme.textMuted
                        font.pixelSize: Theme.captionSize
                        font.family: Theme.monospaceFontFamily
                    }
                }
                background: Rectangle {
                    color: saveAsAction.highlighted ? Theme.tooltipBg : "transparent"
                    radius: 4
                }
                onTriggered: actionsBridge.requestSaveAs()
            }
        }
    }

    Item {
        id: testHooks
        width: 1; height: 1
        opacity: 0.001
        visible: true
        clip: true

        ToolButton {
            objectName: "projectNew"
            width: 1; height: 1
            enabled: !EngineController.takeBusy && !EngineController.projectLoading
            onClicked: actionsBridge.requestAction("New", EngineController.projectDirty)
        }
        ToolButton {
            objectName: "projectOpen"
            width: 1; height: 1
            enabled: !EngineController.takeBusy && !EngineController.projectLoading
            onClicked: actionsBridge.requestAction("Open", EngineController.projectDirty)
        }
        ToolButton {
            objectName: "projectSave"
            width: 1; height: 1
            enabled: !EngineController.takeBusy && !EngineController.projectLoading
            onClicked: actionsBridge.requestSave(EngineController.projectFile.toString().length > 0)
        }
        ToolButton {
            objectName: "projectSaveAs"
            width: 1; height: 1
            enabled: !EngineController.takeBusy && !EngineController.projectLoading
            onClicked: actionsBridge.requestSaveAs()
        }
        ToolButton {
            objectName: "undoClipEdit"
            width: 1; height: 1
            enabled: !!root.trackModel && root.trackModel.canUndo
            onClicked: root.trackModel.undo()
        }
        ToolButton {
            objectName: "redoClipEdit"
            width: 1; height: 1
            enabled: !!root.trackModel && root.trackModel.canRedo
            onClicked: root.trackModel.redo()
        }
    }
}
