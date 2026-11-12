import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The bass guitar's page in the track's FX panel: presets, then the style, strings, pickups and
// tone, and playing controls. Every control goes by its key through BassWrapper (its range,
// kind and choices come from BassWrapper.controls); every change goes straight to the track.
Item {
    id: root
    property var trackModel
    property int trackId: -1
    property string trackName: ""

    readonly property var values: bridge.values
    readonly property bool userPreset: bridge.currentPreset.length > 0 && !bridge.isFactoryPreset(bridge.currentPreset)

    BassWrapper {
        id: bridge
        objectName: "bassBridge"
        trackModel: root.trackModel ?? null
        trackId: root.trackId
    }

    // ---- How values read on the page
    function signed(v, digits) { return (v > 0 ? "+" : "") + (digits === undefined ? v : v.toFixed(digits)) }
    function format(kind, v) {
        switch (kind) {
        case "percent": return Math.round(v * 100) + " %"
        case "seconds": return v < 0.005 ? "Off" : Math.round(v * 1000) + " ms"
        case "semitones": return v + " st"
        case "db": return signed(v, 1) + " dB"
        case "pluck": return v < 0.05 ? "Bridge" : v > 0.95 ? "Neck" : Math.round(v * 100) + " % to the neck"
        case "blend": return v < 0.05 ? "Neck" : v > 0.95 ? "Bridge" : Math.round((1 - v) * 100) + " / " + Math.round(v * 100)
        }
        return String(v)
    }

    // ---- The layout: sections of controls by key. `fmt` says how a value reads, `show` /
    // `enable` decide from the values.
    readonly property var sections: [
        { title: "STYLE", items: [
            { key: "style", label: "Style", tip: "Finger: a round pluck. Slap / pop: the thumb slaps the E and A strings, the D and G strings pop. Palm mute: short, thuddy notes" },
            { key: "muteAmount", label: "Palm mute", fmt: "percent", show: () => root.values["style"] === "mute", tip: "How firmly the palm rests on the strings" },
            { key: "slapOnHardHits", label: "Slap on hard hits", tip: "In Finger style, notes from the velocity below up slap or pop" },
            { key: "slapVelocity", label: "From velocity", enable: () => root.values["slapOnHardHits"] === true },
            { key: "keyswitches", label: "Keyswitches", tip: "C0 plays Finger, D0 Slap / pop and E0 Palm mute until the style here changes" } ] },
        { title: "STRINGS", items: [
            { key: "pluckPosition", label: "Pluck position", fmt: "pluck", tip: "By the bridge is bright and tight, over the neck round and deep" },
            { key: "sustain", label: "Sustain", fmt: "percent", tip: "How long the strings ring" },
            { key: "brightness", label: "Brightness", fmt: "percent", tip: "How long the high overtones last" },
            { key: "velocity", label: "Velocity", fmt: "percent", tip: "How much softer and darker soft notes are" } ] },
        { title: "PICKUPS AND TONE", items: [
            { key: "pickupBlend", label: "Pickups", fmt: "blend", tip: "Neck pickup (round, full) to bridge pickup (thin, growly)" },
            { key: "tone", label: "Tone", fmt: "percent", tip: "The tone knob: closed is dark, open is bright" },
            { key: "drive", label: "Drive", fmt: "percent", tip: "Soft clipping, like a driven bass amp" },
            { key: "compression", label: "Compression", fmt: "percent", tip: "Evens out loud and soft notes" } ] },
        { title: "PLAYING", items: [
            { key: "slide", label: "Slide", fmt: "seconds", tip: "A key played over a held one slides there on the same string. Off: every key is plucked" },
            { key: "bendRange", label: "Bend range", fmt: "semitones" },
            { key: "volume", label: "Volume", fmt: "db" } ] }
    ]

    function controlName(key) { return "bass_" + key.replace(".", "_") }

    component SectionTitle: Text {
        color: Theme.text
        font.pixelSize: Theme.smallSize
        font.bold: true
        font.letterSpacing: 0.6
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Header: name, presets, reset
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.bottomMargin: 4
            spacing: 8
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 40                    // the title gives way to the preset buttons
                spacing: 3
                Text {
                    text: "Bass Guitar"; color: Theme.text; font.pixelSize: 15; font.bold: true
                    Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight
                }
                Text {
                    text: "Four modelled strings (E A D G, 24 frets) through neck and bridge pickups"
                    color: Theme.textMuted; font.pixelSize: 11
                    Layout.fillWidth: true; elide: Text.ElideRight
                }
            }
            ThemedComboBox {
                id: presetBox
                objectName: "bassPreset"
                implicitWidth: 170
                model: bridge.presets
                textRole: "name"
                currentIndex: bridge.presets.findIndex(p => p.name === bridge.currentPreset)
                displayText: bridge.currentPreset.length > 0 ? bridge.currentPreset : "Custom"
                onActivated: index => bridge.loadPreset(bridge.presets[index].name)
                ToolTip.visible: hovered
                ToolTip.text: "Built-in presets first, then yours"
            }
            PanelButton {
                objectName: "bassSavePreset"
                text: "Save preset…"
                tooltip: "Keep this sound as a preset of your own"
                onClicked: nameDialog.ask("save", bridge.currentPreset.length > 0 && root.userPreset ? bridge.currentPreset : "")
            }
            PanelButton {
                objectName: "bassRenamePreset"
                text: "Rename…"
                visible: root.userPreset
                tooltip: "Rename your preset"
                onClicked: nameDialog.ask("rename", bridge.currentPreset)
            }
            PanelButton {
                objectName: "bassDeletePreset"
                text: "Delete"
                visible: root.userPreset
                tooltip: "Delete your preset"
                onClicked: deleteDialog.open()
            }
            PanelButton {
                objectName: "bassReset"
                text: "Reset"
                tooltip: "Back to the default bass"
                onClicked: bridge.resetToDefaults()
            }
        }

        Text {
            objectName: "bassError"
            visible: bridge.lastError.length > 0 && !nameDialog.visible
            text: bridge.lastError
            color: Theme.meterWarning
            font.pixelSize: Theme.captionSize
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: body.implicitHeight + 32
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            GridLayout {
                id: body
                x: 16; y: 16
                width: parent.width - 32
                columns: root.width > 900 ? 4 : root.width > 560 ? 2 : 1
                columnSpacing: 28
                rowSpacing: 18

                Repeater {
                    model: root.sections
                    ColumnLayout {
                        id: section
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredWidth: 1                   // equal columns
                        spacing: 8
                        SectionTitle { text: section.modelData.title }

                        Repeater {
                            model: section.modelData.items
                            Item {
                                id: entry
                                required property var modelData
                                readonly property string key: modelData.key
                                readonly property var spec: bridge.controls[key] ?? ({ kind: "number", min: 0, max: 1 })
                                readonly property var value: root.values[key]
                                readonly property bool shown: modelData.show ? modelData.show() : true
                                readonly property bool usable: modelData.enable ? modelData.enable() : true
                                Layout.fillWidth: true
                                visible: shown
                                implicitHeight: shown ? holder.implicitHeight : 0

                                ColumnLayout {
                                    id: holder
                                    width: parent.width
                                    spacing: 2

                                    // A toggle: one chip.
                                    ToggleChip {
                                        objectName: entry.spec.kind === "toggle" ? root.controlName(entry.key) : ""
                                        visible: entry.spec.kind === "toggle"
                                        text: entry.modelData.label
                                        active: entry.value === true
                                        enabled: entry.usable
                                        implicitHeight: 28
                                        implicitWidth: Math.max(86, contentItem.implicitWidth + 18)
                                        textSize: Theme.smallSize
                                        cornerRadius: 6
                                        tooltip: entry.modelData.tip ?? ""
                                        onClicked: bridge.set(entry.key, !active)
                                    }

                                    // A choice: label and a list.
                                    RowLayout {
                                        visible: entry.spec.kind === "choice"
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Text {
                                            text: entry.modelData.label
                                            color: Theme.textMuted; font.pixelSize: Theme.captionSize
                                            Layout.preferredWidth: 78
                                        }
                                        ThemedComboBox {
                                            objectName: entry.spec.kind === "choice" ? root.controlName(entry.key) : ""
                                            Layout.fillWidth: true
                                            enabled: entry.usable
                                            model: entry.spec.choices ?? []
                                            textRole: "title"
                                            valueRole: "id"
                                            currentIndex: (entry.spec.choices ?? []).findIndex(c => c.id === entry.value)
                                            onActivated: index => bridge.set(entry.key, entry.spec.choices[index].id)
                                            ToolTip.visible: hovered && (entry.modelData.tip ?? "").length > 0
                                            ToolTip.text: entry.modelData.tip ?? ""
                                            ToolTip.delay: 600
                                        }
                                    }

                                    // A number: label, value and a slider (log scale for times and
                                    // frequencies: the slider runs 0..1).
                                    ColumnLayout {
                                        id: number
                                        objectName: entry.spec.kind === "number" || entry.spec.kind === "integer"
                                                    ? root.controlName(entry.key) : ""
                                        visible: entry.spec.kind === "number" || entry.spec.kind === "integer"
                                        enabled: entry.usable
                                        opacity: enabled ? 1 : 0.45
                                        Layout.fillWidth: true
                                        spacing: 2
                                        readonly property bool log: entry.modelData.log === true && entry.spec.min > 0
                                        readonly property real current: typeof entry.value === "number" ? entry.value : 0
                                        readonly property string valueText: root.format(entry.modelData.fmt ?? "", current)
                                        function toSlider(v) {
                                            return log ? Math.log(v / entry.spec.min) / Math.log(entry.spec.max / entry.spec.min) : v
                                        }
                                        function fromSlider(p) {
                                            return log ? entry.spec.min * Math.pow(entry.spec.max / entry.spec.min, p) : p
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text { text: entry.modelData.label; color: Theme.textMuted; font.pixelSize: Theme.captionSize; Layout.fillWidth: true }
                                            Text { text: number.valueText; color: Theme.text; font.pixelSize: Theme.smallSize }
                                        }
                                        HardwareSlider {
                                            Layout.fillWidth: true
                                            from: number.log ? 0 : entry.spec.min
                                            to: number.log ? 1 : entry.spec.max
                                            stepSize: entry.spec.kind === "integer" ? 1 : number.log ? 0.005 : (entry.spec.max - entry.spec.min) / 200
                                            snapMode: entry.spec.kind === "integer" ? Slider.SnapAlways : Slider.NoSnap
                                            value: number.toSlider(number.current)
                                            onMoved: bridge.set(entry.key, number.fromSlider(value))
                                            ToolTip.visible: hovered && (entry.modelData.tip ?? "").length > 0
                                            ToolTip.text: entry.modelData.tip ?? ""
                                            ToolTip.delay: 600
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Names a preset: a new one from the sound now, or a new name for one of yours. A name that
    // is taken asks once more before replacing.
    Popup {
        id: nameDialog
        objectName: "bassPresetNameDialog"
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
                if (bridge.renamePreset(original, name)) close()
                return
            }
            const taken = bridge.presetExists(name) && !bridge.isFactoryPreset(name)
            if (taken && !confirmReplace) { confirmReplace = true; return }
            if (bridge.savePreset(name, confirmReplace)) close()
            else confirmReplace = false
        }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                text: nameDialog.mode === "rename" ? "Rename preset" : "Save preset"
                color: Theme.text; font.pixelSize: Theme.bodySize; font.bold: true
            }
            TextField {
                id: nameField
                objectName: "bassPresetNameField"
                Layout.fillWidth: true
                maximumLength: 48
                selectByMouse: true
                color: Theme.text
                font.pixelSize: 12
                placeholderText: "Preset name"
                placeholderTextColor: Theme.textDisabled
                background: Rectangle { color: Theme.field; border.color: nameField.activeFocus ? Theme.accent : Theme.fieldBorder; radius: 3 }
                onTextEdited: nameDialog.confirmReplace = false
                onAccepted: nameDialog.accept()
                Keys.onEscapePressed: nameDialog.close()
            }
            Text {
                objectName: "bassPresetReplaceNote"
                visible: nameDialog.confirmReplace
                text: "A preset of yours has this name. Save again to replace it."
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
                    objectName: "bassPresetNameOk"
                    text: nameDialog.confirmReplace ? "Replace" : nameDialog.mode === "rename" ? "Rename" : "Save"
                    enabled: nameField.text.trim().length > 0
                    onClicked: nameDialog.accept()
                }
            }
        }
    }

    Popup {
        id: deleteDialog
        objectName: "bassPresetDeleteDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 320
        modal: true
        padding: 14
        background: Rectangle { color: Theme.popupBg; border.color: Theme.popupBorder; radius: 6 }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                text: "Delete \"" + bridge.currentPreset + "\"?"
                color: Theme.text; font.pixelSize: Theme.bodySize; font.bold: true
                wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            Text {
                text: "The track keeps its sound."
                color: Theme.textMuted; font.pixelSize: Theme.captionSize
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PanelButton { text: "Cancel"; onClicked: deleteDialog.close() }
                PanelButton {
                    objectName: "bassPresetDeleteOk"
                    text: "Delete"
                    onClicked: {
                        bridge.removePreset(bridge.currentPreset)
                        deleteDialog.close()
                    }
                }
            }
        }
    }
}
