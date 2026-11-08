import QtQuick
import QtQuick.Shapes

// The electronic drum kit drawn as a drum machine seen from above: a display, three style
// buttons, a volume knob and sixteen pads, one per key the kit plays. A press on a pad plays it
// on the track (holding the mouse holds the note; near the pad's centre hits harder) and picks
// its drum for the knobs under the picture. Every hit lights its pad, from clicks, playback,
// MIDI and KEYS.
//
// `bridge` is the page's ElectroDrumsWrapper (style, volume, pads, play, stop, hit).
Item {
    id: root
    property var bridge: null
    property string selectedDrum: ""                       // the drum the knobs below edit
    signal drumChosen(string drum)

    // Drawn on a 640 x 400 plan and scaled to the width it gets.
    readonly property real planWidth: 640
    readonly property real planHeight: 400
    readonly property real s: Math.min(width / planWidth, 1.35)
    implicitHeight: planHeight * s

    readonly property var colours: Theme.effects
    // The pads from the top row down: cymbals and the extra hats, the hats, the toms, then the
    // kick, snare, clap and side stick nearest the player (keys from the backend's map).
    readonly property var layout: [55, 56, 73, 75,
                                   66, 68, 70, 57,
                                   65, 67, 69, 64,
                                   60, 62, 63, 61]

    function padColour(drum) {
        if (drum === "kick" || drum.startsWith("tom")) return colours.padKick
        if (drum.startsWith("hihat")) return colours.padHat
        if (drum === "crash" || drum === "ride") return colours.padCymbal
        return colours.padSnare
    }
    function styleTitle(id) {
        return id === "studio" ? "STUDIO" : id
    }
    function signedDb(value) {
        return (value > 0 ? "+" : "") + value.toFixed(1) + " dB"
    }

    // The case.
    Rectangle {
        objectName: "electroMachineCase"
        width: root.planWidth * root.s
        height: root.planHeight * root.s
        radius: 12 * root.s
        color: root.colours.machineBody
        border.color: root.colours.machineEdge
        border.width: 2 * root.s
        Rectangle {                                         // the stripe
            x: 14 * root.s; y: 12 * root.s
            width: parent.width - 28 * root.s; height: 5 * root.s
            radius: height / 2
            color: root.colours.machineTrim
        }
        Text {
            x: 24 * root.s; y: 26 * root.s
            text: "ELECTRONIC DRUMS"
            color: root.colours.machineLabel
            font.pixelSize: Math.max(8, 11 * root.s)
            font.bold: true
            font.letterSpacing: 2 * root.s
        }
    }

    // The display: the style, and the pad last hit or picked.
    Rectangle {
        id: lcd
        objectName: "electroDisplay"
        property string line2: ""
        x: 24 * root.s; y: 46 * root.s
        width: 210 * root.s; height: 62 * root.s
        radius: 4 * root.s
        color: root.colours.machineLcd
        border.color: root.colours.machineEdge
        border.width: 2 * root.s
        Text {
            objectName: "electroDisplayStyle"
            x: 10 * root.s; y: 4 * root.s
            text: root.bridge ? root.styleTitle(root.bridge.style) : ""
            color: root.colours.machineLcdText
            font.family: "monospace"
            font.pixelSize: Math.max(10, 26 * root.s)
            font.bold: true
        }
        Text {
            x: 10 * root.s; y: parent.height - height - 5 * root.s
            text: lcd.line2.toUpperCase()
            color: root.colours.machineLcdText
            font.family: "monospace"
            font.pixelSize: Math.max(8, 12 * root.s)
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 8 * root.s
            y: 8 * root.s
            text: root.bridge ? root.signedDb(root.bridge.volume) : ""
            color: root.colours.machineLcdText
            font.family: "monospace"
            font.pixelSize: Math.max(8, 11 * root.s)
        }
    }

    // The style buttons, each with its LED.
    Row {
        x: 256 * root.s; y: 58 * root.s
        spacing: 10 * root.s
        Repeater {
            model: root.bridge ? root.bridge.styles : []
            delegate: Rectangle {
                required property var modelData
                readonly property bool lit: root.bridge && root.bridge.style === modelData.id
                objectName: "electroStyle_" + modelData.id
                width: 70 * root.s; height: 38 * root.s
                radius: 4 * root.s
                color: styleMouse.pressed ? Qt.lighter(root.colours.machineKnob, 1.6) : root.colours.machineKnob
                border.color: root.colours.machineEdge
                Rectangle {                                 // the LED
                    x: 8 * root.s; y: 8 * root.s
                    width: 8 * root.s; height: 8 * root.s; radius: width / 2
                    color: parent.lit ? root.colours.machineLed : root.colours.machineLedOff
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: parent.height - height - 5 * root.s
                    text: root.styleTitle(modelData.id)
                    color: root.colours.machineLabel
                    font.pixelSize: Math.max(8, 11 * root.s)
                    font.bold: true
                }
                MouseArea {
                    id: styleMouse
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: if (root.bridge) root.bridge.style = modelData.id
                }
            }
        }
    }

    // The volume knob: drag up or down (or the wheel); a double-click goes back to 0 dB.
    Item {
        id: knob
        objectName: "electroVolumeKnob"
        readonly property real r: 28
        readonly property real fraction: root.bridge
            ? (root.bridge.volume - root.bridge.minLevel) / (root.bridge.maxLevel - root.bridge.minLevel) : 0.5
        x: (582 - r) * root.s; y: (72 - r) * root.s
        width: 2 * r * root.s; height: 2 * r * root.s

        function setVolume(db) {
            if (!root.bridge) return
            const v = Math.max(root.bridge.minLevel, Math.min(root.bridge.maxLevel, db))
            root.bridge.volume = Math.round(v * 2) / 2
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {                                     // the travel
                fillColor: "transparent"
                strokeColor: root.colours.machineEdge
                strokeWidth: 4 * root.s
                capStyle: ShapePath.RoundCap
                PathAngleArc {
                    centerX: knob.width / 2; centerY: knob.height / 2
                    radiusX: knob.width / 2 - 3 * root.s; radiusY: radiusX
                    startAngle: 135; sweepAngle: 270
                }
            }
            ShapePath {                                     // the value
                fillColor: "transparent"
                strokeColor: Theme.accent
                strokeWidth: 4 * root.s
                capStyle: ShapePath.RoundCap
                PathAngleArc {
                    centerX: knob.width / 2; centerY: knob.height / 2
                    radiusX: knob.width / 2 - 3 * root.s; radiusY: radiusX
                    startAngle: 135; sweepAngle: Math.max(0.5, 270 * knob.fraction)
                }
            }
        }
        Rectangle {                                         // the cap
            id: cap
            anchors.centerIn: parent
            width: parent.width * 0.7; height: width; radius: width / 2
            color: root.colours.machineKnob
            border.color: root.colours.machineEdge
            Rectangle {                                     // the pointer
                id: pointer
                width: 3 * root.s; height: cap.height / 2 - 4 * root.s
                radius: width / 2
                color: root.colours.machineLabel
                x: (cap.width - width) / 2; y: 4 * root.s
                transform: Rotation {
                    origin.x: pointer.width / 2; origin.y: pointer.height
                    angle: -135 + 270 * knob.fraction
                }
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height + 2 * root.s
            text: "VOLUME"
            color: root.colours.machineLabel
            font.pixelSize: Math.max(8, 9 * root.s)
            font.letterSpacing: 1
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeVerCursor
            property real startY: 0
            property real startValue: 0
            onPressed: mouse => { startY = mouse.y; startValue = root.bridge ? root.bridge.volume : 0 }
            onPositionChanged: mouse => {
                if (!pressed || !root.bridge) return
                const range = root.bridge.maxLevel - root.bridge.minLevel
                knob.setVolume(startValue + (startY - mouse.y) / (160 * root.s) * range)
            }
            onDoubleClicked: knob.setVolume(0)
            onWheel: wheel => {
                if (root.bridge) knob.setVolume(root.bridge.volume + (wheel.angleDelta.y > 0 ? 0.5 : -0.5))
            }
        }
    }

    // The pads.
    Grid {
        id: grid
        x: 24 * root.s; y: 132 * root.s
        columns: 4
        spacing: 10 * root.s
        Repeater {
            id: pads
            model: root.layout
            delegate: Rectangle {
                id: pad
                required property int modelData
                readonly property int key: modelData
                readonly property string drum: root.bridge ? root.bridge.padDrum(key) : ""
                readonly property color tint: root.padColour(drum)
                property real glow: 0
                property int sounding: -1
                objectName: "electroPad_" + key
                width: 140.5 * root.s; height: 55 * root.s
                radius: 6 * root.s
                color: padMouse.pressed ? Qt.lighter(tint, 1.25) : Qt.darker(tint, 1.35)
                border.color: root.selectedDrum.length > 0 && root.selectedDrum === drum
                              ? root.colours.machineLabel : root.colours.machineEdge
                border.width: (root.selectedDrum === drum ? 2 : 1) * root.s

                function flash(velocity) {
                    padFade.stop()
                    pad.glow = 0.35 + 0.65 * velocity / 127
                    padFade.start()
                }
                NumberAnimation { id: padFade; target: pad; property: "glow"; to: 0; duration: 320; easing.type: Easing.OutQuad }

                Rectangle {                                 // the hit light
                    anchors.fill: parent
                    radius: parent.radius
                    color: pad.tint
                    opacity: pad.glow
                }
                Rectangle {                                 // a soft shine across the top
                    x: 4 * root.s; y: 3 * root.s
                    width: parent.width - 8 * root.s; height: parent.height * 0.35
                    radius: parent.radius
                    color: Qt.rgba(1, 1, 1, 0.06)
                }
                Text {
                    x: 10 * root.s
                    y: parent.height - height - 7 * root.s
                    text: root.bridge ? root.bridge.padName(pad.key) : ""
                    color: root.colours.machineLabel
                    font.pixelSize: Math.max(8, 11 * root.s)
                    font.bold: true
                    width: parent.width - 20 * root.s
                    elide: Text.ElideRight
                }
                MouseArea {
                    id: padMouse
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onPressed: mouse => {
                        if (!root.bridge) return
                        const dx = (mouse.x - width / 2) / (width / 2), dy = (mouse.y - height / 2) / (height / 2)
                        const d = Math.min(1, Math.sqrt(dx * dx + dy * dy))
                        pad.sounding = pad.key
                        root.bridge.play(pad.key, Math.max(20, Math.round(127 * (1.0 - 0.5 * d))))
                        lcd.line2 = root.bridge.padName(pad.key)
                        root.drumChosen(pad.drum)
                    }
                    onReleased: { if (pad.sounding >= 0 && root.bridge) root.bridge.stop(pad.sounding); pad.sounding = -1 }
                    onCanceled: { if (pad.sounding >= 0 && root.bridge) root.bridge.stop(pad.sounding); pad.sounding = -1 }
                }
            }
        }
    }

    // Every hit the kit plays lights its pad and shows on the display.
    Connections {
        target: root.bridge
        function onHit(key, velocity) {
            for (let i = 0; i < pads.count; ++i) {
                const pad = pads.itemAt(i)
                if (pad && pad.key === key) pad.flash(velocity)
            }
            if (root.bridge) lcd.line2 = root.bridge.padName(key)
        }
    }
}
