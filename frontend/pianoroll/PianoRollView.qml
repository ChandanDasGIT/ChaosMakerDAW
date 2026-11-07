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
    property real keyHeight: defaultKeyHeight
    // Zoom limits: time (Ctrl+wheel, -/+) and row height (Ctrl+Shift+wheel, Ctrl+wheel on the keys)
    readonly property real minPixelsPerBeat: 40
    readonly property real maxPixelsPerBeat: 240
    readonly property real defaultKeyHeight: 18
    readonly property real minKeyHeight: 8
    readonly property real maxKeyHeight: 36
    property string activeTool: "pencil" // "pointer", "pencil", "eraser"
    // Grid settings live in the backend (PianoRollGrid via PianoRollWrapper)
    property alias snapDivision: pianoRollBackend.gridDivision
    property alias snapStyle: pianoRollBackend.gridStyle
    property string colorMode: "Track" // "Track" (the clip's track colour) or "Velocity"

    // Transport used by the footer buttons and the playhead (replaceable in tests, like Timeline).
    property var transport: EngineController
    // Transport position in clip-relative beats (same origin as the notes); negative before the clip.
    readonly property real playheadBeat: root.transport.positionBeats - pianoRollBackend.clipStartBeat
    property string currentPosText: "1.1.00"
    property string currentNoteText: pianoRollBackend.noteReadout(60)

    property int dragCreatedNoteId: -1
    property real dragStartBeat: 0.0

    signal closeRequested()

    // Small square footer button (the zoom controls)
    component ZoomButton: Rectangle {
        id: zoomButton
        property string label
        property string tip
        signal clicked()
        width: 18; height: 18; radius: 2
        color: zoomHover.containsMouse ? Theme.pianoRoll.smallButtonHover : Theme.pianoRoll.smallButton
        border.color: Theme.pianoRoll.buttonBorder
        Text { anchors.centerIn: parent; text: zoomButton.label; color: Theme.pianoRoll.buttonText; font.pixelSize: 12 }
        Accessible.role: Accessible.Button
        Accessible.name: zoomButton.tip
        MouseArea {
            id: zoomHover; anchors.fill: parent; hoverEnabled: true
            onClicked: zoomButton.clicked()
            ToolTip.visible: containsMouse
            ToolTip.text: zoomButton.tip
            ToolTip.delay: 450
        }
    }

    PianoRollWrapper {
        id: pianoRollBackend
        objectName: "pianoRollBackend"
        trackList: root.trackModel
        trackId: root.trackId
        clipId: root.clipId
        pixelsPerBeat: root.pixelsPerBeat
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

    // Grid and snapping: thin calls into the backend grid (see backend/midi/pianorollgrid.h).
    function quantizeNotes() { pianoRollBackend.quantizeToGrid() }
    property alias noteLengthMode: pianoRollBackend.noteLengthMode
    function newNoteLength() { return pianoRollBackend.newNoteLength() }
    // Magnet button: when off, notes move and draw freely but the grid lines stay.
    property alias snapEnabled: pianoRollBackend.snapEnabled
    function getSnapBeats() { return pianoRollBackend.snapStep }
    function snapDelta(deltaBeats) { return pianoRollBackend.snapDelta(deltaBeats) }

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

    function snapToGrid(beat) { return pianoRollBackend.snapFloor(beat) }

    // Project meter from the backend, in quarter-note beats (6/8 -> 3 per bar, 0.5 per beat).
    readonly property real beatsPerBar: pianoRollBackend.beatsPerBar
    readonly property real beatUnit: pianoRollBackend.beatUnit
    readonly property int beatsInBar: pianoRollBackend.countedBeatsPerBar(beatsPerBar, beatUnit)
    // Visible length in beats (grid lines and ruler ticks cover it)
    readonly property real totalGridBeats: totalGridWidth / pixelsPerBeat

    function isOnGrid(position, unit) { return pianoRollBackend.isOnGrid(position, unit) }
    readonly property real gridLineBeats: pianoRollBackend.gridLineBeats
    readonly property int gridLinesPerBar: pianoRollBackend.gridLinesPerBar

    // Note fill colour. "Velocity" mode blends dim blue (soft) -> bright teal (loud);
    // other modes keep the default note colour for now.
    // "Track" mode shades the track colour the same way (darker = softer).
    // Track mode needs a bound clip; with no track colour the notes keep the blue scale.
    // Each colour checks the track colour itself, so none can use a stale mode while unbinding.
    function trackTint() {
        const colour = pianoRollBackend.trackColour
        return colorMode === "Track" && colour.length > 0 ? colour : ""
    }
    readonly property bool useTrackColour: trackTint().length > 0
    // Note outline and the brighter edge/hover tint, following the note colour mode.
    readonly property color noteOutlineColour: { const c = trackTint(); return c.length > 0 ? Qt.lighter(c, 1.45) : Theme.pianoRoll.noteOutline }
    readonly property color noteHighlightColour: { const c = trackTint(); return c.length > 0 ? Qt.lighter(c, 1.8) : Theme.pianoRoll.noteHighlight }
    function noteFillColor(velocity) {
        const t = Math.max(0, Math.min(1, velocity / 127.0))
        const trackColour = trackTint()
        const useTrack = trackColour.length > 0
        const lo = useTrack ? Qt.darker(trackColour, 2.4) : Theme.pianoRoll.noteSoft
        const hi = useTrack ? Qt.lighter(trackColour, 1.1) : Theme.pianoRoll.noteLoud
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
    // (fixed, so zooming the rows never resizes the dock)
    readonly property real minimumGridHeight: 6 * defaultKeyHeight
    readonly property real minimumVelocityHeight: 45
    readonly property real minimumDockHeight: chromeHeight + minimumGridHeight + minimumVelocityHeight

    // Zoom time, keeping the beat under viewportX (px from the grid's left edge) in place.
    function zoomTimeAt(factor, viewportX) {
        const f = gridVerticalFlickable
        const beat = (f.contentX + viewportX) / pixelsPerBeat
        pixelsPerBeat = Math.max(minPixelsPerBeat, Math.min(maxPixelsPerBeat, pixelsPerBeat * factor))
        f.contentX = Math.max(0, Math.min(Math.max(0, f.contentWidth - f.width), beat * pixelsPerBeat - viewportX))
    }

    // Zoom the row height, keeping the pitch under viewportY (px from the grid's top) in place.
    function zoomRowsAt(factor, viewportY) {
        const f = gridVerticalFlickable
        const row = (f.contentY + viewportY) / keyHeight
        keyHeight = Math.max(minKeyHeight, Math.min(maxKeyHeight, keyHeight * factor))
        f.contentY = Math.max(0, Math.min(Math.max(0, f.contentHeight - f.height), row * keyHeight - viewportY))
    }

    // Wheel over the grid or the keys: plain = pitch scroll, Shift = time scroll,
    // Ctrl = time zoom (rows over the keys), Ctrl+Shift = row zoom. viewportPt is in
    // gridVerticalFlickable coordinates.
    function handleWheel(pixelY, angleY, modifiers, viewportPt, overKeys) {
        const delta = pixelY !== 0 ? pixelY : angleY
        if (modifiers & Qt.ControlModifier) {
            const steps = (angleY !== 0 ? angleY : pixelY) / 120
            if (overKeys || (modifiers & Qt.ShiftModifier)) zoomRowsAt(Math.pow(1.1, steps), viewportPt.y)
            else zoomTimeAt(Math.pow(1.12, steps), viewportPt.x)
        } else if (modifiers & Qt.ShiftModifier) {
            gridVerticalFlickable.scrollHorizontal(delta)
        } else {
            gridVerticalFlickable.scrollVertical(delta)
        }
    }

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
        color: Theme.pianoRoll.background
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 1. Title bar: clip name and close button
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 22
            color: Theme.pianoRoll.menuBar
            border.color: Theme.pianoRoll.menuBarBorder

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 14

                Item { Layout.fillWidth: true }

                Text {
                    text: root.clipLabel
                    color: Theme.pianoRoll.textMuted
                    font.pixelSize: 11
                }

                ToolButton {
                    text: Icons.close
                    Accessible.name: "Close piano roll"
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
            color: Theme.pianoRoll.toolStrip
            border.color: Theme.pianoRoll.panelBorder

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 4

                Rectangle {
                    width: 24; height: 22; radius: 2
                    color: root.activeTool === "pointer" ? Theme.pianoRoll.toolButtonActive : tool1Hover.containsMouse ? Theme.pianoRoll.toolButtonHover : Theme.pianoRoll.toolButton
                    border.color: root.activeTool === "pointer" ? Theme.accent : Theme.pianoRoll.buttonBorder
                    Text { anchors.centerIn: parent; text: Icons.pointer; color: Theme.pianoRoll.text; font.pixelSize: 13 }
                    Accessible.role: Accessible.Button; Accessible.name: "Select tool"
                    MouseArea { id: tool1Hover; anchors.fill: parent; hoverEnabled: true; onClicked: root.activeTool = "pointer" }
                    ToolTip.visible: tool1Hover.containsMouse; ToolTip.text: "Select (Pointer) · drag on empty grid to box-select, Shift adds"
                }

                Rectangle {
                    width: 24; height: 22; radius: 2
                    color: root.activeTool === "pencil" ? Theme.pianoRoll.toolButtonActive : tool2Hover.containsMouse ? Theme.pianoRoll.toolButtonHover : Theme.pianoRoll.toolButton
                    border.color: root.activeTool === "pencil" ? Theme.accent : Theme.pianoRoll.buttonBorder
                    Text { anchors.centerIn: parent; text: Icons.pencil; color: Theme.pianoRoll.text; font.pixelSize: 13 }
                    Accessible.role: Accessible.Button; Accessible.name: "Draw tool"
                    MouseArea { id: tool2Hover; anchors.fill: parent; hoverEnabled: true; onClicked: root.activeTool = "pencil" }
                    ToolTip.visible: tool2Hover.containsMouse; ToolTip.text: "Draw (Pencil)"
                }

                Rectangle {
                    width: 24; height: 22; radius: 2
                    color: root.activeTool === "eraser" ? Theme.pianoRoll.toolButtonActive : tool3Hover.containsMouse ? Theme.pianoRoll.toolButtonHover : Theme.pianoRoll.toolButton
                    border.color: root.activeTool === "eraser" ? Theme.accent : Theme.pianoRoll.buttonBorder
                    Text { anchors.centerIn: parent; text: Icons.eraser; color: Theme.pianoRoll.text; font.pixelSize: 13 }
                    Accessible.role: Accessible.Button; Accessible.name: "Erase tool"
                    MouseArea { id: tool3Hover; anchors.fill: parent; hoverEnabled: true; onClicked: root.activeTool = "eraser" }
                    ToolTip.visible: tool3Hover.containsMouse; ToolTip.text: "Erase"
                }

                Rectangle { width: 1; height: 18; color: Theme.pianoRoll.separator }

                Rectangle {
                    objectName: "pianoRollSnap"
                    width: 24; height: 22; radius: 2
                    color: root.snapEnabled ? Theme.pianoRoll.toolButtonActive : snapHover.containsMouse ? Theme.pianoRoll.toolButtonHover : Theme.pianoRoll.toolButton
                    border.color: root.snapEnabled ? Theme.accent : Theme.pianoRoll.buttonBorder
                    Text { anchors.centerIn: parent; text: Icons.snap; color: Theme.pianoRoll.text; font.pixelSize: 11 }
                    Accessible.role: Accessible.Button; Accessible.name: "Snap to grid"
                    MouseArea { id: snapHover; anchors.fill: parent; hoverEnabled: true; onClicked: root.snapEnabled = !root.snapEnabled }
                    ToolTip.visible: snapHover.containsMouse
                    ToolTip.text: root.snapEnabled ? "Snap to grid: on (click to place notes freely)" : "Snap to grid: off (click to snap to the Grid setting)"
                }

                Rectangle {
                    objectName: "pianoRollQuantize"
                    width: 24; height: 22; radius: 2
                    enabled: pianoRollBackend.count > 0
                    opacity: enabled ? 1 : 0.4
                    color: qHover.containsMouse ? Theme.pianoRoll.toolButtonHover : Theme.pianoRoll.toolButton
                    border.color: Theme.pianoRoll.buttonBorder
                    Text { anchors.centerIn: parent; text: Icons.quantize + "Q"; color: Theme.pianoRoll.text; font.pixelSize: 10; font.bold: true }
                    Accessible.role: Accessible.Button; Accessible.name: "Quantize"
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
                    color: undoHover.containsMouse ? Theme.pianoRoll.toolButtonHover : Theme.pianoRoll.toolButton
                    border.color: Theme.pianoRoll.buttonBorder
                    Text { anchors.centerIn: parent; text: Icons.undo; color: Theme.pianoRoll.text; font.pixelSize: 13 }
                    Accessible.role: Accessible.Button; Accessible.name: "Undo"
                    MouseArea { id: undoHover; anchors.fill: parent; hoverEnabled: true; onClicked: root.trackModel.undo() }
                    ToolTip.visible: undoHover.containsMouse; ToolTip.text: "Undo (Ctrl+Z)"
                }

                Rectangle {
                    objectName: "pianoRollRedo"
                    width: 24; height: 22; radius: 2
                    enabled: !!root.trackModel && root.trackModel.canRedo
                    opacity: enabled ? 1 : 0.4
                    color: redoHover.containsMouse ? Theme.pianoRoll.toolButtonHover : Theme.pianoRoll.toolButton
                    border.color: Theme.pianoRoll.buttonBorder
                    Text { anchors.centerIn: parent; text: Icons.redo; color: Theme.pianoRoll.text; font.pixelSize: 13 }
                    Accessible.role: Accessible.Button; Accessible.name: "Redo"
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
                color: Theme.pianoRoll.panel
                border.color: Theme.pianoRoll.panelBorder

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
                            color: Theme.pianoRoll.readoutBox
                            border.color: Theme.pianoRoll.readoutBorder

                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 2
                                anchors.rightMargin: 2
                                text: modelData.text
                                color: Theme.pianoRoll.text
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
                    color: Theme.pianoRoll.ruler
                    border.color: Theme.pianoRoll.panelBorder

                    Rectangle {
                        x: 0
                        y: 0
                        width: root.clipWidthPixels
                        height: 4
                        color: Theme.pianoRoll.playhead
                    }

                    // One tick per counted beat of the project meter
                    Repeater {
                        model: pianoRollBackend.tickCount(root.totalGridBeats, root.beatUnit, false)
                        delegate: Item {
                            required property int index
                            readonly property bool isMajorBar: pianoRollBackend.isBarTick(index, root.beatsInBar)

                            x: index * root.beatUnit * root.pixelsPerBeat
                            y: 0
                            width: root.beatUnit * root.pixelsPerBeat
                            height: parent.height

                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: isMajorBar ? 1.5 : 1
                                color: isMajorBar ? Theme.pianoRoll.rulerTickBar : Theme.pianoRoll.rulerTick
                            }

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 3
                                anchors.top: parent.top
                                anchors.topMargin: isMajorBar ? 4 : 8
                                // Beat labels drop out when beats are too narrow to read
                                visible: isMajorBar || parent.width >= 24
                                text: pianoRollBackend.tickLabel(index, root.beatsInBar)
                                color: isMajorBar ? Theme.pianoRoll.text : Theme.pianoRoll.rulerLabelMinor
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
                        id: keysWheel
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        target: null
                        onWheel: event => root.handleWheel(event.pixelDelta.y, event.angleDelta.y, event.modifiers,
                            Qt.point(0, keysWheel.parent.mapToItem(gridVerticalFlickable, keysWheel.point.position).y), true)
                    }

                    PianoKeys {
                        id: pianoKeys
                        objectName: "pianoKeys"
                        noteSource: pianoRollBackend
                        drumMode: pianoRollBackend.drumMode
                        width: 90
                        height: root.contentGridHeight
                        keyHeight: root.keyHeight
                        onKeyHovered: (note, noteName) => {
                            root.currentNoteText = pianoRollBackend.noteReadout(note)
                        }
                        onKeyClicked: note => {
                            root.currentNoteText = pianoRollBackend.noteReadout(note)
                        }
                        onKeyPressed: (note, velocity) => {
                            root.currentNoteText = pianoRollBackend.noteReadout(note)
                            EngineController.sendLiveNoteOn(root.trackId, note, velocity)
                        }
                        onKeyReleased: note => {
                            EngineController.sendLiveNoteOff(root.trackId, note)
                        }
                        onWheelScrolled: (pixelY, angleY, modifiers, keyY) => {
                            root.handleWheel(pixelY, angleY, modifiers,
                                             Qt.point(0, keyY - gridVerticalFlickable.contentY), true)
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
                        id: gridWheel
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        target: null
                        onWheel: event => root.handleWheel(event.pixelDelta.y, event.angleDelta.y, event.modifiers,
                            gridWheel.parent.mapToItem(gridVerticalFlickable, gridWheel.point.position), false)
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
                            color: vScrollBar.pressed ? Theme.pianoRoll.scrollHandlePressed : (vScrollBar.hovered ? Theme.pianoRoll.scrollHandleHover : Theme.pianoRoll.scrollHandle)
                        }
                        background: Rectangle {
                            color: Theme.pianoRoll.scrollTrack
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
                            color: hScrollBar.pressed ? Theme.pianoRoll.scrollHandlePressed : (hScrollBar.hovered ? Theme.pianoRoll.scrollHandleHover : Theme.pianoRoll.scrollHandle)
                        }
                        background: Rectangle {
                            color: Theme.pianoRoll.scrollTrack
                        }
                    }

                    // Active Clip Background Highlight vs Dimmed Outside
                    Rectangle {
                        x: 0
                        y: 0
                        width: root.clipWidthPixels
                        height: root.contentGridHeight
                        color: Theme.pianoRoll.clipInside
                    }

                    Rectangle {
                        x: root.clipWidthPixels
                        y: 0
                        width: Math.max(0, root.totalGridWidth - root.clipWidthPixels)
                        height: root.contentGridHeight
                        color: Theme.pianoRoll.clipOutside
                    }

                    // Zebra Striped Semitone Rows
                    Column {
                        Repeater {
                            model: 128
                            delegate: Rectangle {
                                required property int index
                                readonly property int note: 127 - index
                                readonly property bool black: pianoRollBackend.isBlackKey(note)
                                readonly property bool isOctaveBorder: pianoRollBackend.isOctaveStart(note)

                                width: root.totalGridWidth
                                height: root.keyHeight
                                color: black ? Theme.pianoRoll.rowBlackKey : Theme.pianoRoll.rowWhiteKey
                                border.color: isOctaveBorder ? Theme.pianoRoll.rowOctaveBorder : Theme.pianoRoll.rowBorder
                                border.width: 1
                            }
                        }
                    }

                    // Vertical Bar / Beat / Subdivision Grid Lines (follow the Grid setting)
                    Repeater {
                        model: pianoRollBackend.lineCount(root.totalGridBeats, root.beatsPerBar, root.gridLinesPerBar)
                        delegate: Rectangle {
                            required property int index
                            readonly property real offsetInBar: pianoRollBackend.lineOffsetInBar(index, root.gridLinesPerBar, root.gridLineBeats)
                            readonly property real position: pianoRollBackend.linePosition(index, root.gridLinesPerBar, root.gridLineBeats, root.beatsPerBar)
                            readonly property bool isMajorBar: pianoRollBackend.isBarLineIndex(index, root.gridLinesPerBar)
                            readonly property bool isBeat: root.isOnGrid(offsetInBar, root.beatUnit)

                            // Whole-pixel positions & widths keep every line crisp
                            x: Math.round(position * root.pixelsPerBeat)
                            y: 0
                            width: isMajorBar ? 2 : 1
                            height: root.contentGridHeight
                            color: isMajorBar ? Theme.pianoRoll.gridBar : (isBeat ? Theme.pianoRoll.gridBeat : Theme.pianoRoll.gridSub)
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
                        color: Theme.pianoRoll.playhead
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
                        color: Qt.alpha(root.noteFillColor(100), 0.5)
                        border.color: root.noteHighlightColour
                        border.width: 1
                        z: 8
                    }

                    // Rubber-band box (Pointer tool drag on empty grid)
                    Rectangle {
                        objectName: "pianoRollSelectionBox"
                        visible: gridMouse.isBanding && gridMouse.bandMoved
                        x: Math.min(gridMouse.bandStartX, gridMouse.bandEndX)
                        y: Math.min(gridMouse.bandStartY, gridMouse.bandEndY)
                        width: Math.abs(gridMouse.bandEndX - gridMouse.bandStartX)
                        height: Math.abs(gridMouse.bandEndY - gridMouse.bandStartY)
                        color: Theme.pianoRoll.selectionBox
                        border.color: Theme.accent
                        border.width: 1
                        z: 9
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
                            border.color: showSelected ? Theme.pianoRoll.noteSelected : (isHovered ? root.noteHighlightColour : root.noteOutlineColour)
                            border.width: isSelected ? 1.5 : 1

                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 3
                                color: isSelected ? Theme.pianoRoll.noteSelectedEdge : root.noteHighlightColour
                            }

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 5
                                anchors.right: resizeHandle.left
                                anchors.rightMargin: 2
                                anchors.verticalCenter: parent.verticalCenter
                                visible: root.keyHeight >= 9
                                text: pianoRollBackend.noteName(noteCard.visualPitch)
                                color: Theme.pianoRoll.noteName
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
                                        root.currentNoteText = pianoRollBackend.noteReadout(noteCard.visualPitch)
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
                                        const lenDelta = root.snapDelta(dx / root.pixelsPerBeat)
                                        noteCard.visualLength = Math.max(pianoRollBackend.minimumNoteLength(), pressStartLength + lenDelta)
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
                        // Rubber band (content coordinates); bandAdd = Shift/Ctrl held at press
                        property bool isBanding: false
                        property bool bandMoved: false
                        property bool bandAdd: false
                        property real bandStartX: 0
                        property real bandStartY: 0
                        property real bandEndX: 0
                        property real bandEndY: 0

                        onPositionChanged: mouse => {
                            root.currentPosText = pianoRollBackend.positionLabel(mouse.x / root.pixelsPerBeat)

                            const notePitch = Math.max(0, Math.min(127, 127 - Math.floor(mouse.y / root.keyHeight)))
                            root.currentNoteText = pianoRollBackend.noteReadout(notePitch)

                            if (pressed && (mouse.buttons & Qt.LeftButton) && isDrawing) {
                                applyDrag(mouse.x, mouse.y)
                                root.trackAutoScroll(gridMouse, mapToItem(gridVerticalFlickable, mouse.x, mouse.y), false)
                            } else if (pressed && (mouse.buttons & Qt.LeftButton) && isBanding) {
                                const vp = mapToItem(gridVerticalFlickable, mouse.x, mouse.y)
                                applyDrag(mouse.x, mouse.y)
                                if (bandMoved)
                                    root.trackAutoScroll(gridMouse, vp, true)
                            }
                        }

                        // gridMouse fills the content item, so cx is already a content coordinate
                        function applyDrag(cx, cy) {
                            if (isBanding) {
                                bandEndX = Math.max(0, Math.min(width, cx))
                                bandEndY = Math.max(0, Math.min(height, cy))
                                if (Math.abs(bandEndX - bandStartX) > 3 || Math.abs(bandEndY - bandStartY) > 3)
                                    bandMoved = true
                                return
                            }
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
                            isBanding = false
                        }

                        // Select the notes the band touches: pitches of the rows it crosses and
                        // the beats it spans (the backend decides what overlaps).
                        function finishBand() {
                            const top = Math.min(bandStartY, bandEndY)
                            const bottom = Math.max(bandStartY, bandEndY)
                            const highNote = 127 - Math.floor(top / root.keyHeight)
                            const lowNote = 127 - Math.min(127, Math.floor(Math.max(top, bottom - 0.01) / root.keyHeight))
                            pianoRollBackend.selectNotesInRange(lowNote, highNote,
                                                                bandStartX / root.pixelsPerBeat,
                                                                bandEndX / root.pixelsPerBeat, bandAdd)
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
                                    // Pointer: start a rubber band; a plain click (no drag)
                                    // clears the selection on release.
                                    bandAdd = (mouse.modifiers & Qt.ShiftModifier) || (mouse.modifiers & Qt.ControlModifier)
                                    bandStartX = bandEndX = mouse.x
                                    bandStartY = bandEndY = mouse.y
                                    bandMoved = false
                                    isBanding = true
                                }
                            }
                        }

                        onReleased: mouse => {
                            root.endAutoScroll()
                            if (isBanding) {
                                isBanding = false
                                if (bandMoved) finishBand()
                                else if (!bandAdd) pianoRollBackend.clearSelection()
                                return
                            }
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
            gridSource: pianoRollBackend
            onVelocitiesEdited: (ids, velocities) => pianoRollBackend.setNoteVelocities(ids, velocities)
        }

        // 6. Bottom Footer Control Bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: Theme.pianoRoll.panel
            border.color: Theme.pianoRoll.panelBorder

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
                            { text: Icons.goToStart, tt: "Go to start", command: "rewind" },
                            { text: Icons.play, tt: "Play (Space)", command: "play" },
                            { text: Icons.pause, tt: "Pause (Space)", command: "pause" },
                            { text: Icons.stop, tt: "Stop", command: "stop" },
                            { text: Icons.skipForward, tt: "Skip forward one bar", command: "forward" }
                        ]
                        delegate: Rectangle {
                            required property var modelData
                            readonly property bool active: modelData.command === "play" && !!root.transport && root.transport.playing
                            objectName: "pianoRollTransport_" + modelData.command
                            Accessible.role: Accessible.Button
                            Accessible.name: modelData.tt
                            width: 22
                            height: 20
                            radius: 2
                            color: active ? Theme.pianoRoll.transportActive : transHover.containsMouse ? Theme.pianoRoll.smallButtonHover : Theme.pianoRoll.smallButton
                            border.color: active ? Theme.accent : Theme.pianoRoll.buttonBorder

                            Text {
                                anchors.centerIn: parent
                                text: modelData.text
                                color: Theme.pianoRoll.buttonText
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

                Rectangle { width: 1; height: 16; color: Theme.pianoRoll.separator }

                Text { text: "Grid:"; color: Theme.pianoRoll.label; font.pixelSize: 11 }
                PianoRollComboBox {
                    implicitWidth: 70
                    implicitHeight: 22
                    model: pianoRollBackend.gridDivisionOptions
                    currentIndex: model.indexOf(pianoRollBackend.gridDivision)
                    onCurrentTextChanged: pianoRollBackend.gridDivision = currentText
                }

                PianoRollComboBox {
                    objectName: "pianoRollGridStyle"
                    implicitWidth: 80
                    implicitHeight: 22
                    model: pianoRollBackend.gridStyleOptions
                    currentIndex: model.indexOf(pianoRollBackend.gridStyle)
                    onCurrentTextChanged: pianoRollBackend.gridStyle = currentText
                }

                Text { text: "Notes:"; color: Theme.pianoRoll.label; font.pixelSize: 11 }
                PianoRollComboBox {
                    objectName: "pianoRollNoteLength"
                    implicitWidth: 70
                    implicitHeight: 22
                    model: pianoRollBackend.noteLengthOptions
                    currentIndex: model.indexOf(pianoRollBackend.noteLengthMode)
                    onCurrentTextChanged: pianoRollBackend.noteLengthMode = currentText
                    ToolTip.visible: hovered
                    ToolTip.text: "Length of newly drawn notes (Grid = the Grid division)"
                }

                Text { text: "Color:"; color: Theme.pianoRoll.label; font.pixelSize: 11 }
                PianoRollComboBox {
                    objectName: "pianoRollColorMode"
                    implicitWidth: 80
                    implicitHeight: 22
                    model: ["Velocity", "Track"]
                    currentIndex: model.indexOf(root.colorMode)
                    onCurrentTextChanged: root.colorMode = currentText
                    ToolTip.visible: hovered
                    ToolTip.text: "Note colour: the track colour (shaded by velocity), or velocity shades of blue"
                }

                Item { Layout.fillWidth: true }

                // Zoom: time (also Ctrl+wheel) and row height (also Ctrl+Shift+wheel)
                Row {
                    spacing: 2
                    Text { text: "Zoom:"; color: Theme.pianoRoll.label; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter; rightPadding: 2 }
                    ZoomButton {
                        objectName: "pianoRollZoomOut"
                        label: Icons.minus; tip: "Zoom out in time · Ctrl+wheel"
                        onClicked: root.pixelsPerBeat = Math.max(root.minPixelsPerBeat, root.pixelsPerBeat - 16)
                    }
                    ZoomButton {
                        objectName: "pianoRollZoomIn"
                        label: Icons.plus; tip: "Zoom in on time · Ctrl+wheel"
                        onClicked: root.pixelsPerBeat = Math.min(root.maxPixelsPerBeat, root.pixelsPerBeat + 16)
                    }
                    Item { width: 6; height: 1 }
                    Text { text: "Rows:"; color: Theme.pianoRoll.label; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter; rightPadding: 2 }
                    ZoomButton {
                        objectName: "pianoRollRowsShorter"
                        label: Icons.minus; tip: "Shorter rows (Ctrl+Shift+wheel, or Ctrl+wheel over the keys)"
                        onClicked: root.zoomRowsAt(1 / 1.25, gridVerticalFlickable.height / 2)
                    }
                    ZoomButton {
                        objectName: "pianoRollRowsTaller"
                        label: Icons.plus; tip: "Taller rows (Ctrl+Shift+wheel, or Ctrl+wheel over the keys)"
                        onClicked: root.zoomRowsAt(1.25, gridVerticalFlickable.height / 2)
                    }
                }
            }
        }
    }
}
