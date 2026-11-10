import QtQuick
import QtQuick.Controls

// A track's volume envelope (restMap G6): the line, its shaded area and its points. Used in the
// track's own lane under the clips and drawn over the clips; levels are 0 (bottom, -inf dB) ..
// 1 (top, +6 dB) on the backend's fader-shaped scale, so straight lines here are what is heard.
Item {
    id: lane
    property var trackModel: null
    property int envelopeTrackId: -1
    property var points: []          // [{beat, level}] in time order
    property bool bypassed: false
    property bool overClips: false
    property real beatWidth: 1
    property real viewX: 0           // the visible part of the timeline, in content x
    property real viewWidth: 0
    property int selectedPoint: -1
    signal menuRequested(int trackId, real x, real y)

    readonly property real unityLevel: trackModel ? trackModel.envelopeUnityLevel : 0.8
    readonly property color lineColour: bypassed ? Theme.timeline.envelopeBypassed : Theme.timeline.envelopeLine

    function yForLevel(level) { return (1 - level) * height }
    function levelForY(y) { return Math.max(0, Math.min(1, 1 - y / Math.max(1, height))) }
    function levelAtX(x) {
        if (points.length === 0) return unityLevel
        const beat = x / beatWidth
        if (beat <= points[0].beat) return points[0].level
        const last = points[points.length - 1]
        if (beat >= last.beat) return last.level
        for (let i = 1; i < points.length; ++i) {
            if (beat <= points[i].beat) {
                const a = points[i - 1], b = points[i]
                return a.level + (b.level - a.level) * (beat - a.beat) / Math.max(1e-12, b.beat - a.beat)
            }
        }
        return last.level
    }
    // Over the clips, only the pointer near the line belongs to the envelope; the rest (presses
    // and the hand cursor) stays with the clips.
    function nearLine(x, y) { return Math.abs(y - yForLevel(levelAtX(x))) <= 6 }
    HoverHandler { id: hover; enabled: lane.overClips }
    readonly property bool hoverNearLine: hover.hovered && nearLine(hover.point.position.x, hover.point.position.y)

    onPointsChanged: { if (selectedPoint >= points.length) selectedPoint = -1; curve.requestPaint() }
    onBypassedChanged: curve.requestPaint()
    onBeatWidthChanged: curve.requestPaint()
    onHeightChanged: curve.requestPaint()

    Rectangle {
        visible: !lane.overClips
        width: parent.width; height: 1
        color: Theme.timeline.envelopeLaneTop
    }

    // Only the visible part is painted, like the clip waveforms.
    Canvas {
        id: curve
        objectName: "envelopeCurve" + lane.envelopeTrackId
        x: Math.max(0, lane.viewX)
        width: Math.max(0, Math.min(lane.width - x, lane.viewWidth))
        height: lane.height
        onXChanged: requestPaint()
        onWidthChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (width <= 0) return
            // Corners of the line inside the view: its ends plus every point in between.
            const corners = [{ x: 0, y: lane.yForLevel(lane.levelAtX(x)) }]
            for (const p of lane.points) {
                const px = p.beat * lane.beatWidth - x
                if (px > 0 && px < width) corners.push({ x: px, y: lane.yForLevel(p.level) })
            }
            corners.push({ x: width, y: lane.yForLevel(lane.levelAtX(x + width)) })
            if (!lane.overClips) {
                ctx.fillStyle = lane.bypassed ? Qt.rgba(0.4, 0.4, 0.4, 0.15) : Theme.timeline.envelopeFill
                ctx.beginPath()
                ctx.moveTo(0, height)
                for (const c of corners) ctx.lineTo(c.x, c.y)
                ctx.lineTo(width, height)
                ctx.closePath()
                ctx.fill()
            }
            ctx.strokeStyle = lane.lineColour
            ctx.lineWidth = 1.5
            ctx.beginPath()
            ctx.moveTo(corners[0].x, corners[0].y)
            for (let i = 1; i < corners.length; ++i) ctx.lineTo(corners[i].x, corners[i].y)
            ctx.stroke()
        }
    }

    // Background: double-click adds a point at the mouse, right-click opens the menu.
    MouseArea {
        id: background
        objectName: "envelopeArea" + lane.envelopeTrackId
        anchors.fill: parent
        enabled: !lane.overClips || lane.hoverNearLine || pressed
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: lane.overClips ? Qt.PointingHandCursor : Qt.CrossCursor
        onPressed: mouse => {
            if (lane.overClips && !lane.nearLine(mouse.x, mouse.y)) { mouse.accepted = false; return }
            if (lane.trackModel) lane.trackModel.selectTrack(lane.envelopeTrackId)
            lane.selectedPoint = -1
        }
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton) {
                const at = mapToItem(null, mouse.x, mouse.y)
                lane.menuRequested(lane.envelopeTrackId, at.x, at.y)
            }
        }
        onDoubleClicked: mouse => {
            if (mouse.button !== Qt.LeftButton || !lane.trackModel) return
            const index = lane.trackModel.addEnvelopePoint(lane.envelopeTrackId, mouse.x / lane.beatWidth, lane.levelForY(mouse.y))
            if (index >= 0) lane.selectedPoint = index
        }
    }

    // A count model: a drag changes the points but not their number, so the dragged delegate
    // (and its mouse grab) stays.
    Repeater {
        model: lane.points.length
        delegate: Rectangle {
            id: pointItem
            required property int index
            readonly property var modelData: lane.points[index] ?? ({ beat: 0, level: 0 })
            objectName: "envelopePoint" + lane.envelopeTrackId + "_" + index
            readonly property bool selected: lane.selectedPoint === index
            x: modelData.beat * lane.beatWidth - width / 2
            y: lane.yForLevel(modelData.level) - height / 2
            width: 9; height: 9; radius: 2
            color: selected ? Theme.timeline.envelopePointSelected : lane.lineColour
            border.color: Theme.windowBg
            z: 2
            MouseArea {
                anchors.fill: parent
                anchors.margins: -3
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                hoverEnabled: true
                cursorShape: Qt.SizeAllCursor
                property bool dragging: false
                property bool moved: false
                property real grabX: 0
                property real grabY: 0
                onPressed: mouse => {
                    if (lane.trackModel) lane.trackModel.selectTrack(lane.envelopeTrackId)
                    lane.selectedPoint = pointItem.index
                    if (mouse.button !== Qt.LeftButton) return
                    const centre = mapToItem(lane, mouse.x, mouse.y)
                    grabX = centre.x - pointItem.modelData.beat * lane.beatWidth
                    grabY = centre.y - lane.yForLevel(pointItem.modelData.level)
                    dragging = true; moved = false
                }
                onPositionChanged: mouse => {
                    if (!dragging || !lane.trackModel) return
                    const at = mapToItem(lane, mouse.x, mouse.y)
                    lane.trackModel.moveEnvelopePoint(lane.envelopeTrackId, pointItem.index,
                        (at.x - grabX) / lane.beatWidth, lane.levelForY(at.y - grabY), moved)
                    moved = true
                }
                onReleased: dragging = false
                onCanceled: dragging = false
                // Right-click a point: delete it.
                onClicked: mouse => {
                    if (mouse.button === Qt.RightButton && lane.trackModel) {
                        // This delegate goes away during the removal: nothing after it here.
                        const owner = lane, point = pointItem.index
                        owner.selectedPoint = -1
                        owner.trackModel.removeEnvelopePoint(owner.envelopeTrackId, point)
                    }
                }
                ToolTip.visible: containsMouse || dragging
                ToolTip.delay: dragging ? 0 : 450
                ToolTip.text: lane.trackModel ? lane.trackModel.envelopeDbText(pointItem.modelData.level) : ""
            }
        }
    }
}
