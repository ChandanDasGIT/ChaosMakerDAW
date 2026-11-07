import QtQuick
import QtQuick.Shapes

// The acoustic drum kit drawn from the front, like a drummer's kit on stage. Click a piece to
// play it on the track (holding the mouse holds the note); near a drum's centre hits harder,
// near its edge softer. Every hit lights its piece, from clicks, playback, MIDI and KEYS.
//
//   snare     centre: snare, rim: snare edge, the stick across the rim: side stick
//   hi-hat    centre: closed, edge: open, the pedal: pedal; the chips: tight, loose, half-open
//
// `bridge` is the page's DrumKitWrapper (partKeys, play, stop, hit).
Item {
    id: root
    property var bridge: null

    // The picture is drawn on a 640 x 300 plan and scaled to the width it gets; the plan's top
    // 50 units are empty and cut off.
    readonly property real planWidth: 640
    readonly property real planTop: 50
    readonly property real planHeight: 300
    readonly property real s: Math.min(width / planWidth, 1.35)
    implicitHeight: (planHeight - planTop) * Math.min(width / planWidth, 1.35)

    readonly property color shellColor: Theme.effects.drumShell
    readonly property color shellEdge: Theme.effects.drumShellEdge
    readonly property color headColor: Theme.effects.drumHead
    readonly property color headEdge: Theme.effects.drumHeadEdge
    readonly property color kickHead: Theme.effects.drumKickHead
    readonly property color brass: Theme.effects.drumBrass
    readonly property color brassEdge: Theme.effects.drumBrassEdge
    readonly property color hardware: Theme.effects.drumHardware

    function keyOf(part) { return root.bridge ? (root.bridge.partKeys[part] ?? -1) : -1 }

    // One drum or cymbal: an elliptic head (rx, ry) with an optional shell below. The head's
    // outer ring plays `rimPart` when one is set.
    component Piece: Item {
        id: piece
        property string part
        property string rimPart: ""
        property string label: ""
        property real cx
        property real cy
        property real rx
        property real ry
        property real shell: 0
        property color face: root.headColor
        property color faceEdge: root.headEdge
        property real edgeWidth: 1.5                       // the hoop
        property color body: root.shellColor
        property bool cymbal: false
        property real glow: 0
        readonly property int key: root.keyOf(part)
        readonly property int rimKey: rimPart.length > 0 ? root.keyOf(rimPart) : -1
        property int sounding: -1

        objectName: "drumPiece_" + part
        x: (cx - rx) * root.s
        y: (cy - ry) * root.s
        width: 2 * rx * root.s
        height: (2 * ry + shell) * root.s

        function flash(velocity) {
            glowFade.stop()
            piece.glow = 0.35 + 0.65 * velocity / 127
            glowFade.start()
        }
        NumberAnimation { id: glowFade; target: piece; property: "glow"; to: 0; duration: 320; easing.type: Easing.OutQuad }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            // The shell: the sides and the bottom edge, behind the head.
            ShapePath {
                fillColor: piece.shell > 0 ? piece.body : "transparent"
                strokeColor: piece.shell > 0 ? root.shellEdge : "transparent"
                strokeWidth: 1
                startX: 0; startY: piece.ry * root.s
                PathLine { x: 0; y: (piece.ry + piece.shell) * root.s }
                PathArc { x: piece.width; y: (piece.ry + piece.shell) * root.s; radiusX: piece.rx * root.s; radiusY: piece.ry * root.s; direction: PathArc.Counterclockwise }
                PathLine { x: piece.width; y: piece.ry * root.s }
            }
            // The head.
            ShapePath {
                fillColor: piece.face
                strokeColor: piece.faceEdge
                strokeWidth: piece.edgeWidth * root.s
                startX: 0; startY: piece.ry * root.s
                PathArc { x: piece.width; y: piece.ry * root.s; radiusX: piece.rx * root.s; radiusY: piece.ry * root.s }
                PathArc { x: 0; y: piece.ry * root.s; radiusX: piece.rx * root.s; radiusY: piece.ry * root.s }
            }
            // Where the rim (or a cymbal's edge) begins.
            ShapePath {
                fillColor: "transparent"
                strokeColor: piece.rimPart.length > 0 ? Qt.rgba(0, 0, 0, 0.22) : "transparent"
                strokeWidth: 1
                startX: piece.width * 0.11; startY: piece.ry * root.s
                PathArc { x: piece.width * 0.89; y: piece.ry * root.s; radiusX: piece.rx * root.s * 0.78; radiusY: piece.ry * root.s * 0.78 }
                PathArc { x: piece.width * 0.11; y: piece.ry * root.s; radiusX: piece.rx * root.s * 0.78; radiusY: piece.ry * root.s * 0.78 }
            }
            // A cymbal's bell.
            ShapePath {
                fillColor: piece.cymbal ? Qt.darker(piece.face, 1.15) : "transparent"
                strokeColor: "transparent"
                startX: piece.width * 0.42; startY: piece.ry * root.s
                PathArc { x: piece.width * 0.58; y: piece.ry * root.s; radiusX: piece.rx * root.s * 0.16; radiusY: piece.ry * root.s * 0.4 }
                PathArc { x: piece.width * 0.42; y: piece.ry * root.s; radiusX: piece.rx * root.s * 0.16; radiusY: piece.ry * root.s * 0.4 }
            }
            // The hit light.
            ShapePath {
                fillColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, piece.glow * 0.75)
                strokeColor: piece.glow > 0.01 ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, piece.glow) : "transparent"
                strokeWidth: 2
                startX: 0; startY: piece.ry * root.s
                PathArc { x: piece.width; y: piece.ry * root.s; radiusX: piece.rx * root.s; radiusY: piece.ry * root.s }
                PathArc { x: 0; y: piece.ry * root.s; radiusX: piece.rx * root.s; radiusY: piece.ry * root.s }
            }
        }

        Text {
            visible: piece.label.length > 0
            text: piece.label
            color: Theme.textMuted
            font.pixelSize: Math.max(9, 10 * root.s)
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height + 2 * root.s
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true
            // Only the head counts; a press beside it goes to the piece behind.
            function distance(mx, my) {
                const dx = (mx - piece.rx * root.s) / (piece.rx * root.s)
                const dy = (my - piece.ry * root.s) / (piece.ry * root.s)
                return Math.sqrt(dx * dx + dy * dy)
            }
            onPressed: mouse => {
                const d = distance(mouse.x, mouse.y)
                if (d > 1.0 || !root.bridge) { mouse.accepted = false; return }
                const onRim = piece.rimKey >= 0 && d > 0.78
                piece.sounding = onRim ? piece.rimKey : piece.key
                const velocity = onRim ? 100 : Math.round(127 * (1.0 - 0.55 * d))
                root.bridge.play(piece.sounding, Math.max(20, Math.min(127, velocity)))
            }
            onReleased: { if (piece.sounding >= 0 && root.bridge) root.bridge.stop(piece.sounding); piece.sounding = -1 }
            onCanceled: { if (piece.sounding >= 0 && root.bridge) root.bridge.stop(piece.sounding); piece.sounding = -1 }
        }
    }

    // A small labelled pad: the hi-hat pedal, the side stick, the extra hi-hats.
    component Pad: Rectangle {
        id: pad
        property string part
        property string text: ""
        property real px
        property real py
        property real pw
        property real ph
        property real glow: 0
        readonly property int key: root.keyOf(part)
        objectName: "drumPiece_" + part
        x: px * root.s; y: py * root.s; width: pw * root.s; height: ph * root.s
        radius: 3 * root.s
        color: padMouse.pressed ? Qt.lighter(Theme.cardBgAlt, 1.4) : Theme.cardBgAlt
        border.color: glow > 0.01 ? Theme.accent : Theme.divider
        border.width: 1 + glow * 2
        function flash(velocity) { padFade.stop(); pad.glow = 0.35 + 0.65 * velocity / 127; padFade.start() }
        NumberAnimation { id: padFade; target: pad; property: "glow"; to: 0; duration: 320; easing.type: Easing.OutQuad }
        Rectangle { anchors.fill: parent; radius: parent.radius; color: Theme.accent; opacity: pad.glow * 0.45 }
        Text {
            anchors.centerIn: parent
            text: pad.text
            color: Theme.text
            font.pixelSize: Math.max(8, 9 * root.s)
        }
        MouseArea {
            id: padMouse
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onPressed: if (root.bridge) root.bridge.play(pad.key, 100)
            onReleased: if (root.bridge) root.bridge.stop(pad.key)
            onCanceled: if (root.bridge) root.bridge.stop(pad.key)
        }
    }

    // Everything sits on the plan, moved up by its empty top.
    Item {
    y: -root.planTop * root.s
    width: root.width
    height: root.planHeight * root.s

    // Stands and the hi-hat rod, behind everything.
    Rectangle { x: 70 * root.s; y: 122 * root.s; width: 3 * root.s; height: 150 * root.s; color: root.hardware }
    Rectangle { x: 470 * root.s; y: 262 * root.s; width: 3 * root.s; height: 22 * root.s; color: root.hardware }
    Rectangle { x: 175 * root.s; y: 200 * root.s; width: 3 * root.s; height: 84 * root.s; color: root.hardware }

    Piece {
        id: kick
        part: "kick"; label: "Kick"
        cx: 320; cy: 196; rx: 88; ry: 88
        face: root.kickHead; faceEdge: root.shellColor; edgeWidth: 12
    }
    Piece {
        id: tomHigh
        part: "tomHigh"; label: "Tom high"
        cx: 262; cy: 84; rx: 44; ry: 16; shell: 36
    }
    Piece {
        id: tomMid
        part: "tomMid"; label: "Tom mid"
        cx: 380; cy: 84; rx: 48; ry: 17; shell: 40
    }
    Piece {
        id: tomLow
        part: "tomLow"; label: "Tom low"
        cx: 500; cy: 176; rx: 62; ry: 22; shell: 64
    }
    Piece {
        id: snare
        part: "snare"; rimPart: "snareEdge"; label: "Snare"
        cx: 176; cy: 182; rx: 58; ry: 20; shell: 26
    }
    Piece {
        id: hihat
        part: "hihatClosed"; rimPart: "hihatOpen"; label: "Hi-hat"
        cx: 72; cy: 112; rx: 62; ry: 12; shell: 0; cymbal: true
        face: root.brass; faceEdge: root.brassEdge
    }

    Pad { id: sideStick; part: "sideStick"; text: "Stick"; px: 206; py: 158; pw: 42; ph: 14 }
    Pad { id: pedal; part: "hihatPedal"; text: "Pedal"; px: 46; py: 270; pw: 52; ph: 18 }
    Pad { id: tight; part: "hihatTight"; text: "Tight"; px: 8; py: 150; pw: 40; ph: 16 }
    Pad { id: loose; part: "hihatLoose"; text: "Loose"; px: 52; py: 150; pw: 40; ph: 16 }
    Pad { id: halfOpen; part: "hihatHalfOpen"; text: "Half"; px: 96; py: 150; pw: 40; ph: 16 }
    }

    // Every hit the kit plays lights its piece.
    Connections {
        target: root.bridge
        function onHit(key, velocity) {
            for (const item of [kick, tomHigh, tomMid, tomLow, snare, hihat, sideStick, pedal, tight, loose, halfOpen]) {
                if (item.key === key || (item.rimKey !== undefined && item.rimKey === key)) item.flash(velocity)
            }
        }
    }
}
