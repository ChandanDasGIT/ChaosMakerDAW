import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The synth's page in the track's FX panel: presets, then the oscillators, filter, envelopes,
// LFO and playing controls. Every control goes by its key through SynthWrapper (its range,
// kind and choices come from SynthWrapper.controls); every change goes straight to the track.
Item {
    id: root
    property var trackModel
    property int trackId: -1
    property string trackName: ""

    readonly property var values: bridge.values
    readonly property bool userPreset: bridge.currentPreset.length > 0 && !bridge.isFactoryPreset(bridge.currentPreset)

    SynthWrapper {
        id: bridge
        objectName: "synthBridge"
        trackModel: root.trackModel ?? null
        trackId: root.trackId
    }

    // ---- How values read on the page
    function signed(v, digits) { return (v > 0 ? "+" : "") + (digits === undefined ? v : v.toFixed(digits)) }
    function format(kind, v) {
        switch (kind) {
        case "percent": return Math.round(v * 100) + " %"
        case "hz": return v >= 1000 ? (v / 1000).toFixed(v >= 10000 ? 1 : 2) + " kHz" : v.toFixed(v < 100 ? 1 : 0) + " Hz"
        case "time": return v < 1 ? Math.round(v * 1000) + " ms" : v.toFixed(2) + " s"
        case "seconds": return v < 0.005 ? "Off" : v.toFixed(2) + " s"
        case "octave": return v === 0 ? "0" : signed(v)
        case "semitones": return signed(v) + " st"
        case "cents": return signed(v, 1) + " ct"
        case "octaves": return signed(v, 1) + " oct"
        case "db": return signed(v, 1) + " dB"
        }
        return String(v)
    }

    // ---- The layout: sections of controls by key. `fmt` says how a value reads, `log` puts a
    // slider on a log scale (frequencies and times), `show` / `enable` decide from the values.
    readonly property var sections: [
        { title: "OSCILLATOR 1", items: [
            { key: "osc1.on", label: "On", tip: "Turn oscillator 1 on or off" },
            { key: "osc1.wave", label: "Wave" },
            { key: "osc1.octave", label: "Octave", fmt: "octave" },
            { key: "osc1.semitones", label: "Semitones", fmt: "semitones" },
            { key: "osc1.fine", label: "Fine", fmt: "cents", tip: "Detune in cents (1/100 semitone)" },
            { key: "osc1.pulseWidth", label: "Pulse width", fmt: "percent", show: () => root.values["osc1.wave"] === "pulse" },
            { key: "osc1.level", label: "Level", fmt: "percent" } ] },
        { title: "OSCILLATOR 2", items: [
            { key: "osc2.on", label: "On", tip: "Turn oscillator 2 on or off" },
            { key: "osc2.wave", label: "Wave" },
            { key: "osc2.octave", label: "Octave", fmt: "octave" },
            { key: "osc2.semitones", label: "Semitones", fmt: "semitones" },
            { key: "osc2.fine", label: "Fine", fmt: "cents", tip: "Detune against oscillator 1 makes it thicker" },
            { key: "osc2.pulseWidth", label: "Pulse width", fmt: "percent", show: () => root.values["osc2.wave"] === "pulse" },
            { key: "osc2.level", label: "Level", fmt: "percent" } ] },
        { title: "SUB AND NOISE", items: [
            { key: "sub.wave", label: "Sub wave", tip: "An octave below oscillator 1" },
            { key: "sub.level", label: "Sub level", fmt: "percent" },
            { key: "noise", label: "Noise", fmt: "percent" } ] },
        { title: "FILTER", items: [
            { key: "filter.mode", label: "Type" },
            { key: "filter.cutoff", label: "Cutoff", fmt: "hz", log: true },
            { key: "filter.resonance", label: "Resonance", fmt: "percent", tip: "A peak at the cutoff; near the top it rings" },
            { key: "filter.drive", label: "Drive", fmt: "percent", tip: "Soft saturation before the filter" },
            { key: "filter.keyTrack", label: "Key tracking", fmt: "percent", tip: "Higher keys open the filter more" },
            { key: "filter.envAmount", label: "Envelope amount", fmt: "octaves", tip: "How far the filter envelope moves the cutoff" },
            { key: "filter.velocity", label: "Velocity", fmt: "percent", tip: "Harder keys open the filter more" } ] },
        { title: "FILTER ENVELOPE", items: [
            { key: "filterEnv.attack", label: "Attack", fmt: "time", log: true },
            { key: "filterEnv.decay", label: "Decay", fmt: "time", log: true },
            { key: "filterEnv.sustain", label: "Sustain", fmt: "percent" },
            { key: "filterEnv.release", label: "Release", fmt: "time", log: true } ] },
        { title: "AMP ENVELOPE", items: [
            { key: "ampEnv.attack", label: "Attack", fmt: "time", log: true },
            { key: "ampEnv.decay", label: "Decay", fmt: "time", log: true },
            { key: "ampEnv.sustain", label: "Sustain", fmt: "percent" },
            { key: "ampEnv.release", label: "Release", fmt: "time", log: true },
            { key: "ampVelocity", label: "Velocity", fmt: "percent", tip: "How much softer soft keys are" } ] },
        { title: "LFO", items: [
            { key: "lfo.shape", label: "Shape" },
            { key: "lfo.target", label: "Moves" },
            { key: "lfo.sync", label: "Sync to tempo", tip: "Follow the project's BPM by a note length instead of a rate in Hz" },
            { key: "lfo.division", label: "Note length", show: () => root.values["lfo.sync"] === true },
            { key: "lfo.rate", label: "Rate", fmt: "hz", log: true, show: () => root.values["lfo.sync"] !== true },
            { key: "lfo.depth", label: "Depth", fmt: "percent", tip: "The mod wheel adds to it" },
            { key: "lfo.delay", label: "Fade-in", fmt: "seconds", tip: "The LFO fades in over this time after each new note" } ] },
        { title: "PLAYING", items: [
            { key: "mode", label: "Voices", tip: "Poly: chords. Mono: one note, each key starts again. Legato: one note, held keys slide on" },
            { key: "glide", label: "Glide", fmt: "seconds", enable: () => root.values["mode"] !== "poly", tip: "Slide between notes (Mono and Legato)" },
            { key: "bendRange", label: "Bend range", fmt: "semitones" },
            { key: "volume", label: "Volume", fmt: "db" } ] }
    ]

    function controlName(key) { return "synth_" + key.replace(".", "_") }

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
                spacing: 3
                Text { text: "Synth"; color: Theme.text; font.pixelSize: Theme.headingSize; font.bold: true }
                Text {
                    text: "Two oscillators, sub and noise through a filter, with envelopes and an LFO"
                    color: Theme.textMuted; font.pixelSize: Theme.smallSize
                    Layout.fillWidth: true; elide: Text.ElideRight
                }
            }
            ThemedComboBox {
                id: presetBox
                objectName: "synthPreset"
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
                objectName: "synthSavePreset"
                text: "Save preset…"
                tooltip: "Keep this sound as a preset of your own"
                onClicked: nameDialog.ask("save", bridge.currentPreset.length > 0 && root.userPreset ? bridge.currentPreset : "")
            }
            PanelButton {
                objectName: "synthRenamePreset"
                text: "Rename…"
                visible: root.userPreset
                tooltip: "Rename your preset"
                onClicked: nameDialog.ask("rename", bridge.currentPreset)
            }
            PanelButton {
                objectName: "synthDeletePreset"
                text: "Delete"
                visible: root.userPreset
                tooltip: "Delete your preset"
                onClicked: deleteDialog.open()
            }
            PanelButton {
                objectName: "synthReset"
                text: "Reset"
                tooltip: "Back to the default synth"
                onClicked: bridge.resetToDefaults()
            }
        }

        Text {
            objectName: "synthError"
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
                columns: root.width > 900 ? 3 : root.width > 440 ? 2 : 1
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
        objectName: "synthPresetNameDialog"
        property string mode: "save"                         // "save" or "rename"
        property string original: ""
        property bool confirmReplace: false
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 340
        modal: true
        padding: 14
        background: Rectangle { color: Theme.popupBg; border.color: Theme.popupBorder; radius: Theme.panelRadius }
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
                objectName: "synthPresetNameField"
                Layout.fillWidth: true
                maximumLength: 48
                selectByMouse: true
                color: Theme.text
                font.pixelSize: Theme.bodySize
                placeholderText: "Preset name"
                placeholderTextColor: Theme.textDisabled
                background: Rectangle { color: Theme.field; border.color: nameField.activeFocus ? Theme.accent : Theme.fieldBorder; radius: Theme.controlRadius }
                onTextEdited: nameDialog.confirmReplace = false
                onAccepted: nameDialog.accept()
                Keys.onEscapePressed: nameDialog.close()
            }
            Text {
                objectName: "synthPresetReplaceNote"
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
                    objectName: "synthPresetNameOk"
                    text: nameDialog.confirmReplace ? "Replace" : nameDialog.mode === "rename" ? "Rename" : "Save"
                    enabled: nameField.text.trim().length > 0
                    onClicked: nameDialog.accept()
                }
            }
        }
    }

    Popup {
        id: deleteDialog
        objectName: "synthPresetDeleteDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 320
        modal: true
        padding: 14
        background: Rectangle { color: Theme.popupBg; border.color: Theme.popupBorder; radius: Theme.panelRadius }
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
                    objectName: "synthPresetDeleteOk"
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
