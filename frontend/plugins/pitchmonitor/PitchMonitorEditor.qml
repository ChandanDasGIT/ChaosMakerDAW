import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Pitch Monitor's FX page (restMap G18). The pitch of the track's recorded audio over the song,
// laid out by bars like the timeline, on note lanes (in-key notes lighter, the key's tonic
// marked); the line is green on the note, amber a little off, red far off. While recording,
// the live input draws in the accent colour at the playhead; the readout shows the pitch now,
// and the note being hit lights its lane and its key. About an octave shows at a time and the
// view glides to keep the sung note in it; while playing, the song scrolls smoothly under a
// fixed playhead (moved on every frame from the tempo between the engine's position updates).
// Wheel scrolls (when stopped, or with Follow off), Ctrl+wheel zooms, Shift+wheel moves the
// notes, a click moves the playhead. Only the Follow chip stops the following. The key and scale go through MixEffectWrapper ("pitchmonitor"),
// the lines and the readout through PitchMonitorWrapper.
Item {
    id: root
    property var trackModel
    property int trackId: -1          // a track id, or a clip's effects id
    property string trackName: ""
    property bool active: false
    readonly property alias effect: fx
    readonly property alias page: monitor

    // The view: beats from the song start, and the notes shown.
    property real viewStart: 0
    property real viewBars: 8
    property bool follow: true
    readonly property real beatsPerBar: EngineController.numerator * 4 / Math.max(1, EngineController.denominator)
    readonly property real viewBeats: viewBars * beatsPerBar
    // The playhead as drawn: the engine's position, moved on every frame while playing.
    property real playhead: EngineController.positionBeats
    readonly property real playheadAt: 0.25          // where it stays while following, from the left
    // The notes shown: viewNotes lanes around centreNote, which glides towards targetNote.
    readonly property int viewNotes: 13
    property real centreNote: 60
    property real targetNote: 60
    property bool centred: false                      // placed once, on the line or the first pitch
    readonly property real lowNote: centreNote - viewNotes / 2

    // Green within 15 cents, amber within 35, red beyond.
    function centsColour(cents) {
        const off = Math.abs(cents)
        return off <= 15 ? Theme.meterNormal : off <= 35 ? Theme.meterWarning : Theme.meterHot
    }

    // The note to keep in view: the pitch now, once it nears an edge (so vibrato does not move
    // the view); before any pitch, the middle of the recorded line.
    function aim() {
        if (monitor.hasPitch) {
            if (!centred || Math.abs(monitor.pitch - targetNote) > viewNotes / 2 - 2.5) targetNote = Math.round(monitor.pitch)
            if (!centred) { centreNote = targetNote; centred = true }
        } else if (!centred && monitor.hasLine) {
            targetNote = centreNote = Math.round((monitor.lowestNote + monitor.highestNote) / 2)
            centred = true
        }
    }
    function followPlayhead() {
        if (follow && EngineController.playing) viewStart = Math.max(0, playhead - viewBeats * playheadAt)
        else if (follow && (playhead < viewStart || playhead > viewStart + viewBeats))
            viewStart = Math.max(0, playhead - viewBeats * playheadAt)
    }
    Connections {
        target: EngineController
        // A new position from the engine: take it when stopped, after a jump, or when the frames
        // ran ahead; else ease towards it, so the playhead never steps.
        function onPositionBeatsChanged() {
            const reported = EngineController.positionBeats
            if (!EngineController.playing || Math.abs(reported - root.playhead) > 0.5) root.playhead = reported
            else root.playhead += (reported - root.playhead) * 0.25
            root.followPlayhead()
        }
    }
    FrameAnimation {
        id: frames
        running: root.active
        onTriggered: {
            let moved = false
            if (EngineController.playing) {
                root.playhead += frameTime * EngineController.bpm / 60
                root.followPlayhead()
                moved = true
            }
            const gap = root.targetNote - root.centreNote
            if (Math.abs(gap) > 0.01) {
                root.centreNote += gap * Math.min(1, frameTime * 6)
                moved = true
            }
            if (moved) canvas.requestPaint()
        }
    }
    onViewStartChanged: canvas.requestPaint()
    onViewBeatsChanged: canvas.requestPaint()

    MixEffectWrapper {
        id: fx
        trackModel: root.trackModel ?? null
        trackId: root.trackId
        effectType: "pitchmonitor"
        engine: EngineController
        metering: false
    }
    PitchMonitorWrapper {
        id: monitor
        trackModel: root.trackModel ?? null
        effectsId: root.trackId
        engine: EngineController
        active: root.active
        onCurveChanged: { root.aim(); canvas.requestPaint() }
        onLiveChanged: canvas.requestPaint()
        onReadingChanged: { root.aim(); canvas.requestPaint() }
        onKeyChanged: canvas.requestPaint()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                text: "PITCH MONITOR"
                color: Theme.text; font.pixelSize: Theme.smallSize; font.bold: true; font.letterSpacing: 0.7
            }
            Text {
                text: root.trackName
                color: Theme.textMuted; font.pixelSize: Theme.captionSize
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                objectName: "pitchAnalysing"
                visible: monitor.analysing
                text: "Reading the recording..."
                color: Theme.textMuted; font.pixelSize: Theme.captionSize
            }
            ToggleChip {
                objectName: "pitchBypass"
                active: !fx.bypassed
                text: fx.bypassed ? "BYP" : "ON"; textSize: Theme.microSize
                implicitWidth: 34; implicitHeight: 22
                onClicked: fx.bypassed = !fx.bypassed
                tooltip: fx.bypassed ? "Off: no pitch is measured" : "Turn the pitch monitor off"
            }
        }

        // Key, readout, view.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text { text: "Key"; color: Theme.textMuted; font.pixelSize: Theme.captionSize }
            ThemedComboBox {
                objectName: "pitchKey"
                model: monitor.tonicNames
                currentIndex: monitor.keyTonic
                implicitWidth: 64; implicitHeight: 26
                enabled: monitor.scale !== 5
                onActivated: (index) => fx.setValue("key", index)
            }
            ThemedComboBox {
                objectName: "pitchScale"
                model: monitor.scaleNames
                currentIndex: monitor.scale
                implicitWidth: 150; implicitHeight: 26
                onActivated: (index) => fx.setValue("scale", index)
            }
            Item { Layout.fillWidth: true }
            Text {
                objectName: "pitchNote"
                text: monitor.hasPitch ? monitor.noteName : "—"
                color: monitor.hasPitch ? Theme.text : Theme.textMuted
                font.pixelSize: 20; font.bold: true
                Layout.preferredWidth: 52
                horizontalAlignment: Text.AlignRight
            }
            Text {
                objectName: "pitchCents"
                text: monitor.hasPitch ? (monitor.cents >= 0 ? "+" : "−") + Math.abs(Math.round(monitor.cents)) + " ¢" : ""
                color: root.centsColour(monitor.cents)
                font.pixelSize: Theme.bodySize; font.family: Theme.monospaceFontFamily
                Layout.preferredWidth: 58
            }
            Item { Layout.fillWidth: true }
            ToggleChip {
                objectName: "pitchFollow"
                active: root.follow
                text: "Follow"; textSize: Theme.microSize
                implicitWidth: 52; implicitHeight: 22
                onClicked: root.follow = !root.follow
                tooltip: "Keep the playhead in view while the song plays"
            }
        }

        // The graph: note names on the left, lanes and lines on the right.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 160
            color: Theme.pianoRoll.background
            radius: Theme.controlRadius
            clip: true

            Item {
                id: plot
                anchors.fill: parent
                anchors.margins: 6
                readonly property real keysWidth: 52
                readonly property real rulerHeight: 16
                readonly property real laneHeight: (height - rulerHeight) / root.viewNotes
                function xOf(beat) { return keysWidth + (beat - root.viewStart) / root.viewBeats * (width - keysWidth) }
                function beatAt(x) { return root.viewStart + (x - keysWidth) / (width - keysWidth) * root.viewBeats }
                // A note's lane is centred on its MIDI number.
                function yOf(midi) { return rulerHeight + (root.lowNote + root.viewNotes - midi) * laneHeight }

                Canvas {
                    id: canvas
                    objectName: "pitchGraph"
                    anchors.fill: parent
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        const w = width, h = height, left = plot.keysWidth, top = plot.rulerHeight
                        const lane = plot.laneHeight
                        ctx.font = Theme.captionSize + "px sans-serif"
                        const black = (note) => [1, 3, 6, 8, 10].indexOf(((note % 12) + 12) % 12) >= 0
                        const chromatic = monitor.scale === 5
                        const hit = monitor.hasPitch ? monitor.note : -1000
                        const hitColour = String(root.centsColour(monitor.cents))
                        // Note lanes: in-key lighter (chromatic: white / black keys like the piano
                        // roll), a faint line through each lane's exact pitch, the note being hit lit.
                        const firstNote = Math.floor(root.lowNote), lastNote = Math.ceil(root.lowNote + root.viewNotes)
                        for (let note = firstNote; note <= lastNote; ++note) {
                            const y = plot.yOf(note) - lane / 2
                            if (y > h || y + lane < top) continue
                            const light = chromatic ? !black(note) : monitor.inKey(note)
                            ctx.fillStyle = String(light ? Theme.pianoRoll.rowWhiteKey : Theme.pianoRoll.rowBlackKey)
                            ctx.fillRect(left, y, w - left, lane)
                            if (note === hit) {
                                ctx.globalAlpha = 0.22
                                ctx.fillStyle = hitColour
                                ctx.fillRect(left, y, w - left, lane)
                                ctx.globalAlpha = 1
                            }
                            const tonic = !chromatic && ((note - monitor.keyTonic) % 12 + 12) % 12 === 0
                            ctx.fillStyle = String(tonic || note % 12 === 0 ? Theme.pianoRoll.rowOctaveBorder : Theme.pianoRoll.rowBorder)
                            ctx.fillRect(left, Math.round(y + lane) - 1, w - left, 1)
                            ctx.fillStyle = String(Theme.pianoRoll.gridSub)
                            ctx.fillRect(left, Math.round(y + lane / 2), w - left, 1)
                        }
                        // The keyboard: every key named; the key being hit lit in its colour.
                        ctx.font = (lane >= 16 ? "bold 11px" : "bold 9px") + " sans-serif"
                        for (let note = firstNote; note <= lastNote; ++note) {
                            const y = plot.yOf(note) - lane / 2
                            if (y > h || y + lane < top) continue
                            const isBlack = black(note), isHit = note === hit
                            ctx.fillStyle = isHit ? hitColour : String(isBlack ? Theme.pianoRoll.blackKey : Theme.pianoRoll.whiteKey)
                            ctx.fillRect(0, y, left - 2, lane)
                            ctx.fillStyle = String(isBlack ? Theme.pianoRoll.blackKeyBorder : Theme.pianoRoll.whiteKeyBorder)
                            ctx.fillRect(0, Math.round(y + lane) - 1, left - 2, 1)
                            const inKey = monitor.inKey(note)
                            ctx.fillStyle = isHit ? String(Theme.pianoRoll.blackKey)
                                          : String(isBlack ? Theme.pianoRoll.blackKeyLabel
                                                           : (inKey ? Theme.pianoRoll.whiteKeyLabelC : Theme.pianoRoll.whiteKeyLabel))
                            ctx.globalAlpha = isHit || chromatic || inKey ? 1 : 0.55
                            ctx.fillText(monitor.nameOf(note), 6, y + lane / 2 + 4)
                            ctx.globalAlpha = 1
                        }
                        ctx.fillStyle = String(Theme.pianoRoll.background)
                        ctx.fillRect(0, 0, left, top)
                        ctx.font = Theme.captionSize + "px sans-serif"
                        // Bars and beats, bar numbers in the ruler.
                        ctx.fillStyle = String(Theme.pianoRoll.ruler)
                        ctx.fillRect(left, 0, w - left, top)
                        const firstBeat = Math.floor(root.viewStart)
                        for (let beat = firstBeat; beat <= root.viewStart + root.viewBeats; ++beat) {
                            const x = Math.round(plot.xOf(beat)) + 0.5
                            if (x < left) continue
                            const bar = Math.abs(beat / root.beatsPerBar - Math.round(beat / root.beatsPerBar)) < 1e-6
                            if (!bar && root.viewBeats > 64) continue
                            ctx.strokeStyle = String(bar ? Theme.pianoRoll.gridBar : Theme.pianoRoll.gridSub)
                            ctx.beginPath(); ctx.moveTo(x, bar ? 0 : top); ctx.lineTo(x, h); ctx.stroke()
                            if (bar) {
                                ctx.fillStyle = String(Theme.pianoRoll.rulerLabelMinor)
                                ctx.fillText(String(Math.round(beat / root.beatsPerBar) + 1), x + 3, top - 4)
                            }
                        }
                        // A line from flat triples (beat, pitch, cents); a pitch of 0 breaks it.
                        const drawLine = (list, colourOf, width) => {
                            ctx.lineWidth = width
                            ctx.lineJoin = "round"
                            let prev = null, colour = "", open = false
                            for (let i = 0; i + 2 < list.length; i += 3) {
                                const midi = list[i + 1]
                                if (midi <= 0) { if (open) ctx.stroke(); open = false; prev = null; continue }
                                const x = plot.xOf(list[i]), y = plot.yOf(midi)
                                const c = String(colourOf(list[i + 2]))
                                if (prev === null) { prev = [x, y]; continue }
                                if (!open || c !== colour) {
                                    if (open) ctx.stroke()
                                    ctx.strokeStyle = c; colour = c
                                    ctx.beginPath(); ctx.moveTo(prev[0], prev[1]); open = true
                                }
                                ctx.lineTo(x, y)
                                prev = [x, y]
                            }
                            if (open) ctx.stroke()
                        }
                        ctx.save()
                        ctx.beginPath(); ctx.rect(left, top, w - left, h - top); ctx.clip()
                        const from = root.viewStart, to = root.viewStart + root.viewBeats
                        const points = Math.max(200, Math.round((w - left) * 2))
                        drawLine(monitor.curve(from, to, points), root.centsColour, 2)
                        drawLine(monitor.live(from, to, points), () => Theme.accent, 2)
                        // The playhead, and the pitch now on it.
                        const px = Math.round(plot.xOf(root.playhead)) + 0.5
                        ctx.lineWidth = 1
                        ctx.strokeStyle = String(Theme.pianoRoll.playhead)
                        ctx.beginPath(); ctx.moveTo(px, top); ctx.lineTo(px, h); ctx.stroke()
                        if (monitor.hasPitch) {
                            ctx.fillStyle = String(root.centsColour(monitor.cents))
                            ctx.beginPath(); ctx.arc(px, plot.yOf(monitor.pitch), 5, 0, 2 * Math.PI); ctx.fill()
                        }
                        ctx.restore()
                    }
                }

                MouseArea {
                    objectName: "pitchGraphMouse"
                    anchors.fill: parent
                    anchors.leftMargin: plot.keysWidth
                    acceptedButtons: Qt.LeftButton
                    onClicked: (mouse) => EngineController.seekBeat(Math.max(0, plot.beatAt(mouse.x + plot.keysWidth)))
                    onWheel: (wheel) => {
                        const steps = (wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.angleDelta.x) / 120
                        if (wheel.modifiers & Qt.ControlModifier) {
                            const options = [1, 2, 4, 8, 16, 32, 64]
                            const i = Math.max(0, Math.min(options.length - 1, options.indexOf(root.viewBars) - Math.sign(steps)))
                            root.viewBars = options[i] ?? 8
                        } else if (wheel.modifiers & Qt.ShiftModifier) {
                            root.targetNote += Math.round(steps) * 2
                            root.centred = true
                        } else if (!(root.follow && EngineController.playing)) {
                            // While the song runs with Follow on, the view stays on the playhead
                            // (like the timeline's): scrolling only moves it when stopped.
                            root.viewStart = Math.max(0, root.viewStart - steps * root.viewBeats / 8)
                        }
                        wheel.accepted = true
                    }
                }
            }

            Text {
                objectName: "pitchEmpty"
                anchors.centerIn: parent
                visible: !monitor.hasLine && monitor.liveCount === 0 && !monitor.hasPitch && !monitor.analysing
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: "No pitch yet. Record or sing with monitoring on; recorded audio on this track shows here."
                color: Theme.chaosEq.gridLabel
                font.pixelSize: Theme.bodySize
            }
        }

        Text {
            objectName: "pitchHint"
            Layout.fillWidth: true
            text: monitor.scale === 5
                  ? "Pick the song's key to measure against its notes. Works on one voice or one melody line; a full mix jumps around."
                  : "Measured against " + monitor.keyName + ". Green: on the note · amber: a little off · red: far off. Wheel scrolls, Ctrl+wheel zooms, Shift+wheel moves the notes."
            color: Theme.textMuted
            font.pixelSize: Theme.captionSize
            wrapMode: Text.WordWrap
        }
    }
}
