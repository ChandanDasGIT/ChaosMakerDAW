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

    Shortcut {
        sequence: "Delete"
        enabled: root.canEditClipSelection
        autoRepeat: false
        onActivated: root.trackModel.editClips(root.selectedClipIds, "delete", 0)
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
    readonly property real barLabelStride: Math.max(1, Math.ceil(48 / barWidth))
    readonly property real firstVisibleBar: Math.floor(scroll.contentX / (barWidth * barLabelStride)) * barLabelStride
    readonly property int visibleBarCount: Math.ceil(width / (barWidth * barLabelStride)) + 3
    readonly property real gridStepBeats: editor.gridStepBeats
    readonly property real gridUnitWidth: gridStepBeats * beatWidth
    readonly property real beatGridStride: Math.max(1, Math.ceil(8 / gridUnitWidth))
    readonly property real firstVisibleBeat: Math.floor(scroll.contentX / (gridUnitWidth * beatGridStride)) * beatGridStride
    readonly property int visibleBeatCount: Math.ceil(width / (gridUnitWidth * beatGridStride)) + 3

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

        background: Rectangle {
            color: Theme.panelBg
            border.color: Theme.divider
            radius: 6
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
                x: 0
                y: root.rulerHeight + index * root.laneHeight
                width: root.contentWidth
                height: root.laneHeight
                color: index % 2 ? "#1c1c1c" : "#202020"
                border.color: "#303030"
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
                        const position = mapToItem(root, mouse.x, mouse.y)
                        clipContextMenu.popup(position.x, position.y)
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
            onPressed: mouse => root.beginSelection(mouse.x, mouse.y, !!(mouse.modifiers & Qt.ShiftModifier))
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
