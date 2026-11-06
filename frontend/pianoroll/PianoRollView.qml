import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Item {
    id: root

    implicitWidth: 1040
    implicitHeight: 640

    property var trackModel: null // TrackListWrapper; when set, notes are read from / written to the clip
    property int trackId: -1
    property int clipId: -1
    property string clipLabel: "Clip"
    property real clipLengthBeats: 4.0
    property real bpm: 120.0
    property real pixelsPerBeat: 96.0
    property real keyHeight: 18.0
    property string activeTool: "pencil" // "pointer", "pencil", "eraser"
    property string snapDivision: "1/16"
    property string snapStyle: "straight"
    property string colorMode: "Velocity"

    // Transport used by the footer buttons and the playhead (replaceable in tests, like Timeline).
    property var transport: EngineController
    // Transport position in clip-relative beats (same origin as the notes); negative before the clip.
    readonly property real playheadBeat: root.transport.positionBeats - pianoRollBackend.clipStartBeat
    property string currentPosText: "1.1.00"
    property string currentNoteText: "C4 60"

    property int dragCreatedNoteId: -1
    property real dragStartBeat: 0.0

    signal closeRequested()

    PianoRollWrapper {
        id: pianoRollBackend
        objectName: "pianoRollBackend"
        trackList: root.trackModel
        trackId: root.trackId
        clipId: root.clipId
    }

    readonly property alias notes: pianoRollBackend.notes

    focus: true

    // Editing keys only while this piano roll is shown and has keyboard focus (clicking in it
    // takes focus). Otherwise they would clash with the timeline's Delete / Ctrl+A / Escape in
    // the same window, and Qt would hand the key to this view even while it is hidden.
    readonly property bool keyboardActive: visible && activeFocus

    Shortcut {
        sequence: "Delete"
        enabled: root.keyboardActive
        onActivated: pianoRollBackend.deleteSelected()
    }
    Shortcut {
        sequence: "Backspace"
        enabled: root.keyboardActive
        onActivated: pianoRollBackend.deleteSelected()
    }
    Shortcut {
        sequence: "Ctrl+A"
        enabled: root.keyboardActive
        onActivated: pianoRollBackend.selectAll()
    }
    Shortcut {
        sequence: "Escape"
        enabled: root.keyboardActive
        onActivated: pianoRollBackend.clearSelection()
    }
    Shortcut {
        sequence: "Q"
        enabled: root.keyboardActive
        onActivated: root.quantizeNotes()
    }

    // Quantize to the Grid division (even with the magnet off); one meter beat when Grid is Off.
    function quantizeNotes() {
        const step = gridDivisionBeats()
        pianoRollBackend.quantize(step > 0 ? step : root.beatUnit)
    }

    // "Notes:" box: length of newly drawn notes. "Grid" follows the Grid division.
    property string noteLengthMode: "Grid"
    function newNoteLength() {
        switch (root.noteLengthMode) {
            case "1/16": return 0.25
            case "1/8": return 0.5
            case "1/4": return 1.0
            default: {
                const division = gridDivisionBeats()
                return division > 0 ? division : 0.25
            }
        }
    }

    // Magnet button: when off, notes move and draw freely but the grid lines stay.
    property bool snapEnabled: true

    // Snap step used by editing: the Grid division, or 0 when snapping is off.
    function getSnapBeats() {
        return root.snapEnabled ? gridDivisionBeats() : 0.0
    }

    // The Grid division in beats ("Off" -> 0), independent of the magnet button.
    // Grid style: triplet = 2/3 of the division, dotted = 1.5x.
    function gridDivisionBeats() {
        const factor = root.snapStyle === "triplet" ? 2.0 / 3.0 : root.snapStyle === "dotted" ? 1.5 : 1.0
        switch (root.snapDivision) {
            case "1/4": return 1.0 * factor
            case "1/8": return 0.5 * factor
            case "1/16": return 0.25 * factor
            case "1/32": return 0.125 * factor
            case "1/64": return 0.0625 * factor
            case "Off": return 0.0
            default: return 0.25 * factor
        }
    }

    // Snap a drag distance (in beats) to the nearest grid step. Snapping the delta rather than
    // the absolute position keeps the note under the cursor at the point where it was grabbed.
    function snapDelta(deltaBeats) {
        const step = getSnapBeats()
        if (step <= 0) return deltaBeats
        return Math.round(deltaBeats / step) * step
    }

    // ---- Edge auto-scroll while dragging / resizing / drawing ----
    // The active MouseArea registers itself here; it must expose applyDrag(contentX, contentY).
    property var autoScrollArea: null
    property bool autoScrollVertical: true
    property point autoScrollViewportPt: Qt.point(0, 0)
    readonly property real autoScrollZone: 24      // px from the edge where scrolling starts
    readonly property real autoScrollMaxStep: 40   // px per tick at full speed

    // viewportPt must be mapped BEFORE applyDrag() moves the item, or the item's own
    // movement gets counted twice.
    function trackAutoScroll(area, viewportPt, vertical) {
        autoScrollArea = area
        autoScrollVertical = vertical
        autoScrollViewportPt = viewportPt
        autoScrollTimer.running = autoScrollSpeed().x !== 0 || autoScrollSpeed().y !== 0
    }

    // Stops scrolling only; dragEndBeats is cleared after the edit is committed (see releases)
    // so the grid never shrinks under the cursor before the note lands.
    function endAutoScroll() {
        autoScrollTimer.running = false
        autoScrollArea = null
    }

    function edgeSpeed(pos, size) {
        const z = autoScrollZone
        let over = 0
        if (pos < z) over = pos - z
        else if (pos > size - z) over = pos - (size - z)
        if (over === 0) return 0
        const step = Math.min(autoScrollMaxStep, Math.abs(over) * 0.35 + 2)
        return over < 0 ? -step : step
    }

    function autoScrollSpeed() {
        const f = gridVerticalFlickable
        // Leave out the overlaid scrollbars (14px right, 10px bottom)
        const vx = edgeSpeed(autoScrollViewportPt.x, f.width - 14)
        const vy = autoScrollVertical ? edgeSpeed(autoScrollViewportPt.y, f.height - 10) : 0
        return Qt.point(vx, vy)
    }

    Timer {
        id: autoScrollTimer
        interval: 16
        repeat: true
        onTriggered: {
            const f = gridVerticalFlickable
            if (!root.autoScrollArea) { running = false; return }
            const v = root.autoScrollSpeed()
            const oldX = f.contentX, oldY = f.contentY
            f.contentX = Math.max(0, Math.min(Math.max(0, f.contentWidth - f.width), f.contentX + v.x))
            f.contentY = Math.max(0, Math.min(Math.max(0, f.contentHeight - f.height), f.contentY + v.y))
            if (f.contentX === oldX && f.contentY === oldY) return
            // Content moved under a still cursor: re-apply the drag at the new content position
            root.autoScrollArea.applyDrag(root.autoScrollViewportPt.x + f.contentX,
                                          root.autoScrollViewportPt.y + f.contentY)
        }
    }

    // Floor to the grid cell. The grid restarts at every bar line, so steps that do not divide
    // the bar (triplets, dotted) still line up with bars.
    function snapToGrid(beat) {
        const step = getSnapBeats()
        if (step <= 0) return Math.max(0, beat)
        const bar = Math.floor(beat / root.beatsPerBar + 1e-9) * root.beatsPerBar
        return Math.max(0, bar + Math.floor((beat - bar) / step + 1e-9) * step)
    }

    // Project meter from the backend, in quarter-note beats (6/8 -> 3 per bar, 0.5 per beat).
    readonly property real beatsPerBar: pianoRollBackend.beatsPerBar
    readonly property real beatUnit: pianoRollBackend.beatUnit
    readonly property int beatsInBar: Math.max(1, Math.round(beatsPerBar / beatUnit))

    // True when position lies on a multiple of unit (both in beats).
    function isOnGrid(position, unit) {
        const r = position / unit
        return Math.abs(r - Math.round(r)) < 1e-6
    }

    // Grid line spacing: one line per snap division ("Off" -> meter beats only), coarsened by
    // doubling until lines are at least 6px apart so zoomed-out grids stay readable.
    // Straight grids keep at least one line per meter beat; triplet/dotted grids show their
    // own step (doubled while too dense).
    readonly property real gridLineBeats: {
        const step = gridDivisionBeats()
        if (step > 0 && snapStyle !== "straight") {
            let beats = step
            while (beats * pixelsPerBeat < 6) beats *= 2
            return beats
        }
        let beats = step > 0 ? Math.min(beatUnit, step) : beatUnit
        while (beats < beatUnit && beats * pixelsPerBeat < 6)
            beats *= 2
        return Math.min(beats, beatUnit)
    }
    // Grid lines restart at every bar line
    readonly property int gridLinesPerBar: Math.max(1, Math.ceil(beatsPerBar / gridLineBeats - 1e-6))

    // Note fill colour. "Velocity" mode blends dim blue (soft) -> bright teal (loud);
    // other modes keep the default note colour for now.
    function noteFillColor(velocity) {
        if (colorMode !== "Velocity")
            return "#2d7088"
        const t = Math.max(0, Math.min(1, velocity / 127.0))
        const lo = Qt.color("#24405e")
        const hi = Qt.color("#38c2b4")
        return Qt.rgba(lo.r + (hi.r - lo.r) * t, lo.g + (hi.g - lo.g) * t, lo.b + (hi.b - lo.b) * t, 1)
    }

    readonly property real clipWidthPixels: clipLengthBeats * pixelsPerBeat
    // Furthest note end (beats), so the grid keeps room to scroll/drag past the clip end
    property real dragEndBeats: 0
    readonly property real notesEndBeats: {
        let end = 0
        for (let i = 0; i < notes.length; ++i)
            end = Math.max(end, (notes[i].startBeat ?? 0) + (notes[i].lengthBeats ?? 0))
        return end
    }
    readonly property real totalGridWidth: Math.max(gridContainer.width,
        (Math.max(clipLengthBeats, notesEndBeats, dragEndBeats) + 8) * pixelsPerBeat)
    readonly property real contentGridHeight: 128 * keyHeight

    // Fixed chrome: menu bar (22) + tool strip (28) + ruler (34) + footer (30).
    readonly property real chromeHeight: 22 + 28 + 34 + 30
    // The note grid always keeps at least this many pixels; the velocity lane shrinks first.
    readonly property real minimumGridHeight: 6 * keyHeight
    readonly property real minimumVelocityHeight: 45
    readonly property real minimumDockHeight: chromeHeight + minimumGridHeight + minimumVelocityHeight

    function centerOnMiddleC() {
        if (!gridVerticalFlickable) return
        const viewH = (gridVerticalFlickable.height > 50) ? gridVerticalFlickable.height : (root.height > 212 ? root.height - 212 : 400)
        const noteC4Y = (127 - 60) * root.keyHeight // note 60 (C4)
        const targetY = Math.round(noteC4Y - (viewH - root.keyHeight) / 2)
        const clampedY = Math.max(0, Math.min(root.contentGridHeight - viewH, targetY))
        gridVerticalFlickable.contentY = clampedY
    }

    onVisibleChanged: {
        if (visible) {
            Qt.callLater(centerOnMiddleC)
        }
    }

    Component.onCompleted: {
        Qt.callLater(centerOnMiddleC)
    }

    Rectangle {
        anchors.fill: parent
        color: "#23252a"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 1. Title bar: clip name and close button
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 22
            color: "#32353e"
            border.color: "#212328"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 14

                Item { Layout.fillWidth: true }

                Text {
                    text: root.clipLabel
                    color: "#8a919f"
                    font.pixelSize: 11
                }

                Button {
                    text: "✕"
                    flat: true
                    implicitWidth: 20
                    implicitHeight: 20
                    onClicked: root.closeRequested()
                }
            }
        }

        // 2. Icon Toolbar Strip (tools, quantize, magnet snap, etc.)
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: "#2a2c33"
            border.color: "#1d1f24"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 4

                Rectangle {
                    width: 24; height: 22; radius: 2
                    color: root.activeTool === "pointer" ? "#4a505e" : tool1Hover.containsMouse ? "#373a44" : "#2f323a"
                    border.color: root.activeTool === "pointer" ? "#60cdff" : "#202227"
                    Text { anchors.centerIn: parent; text: "⌖"; color: "#d8dce4"; font.pixelSize: 13 }
                    MouseArea { id: tool1Hover; anchors.fill: parent; hoverEnabled: true; onClicked: root.activeTool = "pointer" }
                    ToolTip.visible: tool1Hover.containsMouse; ToolTip.text: "Select (Pointer)"
                }

                Rectangle {
                    width: 24; height: 22; radius: 2
                    color: root.activeTool === "pencil" ? "#4a505e" : tool2Hover.containsMouse ? "#373a44" : "#2f323a"
                    border.color: root.activeTool === "pencil" ? "#60cdff" : "#202227"
                    Text { anchors.centerIn: parent; text: "✎"; color: "#d8dce4"; font.pixelSize: 13 }
                    MouseArea { id: tool2Hover; anchors.fill: parent; hoverEnabled: true; onClicked: root.activeTool = "pencil" }
                    ToolTip.visible: tool2Hover.containsMouse; ToolTip.text: "Draw (Pencil)"
                }

                Rectangle {
                    width: 24; height: 22; radius: 2
                    color: root.activeTool === "eraser" ? "#4a505e" : tool3Hover.containsMouse ? "#373a44" : "#2f323a"
                    border.color: root.activeTool === "eraser" ? "#60cdff" : "#202227"
                    Text { anchors.centerIn: parent; text: "⌫"; color: "#d8dce4"; font.pixelSize: 13 }
                    MouseArea { id: tool3Hover; anchors.fill: parent; hoverEnabled: true; onClicked: root.activeTool = "eraser" }
                    ToolTip.visible: tool3Hover.containsMouse; ToolTip.text: "Erase"
                }

                Rectangle { width: 1; height: 18; color: "#3d414c" }

                Rectangle {
                    objectName: "pianoRollSnap"
                    width: 24; height: 22; radius: 2
                    color: root.snapEnabled ? "#4a505e" : snapHover.containsMouse ? "#373a44" : "#2f323a"
                    border.color: root.snapEnabled ? "#60cdff" : "#202227"
                    Text { anchors.centerIn: parent; text: "🧲"; color: "#d8dce4"; font.pixelSize: 11 }
                    MouseArea { id: snapHover; anchors.fill: parent; hoverEnabled: true; onClicked: root.snapEnabled = !root.snapEnabled }
                    ToolTip.visible: snapHover.containsMouse
                    ToolTip.text: root.snapEnabled ? "Snap to grid: on (click to place notes freely)" : "Snap to grid: off (click to snap to the Grid setting)"
                }

                Rectangle {
                    objectName: "pianoRollQuantize"
                    width: 24; height: 22; radius: 2
                    enabled: pianoRollBackend.count > 0
                    opacity: enabled ? 1 : 0.4
                    color: qHover.containsMouse ? "#373a44" : "#2f323a"
                    border.color: "#202227"
                    Text { anchors.centerIn: parent; text: "⇥Q"; color: "#d8dce4"; font.pixelSize: 10; font.bold: true }
                    MouseArea { id: qHover; anchors.fill: parent; hoverEnabled: true; onClicked: root.quantizeNotes() }
                    ToolTip.visible: qHover.containsMouse
                    ToolTip.text: "Quantize note starts to the Grid (Q) · selected notes, or all if none selected"
                }

                // Undo/redo use the project history (note edits are undoable steps there)
                Rectangle {
                    objectName: "pianoRollUndo"
                    width: 24; height: 22; radius: 2
                    enabled: !!root.trackModel && root.trackModel.canUndo
                    opacity: enabled ? 1 : 0.4
                    color: undoHover.containsMouse ? "#373a44" : "#2f323a"
                    border.color: "#202227"
                    Text { anchors.centerIn: parent; text: "↶"; color: "#d8dce4"; font.pixelSize: 13 }
                    MouseArea { id: undoHover; anchors.fill: parent; hoverEnabled: true; onClicked: root.trackModel.undo() }
                    ToolTip.visible: undoHover.containsMouse; ToolTip.text: "Undo (Ctrl+Z)"
                }

                Rectangle {
                    objectName: "pianoRollRedo"
                    width: 24; height: 22; radius: 2
                    enabled: !!root.trackModel && root.trackModel.canRedo
                    opacity: enabled ? 1 : 0.4
                    color: redoHover.containsMouse ? "#373a44" : "#2f323a"
                    border.color: "#202227"
                    Text { anchors.centerIn: parent; text: "↷"; color: "#d8dce4"; font.pixelSize: 13 }
                    MouseArea { id: redoHover; anchors.fill: parent; hoverEnabled: true; onClicked: root.trackModel.redo() }
                    ToolTip.visible: redoHover.containsMouse; ToolTip.text: "Redo (Ctrl+Shift+Z)"
                }

                Item { Layout.fillWidth: true }
            }
        }

        // 3. Top Info Readouts & Time Ruler
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            Layout.minimumHeight: 34
            Layout.maximumHeight: 34
            spacing: 0

            // Pinned header directly above the piano keys (width 90): cursor position & note readouts
            Rectangle {
                Layout.preferredWidth: 90
                Layout.fillHeight: true
                color: "#2c2e35"
                border.color: "#1e2025"

                Row {
                    anchors.centerIn: parent
                    spacing: 4

                    Repeater {
                        model: [
                            { text: root.currentPosText, tip: "Cursor position (bar.beat.hundredths)" },
                            { text: root.currentNoteText, tip: "Note under cursor (name & MIDI number)" }
                        ]
                        delegate: Rectangle {
                            required property var modelData
                            width: 40
                            height: 18
                            radius: 2
                            color: "#1b1d22"
                            border.color: "#454a56"

                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 2
                                anchors.rightMargin: 2
                                text: modelData.text
                                color: "#d8dce5"
                                font.pixelSize: 10
                                font.family: "monospace"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                fontSizeMode: Text.HorizontalFit
                                minimumPixelSize: 7
                            }

                            HoverHandler { id: readoutHover }
                            ToolTip.visible: readoutHover.hovered
                            ToolTip.text: modelData.tip
                        }
                    }
                }
            }

            // Fractional musical time ruler
            Flickable {
                id: rulerFlickable
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: root.totalGridWidth
                contentX: gridVerticalFlickable.contentX
                interactive: false
                clip: true

                Rectangle {
                    width: root.totalGridWidth
                    height: parent.height
                    color: "#282a31"
                    border.color: "#1d1f25"

                    Rectangle {
                        x: 0
                        y: 0
                        width: root.clipWidthPixels
                        height: 4
                        color: "#38b2ac"
                    }

                    // One tick per counted beat of the project meter
                    Repeater {
                        model: Math.ceil(root.totalGridWidth / (root.beatUnit * root.pixelsPerBeat))
                        delegate: Item {
                            required property int index
                            readonly property int bar: Math.floor(index / root.beatsInBar) + 1
                            readonly property int beat: index % root.beatsInBar + 1
                            readonly property bool isMajorBar: beat === 1

                            x: index * root.beatUnit * root.pixelsPerBeat
                            y: 0
                            width: root.beatUnit * root.pixelsPerBeat
                            height: parent.height

                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: isMajorBar ? 1.5 : 1
                                color: isMajorBar ? "#606675" : "#3e424e"
                            }

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 3
                                anchors.top: parent.top
                                anchors.topMargin: isMajorBar ? 4 : 8
                                // Beat labels drop out when beats are too narrow to read
                                visible: isMajorBar || parent.width >= 24
                                text: isMajorBar ? String(bar) : (bar + "." + beat)
                                color: isMajorBar ? "#d8dce5" : "#7c8393"
                                font.pixelSize: isMajorBar ? 11 : 9
                                font.bold: isMajorBar
                            }
                        }
                    }
                }
            }
        }

        // 4. Main Note Grid & Piano Keys Area
        Item {
            id: gridContainer
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: root.minimumGridHeight

            RowLayout {
                anchors.fill: parent
                spacing: 0

                // Pinned Left Piano Keys
                Flickable {
                    id: keysFlickable
                    Layout.preferredWidth: 90
                    Layout.fillHeight: true
                    contentWidth: 90
                    contentHeight: root.contentGridHeight
                    contentY: gridVerticalFlickable.contentY
                    interactive: false
                    clip: true

                    Binding {
                        target: keysFlickable
                        property: "contentY"
                        value: gridVerticalFlickable.contentY
                    }

                    WheelHandler {
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        target: null
                        onWheel: event => {
                            const deltaY = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y
                            gridVerticalFlickable.scrollVertical(deltaY)
                        }
                    }

                    PianoKeys {
                        id: pianoKeys
                        width: 90
                        height: root.contentGridHeight
                        keyHeight: root.keyHeight
                        onKeyHovered: (note, noteName) => {
                            root.currentNoteText = noteName + " " + note
                        }
                        onKeyClicked: note => {
                            root.currentNoteText = pianoKeys.noteLabel(note) + " " + note
                        }
                        onKeyPressed: (note, velocity) => {
                            root.currentNoteText = pianoKeys.noteLabel(note) + " " + note
                            EngineController.sendLiveNoteOn(root.trackId, note, velocity)
                        }
                        onKeyReleased: note => {
                            EngineController.sendLiveNoteOff(root.trackId, note)
                        }
                        onWheelScrolled: delta => {
                            gridVerticalFlickable.scrollVertical(delta)
                        }
                    }
                }

                // Center Scrollable Note Grid (Horizontal & Vertical)
                Flickable {
                    id: gridVerticalFlickable
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: root.totalGridWidth
                    contentHeight: root.contentGridHeight
                    contentY: Math.round((127 - 60) * root.keyHeight - (height > 50 ? (height - root.keyHeight) / 2 : 200))
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true

                    function scrollVertical(delta) {
                        const step = (delta / 120) * (root.keyHeight * 3)
                        const maxY = Math.max(0, contentHeight - height)
                        contentY = Math.max(0, Math.min(maxY, contentY - step))
                    }

                    function scrollHorizontal(delta) {
                        const step = (delta / 120) * (root.pixelsPerBeat * 0.5)
                        const maxX = Math.max(0, contentWidth - width)
                        contentX = Math.max(0, Math.min(maxX, contentX - step))
                    }

                    WheelHandler {
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        target: null
                        onWheel: event => {
                            const deltaY = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y
                            if (event.modifiers & Qt.ShiftModifier) {
                                gridVerticalFlickable.scrollHorizontal(deltaY)
                            } else {
                                gridVerticalFlickable.scrollVertical(deltaY)
                            }
                        }
                    }

                    WheelHandler {
                        orientation: Qt.Horizontal
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        target: null
                        onWheel: event => {
                            const deltaX = event.pixelDelta.x !== 0 ? event.pixelDelta.x : event.angleDelta.x
                            gridVerticalFlickable.scrollHorizontal(deltaX)
                        }
                    }

                    ScrollBar.vertical: ScrollBar {
                        id: vScrollBar
                        policy: ScrollBar.AlwaysOn
                        width: 14
                        active: true
                        contentItem: Rectangle {
                            implicitWidth: 10
                            radius: 4
                            color: vScrollBar.pressed ? "#6b7280" : (vScrollBar.hovered ? "#565b67" : "#424652")
                        }
                        background: Rectangle {
                            color: "#1a1c22"
                        }
                    }

                    ScrollBar.horizontal: ScrollBar {
                        id: hScrollBar
                        policy: ScrollBar.AlwaysOn
                        height: 10
                        active: true
                        contentItem: Rectangle {
                            implicitHeight: 8
                            radius: 4
                            color: hScrollBar.pressed ? "#6b7280" : (hScrollBar.hovered ? "#565b67" : "#424652")
                        }
                        background: Rectangle {
                            color: "#1a1c22"
                        }
                    }

                    // Active Clip Background Highlight vs Dimmed Outside
                    Rectangle {
                        x: 0
                        y: 0
                        width: root.clipWidthPixels
                        height: root.contentGridHeight
                        color: "#0a283020"
                    }

                    Rectangle {
                        x: root.clipWidthPixels
                        y: 0
                        width: Math.max(0, root.totalGridWidth - root.clipWidthPixels)
                        height: root.contentGridHeight
                        color: "#00000040"
                    }

                    // Zebra Striped Semitone Rows
                    Column {
                        Repeater {
                            model: 128
                            delegate: Rectangle {
                                required property int index
                                readonly property int note: 127 - index
                                readonly property int pitchClass: note % 12
                                readonly property bool black: pitchClass === 1 || pitchClass === 3 || pitchClass === 6 || pitchClass === 8 || pitchClass === 10
                                readonly property bool isOctaveBorder: pitchClass === 0

                                width: root.totalGridWidth
                                height: root.keyHeight
                                color: black ? "#1d1f24" : "#26282f"
                                border.color: isOctaveBorder ? "#424652" : "#202227"
                                border.width: 1
                            }
                        }
                    }

                    // Vertical Bar / Beat / Subdivision Grid Lines (follow the Grid setting)
                    Repeater {
                        model: Math.ceil(root.totalGridWidth / (root.beatsPerBar * root.pixelsPerBeat)) * root.gridLinesPerBar + 1
                        delegate: Rectangle {
                            required property int index
                            readonly property int lineInBar: index % root.gridLinesPerBar
                            readonly property real offsetInBar: lineInBar * root.gridLineBeats
                            readonly property real position: Math.floor(index / root.gridLinesPerBar) * root.beatsPerBar + offsetInBar
                            readonly property bool isMajorBar: lineInBar === 0
                            readonly property bool isBeat: root.isOnGrid(offsetInBar, root.beatUnit)

                            // Whole-pixel positions & widths keep every line crisp
                            x: Math.round(position * root.pixelsPerBeat)
                            y: 0
                            width: isMajorBar ? 2 : 1
                            height: root.contentGridHeight
                            color: isMajorBar ? "#4c515e" : (isBeat ? "#363944" : "#2b2d34")
                        }
                    }

                    // Cyan playhead line; hidden while the transport is before the clip
                    Rectangle {
                        objectName: "pianoRollPlayhead"
                        visible: root.playheadBeat >= 0
                        x: root.playheadBeat * root.pixelsPerBeat
                        y: 0
                        width: 1
                        height: root.contentGridHeight
                        color: "#38b2ac"
                        z: 5
                    }

                    // Ghost Drawing Preview
                    Rectangle {
                        id: drawingPreview
                        visible: gridMouse.isDrawing
                        x: gridMouse.drawStartBeat * root.pixelsPerBeat + 1
                        y: (127 - gridMouse.drawPitch) * root.keyHeight + 1
                        width: Math.max(6, gridMouse.drawLength * root.pixelsPerBeat - 2)
                        height: root.keyHeight - 2
                        radius: 2
                        color: "#38b2ac80"
                        border.color: "#a4e2f5"
                        border.width: 1
                        z: 8
                    }

                    // Notes Blocks
                    Repeater {
                        model: root.notes
                        delegate: Rectangle {
                            id: noteCard
                            required property var modelData
                            required property int index
                            readonly property int noteId: modelData.id ?? 0
                            readonly property int notePitch: modelData.note ?? 60
                            readonly property real startBeat: modelData.startBeat ?? 0
                            readonly property real lengthBeats: modelData.lengthBeats ?? 1
                            readonly property bool isSelected: modelData.selected ?? false

                            property real visualBeat: startBeat
                            property int visualPitch: notePitch
                            property real visualLength: lengthBeats
                            property bool isDragging: false
                            property bool isResizing: false

                            onStartBeatChanged: if (!isDragging) visualBeat = startBeat
                            onNotePitchChanged: if (!isDragging) visualPitch = notePitch
                            onLengthBeatsChanged: if (!isResizing) visualLength = lengthBeats

                            x: visualBeat * root.pixelsPerBeat + 1
                            y: (127 - visualPitch) * root.keyHeight + 1
                            width: Math.max(6, visualLength * root.pixelsPerBeat - 2)
                            height: root.keyHeight - 2
                            radius: 2
                            z: (isDragging || isResizing) ? 10 : 1
                            readonly property int noteVelocity: modelData.velocity ?? 100
                            readonly property bool isHovered: noteMouse.containsMouse || resizeMouse.containsMouse
                            // Follows the velocity lane's live drag preview
                            readonly property int shownVelocity: velocityLane.previewVelocities[String(noteId)] ?? noteVelocity
                            readonly property color baseFill: root.noteFillColor(shownVelocity)
                            readonly property bool showSelected: isSelected || isDragging
                            color: showSelected ? Qt.lighter(baseFill, 1.35) : (isHovered ? Qt.lighter(baseFill, 1.18) : baseFill)
                            border.color: showSelected ? "#ffffff" : (isHovered ? "#a4e2f5" : "#5bc0de")
                            border.width: isSelected ? 1.5 : 1

                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 3
                                color: isSelected ? "#e6fffa" : "#a4e2f5"
                            }

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 5
                                anchors.right: resizeHandle.left
                                anchors.rightMargin: 2
                                anchors.verticalCenter: parent.verticalCenter
                                text: pianoKeys.noteLabel(noteCard.visualPitch)
                                color: "#f0f8ff"
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }

                            // Body MouseArea for moving / selecting note
                            MouseArea {
                                id: noteMouse
                                // Keep the grid Flickable from stealing the drag (it would cancel the move)
                                preventStealing: true
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.right: resizeHandle.left
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                hoverEnabled: true
                                cursorShape: root.activeTool === "eraser" ? Qt.PointingHandCursor : (noteCard.isDragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor)
                                ToolTip.visible: noteMouse.containsMouse
                                ToolTip.text: "Drag to move · Right-click/Eraser to delete"

                                property real pressStartBeat: 0
                                property int pressPitch: 0
                                property real pressGlobalX: 0
                                property real pressGlobalY: 0
                                property bool pressAddToSelection: false

                                onPressed: mouse => {
                                    if (mouse.button === Qt.RightButton || root.activeTool === "eraser") {
                                        pianoRollBackend.removeNote(noteCard.noteId)
                                        return
                                    }

                                    if (mouse.button === Qt.LeftButton) {
                                        root.forceActiveFocus()
                                        // Do NOT touch the backend here: any notesChanged rebuilds every note
                                        // delegate (including this one) mid-press and kills the drag.
                                        // Selection is applied on release instead.
                                        pressAddToSelection = (mouse.modifiers & Qt.ShiftModifier) || (mouse.modifiers & Qt.ControlModifier)
                                        pressStartBeat = noteCard.startBeat
                                        pressPitch = noteCard.notePitch
                                        const pt = mapToItem(gridVerticalFlickable.contentItem, mouse.x, mouse.y)
                                        pressGlobalX = pt.x
                                        pressGlobalY = pt.y
                                        noteCard.visualBeat = noteCard.startBeat
                                        noteCard.visualPitch = noteCard.notePitch
                                        noteCard.isDragging = false
                                    }
                                }

                                // cx/cy are in grid content coordinates (also called by the auto-scroll timer)
                                function applyDrag(cx, cy) {
                                    const dx = cx - pressGlobalX
                                    const dy = cy - pressGlobalY
                                    if (!noteCard.isDragging && (Math.abs(dx) > 3 || Math.abs(dy) > 3)) {
                                        noteCard.isDragging = true
                                    }
                                    if (noteCard.isDragging) {
                                        const beatDelta = root.snapDelta(dx / root.pixelsPerBeat)
                                        noteCard.visualBeat = Math.max(0, pressStartBeat + beatDelta)

                                        const pitchDelta = -Math.round(dy / root.keyHeight)
                                        noteCard.visualPitch = Math.max(0, Math.min(127, pressPitch + pitchDelta))

                                        root.dragEndBeats = noteCard.visualBeat + noteCard.lengthBeats
                                        root.currentNoteText = pianoKeys.noteLabel(noteCard.visualPitch) + " " + noteCard.visualPitch
                                    }
                                }

                                onPositionChanged: mouse => {
                                    if (pressed && (mouse.buttons & Qt.LeftButton) && root.activeTool !== "eraser") {
                                        const pt = mapToItem(gridVerticalFlickable.contentItem, mouse.x, mouse.y)
                                        const vp = mapToItem(gridVerticalFlickable, mouse.x, mouse.y)
                                        applyDrag(pt.x, pt.y)
                                        if (noteCard.isDragging)
                                            root.trackAutoScroll(noteMouse, vp, true)
                                    }
                                }

                                onCanceled: {
                                    root.endAutoScroll()
                                    root.dragEndBeats = 0
                                    noteCard.isDragging = false
                                    noteCard.visualBeat = noteCard.startBeat
                                    noteCard.visualPitch = noteCard.notePitch
                                }

                                onReleased: mouse => {
                                    if (mouse.button !== Qt.LeftButton || root.activeTool === "eraser")
                                        return
                                    // Capture everything first: each backend call below rebuilds the note
                                    // delegates, so this delegate's context is gone after the first call.
                                    const backend = pianoRollBackend
                                    const view = root
                                    const id = noteCard.noteId
                                    const addToSelection = pressAddToSelection
                                    const wasDragging = noteCard.isDragging
                                    const pitch = noteCard.visualPitch
                                    const beat = noteCard.visualBeat
                                    root.endAutoScroll()
                                    noteCard.isDragging = false
                                    if (wasDragging)
                                        backend.moveNote(id, pitch, beat)
                                    backend.selectNote(id, addToSelection)
                                    view.dragEndBeats = 0
                                }
                            }

                            // Right Edge Handle for resizing note length
                            Item {
                                id: resizeHandle
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 8

                                MouseArea {
                                    id: resizeMouse
                                    preventStealing: true
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    hoverEnabled: true
                                    cursorShape: Qt.SizeHorCursor
                                    ToolTip.visible: resizeMouse.containsMouse
                                    ToolTip.text: "Drag edge to change length"

                                    property real pressStartLength: 0
                                    property real pressGlobalX: 0

                                    onPressed: mouse => {
                                        root.forceActiveFocus()
                                        pressStartLength = noteCard.lengthBeats
                                        const pt = mapToItem(gridVerticalFlickable.contentItem, mouse.x, mouse.y)
                                        pressGlobalX = pt.x
                                        noteCard.visualLength = noteCard.lengthBeats
                                        noteCard.isResizing = true
                                    }

                                    function applyDrag(cx, cy) {
                                        const dx = cx - pressGlobalX
                                        const step = root.getSnapBeats()
                                        const unitStep = step > 0 ? step : 0.25
                                        const lenDelta = root.snapDelta(dx / root.pixelsPerBeat)
                                        noteCard.visualLength = Math.max(unitStep, pressStartLength + lenDelta)
                                        root.dragEndBeats = noteCard.startBeat + noteCard.visualLength
                                    }

                                    onPositionChanged: mouse => {
                                        if (pressed && (mouse.buttons & Qt.LeftButton) && noteCard.isResizing) {
                                            const pt = mapToItem(gridVerticalFlickable.contentItem, mouse.x, mouse.y)
                                            const vp = mapToItem(gridVerticalFlickable, mouse.x, mouse.y)
                                            applyDrag(pt.x, pt.y)
                                            root.trackAutoScroll(resizeMouse, vp, false)
                                        }
                                    }

                                    onCanceled: {
                                        root.endAutoScroll()
                                        root.dragEndBeats = 0
                                        noteCard.isResizing = false
                                        noteCard.visualLength = noteCard.lengthBeats
                                    }

                                    onReleased: mouse => {
                                        root.endAutoScroll()
                                        if (noteCard.isResizing) {
                                            // Capture first: resizeNote rebuilds this delegate
                                            const view = root
                                            noteCard.isResizing = false
                                            pianoRollBackend.resizeNote(noteCard.noteId, noteCard.visualLength)
                                            view.dragEndBeats = 0
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Interactive Grid Clicking
                    MouseArea {
                        id: gridMouse
                        objectName: "pianoRollGrid"
                        // Pencil-drag must not turn into a Flickable scroll
                        preventStealing: true
                        anchors.fill: parent
                        z: -1
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: root.activeTool === "pencil" ? Qt.CrossCursor : (root.activeTool === "eraser" ? Qt.PointingHandCursor : Qt.ArrowCursor)
                        hoverEnabled: true

                        property bool isDrawing: false
                        property int drawPitch: 60
                        property real drawStartBeat: 0.0
                        property real drawLength: 0.25

                        onPositionChanged: mouse => {
                            root.currentPosText = pianoRollBackend.positionLabel(mouse.x / root.pixelsPerBeat)

                            const notePitch = Math.max(0, Math.min(127, 127 - Math.floor(mouse.y / root.keyHeight)))
                            root.currentNoteText = pianoKeys.noteLabel(notePitch) + " " + notePitch

                            if (pressed && (mouse.buttons & Qt.LeftButton) && isDrawing) {
                                applyDrag(mouse.x, mouse.y)
                                root.trackAutoScroll(gridMouse, mapToItem(gridVerticalFlickable, mouse.x, mouse.y), false)
                            }
                        }

                        // gridMouse fills the content item, so cx is already a content coordinate
                        function applyDrag(cx, cy) {
                            // Stretch to the grid cell under the cursor (or the cursor itself with
                            // snap off), never shorter than the chosen note length.
                            const step = root.getSnapBeats()
                            const snappedCurrent = root.snapToGrid(cx / root.pixelsPerBeat)
                            drawLength = Math.max(root.newNoteLength(), (snappedCurrent - drawStartBeat) + step)
                            root.dragEndBeats = drawStartBeat + drawLength
                        }

                        onCanceled: {
                            root.endAutoScroll()
                            root.dragEndBeats = 0
                            isDrawing = false
                        }

                        onPressed: mouse => {
                            root.forceActiveFocus()
                            const beat = mouse.x / root.pixelsPerBeat
                            const pitch = Math.max(0, Math.min(127, 127 - Math.floor(mouse.y / root.keyHeight)))

                            if (mouse.button === Qt.RightButton || root.activeTool === "eraser") {
                                pianoRollBackend.removeNotesAt(pitch, beat)
                                return
                            }

                            if (mouse.button === Qt.LeftButton) {
                                if (root.activeTool === "pencil") {
                                    drawStartBeat = root.snapToGrid(beat)
                                    drawPitch = pitch
                                    drawLength = root.newNoteLength()
                                    isDrawing = true
                                } else {
                                    pianoRollBackend.clearSelection()
                                }
                            }
                        }

                        onReleased: mouse => {
                            root.endAutoScroll()
                            if (isDrawing) {
                                isDrawing = false
                                pianoRollBackend.addNote(drawPitch, drawStartBeat, drawLength, 100)
                                root.dragEndBeats = 0
                            }
                        }
                    }
                }
            }
        }

        // 5. Bottom Velocity / CC Lane (header pinned, stalks follow the grid's horizontal scroll)
        VelocityLane {
            id: velocityLane
            objectName: "velocityLane"
            Layout.fillWidth: true
            // Shrink the lane (never the grid) when the panel is short, e.g. when docked.
            Layout.preferredHeight: Math.max(0, Math.min(velocityLane.laneHeight,
                                                          root.height - root.chromeHeight - root.minimumGridHeight))
            visible: Layout.preferredHeight >= 20
            headerWidth: 90
            beatWidth: root.pixelsPerBeat
            beatUnit: root.beatUnit
            beatsInBar: root.beatsInBar
            contentWidth: root.totalGridWidth
            contentX: gridVerticalFlickable.contentX
            notes: root.notes
            colorSource: root
            onVelocitiesEdited: (ids, velocities) => pianoRollBackend.setNoteVelocities(ids, velocities)
        }

        // 6. Bottom Footer Control Bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: "#2c2e35"
            border.color: "#1d1f24"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 8

                Row {
                    spacing: 3
                    // Same transport commands as the main transport bar
                    Repeater {
                        model: [
                            { text: "|◀", tt: "Go to start", command: "rewind" },
                            { text: "▶", tt: "Play (Space)", command: "play" },
                            { text: "❚❚", tt: "Pause (Space)", command: "pause" },
                            { text: "■", tt: "Stop", command: "stop" },
                            { text: "▶|", tt: "Skip forward one bar", command: "forward" }
                        ]
                        delegate: Rectangle {
                            required property var modelData
                            readonly property bool active: modelData.command === "play" && !!root.transport && root.transport.playing
                            objectName: "pianoRollTransport_" + modelData.command
                            width: 22
                            height: 20
                            radius: 2
                            color: active ? "#203a45" : transHover.containsMouse ? "#454955" : "#353842"
                            border.color: active ? "#60cdff" : "#202227"

                            Text {
                                anchors.centerIn: parent
                                text: modelData.text
                                color: "#c8ccd6"
                                font.pixelSize: 10
                            }

                            MouseArea {
                                id: transHover
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: root.transport.transportAction(modelData.command)
                                ToolTip.visible: containsMouse
                                ToolTip.text: modelData.tt
                            }
                        }
                    }
                }

                Rectangle { width: 1; height: 16; color: "#3e424d" }

                Text { text: "Grid:"; color: "#8f95a3"; font.pixelSize: 11 }
                PianoRollComboBox {
                    implicitWidth: 70
                    implicitHeight: 22
                    model: ["1/4", "1/8", "1/16", "1/32", "1/64", "Off"]
                    currentIndex: 2
                    onCurrentTextChanged: root.snapDivision = currentText
                }

                PianoRollComboBox {
                    objectName: "pianoRollGridStyle"
                    implicitWidth: 80
                    implicitHeight: 22
                    model: ["straight", "triplet", "dotted"]
                    currentIndex: 0
                    onCurrentTextChanged: root.snapStyle = currentText
                }

                Text { text: "Notes:"; color: "#8f95a3"; font.pixelSize: 11 }
                PianoRollComboBox {
                    objectName: "pianoRollNoteLength"
                    implicitWidth: 70
                    implicitHeight: 22
                    model: ["Grid", "1/16", "1/8", "1/4"]
                    currentIndex: 0
                    onCurrentTextChanged: root.noteLengthMode = currentText
                    ToolTip.visible: hovered
                    ToolTip.text: "Length of newly drawn notes (Grid = the Grid division)"
                }

                Item { Layout.fillWidth: true }

                Row {
                    spacing: 2
                    Rectangle {
                        width: 18; height: 18; radius: 2
                        color: zoutHover.containsMouse ? "#454955" : "#353842"
                        border.color: "#202227"
                        Text { anchors.centerIn: parent; text: "-"; color: "#c8ccd6"; font.pixelSize: 12 }
                        MouseArea {
                            id: zoutHover; anchors.fill: parent; hoverEnabled: true
                            onClicked: root.pixelsPerBeat = Math.max(40, root.pixelsPerBeat - 16)
                        }
                    }
                    Rectangle {
                        width: 18; height: 18; radius: 2
                        color: zinHover.containsMouse ? "#454955" : "#353842"
                        border.color: "#202227"
                        Text { anchors.centerIn: parent; text: "+"; color: "#c8ccd6"; font.pixelSize: 12 }
                        MouseArea {
                            id: zinHover; anchors.fill: parent; hoverEnabled: true
                            onClicked: root.pixelsPerBeat = Math.min(240, root.pixelsPerBeat + 16)
                        }
                    }
                }
            }
        }
    }
}
