import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Chaos EQ editor (plan 6.2): the curve with draggable band nodes over the live spectrum on
// the left, the selected band's controls on the right. View only: every change goes through
// ChaosEqWrapper.
//
// On the curve: double-click to add a band, drag a node to move it, scroll on a node to
// change its Q, double-click a node to bypass it, Delete removes the selected band.
Item {
    id: root
    property var trackModel
    property int trackId: -1
    property string trackName: ""
    // True while this page is the one shown; the analyzer runs only then.
    property bool active: false
    property int selectedSlot: -1
    property real displayRange: 12      // the curve shows ± this many dB
    property bool analyzerOn: true

    readonly property var bandBySlot: {
        const map = {}
        for (const band of bridge.bands) map[band.slot] = band
        return map
    }
    readonly property var selectedBand: selectedSlot >= 0 ? (bandBySlot[selectedSlot] ?? null) : null
    onBandBySlotChanged: if (selectedSlot >= 0 && !bandBySlot[selectedSlot]) selectedSlot = -1
    onTrackIdChanged: selectedSlot = -1

    function bandColor(slot) {
        const colors = Theme.chaosEq.bandColors
        return colors[slot % colors.length]
    }
    function describe(band) {
        let text = "Band " + (band.slot + 1) + " · " + bridge.shapeTitle(band.shape) + " · "
                 + bridge.formatFrequency(band.frequency)
        if (band.usesGain) text += " · " + bridge.formatGain(band.gain)
        return text + " · Q " + bridge.formatQ(band.q) + (band.bypass ? " · bypassed" : "")
    }

    ChaosEqWrapper {
        id: bridge
        objectName: "chaosEqBridge"
        trackModel: root.trackModel ?? null
        trackId: root.trackId
        engine: EngineController
        analyzerEnabled: root.active && root.analyzerOn
    }

    // A labelled slider; log scale for frequency and Q.
    component ValueSlider: ColumnLayout {
        id: valueSlider
        property string label
        property string valueText
        property real from: 0
        property real to: 1
        property real value: 0
        property bool logScale: false
        signal moved(real value)
        Layout.fillWidth: true
        spacing: 2
        opacity: enabled ? 1 : 0.45
        RowLayout {
            Layout.fillWidth: true
            Text { text: valueSlider.label; color: Theme.textMuted; font.pixelSize: Theme.captionSize; Layout.fillWidth: true }
            Text { text: valueSlider.valueText; color: Theme.text; font.pixelSize: Theme.smallSize }
        }
        HardwareSlider {
            Layout.fillWidth: true
            from: 0
            to: 1
            value: valueSlider.logScale
                   ? Math.log(valueSlider.value / valueSlider.from) / Math.log(valueSlider.to / valueSlider.from)
                   : (valueSlider.value - valueSlider.from) / (valueSlider.to - valueSlider.from)
            onMoved: valueSlider.moved(valueSlider.logScale
                                       ? valueSlider.from * Math.pow(valueSlider.to / valueSlider.from, value)
                                       : valueSlider.from + value * (valueSlider.to - valueSlider.from))
        }
    }

    component FieldLabel: Text {
        color: Theme.textMuted
        font.pixelSize: Theme.captionSize
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text { text: root.trackName + " · CHAOS EQ"; color: Theme.text; font.pixelSize: Theme.headingSize; font.bold: true; Layout.fillWidth: true }
            Text {
                objectName: "chaosEqBandCount"
                text: bridge.bandCount + " / " + bridge.maxBands + " bands"
                color: Theme.textMuted
                font.pixelSize: Theme.captionSize
            }
            ThemedComboBox {
                objectName: "chaosEqRange"
                implicitWidth: 92
                model: ["± 12 dB", "± 24 dB", "± 30 dB"]
                currentIndex: [12, 24, 30].indexOf(root.displayRange)
                onActivated: index => root.displayRange = [12, 24, 30][index]
                ToolTip.visible: hovered
                ToolTip.text: "Gain range shown on the curve"
            }
            ToggleChip {
                objectName: "chaosEqAnalyzer"
                text: "ANALYZER"
                implicitWidth: 78
                implicitHeight: 26
                active: root.analyzerOn
                tooltip: root.analyzerOn ? "Spectrum analyzer on" : "Spectrum analyzer off"
                onClicked: root.analyzerOn = !root.analyzerOn
            }
            PanelButton {
                objectName: "chaosEqClear"
                text: "Clear all"
                enabled: bridge.bandCount > 0
                onClicked: bridge.removeAllBands()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.chaosEq.background
                radius: Theme.panelRadius
                clip: true

                Item {
                    id: plot
                    objectName: "chaosEqPlot"
                    anchors.fill: parent
                    focus: true
                    readonly property real labelStrip: 18          // frequency labels at the bottom
                    readonly property real plotHeight: height - labelStrip
                    readonly property real halfSpan: Math.max(1, plotHeight / 2 - 10)
                    readonly property var frequencies: bridge.curveFrequencies

                    function xOf(hz) { return width * Math.log(hz / 20) / Math.log(1000) }
                    function hzOf(x) { return 20 * Math.pow(1000, Math.max(0, Math.min(1, x / width))) }
                    function yOf(db) {
                        const t = Math.max(-1.06, Math.min(1.06, db / root.displayRange))
                        return plotHeight / 2 - t * halfSpan
                    }
                    function dbOf(y) { return (plotHeight / 2 - y) / halfSpan * root.displayRange }
                    // The spectrum has its own scale: -96 dBFS at the bottom, 0 dBFS at the top.
                    function ySpectrum(db) { return plotHeight * (1 - Math.max(0, Math.min(1, (db + 96) / 96))) }

                    Keys.onDeletePressed: if (root.selectedSlot >= 0) bridge.removeBand(root.selectedSlot)
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Backspace && root.selectedSlot >= 0) {
                            bridge.removeBand(root.selectedSlot)
                            event.accepted = true
                        }
                    }

                    Canvas {
                        id: canvas
                        anchors.fill: parent
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            const w = width, h = plot.plotHeight
                            const freqs = plot.frequencies
                            ctx.lineWidth = 1
                            ctx.font = Theme.captionSize + "px sans-serif"

                            // Frequency grid and labels.
                            const lines = [20, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900,
                                           1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 20000]
                            const labels = {20: "20", 50: "50", 100: "100", 200: "200", 500: "500", 1000: "1k",
                                            2000: "2k", 5000: "5k", 10000: "10k", 20000: "20k"}
                            for (const hz of lines) {
                                const x = Math.round(plot.xOf(hz)) + 0.5
                                const strong = hz === 100 || hz === 1000 || hz === 10000
                                ctx.strokeStyle = String(strong ? Theme.chaosEq.gridStrong : Theme.chaosEq.grid)
                                ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke()
                                if (labels[hz] !== undefined) {
                                    ctx.fillStyle = String(Theme.chaosEq.gridLabel)
                                    const tw = ctx.measureText(labels[hz]).width
                                    ctx.fillText(labels[hz], Math.max(2, Math.min(w - tw - 2, x - tw / 2)), h + 13)
                                }
                            }
                            // Gain grid and labels.
                            const step = root.displayRange <= 12 ? 3 : 6
                            for (let db = -root.displayRange; db <= root.displayRange; db += step) {
                                const y = Math.round(plot.yOf(db)) + 0.5
                                ctx.strokeStyle = String(db === 0 ? Theme.chaosEq.gridStrong : Theme.chaosEq.grid)
                                ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke()
                                ctx.fillStyle = String(Theme.chaosEq.gridLabel)
                                ctx.fillText((db > 0 ? "+" : "") + db, 4, y - 3)
                            }

                            // Live spectrum.
                            const spectrum = bridge.spectrum
                            if (spectrum.length === freqs.length && freqs.length > 1) {
                                ctx.beginPath()
                                ctx.moveTo(plot.xOf(freqs[0]), h)
                                for (let i = 0; i < freqs.length; ++i) ctx.lineTo(plot.xOf(freqs[i]), plot.ySpectrum(spectrum[i]))
                                ctx.lineTo(plot.xOf(freqs[freqs.length - 1]), h)
                                ctx.closePath()
                                ctx.globalAlpha = 0.7
                                ctx.fillStyle = String(Theme.chaosEq.spectrumFill)
                                ctx.fill()
                                ctx.globalAlpha = 1
                                ctx.beginPath()
                                for (let i = 0; i < freqs.length; ++i) {
                                    const x = plot.xOf(freqs[i]), y = plot.ySpectrum(spectrum[i])
                                    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                                }
                                ctx.strokeStyle = String(Theme.chaosEq.spectrumLine)
                                ctx.stroke()
                            }

                            // The selected band's own response, filled from 0 dB.
                            const selected = bridge.bandCurves[String(root.selectedSlot)]
                            if (selected && selected.length === freqs.length) {
                                ctx.beginPath()
                                ctx.moveTo(plot.xOf(freqs[0]), plot.yOf(0))
                                for (let i = 0; i < freqs.length; ++i) ctx.lineTo(plot.xOf(freqs[i]), plot.yOf(selected[i]))
                                ctx.lineTo(plot.xOf(freqs[freqs.length - 1]), plot.yOf(0))
                                ctx.closePath()
                                ctx.globalAlpha = 0.22
                                ctx.fillStyle = root.bandColor(root.selectedSlot)
                                ctx.fill()
                                ctx.globalAlpha = 0.8
                                ctx.strokeStyle = root.bandColor(root.selectedSlot)
                                ctx.stroke()
                                ctx.globalAlpha = 1
                            }

                            // The summed curve.
                            const total = bridge.totalCurve
                            if (total.length === freqs.length) {
                                ctx.beginPath()
                                for (let i = 0; i < freqs.length; ++i) {
                                    const x = plot.xOf(freqs[i]), y = plot.yOf(total[i])
                                    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                                }
                                ctx.lineWidth = 2
                                ctx.strokeStyle = String(Theme.chaosEq.curve)
                                ctx.stroke()
                            }
                        }
                        Connections {
                            target: bridge
                            function onSettingsChanged() { canvas.requestPaint() }
                            function onSpectrumChanged() { canvas.requestPaint() }
                        }
                        Connections {
                            target: root
                            function onSelectedSlotChanged() { canvas.requestPaint() }
                            function onDisplayRangeChanged() { canvas.requestPaint() }
                        }
                    }

                    // Empty area: click to deselect, double-click to add a band there.
                    MouseArea {
                        objectName: "chaosEqCurveArea"
                        anchors.fill: parent
                        onClicked: {
                            root.selectedSlot = -1
                            plot.forceActiveFocus()
                        }
                        onDoubleClicked: mouse => {
                            if (!bridge.canAddBand || mouse.y > plot.plotHeight) return
                            const gain = Math.max(-root.displayRange, Math.min(root.displayRange, plot.dbOf(mouse.y)))
                            const slot = bridge.addBand(plot.hzOf(mouse.x), Math.round(gain * 10) / 10)
                            if (slot >= 0) root.selectedSlot = slot
                            plot.forceActiveFocus()
                        }
                    }

                    Text {
                        visible: bridge.bandCount === 0
                        anchors.centerIn: parent
                        text: "Double-click the curve to add a band"
                        color: Theme.textMuted
                        font.pixelSize: Theme.bodySize
                    }

                    // One node per slot, kept alive while its band exists so a drag is never cut off
                    // when the band list updates.
                    Repeater {
                        model: bridge.maxBands
                        delegate: Item {
                            id: node
                            required property int index
                            readonly property var band: root.bandBySlot[index] ?? null
                            readonly property bool selected: root.selectedSlot === index
                            objectName: "chaosEqNode" + index
                            visible: band !== null
                            width: 18
                            height: 18
                            z: selected ? 3 : 2
                            x: band ? plot.xOf(band.frequency) - width / 2 : 0
                            y: band ? plot.yOf(band.usesGain ? band.gain : 0) - height / 2 : 0

                            Rectangle {
                                anchors.fill: parent
                                radius: width / 2
                                color: node.band && node.band.bypass ? "transparent" : root.bandColor(node.index)
                                border.width: node.selected ? 2 : 1
                                border.color: node.selected ? Theme.textStrong
                                            : node.band && node.band.bypass ? Theme.chaosEq.bypassed
                                            : Qt.darker(root.bandColor(node.index), 1.6)
                            }
                            Text {
                                anchors.centerIn: parent
                                text: node.index + 1
                                font.pixelSize: Theme.microSize
                                font.bold: true
                                color: node.band && node.band.bypass ? Theme.chaosEq.bypassed : Theme.chaosEq.nodeText
                            }
                            MouseArea {
                                id: nodeArea
                                anchors.fill: parent
                                anchors.margins: -4
                                hoverEnabled: true
                                cursorShape: Qt.SizeAllCursor
                                property point grab: Qt.point(0, 0)   // where in the node it was grabbed
                                onPressed: mouse => {
                                    root.selectedSlot = node.index
                                    plot.forceActiveFocus()
                                    grab = Qt.point(mouse.x - width / 2, mouse.y - height / 2)
                                }
                                onPositionChanged: mouse => {
                                    if (!pressed || !node.band) return
                                    const p = mapToItem(plot, mouse.x - grab.x, mouse.y - grab.y)
                                    const changes = { frequency: plot.hzOf(p.x) }
                                    if (node.band.usesGain)
                                        changes.gain = Math.max(bridge.minGain, Math.min(bridge.maxGain, plot.dbOf(p.y)))
                                    bridge.setBand(node.index, changes)
                                }
                                onDoubleClicked: if (node.band) bridge.setBand(node.index, { bypass: !node.band.bypass })
                                onWheel: wheel => {
                                    if (!node.band) return
                                    bridge.setBand(node.index, { q: node.band.q * Math.pow(1.1, wheel.angleDelta.y / 120) })
                                }
                                ToolTip.visible: containsMouse && !pressed && node.band !== null
                                ToolTip.text: node.band ? root.describe(node.band) : ""
                                ToolTip.delay: 450
                            }
                        }
                    }
                }
            }

            // The selected band's controls, then the output section.
            Rectangle {
                Layout.preferredWidth: 240
                Layout.fillHeight: true
                color: Theme.chaosEq.inspectorBg
                radius: Theme.panelRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    Text {
                        objectName: "chaosEqNoSelection"
                        visible: root.selectedBand === null
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: bridge.bandCount === 0 ? "No bands yet. Double-click the curve to add one."
                                                     : "Click a band on the curve to edit it."
                        color: Theme.textMuted
                        font.pixelSize: Theme.smallSize
                    }

                    ColumnLayout {
                        id: inspector
                        objectName: "chaosEqInspector"
                        visible: root.selectedBand !== null
                        Layout.fillWidth: true
                        spacing: 8
                        readonly property var band: root.selectedBand ?? ({ slot: 0, shape: "bell", slope: 12, frequency: 1000,
                                                                              gain: 0, q: 1, placement: "stereo", bypass: false,
                                                                              solo: false, usesGain: true, usesSlope: false })

                        RowLayout {
                            spacing: 8
                            Rectangle {
                                width: 12; height: 12; radius: 6
                                color: root.bandColor(inspector.band.slot)
                            }
                            Text {
                                text: "Band " + (inspector.band.slot + 1)
                                color: Theme.text
                                font.pixelSize: Theme.bodySize
                                font.bold: true
                                Layout.fillWidth: true
                            }
                        }

                        FieldLabel { text: "Shape" }
                        ThemedComboBox {
                            id: shapeBox
                            objectName: "chaosEqShape"
                            Layout.fillWidth: true
                            model: bridge.shapes
                            textRole: "title"
                            valueRole: "id"
                            currentIndex: bridge.shapes.findIndex(s => s.id === inspector.band.shape)
                            onActivated: bridge.setBand(root.selectedSlot, { shape: currentValue })
                        }

                        FieldLabel { text: "Slope" }
                        ThemedComboBox {
                            id: slopeBox
                            objectName: "chaosEqSlope"
                            Layout.fillWidth: true
                            enabled: inspector.band.usesSlope
                            model: bridge.slopes.map(s => s + " dB/oct")
                            currentIndex: bridge.slopes.indexOf(inspector.band.slope)
                            onActivated: index => bridge.setBand(root.selectedSlot, { slope: bridge.slopes[index] })
                        }

                        ValueSlider {
                            objectName: "chaosEqFrequency"
                            label: "Frequency"
                            valueText: bridge.formatFrequency(value)
                            logScale: true
                            from: bridge.minFrequency
                            to: bridge.maxFrequency
                            value: inspector.band.frequency
                            onMoved: v => bridge.setBand(root.selectedSlot, { frequency: v })
                        }
                        ValueSlider {
                            objectName: "chaosEqGain"
                            label: "Gain"
                            enabled: inspector.band.usesGain
                            valueText: enabled ? bridge.formatGain(value) : "—"
                            from: bridge.minGain
                            to: bridge.maxGain
                            value: inspector.band.gain
                            onMoved: v => bridge.setBand(root.selectedSlot, { gain: Math.round(v * 10) / 10 })
                        }
                        ValueSlider {
                            objectName: "chaosEqQ"
                            label: inspector.band.usesSlope ? "Q (corner)" : "Q (width)"
                            valueText: bridge.formatQ(value)
                            logScale: true
                            from: bridge.minQ
                            to: bridge.maxQ
                            value: inspector.band.q
                            onMoved: v => bridge.setBand(root.selectedSlot, { q: v })
                        }

                        FieldLabel { text: "Stereo" }
                        ThemedComboBox {
                            id: placementBox
                            objectName: "chaosEqPlacement"
                            Layout.fillWidth: true
                            model: bridge.placements
                            textRole: "title"
                            valueRole: "id"
                            currentIndex: bridge.placements.findIndex(p => p.id === inspector.band.placement)
                            onActivated: bridge.setBand(root.selectedSlot, { placement: currentValue })
                        }

                        RowLayout {
                            id: bandButtons
                            Layout.fillWidth: true
                            spacing: 6
                            ToggleChip {
                                objectName: "chaosEqBypass"
                                text: "BYPASS"
                                implicitWidth: 62; implicitHeight: 26
                                active: inspector.band.bypass
                                activeBg: Theme.muteBg; activeBorder: Theme.muteBorder; activeText: Theme.muteText
                                tooltip: active ? "Band bypassed" : "Bypass this band"
                                onClicked: bridge.setBand(root.selectedSlot, { bypass: !inspector.band.bypass })
                            }
                            ToggleChip {
                                objectName: "chaosEqSolo"
                                text: "SOLO"
                                implicitWidth: 52; implicitHeight: 26
                                active: inspector.band.solo
                                activeBg: Theme.soloBg; activeBorder: Theme.soloBorder; activeText: Theme.soloText
                                tooltip: active ? "Listening to this band only" : "Listen to this band only"
                                onClicked: bridge.setBand(root.selectedSlot, { solo: !inspector.band.solo })
                            }
                            Item { Layout.fillWidth: true }
                            PanelButton {
                                objectName: "chaosEqRemove"
                                text: "Remove"
                                onClicked: bridge.removeBand(root.selectedSlot)
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
                    Text { text: "OUTPUT"; color: Theme.textMuted; font.pixelSize: Theme.captionSize; font.letterSpacing: 1 }
                    ValueSlider {
                        objectName: "chaosEqOutput"
                        label: "Output gain"
                        valueText: bridge.formatGain(value)
                        from: bridge.minOutputGain
                        to: bridge.maxOutputGain
                        value: bridge.outputGain
                        onMoved: v => bridge.outputGain = Math.round(v * 10) / 10
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ToggleChip {
                            objectName: "chaosEqAutoGain"
                            text: "AUTO GAIN"
                            implicitWidth: 82; implicitHeight: 26
                            active: bridge.autoGain
                            tooltip: "Keep the loudness steady while boosting or cutting"
                            onClicked: bridge.autoGain = !bridge.autoGain
                        }
                        Text {
                            visible: bridge.autoGain
                            text: bridge.formatGain(bridge.autoGainAmount)
                            color: Theme.textMuted
                            font.pixelSize: Theme.smallSize
                        }
                    }
                }
            }
        }
    }
}
