import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."
import "../common"

RowLayout {
    id: root
    spacing: 6

    property var trackModel: null
    signal closeRequested()
    // Import and export (restMap E2, F2): one job object for the File menu and file drops.
    readonly property alias importJob: importJob
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

    ImportExportWrapper {
        id: importJob
        objectName: "importJob"
        trackModel: root.trackModel ?? null
        onFinished: importStatus.open()
        onBusyChanged: if (busy) importStatus.open()
    }

    FilePicker {
        id: importDialog
        objectName: "importAudioDialog"
        title: "Import audio"
        fileMode: FilePicker.OpenFiles
        acceptLabel: "Import"
        nameFilters: ["Audio files (*.wav *.WAV *.flac *.FLAC *.mp3 *.MP3)", "All files (*)"]
        onAccepted: importJob.importFiles(selectedFiles, EngineController.positionBeats, -1)
    }

    // What an import is doing, and what went wrong with which file.
    Popup {
        id: importStatus
        objectName: "importStatusPopup"
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: 56
        width: 420
        padding: 12
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: Theme.panelBg; border.color: Theme.divider; radius: 8 }
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                objectName: "importStatusText"
                Layout.fillWidth: true
                text: importJob.status
                color: Theme.text
                font.pixelSize: Theme.bodySize
                wrapMode: Text.WordWrap
            }
            ProgressBar {
                Layout.fillWidth: true
                visible: importJob.busy
                value: importJob.progress
            }
            Repeater {
                model: importJob.busy ? [] : importJob.problems
                delegate: Text {
                    required property string modelData
                    Layout.fillWidth: true
                    text: "• " + modelData
                    color: Theme.textMuted
                    font.pixelSize: Theme.smallSize
                    wrapMode: Text.WordWrap
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PanelButton {
                    objectName: "importCancelButton"
                    visible: importJob.busy
                    text: "Cancel"
                    onClicked: importJob.cancel()
                }
                PanelButton {
                    objectName: "importStatusClose"
                    visible: !importJob.busy
                    text: "Close"
                    onClicked: importStatus.close()
                }
            }
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
        sequence: Shortcuts.keys["file.save"]
        enabled: !EngineController.takeBusy && !EngineController.projectLoading
        onActivated: actionsBridge.requestSave(EngineController.projectFile.toString().length > 0)
    }

    Shortcut {
        sequence: Shortcuts.keys["file.import"]
        enabled: !EngineController.takeBusy && !EngineController.projectLoading && !importJob.busy
        onActivated: importDialog.open()
    }
    Shortcut {
        sequence: Shortcuts.keys["file.export"]
        enabled: !EngineController.takeBusy && !EngineController.projectLoading
        onActivated: root.exportRequested()
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

        ThemedMenu {
            id: fileMenu
            objectName: "fileMenu"
            y: parent.height + 4
            width: 190
            onAboutToShow: recentProjects.refresh()

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

            ThemedMenu {
                id: recentMenu
                objectName: "fileOpenRecentMenu"
                title: "Open Recent"
                enabled: !recentProjects.empty && !EngineController.takeBusy && !EngineController.projectLoading
                width: 300

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

                ThemedMenuSeparator {}

                ThemedMenuItem {
                    objectName: "clearRecentProjects"
                    text: "Clear Recent"
                    onTriggered: recentProjects.clear()
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
                enabled: !EngineController.takeBusy && !EngineController.projectLoading && !importJob.busy
                onTriggered: importDialog.open()
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
