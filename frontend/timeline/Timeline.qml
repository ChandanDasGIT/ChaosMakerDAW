import QtQuick
import QtQuick.Controls

Item {
    id: root
    property int timeSignature: 4
    property int timeSignatureDenominator: 4
    property real bpm: 120
    property real beatWidth: 48
    readonly property real playhead: EngineController.positionBeats * beatWidth
    readonly property bool playing: EngineController.playing
    readonly property bool recording: EngineController.recording
    readonly property bool repeating: EngineController.repeating
    readonly property real recordingTailX: playing ? playhead + width * 0.2 : 0
    property bool selecting: false
    property bool hasSelection: false
    property real selectionStartX: 0
    property real selectionEndX: 0
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
        EngineController.seekBeat(Math.max(0, position / beatWidth))
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

    function zoomAtCursor(factor, cursorX) {
        const anchor = scroll.contentX + cursorX
        const nextBeatWidth = Math.max(0.000001, beatWidth * factor)
        const scale = nextBeatWidth / beatWidth
        beatWidth = nextBeatWidth
        scroll.contentX = Math.max(0, Math.min(scroll.contentWidth - scroll.width, anchor * scale - cursorX))
    }

    Connections {
        target: EngineController
        function onTransportChanged() {
            if (root.playing && root.playhead > scroll.contentX + scroll.width * 0.82)
                scroll.contentX = Math.max(0, Math.min(root.playhead - scroll.width * 0.82,
                                                     scroll.contentWidth - scroll.width))
            if (root.playhead === 0) scroll.contentX = 0
        }
    }

    Rectangle { anchors.fill: parent; color: Theme.windowBg }
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        blocking: true
        target: null
        onWheel: event => {
            const delta = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y
            root.zoomAtCursor(Math.pow(1.12, delta / 120), point.position.x)
        }
    }

    Flickable {
        id: scroll
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
                property real previewLengthBeats: lengthBeats

                onLengthBeatsChanged: previewLengthBeats = lengthBeats

                x: startBeat * root.beatWidth + 4
                y: root.rulerHeight + laneIndex * root.laneHeight + 8
                width: Math.max(4, previewLengthBeats * root.beatWidth - 8)
                height: root.laneHeight - 18
                radius: 4
                color: ["#245b62", "#665238", "#4e466e"][clipId % 3]
                border.color: ["#388b95", "#9a794a", "#7770a3"][clipId % 3]
                z: 3
                Text {
                    anchors.left: parent.left; anchors.leftMargin: 10
                    anchors.top: parent.top; anchors.topMargin: 8
                    text: label; color: "#f0f0f0"; font.pixelSize: 11
                    elide: Text.ElideRight
                    width: Math.max(0, parent.width - 20)
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
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton
                        cursorShape: Qt.SizeHorCursor
                        property real pointerStartX: 0
                        property real lengthStart: 0

                        function updatePreview(mouseX) {
                            const pointerX = mapToItem(scroll.contentItem, mouseX, height / 2).x
                            const desiredWidth = lengthStart * root.beatWidth - 8 + pointerX - pointerStartX
                            const beatStep = 4.0 / root.timeSignatureDenominator
                            const snappedLength = Math.max(beatStep,
                                Math.round((desiredWidth + 8) / (root.beatWidth * beatStep)) * beatStep)
                            clipItem.previewLengthBeats = snappedLength
                        }

                        onPressed: mouse => {
                            pointerStartX = mapToItem(scroll.contentItem, mouse.x, mouse.y).x
                            lengthStart = clipItem.lengthBeats
                        }
                        onPositionChanged: mouse => {
                            if (pressed)
                                updatePreview(mouse.x)
                        }
                        onReleased: mouse => {
                            updatePreview(mouse.x)
                            const resized = root.trackModel.resizeClipAtBeat(clipItem.trackId, clipItem.clipId,
                                                                              clipItem.previewLengthBeats)
                            if (!resized)
                                clipItem.previewLengthBeats = clipItem.lengthBeats
                        }
                        onCanceled: clipItem.previewLengthBeats = clipItem.lengthBeats
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
            onPressed: mouse => {
                root.selecting = true
                root.hasSelection = true
                root.selectionStartX = mouse.x
                root.selectionEndX = mouse.x
            }
            onPositionChanged: mouse => {
                if (pressed) {
                    root.selectionEndX = mouse.x
                }
            }
            onReleased: root.selecting = false
            onDoubleClicked: mouse => root.createClipAt(mouse.x, mouse.y)
        }
        Rectangle {
            x: Math.min(root.selectionStartX, root.selectionEndX)
            y: root.rulerHeight
            width: Math.abs(root.selectionEndX - root.selectionStartX)
            height: scroll.contentHeight - root.rulerHeight
            visible: root.hasSelection && width > 2
            color: "#3860cdff"
            border.color: Theme.accent
            z: 2
        }

        Rectangle {
            visible: root.recording && root.trackModel
            x: EngineController.recordingStartBeats * root.beatWidth
            y: root.rulerHeight + (root.trackModel ? root.trackModel.indexOfTrack(EngineController.recordingTrackId) : 0) * root.laneHeight + 8
            width: Math.max(2, root.playhead - x)
            height: root.laneHeight - 18
            color: "#663238"; border.color: Theme.record; radius: 4; z: 3
            Text { anchors.left: parent.left; anchors.leftMargin: 8; anchors.top: parent.top; anchors.topMargin: 8; text: "Recording…"; color: Theme.text }
        }
        Rectangle { x: root.playhead; y: 0; width: 2; height: scroll.contentHeight; color: Theme.record; z: 4 }

        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOn }
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn }
    }

}
