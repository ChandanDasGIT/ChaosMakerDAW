import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import ChaosMaker

RowLayout {
    id: root
    spacing: 6

    property var trackModel: null
    signal closeRequested()

    ProjectActionsWrapper {
        id: actionsBridge
        onShowUnsavedConfirmation: unsavedDialog.open()
        onShowOpenDialog: openProjectDialog.open()
        onShowSaveDialog: saveProjectDialog.open()
        onExecuteNew: EngineController.newProject()
        onExecuteClose: root.closeRequested()
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

    FileDialog {
        id: saveProjectDialog
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

    Repeater {
        model: ["New", "Open", "Save", "Save As"]
        delegate: ToolButton {
            text: modelData
            objectName: "project" + modelData.replace(" ", "")
            enabled: !EngineController.takeBusy && !EngineController.projectLoading
            onClicked: {
                if (modelData === "New" || modelData === "Open") {
                    actionsBridge.requestAction(modelData, EngineController.projectDirty)
                } else if (modelData === "Save") {
                    actionsBridge.requestSave(EngineController.projectFile.toString().length > 0)
                } else if (modelData === "Save As") {
                    actionsBridge.requestSaveAs()
                }
            }
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
}
