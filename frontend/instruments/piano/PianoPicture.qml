import QtQuick
import QtQuick.Shapes

// The grand piano drawn from above with its lid open: the case, the gold frame with one
// string per key, the damper felt, all 88 keys (A0 to C8) and the sustain pedal.
//
// Click a key to play it on the track (lower on the key is louder; drag along the keys to play
// them in turn). Every key the piano plays lights, with its string: clicks, playback, MIDI and
// KEYS alike. The pedal is a switch (a mouse cannot hold it and play): click it down, click it
// up. It shows the pedal from any source, and the dampers lift while it is down.
//
// `bridge` is the page's PianoWrapper (keysDown, pedal, struck, play, stop, setPedalDown).
Item {
    id: root
    property var bridge: null

    // Drawn on a 640 x 380 plan and scaled to the width it gets.
    readonly property real planWidth: 640
    readonly property real planHeight: 380
    readonly property real s: Math.min(width / planWidth, 1.35)
    implicitHeight: planHeight * s

    readonly property var colours: Theme.effects
    readonly property int lowest: 21                        // A0
    readonly property int highest: 108                      // C8
    readonly property real keysLeft: 40
    readonly property real keysWidth: 560
    readonly property real keysTop: 262
    readonly property real whiteHeight: 74
    readonly property real blackHeight: 46
    readonly property real whiteWidth: keysWidth / 52

    // Keys the piano is playing (held) or has just struck (a short flash for quick taps).
    property var flashes: ({})
    readonly property var down: bridge ? bridge.keysDown : ({})
    readonly property bool pedalDown: bridge ? bridge.pedal >= 64 : false
    function lit(note) { return (down[String(note)] ?? 0) > 0 || (flashes[String(note)] ?? 0) > 0 }
    function litVelocity(note) { return Math.max(down[String(note)] ?? 0, flashes[String(note)] ?? 0) }

    function isBlack(note) { const c = note % 12; return c === 1 || c === 3 || c === 6 || c === 8 || c === 10 }
    // White keys below `note`, counted from A0.
    function whitesBelow(note) {
        let n = 0
        for (let k = root.lowest; k < note; ++k) if (!isBlack(k)) ++n
        return n
    }
    function keyX(note) {
        return isBlack(note) ? keysLeft + whitesBelow(note) * whiteWidth - whiteWidth * 0.3
                             : keysLeft + whitesBelow(note) * whiteWidth
    }
    // A key's string, across the frame: low strings on the left, longest.
    function stringX(note) { return 52 + (note - lowest + 0.5) / 88 * 536 }
    // The frame's curved edge (its bentside), every 10 units from x = 50: strings start just
    // inside it.
    readonly property var frameEdge: [30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 31, 32, 34,
        36, 39, 42, 46, 51, 56, 62, 68, 75, 82, 89, 97, 104, 111, 118, 125, 131, 137, 143, 148,
        153, 157, 162, 166, 169, 173, 176, 179, 182, 185, 188, 190, 193, 196, 198, 201, 204, 207,
        212, 220]
    function stringTop(x) {
        const at = Math.max(0, Math.min(frameEdge.length - 1.001, (x - 50) / 10))
        const i = Math.floor(at)
        return frameEdge[i] + (frameEdge[i + 1] - frameEdge[i]) * (at - i) + 6
    }

    // The key under a point of the plan (black keys first, they lie on top), or -1.
    function noteAt(px, py) {
        if (px < keysLeft || px >= keysLeft + keysWidth || py < keysTop || py > keysTop + whiteHeight) return -1
        if (py <= keysTop + blackHeight) {
            for (let k = lowest; k <= highest; ++k)
                if (isBlack(k) && px >= keyX(k) && px < keyX(k) + whiteWidth * 0.6) return k
        }
        const white = Math.floor((px - keysLeft) / whiteWidth)
        let n = 0
        for (let k = lowest; k <= highest; ++k) {
            if (isBlack(k)) continue
            if (n === white) return k
            ++n
        }
        return -1
    }

    Connections {
        target: root.bridge
        function onStruck(key, velocity) {
            const next = Object.assign({}, root.flashes)
            next[String(key)] = velocity
            root.flashes = next
            flashTimer.restart()
        }
    }
    Timer {
        id: flashTimer
        interval: 160
        onTriggered: root.flashes = ({})
    }

    // The lid, open behind the case, and its prop stick.
    Shape {
        width: root.width; height: root.height
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: root.colours.pianoLid
            strokeColor: root.colours.pianoCaseEdge
            strokeWidth: 1
            startX: 70 * root.s; startY: 248 * root.s
            PathLine { x: 70 * root.s; y: 12 * root.s }
            PathQuad { x: 102 * root.s; y: 0; controlX: 70 * root.s; controlY: 0 }
            PathLine { x: 182 * root.s; y: 0 }
            PathCubic { x: 430 * root.s; y: 122 * root.s; control1X: 292 * root.s; control1Y: 0; control2X: 330 * root.s; control2Y: 72 * root.s }
            PathCubic { x: 632 * root.s; y: 206 * root.s; control1X: 530 * root.s; control1Y: 172 * root.s; control2X: 632 * root.s; control2Y: 172 * root.s }
            PathLine { x: 632 * root.s; y: 248 * root.s }
        }
        ShapePath {
            strokeColor: root.colours.pianoCaseEdge
            strokeWidth: 3 * root.s
            fillColor: "transparent"
            startX: 470 * root.s; startY: 172 * root.s
            PathLine { x: 520 * root.s; y: 150 * root.s }
        }
    }

    // The case, the frame and the strings.
    Shape {
        width: root.width; height: root.height
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: root.colours.pianoCase
            strokeColor: root.colours.pianoCaseEdge
            strokeWidth: 1.5 * root.s
            startX: 34 * root.s; startY: 262 * root.s
            PathLine { x: 34 * root.s; y: 40 * root.s }
            PathQuad { x: 66 * root.s; y: 18 * root.s; controlX: 34 * root.s; controlY: 18 * root.s }
            PathLine { x: 150 * root.s; y: 18 * root.s }
            PathCubic { x: 400 * root.s; y: 140 * root.s; control1X: 262 * root.s; control1Y: 18 * root.s; control2X: 300 * root.s; control2Y: 90 * root.s }
            PathCubic { x: 606 * root.s; y: 222 * root.s; control1X: 500 * root.s; control1Y: 190 * root.s; control2X: 606 * root.s; control2Y: 190 * root.s }
            PathLine { x: 606 * root.s; y: 262 * root.s }
            PathLine { x: 34 * root.s; y: 262 * root.s }
        }
        ShapePath {
            fillColor: root.colours.pianoPlate
            strokeColor: root.colours.pianoPlateEdge
            strokeWidth: 1 * root.s
            startX: 48 * root.s; startY: 242 * root.s
            PathLine { x: 48 * root.s; y: 46 * root.s }
            PathQuad { x: 72 * root.s; y: 30 * root.s; controlX: 48 * root.s; controlY: 30 * root.s }
            PathLine { x: 150 * root.s; y: 30 * root.s }
            PathCubic { x: 394 * root.s; y: 150 * root.s; control1X: 256 * root.s; control1Y: 30 * root.s; control2X: 294 * root.s; control2Y: 100 * root.s }
            PathCubic { x: 592 * root.s; y: 226 * root.s; control1X: 492 * root.s; control1Y: 198 * root.s; control2X: 592 * root.s; control2Y: 198 * root.s }
            PathLine { x: 592 * root.s; y: 242 * root.s }
            PathLine { x: 48 * root.s; y: 242 * root.s }
        }
    }
    Repeater {
        model: root.highest - root.lowest + 1
        delegate: Rectangle {
            required property int index
            readonly property int note: root.lowest + index
            readonly property bool on: root.lit(note)
            readonly property real px: root.stringX(note)
            objectName: "pianoString" + note
            x: (px - (on ? 1 : 0.5)) * root.s
            y: root.stringTop(px) * root.s
            width: (on ? 2 : 1) * root.s
            height: (232 - root.stringTop(px)) * root.s
            color: on ? Theme.accent : root.colours.pianoString
            opacity: on ? 1.0 : 0.55
        }
    }
    // The dampers: on the strings, lifted off them while the pedal is down (only the lift
    // moves smoothly, not the scaling).
    property real damperLift: pedalDown ? 6 : 0
    Behavior on damperLift { NumberAnimation { duration: 90 } }
    Rectangle {
        objectName: "pianoDampers"
        x: 52 * root.s
        y: (226 - root.damperLift) * root.s
        width: 536 * root.s
        height: 10 * root.s
        radius: 2 * root.s
        color: root.colours.pianoFelt
        opacity: root.pedalDown ? 0.45 : 1.0
    }

    // The keys.
    Item {
        id: keys
        x: root.keysLeft * root.s
        y: root.keysTop * root.s
        width: root.keysWidth * root.s
        height: root.whiteHeight * root.s
        Repeater {                                          // white keys
            model: root.highest - root.lowest + 1
            delegate: Rectangle {
                required property int index
                readonly property int note: root.lowest + index
                readonly property bool on: root.lit(note)
                visible: !root.isBlack(note)
                objectName: visible ? "pianoKey" + note : ""
                x: (root.keyX(note) - root.keysLeft) * root.s
                width: root.whiteWidth * root.s
                height: parent.height
                color: on ? Qt.tint(root.colours.pianoKeyWhite, Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.25 + 0.5 * root.litVelocity(note) / 127))
                          : root.colours.pianoKeyWhite
                border.color: root.colours.pianoKeyEdge
                border.width: 0.6
                radius: 1.5 * root.s
            }
        }
        Repeater {                                          // black keys, on top
            model: root.highest - root.lowest + 1
            delegate: Rectangle {
                required property int index
                readonly property int note: root.lowest + index
                readonly property bool on: root.lit(note)
                visible: root.isBlack(note)
                objectName: visible ? "pianoKey" + note : ""
                x: (root.keyX(note) - root.keysLeft) * root.s
                width: root.whiteWidth * 0.6 * root.s
                height: root.blackHeight * root.s
                color: on ? Qt.tint(root.colours.pianoKeyBlack, Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.45 + 0.5 * root.litVelocity(note) / 127))
                          : root.colours.pianoKeyBlack
                radius: 1.5 * root.s
            }
        }
        // The felt strip behind the keys.
        Rectangle { y: -3 * root.s; width: parent.width; height: 3 * root.s; color: root.colours.pianoFelt }
    }

    MouseArea {
        id: keyMouse
        objectName: "pianoKeyboardArea"
        x: keys.x; y: keys.y; width: keys.width; height: keys.height
        cursorShape: Qt.PointingHandCursor
        property int sounding: -1
        function velocityAt(py) { return Math.max(30, Math.min(127, Math.round(40 + 87 * py / height))) }
        function playAt(mx, my) {
            const note = root.noteAt(mx / root.s + root.keysLeft, my / root.s + root.keysTop)
            if (note === sounding) return
            if (sounding >= 0 && root.bridge) root.bridge.stop(sounding)
            sounding = note
            if (note >= 0 && root.bridge) root.bridge.play(note, velocityAt(my))
        }
        onPressed: mouse => playAt(mouse.x, mouse.y)
        onPositionChanged: mouse => { if (pressed) playAt(mouse.x, mouse.y) }
        onReleased: { if (sounding >= 0 && root.bridge) root.bridge.stop(sounding); sounding = -1 }
        onCanceled: { if (sounding >= 0 && root.bridge) root.bridge.stop(sounding); sounding = -1 }
    }

    // The sustain pedal: a switch.
    Rectangle {
        id: pedal
        objectName: "pianoPedal"
        x: 300 * root.s
        y: (root.pedalDown ? 350 : 346) * root.s
        width: 40 * root.s
        height: 20 * root.s
        radius: 5 * root.s
        color: root.pedalDown ? Qt.darker(root.colours.pianoPedal, 1.25) : root.colours.pianoPedal
        border.color: root.pedalDown ? Theme.accent : root.colours.pianoPedalEdge
        border.width: root.pedalDown ? 2 : 1
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: if (root.bridge) root.bridge.setPedalDown(!root.pedalDown)
        }
    }
    Text {
        anchors.left: pedal.right
        anchors.leftMargin: 8 * root.s
        anchors.verticalCenter: pedal.verticalCenter
        text: root.pedalDown ? "Pedal down" : "Pedal"
        color: root.pedalDown ? Theme.text : Theme.textMuted
        font.pixelSize: Math.max(9, 11 * root.s)
    }
}
