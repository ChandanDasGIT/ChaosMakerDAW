import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The "Grooves" section of both drum pages (Drums and Electronic Drums): ready-made beats
// grouped by time signature (the project's first). Pick one, choose how many bars, and "Add to
// track" puts it on the track as a clip at the start of the bar the playhead is in. Grooves are
// in beats on the shared drum keys, so they follow the tempo and play on either kit. The
// selected clip on the track can be saved as a groove of your own; your own grooves can be
// renamed or deleted. All of it goes through DrumGrooveWrapper.
ColumnLayout {
    id: root
    property var trackModel
    property int trackId: -1
    property string selected: ""                           // the picked groove's name

    readonly property var selectedGroove: {
        for (const g of bridge.grooves) if (g.name === root.selected) return g
        return null
    }
    // [{meter, matchesProject, grooves: [...]}] in the wrapper's order (the project's meter first).
    readonly property var groups: {
        const out = []
        for (const g of bridge.grooves) {
            let group = out.find(x => x.meter === g.meter)
            if (!group) { group = {meter: g.meter, matchesProject: g.matchesProject, grooves: []}; out.push(group) }
            group.grooves.push(g)
        }
        return out
    }
    readonly property var barChoices: [1, 2, 4, 8]

    spacing: 10
    Layout.fillWidth: true

    DrumGrooveWrapper {
        id: bridge
        objectName: "drumGrooveBridge"
        trackModel: root.trackModel ?? null
        trackId: root.trackId
        engine: EngineController
    }

    // Keep a groove picked: the first of the project's time signature.
    function ensureSelection() {
        if (root.selectedGroove === null && bridge.grooves.length > 0) root.selected = bridge.grooves[0].name
    }
    Component.onCompleted: ensureSelection()
    Connections {
        target: bridge
        function onGroovesChanged() { root.ensureSelection() }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Text { text: "GROOVES"; color: Theme.text; font.pixelSize: Theme.smallSize; font.bold: true; font.letterSpacing: 0.6 }
        Text {
            text: "Beats that follow the project's tempo and play on this kit · project in " + bridge.projectMeter
            color: Theme.textMuted; font.pixelSize: Theme.captionSize
            Layout.fillWidth: true; elide: Text.ElideRight
        }
    }

    // The grooves, one row of chips per time signature.
    Repeater {
        model: root.groups
        delegate: RowLayout {
            id: groupRow
            required property var modelData
            Layout.fillWidth: true
            spacing: 10
            Text {
                text: groupRow.modelData.meter
                color: groupRow.modelData.matchesProject ? Theme.accent : Theme.textMuted
                font.pixelSize: Theme.smallSize; font.bold: true
                Layout.preferredWidth: 34
                Layout.alignment: Qt.AlignTop
                topPadding: 6
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: groupRow.modelData.grooves
                    delegate: ToggleChip {
                        required property var modelData
                        objectName: "groove_" + modelData.name
                        text: modelData.name + (modelData.factory ? "" : "  ·  yours")
                        textSize: 11
                        leftPadding: 10; rightPadding: 10
                        implicitHeight: 28
                        hoverHighlight: true
                        active: root.selected === modelData.name
                        tooltip: modelData.name + " (" + modelData.meter + ", " + modelData.bars
                                 + (modelData.bars === 1 ? " bar" : " bars") + (modelData.factory ? ")" : ", your groove)")
                        onClicked: root.selected = modelData.name
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Text { text: "Bars"; color: Theme.textMuted; font.pixelSize: Theme.captionSize }
        ThemedComboBox {
            id: barsBox
            objectName: "grooveBars"
            implicitWidth: 92
            model: root.barChoices.map(n => n + (n === 1 ? " bar" : " bars"))
            currentIndex: 2                                    // 4 bars
            ToolTip.visible: hovered
            ToolTip.text: "How many bars the clip lasts; the groove repeats to fill them"
        }
        PanelButton {
            objectName: "grooveAdd"
            text: "Add to track"
            enabled: bridge.valid && root.selectedGroove !== null
            tooltip: "A clip with this groove at the start of the bar the playhead is in"
            onClicked: bridge.addToTrack(root.selected, root.barChoices[barsBox.currentIndex])
        }
        Text {
            objectName: "grooveMeterNote"
            visible: root.selectedGroove !== null && !root.selectedGroove.matchesProject
            text: root.selectedGroove ? root.selectedGroove.meter + " groove in a " + bridge.projectMeter
                                        + " project: its bars won't line up with the project's" : ""
            color: Theme.meterWarning
            font.pixelSize: Theme.captionSize
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Item { Layout.fillWidth: true; visible: !(root.selectedGroove !== null && !root.selectedGroove.matchesProject) }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        PanelButton {
            objectName: "grooveSaveClip"
            text: "Save selected clip as groove…"
            enabled: bridge.canSaveClip
            tooltip: bridge.canSaveClip ? "Keep the selected clip's beat as a groove of your own"
                                        : "Select a drum clip with notes on this track first"
            onClicked: nameDialog.ask("save", "")
        }
        PanelButton {
            objectName: "grooveRename"
            text: "Rename…"
            visible: root.selectedGroove !== null && !root.selectedGroove.factory
            tooltip: "Rename your groove"
            onClicked: nameDialog.ask("rename", root.selected)
        }
        PanelButton {
            objectName: "grooveDelete"
            text: "Delete"
            visible: root.selectedGroove !== null && !root.selectedGroove.factory
            tooltip: "Delete your groove"
            onClicked: deleteDialog.open()
        }
        Item { Layout.fillWidth: true }
    }

    Text {
        objectName: "grooveError"
        visible: bridge.lastError.length > 0
        text: bridge.lastError
        color: Theme.meterWarning
        font.pixelSize: Theme.captionSize
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    // Names a groove: a new one from the selected clip, or a new name for one of yours. A name
    // that is taken asks once more before replacing.
    Popup {
        id: nameDialog
        objectName: "grooveNameDialog"
        property string mode: "save"                         // "save" or "rename"
        property string original: ""
        property bool confirmReplace: false
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 340
        modal: true
        padding: 14
        background: Rectangle { color: Theme.popupBg; border.color: Theme.popupBorder; radius: 6 }
        function ask(mode, name) {
            nameDialog.mode = mode
            nameDialog.original = name
            nameDialog.confirmReplace = false
            nameField.text = name
            open()
            nameField.forceActiveFocus()
            nameField.selectAll()
        }
        function accept() {
            const name = nameField.text.trim()
            if (name.length === 0) return
            if (mode === "rename") {
                if (bridge.renameGroove(original, name)) { root.selected = name; close() }
                return
            }
            const taken = bridge.exists(name)
            if (taken && !confirmReplace) { confirmReplace = true; return }
            if (bridge.saveSelectedClip(name, confirmReplace)) { root.selected = name; close() }
            else confirmReplace = false
        }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                text: nameDialog.mode === "rename" ? "Rename groove" : "Save clip as groove"
                color: Theme.text; font.pixelSize: Theme.bodySize; font.bold: true
            }
            Text {
                visible: nameDialog.mode === "save"
                text: "The selected clip becomes a " + bridge.projectMeter + " groove of whole bars."
                color: Theme.textMuted; font.pixelSize: Theme.captionSize
                wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            TextField {
                id: nameField
                objectName: "grooveNameField"
                Layout.fillWidth: true
                maximumLength: 48
                selectByMouse: true
                color: Theme.text
                font.pixelSize: 12
                placeholderText: "Groove name"
                placeholderTextColor: Theme.textDisabled
                background: Rectangle { color: Theme.field; border.color: nameField.activeFocus ? Theme.accent : Theme.fieldBorder; radius: 3 }
                onTextEdited: nameDialog.confirmReplace = false
                onAccepted: nameDialog.accept()
                Keys.onEscapePressed: nameDialog.close()
            }
            Text {
                objectName: "grooveReplaceNote"
                visible: nameDialog.confirmReplace
                text: "A groove of yours has this name. Save again to replace it."
                color: Theme.meterWarning; font.pixelSize: Theme.captionSize
                wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            Text {
                visible: bridge.lastError.length > 0 && nameDialog.visible
                text: bridge.lastError
                color: Theme.meterWarning; font.pixelSize: Theme.captionSize
                wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PanelButton { text: "Cancel"; onClicked: nameDialog.close() }
                PanelButton {
                    objectName: "grooveNameOk"
                    text: nameDialog.confirmReplace ? "Replace" : nameDialog.mode === "rename" ? "Rename" : "Save"
                    enabled: nameField.text.trim().length > 0
                    onClicked: nameDialog.accept()
                }
            }
        }
    }

    Popup {
        id: deleteDialog
        objectName: "grooveDeleteDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 320
        modal: true
        padding: 14
        background: Rectangle { color: Theme.popupBg; border.color: Theme.popupBorder; radius: 6 }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                text: "Delete \"" + root.selected + "\"?"
                color: Theme.text; font.pixelSize: Theme.bodySize; font.bold: true
                wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            Text {
                text: "Clips already on tracks keep their notes."
                color: Theme.textMuted; font.pixelSize: Theme.captionSize
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PanelButton { text: "Cancel"; onClicked: deleteDialog.close() }
                PanelButton {
                    objectName: "grooveDeleteOk"
                    text: "Delete"
                    onClicked: {
                        if (bridge.removeGroove(root.selected)) { root.selected = ""; root.ensureSelection() }
                        deleteDialog.close()
                    }
                }
            }
        }
    }
}
