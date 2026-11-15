import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Names a preset or a groove: save new, rename, or replace one of yours.
Popup {
    id: root
    // Test names: <namePrefix>NameDialog, NameField, ReplaceNote, NameOk.
    property string namePrefix: ""
    property string saveTitle: ""
    property string renameTitle: ""
    property string placeholder: ""
    property string replaceText: ""
    property string hint: ""               // shown when saving, if not empty
    property string errorText: ""
    // Set by the owner: name -> bool, (name, replace) -> bool, (original, name) -> bool.
    property var isTaken: name => false
    property var save: (name, replace) => false
    property var rename: (original, name) => false

    property string mode: "save"           // "save" or "rename"
    property string original: ""
    property bool confirmReplace: false

    objectName: namePrefix + "NameDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 340
    modal: true
    padding: 14
    background: Rectangle { color: Theme.popupBg; border.color: Theme.popupBorder; radius: Theme.panelRadius }

    function ask(mode, name) {
        root.mode = mode
        root.original = name
        root.confirmReplace = false
        nameField.text = name
        open()
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    function accept() {
        const name = NameDialogWrap.trimmed(nameField.text)
        const renaming = root.mode === "rename"
        switch (NameDialogWrap.nextStep(renaming, name, !renaming && root.isTaken(name), root.confirmReplace)) {
        case NameDialogWrap.Rename:
            if (root.rename(root.original, name)) close()
            break
        case NameDialogWrap.AskReplace:
            root.confirmReplace = true
            break
        case NameDialogWrap.Save:
            if (root.save(name, root.confirmReplace)) close()
            else root.confirmReplace = false
            break
        }
    }

    contentItem: ColumnLayout {
        spacing: 10
        Text {
            text: root.mode === "rename" ? root.renameTitle : root.saveTitle
            color: Theme.text; font.pixelSize: Theme.bodySize; font.bold: true
        }
        Text {
            visible: root.mode === "save" && root.hint.length > 0
            text: root.hint
            color: Theme.textMuted; font.pixelSize: Theme.captionSize
            wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
        TextField {
            id: nameField
            objectName: root.namePrefix + "NameField"
            Layout.fillWidth: true
            maximumLength: 48
            selectByMouse: true
            color: Theme.text
            font.pixelSize: Theme.bodySize
            placeholderText: root.placeholder
            placeholderTextColor: Theme.textDisabled
            background: Rectangle { color: Theme.field; border.color: nameField.activeFocus ? Theme.accent : Theme.fieldBorder; radius: Theme.controlRadius }
            onTextEdited: root.confirmReplace = false
            onAccepted: root.accept()
            Keys.onEscapePressed: root.close()
        }
        Text {
            objectName: root.namePrefix + "ReplaceNote"
            visible: root.confirmReplace
            text: root.replaceText
            color: Theme.meterWarning; font.pixelSize: Theme.captionSize
            wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
        Text {
            visible: root.errorText.length > 0 && root.visible
            text: root.errorText
            color: Theme.meterWarning; font.pixelSize: Theme.captionSize
            wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PanelButton { text: "Cancel"; onClicked: root.close() }
            PanelButton {
                objectName: root.namePrefix + "NameOk"
                text: root.confirmReplace ? "Replace" : root.mode === "rename" ? "Rename" : "Save"
                enabled: NameDialogWrap.trimmed(nameField.text).length > 0
                onClicked: root.accept()
            }
        }
    }
}
