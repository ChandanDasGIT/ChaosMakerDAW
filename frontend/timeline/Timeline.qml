import QtQuick
import QtQuick.Controls

Item {
    id: root
    property var transport: EngineController
    property int timeSignature: 4
    property int timeSignatureDenominator: 4
    property real bpm: 120
    // Zoom is measured in time so tempo changes redraw the musical grid without
    // stretching recorded audio. Keep zoom separate from the BPM binding.
    property real pixelsPerSecond: 96
    readonly property real beatWidth: pixelsPerSecond * 60 / bpm
    readonly property real playhead: root.transport.positionBeats * beatWidth
    readonly property bool playing: root.transport.playing
    readonly property bool recording: root.transport.recording
    readonly property bool repeating: root.transport.repeating
    readonly property real recordingTailX: playhead + width * 0.2
    property bool followPlayhead: true
    property real lastTransportPlayhead: 0
    onPlayingChanged: { if (playing) followPlayhead = true }
    property var selectedClipIds: []
    property string editOperation: ""
    property real editDelta: 0
    function updateGroupEdit(delta) {
        const clips = trackModel.clips.filter(c => selectedClipIds.indexOf(c.clipId) >= 0)
        for (const clip of clips) {
            if (editOperation === "move" || editOperation === "trimStart") delta = Math.max(delta, -clip.startBeat)
            if (editOperation === "resize") delta = Math.max(delta, 4 / timeSignatureDenominator - clip.lengthBeats)
            if (editOperation === "trimStart") delta = Math.min(delta, clip.lengthBeats - 4 / timeSignatureDenominator)
        }
        editDelta = delta
    }
    function commitGroupEdit() {
        const operation = editOperation, delta = editDelta
        editOperation = ""; editDelta = 0
        if (operation && trackModel) trackModel.editClips(selectedClipIds, operation, delta)
    }
    Shortcut {
        sequence: "Delete"
        enabled: root.activeFocus && root.selectedClipIds.length > 0 && !root.recording
        autoRepeat: false
        onActivated: root.trackModel.editClips(root.selectedClipIds, "delete", 0)
    }
    readonly property bool canMergeSelection: {
        if (!trackModel || selectedClipIds.length !== 2) return false
        const clips = trackModel.clips
        const first = clips.find(c => c.clipId === selectedClipIds[0])
        return !!first && trackModel.canMergeClips(first.trackId, selectedClipIds[0], selectedClipIds[1])
    }
    function selectClip(trackId, clipId, toggle, preserve) {
        const ids = selectedClipIds.slice()
        const at = ids.indexOf(clipId)
        if (toggle) {
            if (at >= 0) ids.splice(at, 1)
            else ids.push(clipId)
            selectedClipIds = ids
        } else if (!preserve || at < 0) selectedClipIds = [clipId]
        selectedClipId = selectedClipIds.indexOf(clipId) >= 0 ? clipId : -1
        selectedTrackId = selectedClipId >= 0 ? trackId : -1
        hasSelection = false
    }
    onTrackModelChanged: {
        selectedClipIds = []; selectedClipId = -1; selectedTrackId = -1
        timeRangeStartBeat = 0; timeRangeEndBeat = 0; hasSelection = false
    }
    Connections {
        target: root.trackModel || null
        function onClipsChanged() {
            const clips = root.trackModel.clips
            root.selectedClipIds = root.selectedClipIds.filter(id => clips.some(c => c.clipId === id))
            if (root.selectedClipIds.indexOf(root.selectedClipId) < 0) {
                root.selectedClipId = -1; root.selectedTrackId = -1
            }
        }
    }
    property int selectedClipId: -1
    property int selectedTrackId: -1
    property bool selecting: false
    property bool hasSelection: false
    property bool clipSelectionDrag: false
    property real timeRangeStartBeat: 0
    property real timeRangeEndBeat: 0
    readonly property real rangeStartBeat: selecting && !clipSelectionDrag
        ? Math.floor(Math.min(selectionStartX, selectionEndX) / beatUnitWidth) * 4 / timeSignatureDenominator : timeRangeStartBeat
    readonly property real rangeEndBeat: selecting && !clipSelectionDrag && Math.abs(selectionEndX-selectionStartX)>2
        ? Math.ceil(Math.max(selectionStartX, selectionEndX) / beatUnitWidth) * 4 / timeSignatureDenominator : timeRangeEndBeat
    function beginSelection(x, y, clips) {
        selectedClipIds = []; selectedClipId = -1; selectedTrackId = -1
        selecting = true; hasSelection = true; clipSelectionDrag = clips
        forceActiveFocus()
        selectionStartX = x; selectionEndX = x
        selectionStartY = y; selectionEndY = y
    }
    function finishSelection() {
        if (!clipSelectionDrag) {
            const start = rangeStartBeat, end = Math.abs(selectionEndX-selectionStartX)>2 ? rangeEndBeat : 0
            if (!recording && transport.setRepeatRange(end>start ? start : 0, end)) {
                timeRangeStartBeat = end>start ? start : 0; timeRangeEndBeat = end
            }
            selecting = false
            return
        }
        selecting = false
        const left = Math.min(selectionStartX, selectionEndX) / beatWidth
        const right = Math.max(selectionStartX, selectionEndX) / beatWidth
        const top = Math.min(selectionStartY, selectionEndY)
        const bottom = Math.max(selectionStartY, selectionEndY) + 1
        if (trackModel && right > left)
            selectedClipIds = trackModel.clips.filter(c => c.startBeat < right && c.startBeat+c.lengthBeats > left
                && c.laneIndex*laneHeight+8 < bottom && (c.laneIndex+1)*laneHeight-10 > top).map(c => c.clipId)
    }
    property real selectionStartX: 0
    property real selectionEndX: 0
    property real selectionStartY: 0
    property real selectionEndY: 0
    property var trackModel
    property int laneCount: 5
    property int laneHeight: 106
    property int rulerHeight: 38
    readonly property real verticalScrollPosition: scroll.contentY
    readonly property real beatUnitWidth: beatWidth * 4 / timeSignatureDenominator
    readonly property real barWidth: beatUnitWidth * timeSignature
    readonly property real contentEndBars: trackModel
        ? trackModel.contentEndBeats / (timeSignature * 4.0 / timeSignatureDenominator)
        : 0
    readonly property real contentWidth: Math.max(width, (Math.ceil(contentEndBars) + 1) * barWidth, recordingTailX)
    readonly property real barLabelStride: Math.max(1, Math.ceil(48 / barWidth))
    readonly property real firstVisibleBar: Math.floor(scroll.contentX / (barWidth * barLabelStride)) * barLabelStride
    readonly property int visibleBarCount: Math.ceil(width / (barWidth * barLabelStride)) + 3
    readonly property real beatGridStride: Math.max(1, Math.ceil(8 / beatUnitWidth))
    readonly property real firstVisibleBeat: Math.floor(scroll.contentX / (beatUnitWidth * beatGridStride)) * beatGridStride
    readonly property int visibleBeatCount: Math.ceil(width / (beatUnitWidth * beatGridStride)) + 3

    function setPlayhead(position) {
        root.transport.seekBeat(Math.max(0, position / beatWidth))
    }

    function hasClipOverlap(trackId, startBeat, lengthBeats) {
        if (!trackModel)
            return false
        const endBeat = startBeat + lengthBeats
        for (let i = 0; i < trackModel.clips.length; ++i) {
            const clip = trackModel.clips[i]
            if (clip.trackId === trackId
                && startBeat < clip.startBeat + clip.lengthBeats
                && endBeat > clip.startBeat)
                return true
        }
        return false
    }

    function createClipAt(x, y) {
        if (!trackModel)
            return
        const lane = Math.floor(y / laneHeight)
        if (lane < 0 || lane >= laneCount)
            return
        const trackId = trackModel.trackIdAt(lane)
        const beatsPerBar = timeSignature * 4.0 / timeSignatureDenominator
        const barStartBeat = Math.floor(x / barWidth) * beatsPerBar
        if (trackId >= 0 && !hasClipOverlap(trackId, barStartBeat, beatsPerBar))
            trackModel.addClipAtBeat(trackId, barStartBeat, beatsPerBar)
    }

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
        enabled: root.activeFocus && root.selectedClipIds.length === 1 && root.selectedClipId >= 0 && !root.recording
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
            enabled: !root.recording && root.transport.positionBeats > clipContextMenu.targetStart
                     && root.transport.positionBeats < clipContextMenu.targetStart + clipContextMenu.targetLength
            implicitHeight: 32
            leftPadding: Theme.margin
            contentItem: Text { text: splitClipAction.text; color: !splitClipAction.enabled ? Theme.textMuted : splitClipAction.highlighted ? Theme.accent : Theme.text; font.pixelSize: Theme.bodySize; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: splitClipAction.highlighted ? Theme.tooltipBg : "transparent"; radius: 4 }
            onTriggered: root.trackModel.splitClipAtBeat(clipContextMenu.targetTrackId, clipContextMenu.targetClipId, root.transport.positionBeats)
        }
        MenuItem {
            objectName: "mergeSelectedClips"
            text: "Merge selected clips"
            enabled: root.canMergeSelection && !root.recording
            onTriggered: {
                const first = root.trackModel.clips.find(c => c.clipId === root.selectedClipIds[0])
                if (first) root.trackModel.mergeClips(first.trackId, root.selectedClipIds[0], root.selectedClipIds[1])
            }
        }
        MenuItem {
            id: deleteClipAction
            text: root.selectedClipIds.length > 1 ? "Delete selected clips" : "Delete"
            enabled: !root.recording
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
                    text: (root.firstVisibleBar + index * root.barLabelStride + 1).toString()
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
                x: beatNumber * root.beatUnitWidth
                y: root.rulerHeight
                width: 1
                height: scroll.contentHeight - root.rulerHeight
                color: beatNumber % root.timeSignature === 0 ? "#454545" : "#303030"
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
                property real previewStartBeat: startBeat
                property real previewLengthBeats: lengthBeats
                onStartBeatChanged: previewStartBeat = startBeat

                onLengthBeatsChanged: previewLengthBeats = lengthBeats

                readonly property bool groupEditing: root.editOperation !== "" && root.selectedClipIds.indexOf(clipId) >= 0
                x: (groupEditing && (root.editOperation === "move" || root.editOperation === "trimStart")
                    ? startBeat + root.editDelta : previewStartBeat) * root.beatWidth + 4
                y: root.rulerHeight + laneIndex * root.laneHeight + 8
                width: Math.max(4, (groupEditing && root.editOperation === "resize" ? lengthBeats + root.editDelta
                    : groupEditing && root.editOperation === "trimStart" ? lengthBeats - root.editDelta
                    : previewLengthBeats) * root.beatWidth - 8)
                height: root.laneHeight - 18
                radius: 4
                color: Qt.darker(trackColour, 2.4)
                border.color: root.selectedClipIds.indexOf(clipId) >= 0 ? Theme.accent : trackColour
                border.width: root.selectedClipIds.indexOf(clipId) >= 0 ? 2 : 1
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
                    onPressed: mouse => {
                        if (mouse.button === Qt.LeftButton && (mouse.modifiers & Qt.ShiftModifier)) {
                            const point = mapToItem(scroll.contentItem, mouse.x, mouse.y)
                            root.beginSelection(point.x, point.y-root.rulerHeight, true)
                            return
                        }
                        root.selectClip(clipItem.trackId, clipItem.clipId,
                            mouse.button === Qt.LeftButton && !!(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)),
                            true)
                        root.forceActiveFocus()
                        pointerStartX = mapToItem(scroll.contentItem, mouse.x, mouse.y).x
                        originalBeat = clipItem.startBeat
                        if (mouse.button === Qt.LeftButton && !(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) {
                            root.editOperation = "move"; root.editDelta = 0
                        }
                    }
                    onPositionChanged: mouse => {
                        if (root.selecting && root.clipSelectionDrag) {
                            const point = mapToItem(scroll.contentItem, mouse.x, mouse.y)
                            root.selectionEndX = point.x; root.selectionEndY = point.y-root.rulerHeight
                            return
                        }
                        if (!(pressedButtons & Qt.LeftButton) || root.recording || (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) return
                        const delta = mapToItem(scroll.contentItem, mouse.x, mouse.y).x - pointerStartX
                        const step = 4.0 / root.timeSignatureDenominator
                        root.updateGroupEdit(Math.round(delta / root.beatWidth / step) * step)
                    }
                    onReleased: mouse => {
                        if (mouse.button === Qt.LeftButton) {
                            if (root.selecting) root.finishSelection()
                            else root.commitGroupEdit()
                        }
                    }
                    onCanceled: { root.editOperation = ""; root.editDelta = 0; root.selecting = false }
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
                            root.selectClip(clipItem.trackId, clipItem.clipId, false, true)
                            pointerStart = mapToItem(scroll.contentItem, mouse.x, mouse.y).x
                            originalStart = clipItem.startBeat; originalEnd = clipItem.startBeat + clipItem.lengthBeats
                            root.editOperation = "trimStart"; root.editDelta = 0
                        }
                        onPositionChanged: mouse => {
                            if (!pressed || root.recording) return
                            const step = 4.0 / root.timeSignatureDenominator
                            const delta = (mapToItem(scroll.contentItem, mouse.x, mouse.y).x - pointerStart) / root.beatWidth
                            root.updateGroupEdit(Math.round(delta / step) * step)
                        }
                        onReleased: root.commitGroupEdit()
                        onCanceled: { root.editOperation = ""; root.editDelta = 0 }
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
                            const step = 4.0 / root.timeSignatureDenominator
                            root.updateGroupEdit(Math.round((pointerX - pointerStartX) / root.beatWidth / step) * step)
                        }

                        onPressed: mouse => {
                            root.selectClip(clipItem.trackId, clipItem.clipId, false, true)
                            pointerStartX = mapToItem(scroll.contentItem, mouse.x, mouse.y).x
                            root.editOperation = "resize"; root.editDelta = 0
                        }
                        onPositionChanged: mouse => {
                            if (pressed)
                                updatePreview(mouse.x)
                        }
                        onReleased: mouse => {
                            updatePreview(mouse.x)
                            root.commitGroupEdit()
                        }
                        onCanceled: { root.editOperation = ""; root.editDelta = 0 }
                        ToolTip.visible: containsMouse
                        ToolTip.text: "Drag to trim or extend by beat"
                        ToolTip.delay: 450
                    }
                }
            }
        }

        MouseArea {
            x: 0; y: 0; width: root.contentWidth; height: root.rulerHeight; z: 3
            acceptedButtons: Qt.LeftButton
            onPressed: mouse => root.setPlayhead(mouse.x)
            onPositionChanged: mouse => { if (pressed) root.setPlayhead(mouse.x) }
            cursorShape: Qt.PointingHandCursor
        }

        // Dragging selects a range. Navigation is through the scrollbars, not by dragging
        // the arrangement background.
        MouseArea {
            x: 0; y: root.rulerHeight; width: root.contentWidth; height: scroll.contentHeight - root.rulerHeight; z: 1
            acceptedButtons: Qt.LeftButton
            onPressed: mouse => root.beginSelection(mouse.x, mouse.y, !!(mouse.modifiers & Qt.ShiftModifier))
            onPositionChanged: mouse => {
                if (pressed) { root.selectionEndX = mouse.x; root.selectionEndY = mouse.y }
            }
            onReleased: root.finishSelection()
            onCanceled: { root.selecting = false; root.hasSelection = false }
            onDoubleClicked: mouse => root.createClipAt(mouse.x, mouse.y)
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
