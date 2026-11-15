import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The File menu: new, open, recent, save, import and export.
Item {
    id: root
    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight

    property var trackModel: null
    signal closeRequested()
    // Import and export (restMap E2, F2): one job object for the File menu and file drops.
    readonly property alias importJob: importExport.job
    signal exportRequested()

    function requestAction(action) {
        if (action === "New" || action === "Open" || action === "Close") {
            actionsBridge.requestAction(action, EngineController.projectDirty)
        } else if (action === "Save") {
            actionsBridge.requestSave(EngineController.projectFile.toString().length > 0)
        } else if (action === "Save As") {
            actionsBridge.requestSaveAs()
        }
    }

    ImportExport {
        id: importExport
        trackModel: root.trackModel
        onExportRequested: root.exportRequested()
    }

    ProjectActionsWrap {
        id: actionsBridge
        objectName: "projectActionsBridge"
        onShowUnsavedConfirmation: unsavedDialog.open()
        onShowOpenDialog: openProjectDialog.open()
        onShowSaveDialog: saveProjectDialog.open()
        onExecuteNew: EngineController.newProject()
        onExecuteClose: root.closeRequested()
        // A recent project that cannot be opened (moved or deleted) leaves the list.
        onExecuteOpenFile: file => { if (!EngineController.openProject(file)) recentMenu.refresh() }
        onDirectSaveRequested: {
            if (EngineController.saveProject() && actionsBridge.resumeAfterSave) {
                actionsBridge.onSaveCompleted()
            }
        }
    }

    Shortcut {
        sequence: Shortcuts.keys["file.save"]
        enabled: !EngineController.takeBusy && !EngineController.projectLoading
        onActivated: actionsBridge.requestSave(EngineController.projectFile.toString().length > 0)
    }

    // Literal sequence so it always matches the "Ctrl+Shift+S" shown in the File menu
    // (StandardKey.SaveAs is empty on some desktops).
    Shortcut {
        sequence: Shortcuts.keys["file.saveAs"]
        enabled: !EngineController.takeBusy && !EngineController.projectLoading
        onActivated: actionsBridge.requestSaveAs()
    }

    Shortcut {
        sequence: Shortcuts.keys["file.new"]
        enabled: !EngineController.takeBusy && !EngineController.projectLoading
        onActivated: actionsBridge.requestAction("New", EngineController.projectDirty)
    }

    Shortcut {
        sequence: Shortcuts.keys["file.open"]
        enabled: !EngineController.takeBusy && !EngineController.projectLoading
        onActivated: actionsBridge.requestAction("Open", EngineController.projectDirty)
    }

    FilePicker {
        id: saveProjectDialog
        objectName: "saveProjectDialog"
        title: "Save project"
        fileMode: FilePicker.SaveFile
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

    FilePicker {
        id: openProjectDialog
        title: "Open project"
        fileMode: FilePicker.OpenFile
        nameFilters: ["ChaosMaker projects (*.chaos)", "JSON sessions (*.json)"]
        onAccepted: EngineController.openProject(selectedFile)
    }

    ThemedDialog {
        id: unsavedDialog
        objectName: "unsavedProjectChanges"
        parent: Overlay.overlay
        anchors.centerIn: parent
        title: "Save changes to this project?"
        implicitWidth: 420
        width: Math.min(420, parent.width - 32)
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        contentItem: Text { text: "Save your changes before continuing."; color: Theme.text; padding: 16 }
        onAccepted: actionsBridge.onUnsavedSave(EngineController.projectFile.toString().length > 0)
        onDiscarded: actionsBridge.onUnsavedDiscard()
        onRejected: actionsBridge.onUnsavedCancel()
    }

    RowLayout {
        id: row
        anchors.fill: parent
        spacing: 6

        ToolButton {
            id: fileMenuButton
            objectName: "fileMenuButton"
            text: "File"
            implicitHeight: 30
            checked: fileMenu.visible
            onClicked: fileMenu.visible ? fileMenu.close() : fileMenu.open()
            leftPadding: 10
            rightPadding: 10
            contentItem: RowLayout {
                spacing: 4
                Text {
                    text: "File"
                    color: fileMenuButton.down || fileMenuButton.checked ? Theme.accent : Theme.text
                    font.pixelSize: Theme.bodySize
                    verticalAlignment: Text.AlignVCenter
                }
            }
            background: Rectangle {
                radius: Theme.controlRadius
                color: fileMenuButton.down || fileMenuButton.checked ? Theme.controlBgActive : fileMenuButton.hovered ? Theme.controlBgHover : "transparent"
                border.color: fileMenuButton.checked ? Theme.divider : "transparent"
            }

            ThemedMenu {
                id: fileMenu
                objectName: "fileMenu"
                y: parent.height + 4
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent   // the button toggles it
                width: 190
                onAboutToShow: recentMenu.refresh()

                ThemedMenuItem {
                    id: newAction
                    objectName: "fileNewItem"
                    text: "New"
                    shortcutText: Shortcuts.texts["file.new"]
                    enabled: !EngineController.takeBusy && !EngineController.projectLoading
                    onTriggered: actionsBridge.requestAction("New", EngineController.projectDirty)
                }

                ThemedMenuItem {
                    id: openAction
                    objectName: "fileOpenItem"
                    text: "Open..."
                    shortcutText: Shortcuts.texts["file.open"]
                    enabled: !EngineController.takeBusy && !EngineController.projectLoading
                    onTriggered: actionsBridge.requestAction("Open", EngineController.projectDirty)
                }

                RecentProjects {
                    id: recentMenu
                    enabled: !recentMenu.empty && !EngineController.takeBusy && !EngineController.projectLoading
                    onOpenRequested: path => {
                        fileMenu.close()
                        actionsBridge.requestOpenFile(path, EngineController.projectDirty)
                    }
                }

                ThemedMenuItem {
                    id: saveAction
                    objectName: "fileSaveItem"
                    text: "Save"
                    shortcutText: Shortcuts.texts["file.save"]
                    enabled: !EngineController.takeBusy && !EngineController.projectLoading
                    onTriggered: actionsBridge.requestSave(EngineController.projectFile.toString().length > 0)
                }

                ThemedMenuItem {
                    id: saveAsAction
                    objectName: "fileSaveAsItem"
                    text: "Save As..."
                    shortcutText: Shortcuts.texts["file.saveAs"]
                    enabled: !EngineController.takeBusy && !EngineController.projectLoading
                    onTriggered: actionsBridge.requestSaveAs()
                }

                ThemedMenuSeparator {}

                ThemedMenuItem {
                    objectName: "fileImportItem"
                    text: "Import audio..."
                    shortcutText: Shortcuts.texts["file.import"]
                    enabled: !EngineController.takeBusy && !EngineController.projectLoading && !importExport.job.busy
                    onTriggered: importExport.openImportDialog()
                }

                ThemedMenuItem {
                    objectName: "fileExportItem"
                    text: "Export..."
                    shortcutText: Shortcuts.texts["file.export"]
                    enabled: !EngineController.takeBusy && !EngineController.projectLoading
                    onTriggered: root.exportRequested()
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
}
