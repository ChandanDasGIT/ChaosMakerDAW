import QtQuick
import QtQuick.Controls

Item {
    id: root
    property var transport: EngineController
    property int timeSignature: 4
    property int timeSignatureDenominator: 4
    property real bpm: 120
    property alias snapMode: editor.snapMode // Backend-owned bars/beats/subdivision/off choice.
    TimelineWrapper {
        id: editor
        trackModel: root.trackModel ?? null
        numerator: root.timeSignature
        denominator: root.timeSignatureDenominator
        bpm: root.bpm
        takeBusy: root.transport.takeBusy
        projectLoading: root.transport.projectLoading
        recording: root.recording
        onSeekRequested: beat => root.transport.seekBeat(beat)
        onRepeatRangeRequested: (start, end) => editor.acceptRepeatRange(root.transport.setRepeatRange(start, end))
    }
    readonly property real snapStepBeats: editor.snapStepBeats
    // Pixel geometry and scroll/zoom state are presentation concerns.
    property real pixelsPerSecond: 96
    readonly property real beatWidth: pixelsPerSecond * editor.secondsPerBeat
    readonly property real playhead: root.transport.positionBeats * beatWidth
    readonly property bool playing: root.transport.playing
    readonly property bool recording: root.transport.recording
    readonly property bool repeating: root.transport.repeating
    readonly property real recordingTailX: playhead + width * 0.2
    property bool followPlayhead: true
    property real lastTransportPlayhead: 0
    onPlayingChanged: { if (playing) followPlayhead = true }
    readonly property var selectedClipIds: editor.selectedClipIds
    readonly property int selectedClipId: editor.selectedClipId
    readonly property int selectedTrackId: editor.selectedTrackId
    readonly property var keyboardFocusItem: Window.window ? Window.window.activeFocusItem : null
    readonly property bool editingText: keyboardFocusItem instanceof TextInput || keyboardFocusItem instanceof TextEdit
    readonly property bool clipKeyboardActive: activeFocus && visible && enabled && !editingText && editor.keyboardAllowed
    readonly property bool canEditClipSelection: clipKeyboardActive && editor.canEditSelection
    readonly property bool canCopy: clipKeyboardActive && editor.canCopy
    readonly property bool canCut: clipKeyboardActive && editor.canCut
    readonly property bool canPaste: clipKeyboardActive && editor.canPaste
    readonly property bool canDuplicate: clipKeyboardActive && editor.canDuplicate
    readonly property string editOperation: editor.editOperation
    readonly property real editDelta: editor.editDelta
    readonly property int editLaneDelta: editor.editLaneDelta
    readonly property bool canMergeSelection: editor.canMergeSelection
    readonly property bool selecting: editor.selecting
    readonly property bool hasSelection: editor.hasSelection
    readonly property bool clipSelectionDrag: editor.clipSelectionDrag
    property alias timeRangeStartBeat: editor.timeRangeStartBeat
    property alias timeRangeEndBeat: editor.timeRangeEndBeat
    readonly property real rangeStartBeat: editor.rangeStartBeat
    readonly property real rangeEndBeat: editor.rangeEndBeat

    signal openPianoRollRequested(int trackId, int clipId, string label, real lengthBeats)

    Shortcut {
        sequence: "Delete"
        enabled: root.canEditClipSelection
        autoRepeat: false
        onActivated: root.trackModel.editClips(root.selectedClipIds, "delete", 0)
    }
    Shortcut {
        sequences: [StandardKey.Cut]
        enabled: root.canCut
        autoRepeat: false
        onActivated: editor.cutSelectedClips()
    }
    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: root.canCopy
        autoRepeat: false
        onActivated: editor.copySelectedClips()
    }
    Shortcut {
        sequences: [StandardKey.Paste]
        enabled: root.canPaste
        autoRepeat: false
        onActivated: editor.pasteClips(root.transport.positionBeats, editor.selectedTrackId)
    }
    Shortcut {
        sequence: "Ctrl+D"
        enabled: root.canDuplicate
        autoRepeat: false
        onActivated: editor.duplicateSelectedClips()
    }
    Shortcut {
        sequence: "Left"
        enabled: root.canEditClipSelection
        autoRepeat: true
        onActivated: editor.nudgeClips(-1)
    }
    Shortcut {
        sequence: "Right"
        enabled: root.canEditClipSelection
        autoRepeat: true
        onActivated: editor.nudgeClips(1)
    }
    Shortcut {
        sequences: [StandardKey.SelectAll]
        enabled: root.clipKeyboardActive
        autoRepeat: false
        onActivated: editor.selectAllClips()
    }
    Shortcut {
        sequence: "Escape"
        enabled: root.clipKeyboardActive && root.selectedClipIds.length > 0
        autoRepeat: false
        onActivated: editor.deselectClips()
    }
    function beginSelection(x, y, clips) {
        forceActiveFocus()
        selectionStartX = x; selectionEndX = x
        selectionStartY = y; selectionEndY = y
        editor.beginSelection(x / beatWidth, y / laneHeight, clips)
    }
    function updateSelection(x, y) {
        selectionEndX = x; selectionEndY = y
        editor.updateSelection(x / beatWidth, y / laneHeight, Math.abs(x - selectionStartX) > 2)
    }
    function finishSelection() {
        editor.finishSelection(8 / laneHeight, 10 / laneHeight, 1 / laneHeight)
    }
    property real selectionStartX: 0
    property real selectionEndX: 0
    property real selectionStartY: 0
    property real selectionEndY: 0
    property var trackModel: null
    property int laneCount: 5
    property int laneHeight: 106
    property int rulerHeight: 38
    readonly property real verticalScrollPosition: scroll.contentY
    readonly property real beatUnitWidth: beatWidth * editor.beatUnit
    readonly property real barWidth: beatWidth * editor.beatsPerBar
    readonly property real contentEndBars: editor.contentEndBars
    readonly property real contentWidth: Math.max(width, (Math.ceil(contentEndBars) + 1) * barWidth, recordingTailX)
    // Which bar labels and grid lines to draw: backend rules (MusicalGrid via TimelineWrapper).
    readonly property real barLabelStride: editor.lineStride(barWidth, 48)
    readonly property real firstVisibleBar: editor.firstVisibleLine(scroll.contentX, barWidth, barLabelStride)
    readonly property int visibleBarCount: editor.visibleLineCount(width, barWidth, barLabelStride)
    readonly property real gridStepBeats: editor.gridStepBeats
    readonly property real gridUnitWidth: gridStepBeats * beatWidth
    readonly property real beatGridStride: editor.lineStride(gridUnitWidth, 8)
    readonly property real firstVisibleBeat: editor.firstVisibleLine(scroll.contentX, gridUnitWidth, beatGridStride)
    readonly property int visibleBeatCount: editor.visibleLineCount(width, gridUnitWidth, beatGridStride)

    function scrollVertically(amount) {
        scroll.contentY = Math.max(0, Math.min(scroll.contentHeight - scroll.height, scroll.contentY + amount))
    }

    function scrollHorizontally(amount) {
        followPlayhead = false
        scroll.contentX = Math.max(0, Math.min(scroll.contentWidth - scroll.width, scroll.contentX + amount))
    }

    function zoomAtCursor(factor, cursorX) {
        followPlayhead = false
        const anchor = scroll.contentX + cursorX
        const nextPixelsPerSecond = Math.max(0.000001, pixelsPerSecond * factor)
        const scale = nextPixelsPerSecond / pixelsPerSecond
        pixelsPerSecond = nextPixelsPerSecond
        scroll.contentX = Math.max(0, Math.min(scroll.contentWidth - scroll.width, anchor * scale - cursorX))
    }

    Shortcut {
        sequence: "S"
        enabled: root.activeFocus && editor.canSplitSelection
        onActivated: root.trackModel.splitClipAtBeat(root.selectedTrackId, root.selectedClipId, root.transport.positionBeats)
    }
    Menu {
        id: clipContextMenu
        parent: root
        popupType: Popup.Item
        width: 160
        padding: 5
        property int targetTrackId: -1
        property int targetClipId: -1
        property real targetStart: 0
        property real targetLength: 0
        property bool targetIsRecorded: false
        property string targetLabel: ""

        background: Rectangle {
            color: Theme.panelBg
            border.color: Theme.divider
            radius: 6
        }

        MenuItem {
            id: openPianoRollAction
            objectName: "openPianoRollAction"
            text: "Open Piano Roll"
            visible: !clipContextMenu.targetIsRecorded
            enabled: visible
            implicitHeight: visible ? 32 : 0
            leftPadding: Theme.margin
            rightPadding: Theme.margin
            contentItem: Text {
                text: openPianoRollAction.text
                color: !openPianoRollAction.enabled ? Theme.textMuted : openPianoRollAction.highlighted ? Theme.accent : Theme.text
                font.pixelSize: Theme.bodySize
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: openPianoRollAction.highlighted ? Theme.tooltipBg : "transparent"
                radius: 4
            }
            onTriggered: {
                root.openPianoRollRequested(clipContextMenu.targetTrackId, clipContextMenu.targetClipId,
                    clipContextMenu.targetLabel, clipContextMenu.targetLength)
            }
        }

        MenuSeparator {
            visible: openPianoRollAction.visible
            contentItem: Rectangle {
                implicitHeight: 1
                color: Theme.divider
            }
        }

        MenuItem {
            id: cutClipAction
            text: "Cut"
            enabled: editor.canCut
            implicitHeight: 32
            leftPadding: Theme.margin
            rightPadding: Theme.margin
            contentItem: Text { text: cutClipAction.text; color: !cutClipAction.enabled ? Theme.textMuted : cutClipAction.highlighted ? Theme.accent : Theme.text; font.pixelSize: Theme.bodySize; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: cutClipAction.highlighted ? Theme.tooltipBg : "transparent"; radius: 4 }
            onTriggered: editor.cutSelectedClips()
        }
        MenuItem {
            id: copyClipAction
            text: "Copy"
            enabled: editor.canCopy
            implicitHeight: 32
            leftPadding: Theme.margin
            rightPadding: Theme.margin
            contentItem: Text { text: copyClipAction.text; color: !copyClipAction.enabled ? Theme.textMuted : copyClipAction.highlighted ? Theme.accent : Theme.text; font.pixelSize: Theme.bodySize; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: copyClipAction.highlighted ? Theme.tooltipBg : "transparent"; radius: 4 }
            onTriggered: editor.copySelectedClips()
        }
        MenuItem {
            id: pasteClipAction
            text: "Paste"
            enabled: editor.canPaste
            implicitHeight: 32
            leftPadding: Theme.margin
            rightPadding: Theme.margin
            contentItem: Text { text: pasteClipAction.text; color: !pasteClipAction.enabled ? Theme.textMuted : pasteClipAction.highlighted ? Theme.accent : Theme.text; font.pixelSize: Theme.bodySize; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: pasteClipAction.highlighted ? Theme.tooltipBg : "transparent"; radius: 4 }
            onTriggered: editor.pasteClips(root.transport.positionBeats, clipContextMenu.targetTrackId >= 0 ? clipContextMenu.targetTrackId : editor.selectedTrackId)
        }
        MenuItem {
            id: duplicateClipAction
            text: "Duplicate"
            enabled: editor.canDuplicate
            implicitHeight: 32
            leftPadding: Theme.margin
            rightPadding: Theme.margin
            contentItem: Text { text: duplicateClipAction.text; color: !duplicateClipAction.enabled ? Theme.textMuted : duplicateClipAction.highlighted ? Theme.accent : Theme.text; font.pixelSize: Theme.bodySize; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: duplicateClipAction.highlighted ? Theme.tooltipBg : "transparent"; radius: 4 }
            onTriggered: editor.duplicateSelectedClips()
        }
        MenuItem {
            id: splitClipAction
            text: "Split at playhead"
            enabled: editor.canEditSelection && editor.canSplit(clipContextMenu.targetTrackId, clipContextMenu.targetClipId, root.transport.positionBeats)
            implicitHeight: 32
            leftPadding: Theme.margin
            contentItem: Text { text: splitClipAction.text; color: !splitClipAction.enabled ? Theme.textMuted : splitClipAction.highlighted ? Theme.accent : Theme.text; font.pixelSize: Theme.bodySize; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: splitClipAction.highlighted ? Theme.tooltipBg : "transparent"; radius: 4 }
            onTriggered: root.trackModel.splitClipAtBeat(clipContextMenu.targetTrackId, clipContextMenu.targetClipId, root.transport.positionBeats)
        }
        MenuItem {
            objectName: "mergeSelectedClips"
            text: "Merge selected clips"
            enabled: root.canMergeSelection
            onTriggered: root.trackModel.mergeSelectedClips(root.selectedClipIds)
        }
        MenuItem {
            id: deleteClipAction
            text: root.selectedClipIds.length > 1 ? "Delete selected clips" : "Delete"
            enabled: editor.canEditSelection
            implicitHeight: 32
            leftPadding: Theme.margin
            rightPadding: Theme.margin
            contentItem: Text {
                text: deleteClipAction.text
                color: deleteClipAction.highlighted ? Theme.accent : Theme.text
                font.pixelSize: Theme.bodySize
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: deleteClipAction.highlighted ? Theme.tooltipBg : "transparent"
                radius: 4
            }
            onTriggered: {
                if (root.trackModel)
                    root.trackModel.editClips(root.selectedClipIds, "delete", 0)
            }
        }
    }

    Connections {
        target: root.transport
        function onTransportChanged() {
            const returnedToStart = root.playhead === 0 && root.lastTransportPlayhead > 0
            if (returnedToStart && (!root.playing || root.followPlayhead)) {
                scroll.contentX = 0
                if (!root.playing) root.followPlayhead = true
            } else if (root.followPlayhead && root.playing && root.playhead > scroll.contentX + scroll.width * 0.82)
                scroll.contentX = Math.max(0, Math.min(root.playhead - scroll.width * 0.82,
                                                     scroll.contentWidth - scroll.width))
            root.lastTransportPlayhead = root.playhead
        }
    }

    Rectangle { anchors.fill: parent; color: Theme.windowBg }
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        blocking: true
        target: null
        onWheel: event => {
            const delta = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y
            if (event.modifiers & Qt.ShiftModifier) root.scrollHorizontally(-delta)
            else root.zoomAtCursor(Math.pow(1.12, delta / 120), point.position.x)
        }
    }
    WheelHandler {
        orientation: Qt.Horizontal
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        blocking: true
        target: null
        onWheel: event => root.scrollHorizontally(-(event.pixelDelta.x !== 0 ? event.pixelDelta.x : event.angleDelta.x))
    }

    Flickable {
        id: scroll
        objectName: "timelineScroll"
        anchors.fill: parent
        clip: true
        contentWidth: root.contentWidth
        contentHeight: Math.max(height, root.rulerHeight + root.laneCount * root.laneHeight)
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick
        interactive: false

        Rectangle { x: 0; y: 0; width: root.contentWidth; height: root.rulerHeight; color: "#252525" }
        Repeater {
            model: root.visibleBarCount
            delegate: Item {
                x: (root.firstVisibleBar + index * root.barLabelStride) * root.barWidth
                y: 0
                width: root.barWidth
                height: scroll.contentHeight
                Rectangle { width: 1; height: parent.height; color: "#4a4a4a" }
                Text {
                    x: 8; y: 11
                    text: editor.barLabel(root.firstVisibleBar + index * root.barLabelStride)
                    color: Theme.textMuted
                    font.pixelSize: 11
                }
            }
        }

        Repeater {
            model: root.laneCount
            delegate: Rectangle {
                readonly property bool isSelected: !!root.trackModel && root.trackModel.indexOfTrack(root.selectedTrackId) === index
                x: 0
                y: root.rulerHeight + index * root.laneHeight
                width: root.contentWidth
                height: root.laneHeight
                color: isSelected ? "#232932" : (index % 2 ? "#1c1c1c" : "#202020")
                border.color: isSelected ? "#3a4454" : "#303030"
            }
        }

        // Draw beat dividers over the lane backgrounds so every track shows the grid.
        Repeater {
            model: root.trackModel
            delegate: Rectangle {
                required property int index
                required property int trackId
                required property color trackColour
                objectName: "timelineLaneColour" + trackId
                x: 0; y: root.rulerHeight + index * root.laneHeight
                width: 3; height: root.laneHeight
                color: trackColour
                z: 2
            }
        }
        Repeater {
            model: root.visibleBeatCount
            delegate: Rectangle {
                property real beatNumber: root.firstVisibleBeat + index * root.beatGridStride
                x: beatNumber * root.gridUnitWidth
                y: root.rulerHeight
                width: 1
                height: scroll.contentHeight - root.rulerHeight
                color: editor.isBarLine(beatNumber * root.gridStepBeats, editor.beatsPerBar) ? "#454545" : "#303030"
                z: 1
            }
        }

        // Only actual clips from the project model are drawn in the arrangement.
        Repeater {
            model: root.trackModel ? root.trackModel.clips : []
            delegate: Rectangle {
                id: clipItem
                required property int clipId
                required property int laneIndex
                required property int trackId
                required property real startBeat
                required property real lengthBeats
                required property string label
                required property bool effectsPrinted
                required property color trackColour
                required property bool isRecorded
                required property var notes
                objectName: "clipBlock" + clipId
                readonly property var preview: editor.previews[String(clipId)] ?? ({})
                x: (preview.startBeat ?? startBeat) * root.beatWidth + 4
                y: root.rulerHeight + (preview.laneIndex ?? laneIndex) * root.laneHeight + 8
                width: Math.max(4, (preview.lengthBeats ?? lengthBeats) * root.beatWidth - 8)
                height: root.laneHeight - 18
                radius: 4
                color: Qt.darker(trackColour, 2.4)
                border.color: (preview.selected ?? false) ? Theme.accent : trackColour
                border.width: (preview.selected ?? false) ? 2 : 1
                z: 3
                Text {
                    anchors.left: parent.left; anchors.leftMargin: 10
                    anchors.top: parent.top; anchors.topMargin: 8
                    text: label + (clipItem.effectsPrinted ? "" : " · Dry"); color: "#f0f0f0"; font.pixelSize: 11
                    elide: Text.ElideRight
                    width: Math.max(0, parent.width - 20)
                }
                Canvas {
                    id: waveform
                    objectName: "clipWaveform" + clipItem.clipId
                    x: Math.max(0, scroll.contentX - clipItem.x)
                    y: 30
                    width: Math.max(0, Math.min(clipItem.width - x, scroll.width))
                    height: Math.max(0, clipItem.height - 38)
                    property var peaks: width > 0 && root.trackModel
                        ? root.trackModel.clipWaveform(clipItem.trackId, clipItem.clipId, Math.ceil(width),
                            x / clipItem.width, Math.min(1, (x + width) / clipItem.width)) : []
                    onPeaksChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        ctx.strokeStyle = "#d6edf0"
                        ctx.lineWidth = 1
                        ctx.beginPath()
                        for (let i = 0; i < peaks.length; ++i) {
                            const px = (i + 0.5) * width / peaks.length
                            ctx.moveTo(px, height * (0.5 - Math.min(1, peaks[i][1]) * 0.48))
                            ctx.lineTo(px, height * (0.5 - Math.max(-1, peaks[i][0]) * 0.48) + 1)
                        }
                        ctx.stroke()
                    }
                }
                // Reaper-style outlines of the pattern's notes. Vertical scale fits the clip's own
                // pitch range; only the visible part of the clip is painted (like the waveform).
                Canvas {
                    id: noteOutlines
                    objectName: "clipNotes" + clipItem.clipId
                    visible: !clipItem.isRecorded && clipItem.notes.length > 0
                    x: Math.max(0, scroll.contentX - clipItem.x)
                    y: 26
                    width: visible ? Math.max(0, Math.min(clipItem.width - x, scroll.width)) : 0
                    height: Math.max(0, clipItem.height - 30)
                    // Left-edge trim previews keep notes fixed in time; moves carry them along.
                    readonly property real trimShift: root.editOperation === "trimStart"
                        ? (clipItem.preview.startBeat ?? clipItem.startBeat) - clipItem.startBeat : 0
                    readonly property real visibleLength: clipItem.preview.lengthBeats ?? clipItem.lengthBeats
                    readonly property color noteColour: Qt.lighter(clipItem.trackColour, 1.7)
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        const shown = []
                        let lo = 127, hi = 0
                        for (let i = 0; i < clipItem.notes.length; ++i) {
                            const n = clipItem.notes[i]
                            const start = n.startBeat - trimShift
                            if (start + n.lengthBeats <= 0 || start >= visibleLength) continue // hidden
                            shown.push({ start: start, length: n.lengthBeats, note: n.note })
                            lo = Math.min(lo, n.note); hi = Math.max(hi, n.note)
                        }
                        if (shown.length === 0) return
                        // At least an octave of rows so a single note doesn't fill the clip
                        const rows = Math.max(12, hi - lo + 1)
                        const base = lo - Math.floor((rows - (hi - lo + 1)) / 2)
                        const rowH = height / rows
                        const barH = Math.max(1.5, Math.min(6, rowH - 1))
                        ctx.fillStyle = Qt.rgba(noteColour.r, noteColour.g, noteColour.b, 0.55)
                        ctx.strokeStyle = noteColour
                        ctx.lineWidth = 1
                        for (const n of shown) {
                            // Clip-local x (the block is drawn 4px right of its start beat)
                            const x0 = Math.max(0, n.start * root.beatWidth - 4) - x
                            const x1 = Math.min(clipItem.width, (n.start + n.length) * root.beatWidth - 4) - x
                            if (x1 <= 0 || x0 >= width) continue
                            const ny = (rows - 1 - (n.note - base)) * rowH + (rowH - barH) / 2
                            const w = Math.max(2, x1 - x0)
                            ctx.fillRect(x0, ny, w, barH)
                            ctx.strokeRect(Math.round(x0) + 0.5, Math.round(ny) + 0.5, Math.max(1, Math.round(w) - 1), Math.max(1, Math.round(barH) - 1))
                        }
                    }
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onXChanged: requestPaint()
                    onTrimShiftChanged: requestPaint()
                    onVisibleLengthChanged: requestPaint()
                    onNoteColourChanged: requestPaint()
                    Connections {
                        target: clipItem
                        function onNotesChanged() { noteOutlines.requestPaint() }
                    }
                    Connections {
                        target: root
                        function onBeatWidthChanged() { noteOutlines.requestPaint() }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: pressedButtons & Qt.LeftButton ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    property real pointerStartX: 0
                    property real originalBeat: 0
                    property int originalLane: 0
                    onPressed: mouse => {
                        if (mouse.button === Qt.LeftButton && (mouse.modifiers & Qt.ShiftModifier)) {
                            const point = mapToItem(scroll.contentItem, mouse.x, mouse.y)
                            root.beginSelection(point.x, point.y-root.rulerHeight, true)
                            return
                        }
                        editor.selectClip(clipItem.trackId, clipItem.clipId,
                            mouse.button === Qt.LeftButton && !!(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)),
                            true)
                        root.forceActiveFocus()
                        pointerStartX = mapToItem(scroll.contentItem, mouse.x, mouse.y).x
                        originalBeat = clipItem.startBeat; originalLane = clipItem.laneIndex
                        if (mouse.button === Qt.LeftButton && !(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) {
                            editor.beginEdit("move")
                        }
                    }
                    onPositionChanged: mouse => {
                        if (root.selecting && root.clipSelectionDrag) {
                            const point = mapToItem(scroll.contentItem, mouse.x, mouse.y)
                            root.updateSelection(point.x, point.y-root.rulerHeight)
                            return
                        }
                        if (!(pressedButtons & Qt.LeftButton) || root.recording || (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) return
                        const delta = mapToItem(scroll.contentItem, mouse.x, mouse.y).x - pointerStartX
                        const pointerY = mapToItem(scroll.contentItem, mouse.x, mouse.y).y
                        editor.updateGroupLane(Math.floor((pointerY-root.rulerHeight)/root.laneHeight)-originalLane)
                        const verticalOnly = root.editLaneDelta !== 0 && Math.abs(delta) < Qt.styleHints.startDragDistance
                        editor.updateDrag(originalBeat, delta / root.beatWidth, verticalOnly)
                    }
                    onReleased: mouse => {
                        if (mouse.button === Qt.LeftButton) {
                            if (root.selecting) root.finishSelection()
                            else editor.commitGroupEdit()
                        }
                    }
                    onCanceled: { editor.cancelEdit(); editor.cancelSelection() }
                    onClicked: mouse => {
                        if (mouse.button !== Qt.RightButton) return
                        clipContextMenu.targetTrackId = clipItem.trackId
                        clipContextMenu.targetClipId = clipItem.clipId
                        clipContextMenu.targetStart = clipItem.startBeat
                        clipContextMenu.targetLength = clipItem.lengthBeats
                        clipContextMenu.targetIsRecorded = clipItem.isRecorded
                        clipContextMenu.targetLabel = clipItem.label
                        const position = mapToItem(root, mouse.x, mouse.y)
                        clipContextMenu.popup(position.x, position.y)
                    }
                    onDoubleClicked: mouse => {
                        if (mouse.button === Qt.LeftButton && !clipItem.isRecorded) {
                            root.openPianoRollRequested(clipItem.trackId, clipItem.clipId,
                                clipItem.label, clipItem.lengthBeats)
                        }
                    }
                }
                Rectangle {
                    anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                    width: 8; radius: 3; color: leftTrim.containsMouse ? Theme.accent : "#ffffff24"
                    MouseArea {
                        id: leftTrim
                        objectName: "clipLeftTrim" + clipItem.clipId
                        anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.LeftButton
                        cursorShape: Qt.SizeHorCursor
                        property real pointerStart: 0
                        property real originalStart: 0
                        property real originalEnd: 0
                        onPressed: mouse => {
                            editor.selectClip(clipItem.trackId, clipItem.clipId, false, true)
                            pointerStart = mapToItem(scroll.contentItem, mouse.x, mouse.y).x
                            originalStart = clipItem.startBeat
                            editor.beginEdit("trimStart")
                        }
                        onPositionChanged: mouse => {
                            if (!pressed || root.recording) return
                            const delta = (mapToItem(scroll.contentItem, mouse.x, mouse.y).x - pointerStart) / root.beatWidth
                            editor.updateDrag(originalStart, delta)
                        }
                        onReleased: editor.commitGroupEdit()
                        onCanceled: { editor.cancelEdit() }
                        ToolTip.visible: containsMouse; ToolTip.text: "Drag to trim the start"
                    }
                }
                Rectangle {
                    id: resizeGrip
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    width: 8
                    color: resizeArea.containsMouse ? Theme.accent : "#ffffff24"
                    radius: 3
                    MouseArea {
                        id: resizeArea
                        objectName: "clipRightTrim" + clipItem.clipId
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton
                        cursorShape: Qt.SizeHorCursor
                        property real pointerStartX: 0
                        property real lengthStart: 0

                        function updatePreview(mouseX) {
                            const pointerX = mapToItem(scroll.contentItem, mouseX, height / 2).x
                            const delta = (pointerX - pointerStartX) / root.beatWidth
                            editor.updateDrag(lengthStart, delta)
                        }

                        onPressed: mouse => {
                            editor.selectClip(clipItem.trackId, clipItem.clipId, false, true)
                            pointerStartX = mapToItem(scroll.contentItem, mouse.x, mouse.y).x
                            lengthStart = editor.clipEndBeat(clipItem.trackId, clipItem.clipId)
                            editor.beginEdit("resize")
                        }
                        onPositionChanged: mouse => {
                            if (pressed)
                                updatePreview(mouse.x)
                        }
                        onReleased: mouse => {
                            updatePreview(mouse.x)
                            editor.commitGroupEdit()
                        }
                        onCanceled: { editor.cancelEdit() }
                        ToolTip.visible: containsMouse
                        ToolTip.text: root.snapStepBeats > 0 ? "Drag to trim or extend on the snap grid" : "Drag to trim or extend freely"
                        ToolTip.delay: 450
                    }
                }
            }
        }

        MouseArea {
            x: 0; y: 0; width: root.contentWidth; height: root.rulerHeight; z: 3
            acceptedButtons: Qt.LeftButton
            onPressed: mouse => editor.seek(mouse.x / root.beatWidth)
            onPositionChanged: mouse => { if (pressed) editor.seek(mouse.x / root.beatWidth) }
            cursorShape: Qt.PointingHandCursor
        }

        // Dragging selects a range. Navigation is through the scrollbars, not by dragging
        // the arrangement background.
        MouseArea {
            x: 0; y: root.rulerHeight; width: root.contentWidth; height: scroll.contentHeight - root.rulerHeight; z: 1
            acceptedButtons: Qt.LeftButton
            onPressed: mouse => {
                const lane = Math.floor(mouse.y / root.laneHeight)
                if (root.trackModel && lane >= 0 && lane < root.trackModel.count) {
                    root.trackModel.selectTrack(root.trackModel.trackIdAt(lane))
                }
                root.beginSelection(mouse.x, mouse.y, !!(mouse.modifiers & Qt.ShiftModifier))
            }
            onPositionChanged: mouse => {
                if (pressed) { root.updateSelection(mouse.x, mouse.y) }
            }
            onReleased: root.finishSelection()
            onCanceled: { editor.cancelSelection() }
            onDoubleClicked: mouse => editor.createClipAt(mouse.x / root.beatWidth, Math.floor(mouse.y / root.laneHeight))
        }
        Rectangle {
            objectName: "timelineSelection"
            x: Math.min(root.selectionStartX, root.selectionEndX)
            y: root.rulerHeight + Math.min(root.selectionStartY, root.selectionEndY)
            width: Math.abs(root.selectionEndX-root.selectionStartX)
            height: Math.max(1,Math.abs(root.selectionEndY-root.selectionStartY))
            visible: root.clipSelectionDrag && root.selecting && width>2
            color: "#3860cdff"; border.color: Theme.accent; z: 2
        }
        Rectangle {
            objectName: "timelineTimeRange"
            x: root.rangeStartBeat*root.beatWidth; y: root.rulerHeight
            width: Math.max(0,root.rangeEndBeat-root.rangeStartBeat)*root.beatWidth
            height: scroll.contentHeight-root.rulerHeight
            visible: width>0
            color: "#2858b7b1"; border.color: Theme.accent; z: 2
        }

        Repeater {
          model: root.recording ? root.transport.recordingTracks : []
          delegate: Rectangle {
            required property int modelData
            objectName: "recordingPreview" + modelData
            visible: !!root.trackModel
            x: root.transport.recordingStartBeats * root.beatWidth
            y: root.rulerHeight + (root.trackModel ? root.trackModel.indexOfTrack(modelData) : 0) * root.laneHeight + 8
            width: Math.max(2, root.playhead - x)
            height: root.laneHeight - 18
            color: "#663238"; border.color: Theme.record; radius: 4; z: 3
            Text { anchors.left: parent.left; anchors.leftMargin: 8; anchors.top: parent.top; anchors.topMargin: 8; text: "Recording…"; color: Theme.text }
          }
        }
        Rectangle { objectName: "timelinePlayhead"; x: root.playhead; y: 0; width: 2; height: scroll.contentHeight; color: Theme.record; z: 4 }

        ScrollBar.horizontal: ScrollBar {
            objectName: "timelineHorizontalScroll"
            policy: ScrollBar.AlwaysOn
            onPressedChanged: { if (pressed) root.followPlayhead = false }
        }
        ScrollBar.vertical: ScrollBar {
            objectName: "timelineVerticalScroll"
            policy: ScrollBar.AlwaysOn
            onPressedChanged: { if (pressed) root.followPlayhead = false }
        }
    }

}
