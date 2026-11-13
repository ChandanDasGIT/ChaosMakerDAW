import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Item {
    id: root

    property real beatWidth: 96
    // Meter, from the piano roll: one counted beat in quarter-note beats, and beats per bar
    property real beatUnit: 1
    property int beatsInBar: 4
    property real contentWidth: 1000
    property real contentX: 0 // horizontal scroll offset shared with the note grid
    property real laneHeight: 90
    property var notes: []
    property int headerWidth: 86

    implicitHeight: laneHeight

    signal heightChangeRequested(real newHeight)
    // Emitted once when a velocity drag finishes (parallel arrays). The view owns no edit logic.
    signal velocitiesEdited(var ids, var velocities)

    // Optional object providing noteFillColor(velocity) (the piano roll), so stalks match notes
    property var colorSource: null
    // PianoRollWrapper: shared tick rules (count and bar ticks) so the lane matches the ruler
    property var gridSource: null
    // Live drag preview: note id -> velocity (also read by the note grid to recolour notes)
    property var previewVelocities: ({})
    property int hoveredNoteId: -1
    readonly property bool editing: velocityMouse.dragging

    function shownVelocity(note) {
        const p = previewVelocities[String(note.id)]
        return p !== undefined ? p : (note.velocity ?? 100)
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.pianoRoll.ruler
        border.color: Theme.pianoRoll.panelBorder
        border.width: 1

        // Resizable Splitter Header with dotted grips (::::::::)
        Rectangle {
            id: splitterBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 8
            color: splitterMouse.pressed || splitterHover.hovered ? Theme.pianoRoll.laneSplitterActive : Theme.pianoRoll.laneSplitter
            border.color: Theme.pianoRoll.laneSplitterBorder
            border.width: 1

            HoverHandler { id: splitterHover; cursorShape: Qt.SplitVCursor }

            // Texture dotted grip handles
            Row {
                anchors.centerIn: parent
                spacing: 3
                Repeater {
                    model: 8
                    delegate: Rectangle {
                        width: 2
                        height: 2
                        radius: 1
                        color: Theme.pianoRoll.laneScaleText
                    }
                }
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3
                Repeater {
                    model: 8
                    delegate: Rectangle {
                        width: 2
                        height: 2
                        radius: 1
                        color: Theme.pianoRoll.laneScaleText
                    }
                }
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: root.headerWidth + 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3
                Repeater {
                    model: 8
                    delegate: Rectangle {
                        width: 2
                        height: 2
                        radius: 1
                        color: Theme.pianoRoll.laneScaleText
                    }
                }
            }

            MouseArea {
                id: splitterMouse
                anchors.fill: parent
                cursorShape: Qt.SplitVCursor
                // Track the drag in scene coordinates: the bar itself moves as the lane resizes,
                // so local mouse.y would feed back into the delta and jitter.
                property real startY: 0
                property real startHeight: 0
                onPressed: mouse => {
                    startY = mapToItem(null, mouse.x, mouse.y).y
                    startHeight = root.laneHeight
                }
                onPositionChanged: mouse => {
                    if (pressed) {
                        const diff = startY - mapToItem(null, mouse.x, mouse.y).y
                        root.laneHeight = Math.max(45, Math.min(260, startHeight + diff))
                    }
                }
            }
        }

        // Left controller selector panel (aligned with PianoKeys)
        Rectangle {
            id: leftControlArea
            anchors.left: parent.left
            anchors.top: splitterBar.bottom
            anchors.bottom: parent.bottom
            width: root.headerWidth
            color: Theme.pianoRoll.menuBar
            border.color: Theme.pianoRoll.menuBarBorder

            Column {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4

                // Lane name (velocity is the only lane for now; CC lanes are not implemented)
                Rectangle {
                    objectName: "velocityLaneLabel"
                    width: parent.width
                    height: 22
                    color: Theme.pianoRoll.toolStrip
                    border.color: Theme.pianoRoll.panelBorder
                    radius: Theme.controlRadius

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        text: "Velocity"
                        color: Theme.pianoRoll.laneValueText
                        font.pixelSize: Theme.smallSize
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
            }
        }

        // Velocity stalks view area
        Item {
            anchors.left: leftControlArea.right
            anchors.right: parent.right
            anchors.top: splitterBar.bottom
            anchors.bottom: parent.bottom
            clip: true

            // Scrolled content, offset to stay aligned with the note grid above
            Item {
                id: stalkContent
                x: -root.contentX
                y: 0
                width: root.contentWidth
                height: parent.height

                // Vertical beat grid lines in velocity lane (one per counted beat; bars thicker)
                Repeater {
                    model: root.gridSource ? root.gridSource.tickCount(root.contentWidth / root.beatWidth, root.beatUnit, true) : 0
                    delegate: Rectangle {
                        required property int index
                        readonly property bool isBar: root.gridSource ? root.gridSource.isBarTick(index, root.beatsInBar) : false
                        x: Math.round(index * root.beatUnit * root.beatWidth)
                        y: 0
                        width: isBar ? 2 : 1
                        height: parent.height
                        color: isBar ? Theme.pianoRoll.laneGridBar : Theme.pianoRoll.laneGrid
                    }
                }

                // Note velocity stalks
                Repeater {
                    model: root.notes
                    delegate: Item {
                        id: stalkDelegate
                        required property var modelData
                        readonly property real startBeat: modelData.startBeat ?? 0
                        readonly property int noteId: modelData.id ?? -1
                        readonly property bool isSelected: modelData.selected ?? false
                        readonly property int velocity: root.shownVelocity(modelData)
                        readonly property bool isHot: root.hoveredNoteId === noteId
                        readonly property color fill: root.colorSource ? root.colorSource.noteFillColor(velocity) : Theme.pianoRoll.stalkFallback

                        // Centre the 2px stalk on the note's left edge (note blocks start at x + 1)
                        x: startBeat * root.beatWidth + 2 - width / 2
                        y: 0
                        width: 10
                        height: parent.height
                        z: isSelected ? 2 : 1

                        readonly property real barHeight: Math.max(4, (velocity / 127.0) * (height - 8))

                        // Vertical stalk line
                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: stalkDelegate.isSelected ? 3 : 2
                            height: stalkDelegate.barHeight
                            color: stalkDelegate.isHot ? Theme.accent
                                 : Qt.lighter(stalkDelegate.fill, stalkDelegate.isSelected ? 2.0 : 1.6) // lifted so quiet notes stay visible
                        }

                        // Stalk pin handle; at the lane's left edge (a note on beat 0) it moves right
                        // just enough to show whole, the stalk line stays on the note's edge.
                        Rectangle {
                            objectName: "velocityPin" + stalkDelegate.noteId
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: Math.max(0, stalkDelegate.barHeight - 3)
                            x: Math.max(-stalkDelegate.x, (stalkDelegate.width - width) / 2)
                            width: 7
                            height: 7
                            radius: 3.5
                            color: stalkDelegate.isHot || stalkDelegate.isSelected ? Theme.pianoRoll.stalkSelected : Qt.lighter(stalkDelegate.fill, 1.9)
                            border.color: stalkDelegate.isSelected ? Theme.accent : Theme.pianoRoll.stalkBorder
                            border.width: 1
                        }
                    }
                }
            }

            // One lane-wide handler (not per stalk): committing rebuilds the stalk delegates,
            // which must not interrupt the gesture.
            MouseArea {
                id: velocityMouse
                objectName: "velocityStalkArea"
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton
                cursorShape: root.hoveredNoteId >= 0 || dragging ? Qt.SizeVerCursor : Qt.ArrowCursor

                property bool dragging: false
                property bool painting: false     // a sweep from empty lane space (D3)
                property real lastX: 0            // the sweep's last point, content x and lane y
                property real lastY: 0
                property bool relative: false     // true when dragging a group of selected notes
                property var targetIds: []
                property var startVelocities: []
                property real pressY: 0
                property string label: ""

                readonly property real usable: Math.max(1, height - 8)
                function velocityAtY(y) { return Math.round(Math.max(0, Math.min(127, (height - y) / usable * 127))) }

                // Nearest stalk within 8px horizontally; for stacked chords pick the closest pin.
                function noteAt(mx, my) {
                    const cx = mx + root.contentX
                    let best = null, bestScore = 1e9
                    for (let i = 0; i < root.notes.length; ++i) {
                        const n = root.notes[i]
                        const dx = Math.abs((n.startBeat ?? 0) * root.beatWidth + 2 - cx)
                        if (dx > 8) continue
                        const pinY = height - Math.max(4, root.shownVelocity(n) / 127 * usable)
                        const score = dx * 4 + Math.abs(pinY - my)
                        if (score < bestScore) { bestScore = score; best = n }
                    }
                    return best
                }

                // A paint sweep from (lastX, lastY) to (cx, my): every stalk the segment crosses
                // (the selected ones when there are any) takes the velocity at the segment's
                // height there, so a fast drag skips none.
                function paintTo(cx, my) {
                    const anySelected = root.notes.some(n => n.selected)
                    const x0 = Math.min(lastX, cx) - 3, x1 = Math.max(lastX, cx) + 3
                    const preview = Object.assign({}, root.previewVelocities)
                    let last = -1
                    for (let i = 0; i < root.notes.length; ++i) {
                        const n = root.notes[i]
                        if (anySelected && !n.selected) continue
                        const sx = (n.startBeat ?? 0) * root.beatWidth + 2
                        if (sx < x0 || sx > x1) continue
                        const t = cx === lastX ? 1 : Math.max(0, Math.min(1, (sx - lastX) / (cx - lastX)))
                        last = velocityAtY(lastY + (my - lastY) * t)
                        preview[String(n.id)] = last
                    }
                    lastX = cx
                    lastY = my
                    label = "Vel " + velocityAtY(my)
                    root.previewVelocities = preview
                }

                function applyAt(my) {
                    const preview = {}
                    if (relative) {
                        const delta = Math.round(-(my - pressY) / usable * 127)
                        for (let i = 0; i < targetIds.length; ++i)
                            preview[String(targetIds[i])] = Math.max(0, Math.min(127, startVelocities[i] + delta))
                        label = (delta >= 0 ? "+" : "") + delta
                    } else {
                        const v = velocityAtY(my)
                        preview[String(targetIds[0])] = v
                        label = "Vel " + v
                    }
                    root.previewVelocities = preview
                }

                onPositionChanged: mouse => {
                    if (painting) { paintTo(mouse.x + root.contentX, mouse.y); return }
                    if (dragging) { applyAt(mouse.y); return }
                    const n = noteAt(mouse.x, mouse.y)
                    root.hoveredNoteId = n ? n.id : -1
                }
                onExited: if (!dragging && !painting) root.hoveredNoteId = -1

                onPressed: mouse => {
                    const n = noteAt(mouse.x, mouse.y)
                    if (!n) {
                        // Empty lane space: paint across the stalks the drag passes.
                        if (root.notes.length === 0) { mouse.accepted = false; return }
                        painting = true
                        lastX = mouse.x + root.contentX
                        lastY = mouse.y
                        root.previewVelocities = ({})
                        paintTo(lastX, mouse.y)
                        return
                    }
                    root.hoveredNoteId = n.id
                    relative = !!n.selected
                    const ids = [], vels = []
                    for (let i = 0; i < root.notes.length; ++i) {
                        const m = root.notes[i]
                        if (relative ? m.selected : m.id === n.id) { ids.push(m.id); vels.push(m.velocity ?? 100) }
                    }
                    targetIds = ids
                    startVelocities = vels
                    pressY = mouse.y
                    dragging = true
                    applyAt(mouse.y)
                }

                onReleased: {
                    if (painting) {
                        painting = false
                        const ids = [], vels = []
                        for (const key in root.previewVelocities) {
                            ids.push(Number(key))
                            vels.push(root.previewVelocities[key])
                        }
                        if (ids.length > 0) root.velocitiesEdited(ids, vels)   // one undo step
                        root.previewVelocities = ({})
                        return
                    }
                    if (!dragging) return
                    const ids = [], vels = []
                    for (let i = 0; i < targetIds.length; ++i) {
                        ids.push(targetIds[i])
                        vels.push(root.previewVelocities[String(targetIds[i])])
                    }
                    dragging = false
                    root.velocitiesEdited(ids, vels)   // one commit = one undo step
                    root.previewVelocities = ({})
                }

                onCanceled: {
                    dragging = false
                    painting = false
                    root.previewVelocities = ({})
                }

                // Value readout next to the cursor while dragging
                Rectangle {
                    visible: velocityMouse.dragging || velocityMouse.painting
                    x: Math.min(parent.width - width - 2, velocityMouse.mouseX + 12)
                    y: Math.max(2, Math.min(parent.height - height - 2, velocityMouse.mouseY - height - 4))
                    width: valueText.implicitWidth + 10
                    height: 16
                    radius: 2
                    color: Theme.pianoRoll.readoutBox
                    border.color: Theme.accent
                    Text {
                        id: valueText
                        anchors.centerIn: parent
                        text: velocityMouse.label
                        color: Theme.pianoRoll.noteSelectedEdge
                        font.pixelSize: Theme.captionSize
                    }
                }
            }
        }
    }
}
