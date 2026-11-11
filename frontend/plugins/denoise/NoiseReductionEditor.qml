import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Noise Reduction's FX page (restMap G13): the learned noise drawn as a filled area, the live
// sound going in (muted line) and coming out (accent line) over it, the controls, and "Learn
// from selection" (the timeline's time selection over a part with only noise). The controls go
// through MixEffectWrapper ("denoise"); the curves and learning through DenoiseWrapper.
Item {
    id: root
    property var trackModel
    property int trackId: -1          // a track id, or a clip's effects id
    property string trackName: ""
    property bool active: false
    readonly property alias effect: fx
    readonly property alias page: noise
    readonly property bool hasSelection: EngineController.timeSelectionEndBeat > EngineController.timeSelectionStartBeat

    MixEffectWrapper {
        id: fx
        trackModel: root.trackModel ?? null
        trackId: root.trackId
        effectType: "denoise"
        engine: EngineController
        metering: root.active
    }
    DenoiseWrapper {
        id: noise
        trackModel: root.trackModel ?? null
        effectsId: root.trackId
        engine: EngineController
        analyzerEnabled: root.active
        onProfileChanged: canvas.requestPaint()
        onSpectrumChanged: canvas.requestPaint()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                text: "NOISE REDUCTION"
                color: Theme.text; font.pixelSize: 11; font.bold: true; font.letterSpacing: 0.7
            }
            Text {
                text: root.trackName
                color: Theme.textMuted; font.pixelSize: 10
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                objectName: "denoiseReduction"
                visible: noise.hasProfile && !fx.bypassed
                text: "-" + fx.reductionDb.toFixed(1) + " dB"
                color: Theme.textMuted; font.pixelSize: 10; font.family: Theme.monospaceFontFamily
            }
            ToggleChip {
                objectName: "denoiseBypass"
                active: !fx.bypassed
                text: fx.bypassed ? "BYP" : "ON"; textSize: 9
                implicitWidth: 34; implicitHeight: 22
                onClicked: fx.bypassed = !fx.bypassed
                tooltip: fx.bypassed ? "Bypassed: the sound passes through unchanged" : "Bypass this effect"
            }
        }

        // The spectrum: -110 dBFS at the bottom, -10 dBFS at the top; 20 Hz .. 20 kHz.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            color: Theme.chaosEq.background
            radius: 4
            Item {
                id: plot
                anchors.fill: parent
                anchors.margins: 8
                anchors.bottomMargin: 20
                readonly property real lowDb: -110
                readonly property real highDb: -10
                function xOf(hz) { return width * Math.log(hz / 20) / Math.log(1000) }
                function yOf(db) { return height * (1 - Math.max(0, Math.min(1, (db - lowDb) / (highDb - lowDb)))) }

                Canvas {
                    id: canvas
                    objectName: "denoiseSpectrum"
                    anchors.fill: parent
                    anchors.bottomMargin: -14
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        const w = plot.width, h = plot.height
                        const freqs = noise.frequencies
                        ctx.lineWidth = 1
                        ctx.font = "10px sans-serif"
                        const labels = {20: "20", 50: "50", 100: "100", 200: "200", 500: "500", 1000: "1k",
                                        2000: "2k", 5000: "5k", 10000: "10k", 20000: "20k"}
                        for (const hz of [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]) {
                            const x = Math.round(plot.xOf(hz)) + 0.5
                            ctx.strokeStyle = String(hz === 100 || hz === 1000 || hz === 10000 ? Theme.chaosEq.gridStrong : Theme.chaosEq.grid)
                            ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke()
                            ctx.fillStyle = String(Theme.chaosEq.gridLabel)
                            const tw = ctx.measureText(labels[hz]).width
                            ctx.fillText(labels[hz], Math.max(2, Math.min(w - tw - 2, x - tw / 2)), h + 12)
                        }
                        for (let db = -100; db <= -20; db += 20) {
                            const y = Math.round(plot.yOf(db)) + 0.5
                            ctx.strokeStyle = String(Theme.chaosEq.grid)
                            ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke()
                            ctx.fillStyle = String(Theme.chaosEq.gridLabel)
                            ctx.fillText(db + " dB", 4, y - 3)
                        }
                        const line = (values, colour) => {
                            if (values.length !== freqs.length || freqs.length < 2) return
                            ctx.beginPath()
                            for (let i = 0; i < freqs.length; ++i) {
                                const x = plot.xOf(freqs[i]), y = plot.yOf(values[i])
                                if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                            }
                            ctx.strokeStyle = colour
                            ctx.stroke()
                        }
                        // The learned noise, filled from the bottom.
                        const profile = noise.profile
                        if (profile.length === freqs.length && freqs.length > 1) {
                            ctx.beginPath()
                            ctx.moveTo(plot.xOf(freqs[0]), h)
                            for (let i = 0; i < freqs.length; ++i) ctx.lineTo(plot.xOf(freqs[i]), plot.yOf(profile[i]))
                            ctx.lineTo(plot.xOf(freqs[freqs.length - 1]), h)
                            ctx.closePath()
                            ctx.globalAlpha = 0.7
                            ctx.fillStyle = String(Theme.chaosEq.spectrumFill)
                            ctx.fill()
                            ctx.globalAlpha = 1
                            line(profile, String(Theme.chaosEq.spectrumLine))
                        }
                        line(noise.input, String(Theme.textMuted))
                        line(noise.output, String(Theme.accent))
                    }
                }
            }
            // Key.
            Row {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 8
                spacing: 10
                Repeater {
                    model: [{name: "Learned noise", colour: Theme.chaosEq.spectrumLine},
                            {name: "In", colour: Theme.textMuted}, {name: "Out", colour: Theme.accent}]
                    delegate: Row {
                        required property var modelData
                        spacing: 4
                        Rectangle { width: 10; height: 2; anchors.verticalCenter: parent.verticalCenter; color: modelData.colour }
                        Text { text: modelData.name; color: Theme.chaosEq.gridLabel; font.pixelSize: 10 }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: !noise.hasProfile
                text: "No noise learned yet"
                color: Theme.chaosEq.gridLabel
                font.pixelSize: 12
            }
        }

        // Learning.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            PanelButton {
                objectName: "denoiseLearn"
                text: noise.hasProfile ? "Learn again from selection" : "Learn from selection"
                enabled: root.hasSelection && !EngineController.takeBusy
                tooltip: root.hasSelection ? "Learn the noise from the time selection on the timeline"
                                           : "Drag a time selection on the timeline over a part with only noise first"
                onClicked: noise.learn(EngineController.timeSelectionStartBeat, EngineController.timeSelectionEndBeat)
            }
            PanelButton {
                objectName: "denoiseForget"
                visible: noise.hasProfile
                text: "Forget"
                tooltip: "Forget the learned noise: the sound passes through unchanged"
                onClicked: noise.forget()
            }
            Text {
                objectName: "denoiseStatus"
                Layout.fillWidth: true
                text: noise.message.length > 0 ? noise.message
                    : noise.hasProfile ? "Noise learned. Set Reduction to taste; turn on Hear removed noise to check no voice is taken."
                    : "Select a part with only noise (before the singing or between phrases) on the timeline, then Learn."
                color: noise.message.length > 0 ? Theme.muteText : Theme.textMuted
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
        }

        // The controls, "name | slider | value" rows (double-click a slider for its default).
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 18
            rowSpacing: 4
            Repeater {
                model: fx.cards.length > 0 ? fx.cards[0].controls : []
                delegate: RowLayout {
                    id: controlRow
                    required property var modelData
                    readonly property real modelValue: Number(fx.values[modelData.id] ?? modelData["default"])
                    Layout.fillWidth: true
                    Layout.preferredHeight: 26
                    spacing: 4
                    Text {
                        text: controlRow.modelData.name
                        color: Theme.textMuted
                        font.pixelSize: 10
                        elide: Text.ElideRight
                        Layout.preferredWidth: 110
                    }
                    HardwareSlider {
                        id: parameterSlider
                        objectName: "denoise." + controlRow.modelData.id
                        visible: controlRow.modelData.kind === "slider"
                        Layout.fillWidth: true
                        implicitHeight: 22
                        padding: 0
                        stepSize: fx.step(controlRow.modelData.id)
                        from: controlRow.modelData.min
                        to: controlRow.modelData.max
                        value: controlRow.modelValue
                        onMoved: fx.setValue(controlRow.modelData.id, value)
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onDoubleTapped: Qt.callLater(() => fx.resetValue(controlRow.modelData.id))
                        }
                        ToolTip.visible: hovered || pressed
                        ToolTip.delay: pressed ? 0 : 450
                        ToolTip.text: controlRow.modelData.name + " " + fx.formatValue(controlRow.modelData.id, value)
                                      + " · double-click for default"
                    }
                    Binding {
                        target: parameterSlider
                        property: "value"
                        value: controlRow.modelValue
                        when: !parameterSlider.pressed
                    }
                    Switch {
                        objectName: "denoise." + controlRow.modelData.id + ".switch"
                        visible: controlRow.modelData.kind === "switch"
                        Layout.fillWidth: true
                        checked: controlRow.modelValue >= 0.5
                        onClicked: fx.setValue(controlRow.modelData.id, checked ? 1 : 0)
                    }
                    Text {
                        visible: controlRow.modelData.kind === "slider"
                        text: fx.formatValue(controlRow.modelData.id, parameterSlider.value)
                        color: Theme.text
                        font.pixelSize: 10
                        font.family: Theme.monospaceFontFamily
                        horizontalAlignment: Text.AlignRight
                        Layout.preferredWidth: 56
                    }
                }
            }
        }
    }
}
