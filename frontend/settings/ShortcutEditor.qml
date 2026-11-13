import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../common"

// Keyboard shortcuts editor (restMap G16), opened from Preferences. Every action by group with
// its key; click a key, press the new one. A key another action uses asks first ("Assign"
// takes it over, that action is left with none). Esc cancels; Enter and Tab are kept for
// dialogs. The rules and saving live in the Shortcuts singleton (backend/settings/shortcutmap).
Popup {
    id: root
    // The action whose key is being changed ("" = none), the key pressed, and its takers.
    property string capturingId: ""
    property string pendingKey: ""
    property var pendingConflicts: []
    property string message: ""

    function startCapture(id) {
        root.capturingId = id
        root.pendingKey = ""
        root.pendingConflicts = []
        root.message = "Press the new key for “" + root.nameOf(id) + "” · Esc cancels"
        keyCatcher.forceActiveFocus()
    }
    function stopCapture() {
        root.capturingId = ""
        root.pendingKey = ""
        root.pendingConflicts = []
        root.message = ""
    }
    function nameOf(id) {
        const list = Shortcuts.actions
        for (let i = 0; i < list.length; ++i) if (list[i].id === id) return list[i].name
        return id
    }
    // A key pressed while capturing.
    function offer(key) {
        if (key.length === 0) return                      // a modifier alone: keep waiting
        if (!Shortcuts.isAllowed(key)) {
            root.message = Shortcuts.displayText(key) + " is kept for dialogs (cancel, confirm, move focus). Try another key."
            return
        }
        const taken = Shortcuts.conflicts(root.capturingId, key)
        if (taken.length === 0) { root.assign(key); return }
        root.pendingKey = key
        root.pendingConflicts = taken
        root.message = Shortcuts.displayText(key) + " is used by “" + taken.join("”, “") + "”."
    }
    function assign(key) {
        Shortcuts.set(root.capturingId, key)
        root.stopCapture()
    }

    objectName: "shortcutEditor"
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(480, parent.width - 32)
    height: Math.min(600, parent.height - 32)
    padding: 16
    modal: true
    focus: true
    // Esc while capturing cancels the capture, not the editor.
    closePolicy: root.capturingId.length > 0 ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)
    onClosed: root.stopCapture()
    background: Rectangle {
        color: Theme.panelBg
        border.color: Theme.divider
        border.width: 1
        radius: Theme.panelRadius
    }

    contentItem: ColumnLayout {
        spacing: 10

        // Takes the key presses while capturing; shortcuts elsewhere stay quiet meanwhile.
        Item {
            id: keyCatcher
            objectName: "shortcutKeyCatcher"
            Layout.preferredWidth: 0
            Layout.preferredHeight: 0
            Keys.onShortcutOverride: event => event.accepted = root.capturingId.length > 0
            Keys.onPressed: event => {
                if (root.capturingId.length === 0) return
                event.accepted = true
                if (event.key === Qt.Key_Escape) { root.stopCapture(); return }
                root.offer(Shortcuts.keyFromEvent(event.key, event.modifiers))
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "Keyboard Shortcuts"
                color: Theme.text
                font.pixelSize: Theme.bodySize + 2
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            CloseButton {
                Accessible.name: "Close keyboard shortcuts"
                implicitWidth: 28
                implicitHeight: 28
                glyphSize: 18
                onClicked: root.close()
            }
        }

        Text {
            Layout.fillWidth: true
            text: "Click a key to change it. With KEYS on, the typing keyboard's note keys play notes first."
            color: Theme.textMuted
            font.pixelSize: Theme.smallSize
            wrapMode: Text.WordWrap
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.divider }

        ListView {
            id: list
            objectName: "shortcutList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            // A fixed count, each row reading its action: a key change updates the rows in place
            // (the list keeps its scroll position) instead of rebuilding them.
            model: Shortcuts.actionCount
            ScrollBar.vertical: ScrollBar {}

            delegate: Column {
                id: row
                required property int index
                readonly property var modelData: Shortcuts.actions[row.index]
                readonly property bool capturing: root.capturingId === row.modelData.id
                // The group's name above its first action.
                readonly property bool firstOfGroup: row.index === 0 || Shortcuts.actions[row.index - 1].group !== row.modelData.group
                objectName: "shortcutRow_" + row.modelData.id
                width: ListView.view.width

                Text {
                    visible: row.firstOfGroup
                    width: parent.width
                    topPadding: row.index === 0 ? 0 : 8
                    bottomPadding: 4
                    text: row.modelData.group
                    color: Theme.text
                    font.pixelSize: Theme.bodySize
                    font.bold: true
                }

                RowLayout {
                    width: parent.width - 12
                    height: 30
                    spacing: 6
                    Text {
                        Layout.fillWidth: true
                        text: row.modelData.name
                        color: Theme.text
                        font.pixelSize: Theme.bodySize
                        elide: Text.ElideRight
                    }
                    PanelButton {
                        objectName: "shortcutKey_" + row.modelData.id
                        Layout.preferredWidth: 120
                        checkable: false
                        checked: row.capturing
                        text: row.capturing ? "Press a key…" : (row.modelData.text.length > 0 ? row.modelData.text : "—")
                        tooltip: "Change the key"
                        onClicked: root.startCapture(row.modelData.id)
                    }
                    PanelButton {
                        objectName: "shortcutReset_" + row.modelData.id
                        Layout.preferredWidth: 52
                        text: "Reset"
                        enabled: !row.modelData.isDefault
                        opacity: enabled ? 1 : 0
                        tooltip: "Back to the default key"
                        onClicked: { root.stopCapture(); Shortcuts.reset(row.modelData.id) }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.divider }

        // What is happening: the capture prompt, a refusal, or a conflict to confirm.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text {
                objectName: "shortcutMessage"
                Layout.fillWidth: true
                text: root.message
                color: root.pendingConflicts.length > 0 ? Theme.muteText : Theme.textMuted
                font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap
            }
            PanelButton {
                objectName: "shortcutAssign"
                primary: true
                visible: root.pendingConflicts.length > 0
                text: "Assign"
                tooltip: "Use this key here; the other action is left with none"
                onClicked: root.assign(root.pendingKey)
            }
            PanelButton {
                objectName: "shortcutClear"
                visible: root.capturingId.length > 0
                text: "No key"
                tooltip: "Leave this action without a key"
                onClicked: root.assign("")
            }
            PanelButton {
                objectName: "shortcutCancel"
                visible: root.capturingId.length > 0
                text: "Cancel"
                onClicked: root.stopCapture()
            }
            PanelButton {
                objectName: "shortcutResetAll"
                visible: root.capturingId.length === 0
                text: "Reset all"
                tooltip: "Every action back to its default key"
                onClicked: Shortcuts.resetAll()
            }
        }
    }
}
