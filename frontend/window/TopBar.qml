import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Rectangle {
    id: root
    property var trackModel: null
    property string pendingAction: ""
    property bool resumeAfterSave: false
    signal closeRequested()
    function performPendingAction() {
        const action = pendingAction
        pendingAction = ""
        if (action === "New") EngineController.newProject()
        else if (action === "Open") openProjectDialog.open()
        else if (action === "Close") closeRequested()
    }
    function requestProjectAction(action) {
        pendingAction = action
        if (EngineController.projectDirty) unsavedDialog.open()
        else performPendingAction()
    }
    function saveCurrent(resume) {
        resumeAfterSave = resume
        if (EngineController.projectFile.toString().length === 0) saveProjectDialog.open()
        else if (EngineController.saveProject() && resume) performPendingAction()
    }
    Shortcut { sequences: [StandardKey.Undo]; enabled: !!root.trackModel && root.trackModel.canUndo; onActivated: root.trackModel.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: !!root.trackModel && root.trackModel.canRedo; onActivated: root.trackModel.redo() }
    Shortcut { sequences: [StandardKey.Save]; enabled: !EngineController.takeBusy && !EngineController.projectLoading; onActivated: root.saveCurrent(false) }
    FileDialog {
        id: saveProjectDialog
        title: "Save project"
        fileMode: FileDialog.SaveFile
        nameFilters: ["ChaosMaker projects (*.chaos)"]
        defaultSuffix: "chaos"
        onAccepted: { if (EngineController.saveProject(selectedFile) && root.resumeAfterSave) root.performPendingAction() }
        onRejected: root.pendingAction = ""
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
        onAccepted: root.saveCurrent(true)
        onDiscarded: root.performPendingAction()
        onRejected: root.pendingAction = ""
    }
    height: 48
    color: Theme.panelBg
    border.color: Theme.divider
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 6
        Repeater {
            model: ["New", "Open", "Save", "Save As", "↶", "↷"]
            delegate: ToolButton {
                text: modelData
                objectName: modelData === "↶" ? "undoClipEdit" : modelData === "↷" ? "redoClipEdit" : "project" + modelData.replace(" ", "")
                enabled: modelData === "↶" ? !!root.trackModel && root.trackModel.canUndo
                         : modelData === "↷" ? !!root.trackModel && root.trackModel.canRedo
                         : !EngineController.takeBusy && !EngineController.projectLoading
                onClicked: {
                    if (modelData === "↶") root.trackModel.undo()
                    else if (modelData === "↷") root.trackModel.redo()
                    else if (modelData === "New" || modelData === "Open") root.requestProjectAction(modelData)
                    else if (modelData === "Save") root.saveCurrent(false)
                    else { root.resumeAfterSave = false; saveProjectDialog.open() }
                }
                ToolTip.visible: hovered && (modelData === "↶" || modelData === "↷")
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
        ToolButton {
            objectName: "settingsButton"
            text: "Settings"
            onClicked: settingsMenu.open()
            palette.buttonText: Theme.text
            Menu {
                id: settingsMenu
                y: parent.height
                MenuItem {
                    objectName: "restoreSessionSetting"
                    text: "Restore session on startup"
                    checkable: true
                    checked: EngineController.restoreSessionOnStartup
                    onTriggered: EngineController.restoreSessionOnStartup = checked
                }
            }
        }
        Item { Layout.fillWidth: true }
        Text {
            text: "BUFFER"
            color: Theme.textMuted
            font.pixelSize: 9
            font.bold: true
        }
        ComboBox {
            id: bufferSelector
            Layout.preferredWidth: 82
            Layout.preferredHeight: 30
            model: EngineController.bufferSizes
            enabled: EngineController.selectedBufferSize > 0
            currentIndex: model.indexOf(String(EngineController.selectedBufferSize))
            onActivated: index => EngineController.setBufferSize(Number(currentText))
            contentItem: Text {
                leftPadding: 8
                rightPadding: 20
                text: bufferSelector.currentIndex < 0 ? "—" : bufferSelector.displayText
                color: bufferSelector.enabled ? Theme.text : "#777b83"
                font.pixelSize: 10
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: 5
                color: bufferSelector.enabled ? "#292b30" : "#222328"
                border.color: bufferSelector.activeFocus ? Theme.accent : "#444750"
            }
        }
        Text {
            text: "OUTPUT DEVICE"
            color: Theme.textMuted
            font.pixelSize: 9
            font.bold: true
        }
        ComboBox {
            id: outputSelector
            Layout.preferredWidth: 300
            Layout.preferredHeight: 30
            model: EngineController.outputDevices
            enabled: count > 0
            currentIndex: EngineController.selectedOutputIndex
            onActivated: index => EngineController.setOutputDevice(index)
            contentItem: Text {
                leftPadding: 9
                rightPadding: 23
                text: outputSelector.count === 0 ? "No output devices"
                      : outputSelector.currentIndex < 0 ? "Select output device" : outputSelector.displayText
                color: outputSelector.enabled ? Theme.text : "#777b83"
                font.pixelSize: 10
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideMiddle
            }
            background: Rectangle {
                radius: 5
                color: outputSelector.enabled ? "#292b30" : "#222328"
                border.color: outputSelector.activeFocus ? Theme.accent : "#444750"
            }
        }
    }
}
