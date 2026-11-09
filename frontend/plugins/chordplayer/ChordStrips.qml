import QtQuick
import QtQuick.Controls

// The eight chord strips of a Chords track, GarageBand style: each has its chord's name on top
// (press it to play the strip's middle zone; the caret beside it changes the chord), a chord
// area (both hands) in three zones, high / middle / low = the same chord in a higher, middle or
// lower register (each zone names its range), and a bass area below (the bass alone). Pressing
// an area plays it on the track through the bridge (ChordsWrapper); lower on an area is louder.
// The strip the player is playing lights up; with bass with chords on, a chord lights its BASS
// area too, since it plays it.
Item {
    id: root
    property var bridge
    signal editRequested(int strip)

    readonly property int count: 8
    readonly property real gap: 8

    // A pressable area of one strip: a chord zone (0 low, 1 middle, 2 high) or the bass (-1).
    component StripArea: Rectangle {
        id: area
        property var bridge
        property int stripIndex: 0
        property string chord: ""
        property int zone: 1
        readonly property bool empty: chord.length === 0
        readonly property bool bass: zone < 0
        readonly property bool lit: bridge && bridge.activeStrip === stripIndex
                                    && (bass ? bridge.activeBass || bridge.bassWithChords   // the chord plays its bass too
                                             : !bridge.activeBass && bridge.activeZone === zone)
        radius: 6
        opacity: empty ? 0.4 : 1
        color: lit ? Theme.accentBg : touch.pressed ? Theme.controlBgActive
             : touch.containsMouse ? Theme.controlBgHover : Theme.cardBgAlt
        border.color: lit ? Theme.accent : Theme.chipBorder
        border.width: lit ? 2 : 1
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 8
            text: area.bass ? "BASS" : area.bridge ? ((area.bridge.zoneRanges[area.stripIndex] ?? [])[area.zone] ?? "") : ""
            color: area.lit ? Theme.accent : Theme.textMuted
            font.pixelSize: Theme.microSize
            font.bold: true
            font.letterSpacing: 0.8
        }
        MouseArea {
            id: touch
            anchors.fill: parent
            hoverEnabled: true
            enabled: !area.empty
            onPressed: mouse => area.bridge.press(area.stripIndex, area.bass,
                                                  Math.round(50 + 77 * Math.max(0, Math.min(1, mouse.y / height))),
                                                  area.bass ? 1 : area.zone)
            onReleased: area.bridge.release(area.stripIndex, area.bass)
            onCanceled: area.bridge.release(area.stripIndex, area.bass)
        }
        Accessible.role: Accessible.Button
        Accessible.name: (area.bass ? "Bass of " : ["Play low ", "Play ", "Play high "][area.zone])
                         + (area.empty ? "empty strip" : area.chord)
    }

    Row {
        anchors.fill: parent
        spacing: root.gap
        Repeater {
            model: root.count
            delegate: Column {
                id: strip
                required property int index
                readonly property string chord: root.bridge ? (root.bridge.chords[index] ?? "") : ""
                readonly property bool empty: chord.length === 0
                width: (root.width - root.gap * (root.count - 1)) / root.count
                height: root.height
                spacing: 6

                // The chord's name: press to play the whole strip; the caret changes the chord.
                Rectangle {
                    id: nameChip
                    objectName: "chordStripName" + strip.index
                    width: parent.width
                    height: 34
                    radius: 5
                    color: nameArea.containsMouse ? Theme.chipHover : Theme.chip
                    border.color: nameArea.containsMouse ? Theme.chipBorderHover : Theme.chipBorder
                    Text {
                        anchors.left: parent.left
                        anchors.right: caret.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 4
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        text: strip.empty ? "+" : strip.chord
                        color: strip.empty ? Theme.textMuted : Theme.text
                        font.pixelSize: 15
                        font.bold: true
                    }
                    MouseArea {
                        id: nameArea
                        anchors.left: parent.left
                        anchors.right: caret.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        // An empty strip has nothing to play: its name picks a chord.
                        onPressed: if (strip.empty) root.editRequested(strip.index); else root.bridge.pressWhole(strip.index, 100)
                        onReleased: if (!strip.empty) root.bridge.releaseWhole(strip.index)
                        onCanceled: if (!strip.empty) root.bridge.releaseWhole(strip.index)
                    }
                    Accessible.role: Accessible.Button
                    Accessible.name: strip.empty ? "Choose a chord for strip " + (strip.index + 1) : "Play " + strip.chord + " with its bass"

                    Rectangle {
                        id: caret
                        objectName: "chordStripCaret" + strip.index
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: 3
                        width: 22
                        height: parent.height - 6
                        radius: 4
                        color: caretArea.containsMouse ? Theme.controlBgHover : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: "\u25BE"
                            color: caretArea.containsMouse ? Theme.text : Theme.textMuted
                            font.pixelSize: 13
                        }
                        MouseArea {
                            id: caretArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.editRequested(strip.index)
                        }
                        ToolTip.visible: caretArea.containsMouse
                        ToolTip.delay: 500
                        ToolTip.text: "Change the chord"
                        Accessible.role: Accessible.Button
                        Accessible.name: "Change the chord of strip " + (strip.index + 1)
                        function click() { root.editRequested(strip.index) }
                    }
                }

                // The chord area: high, middle and low zones, top to bottom.
                Column {
                    readonly property real areaHeight: (strip.height - 34 - 12) * 0.7
                    width: strip.width
                    height: areaHeight
                    spacing: 3
                    Repeater {
                        model: [2, 1, 0]
                        delegate: StripArea {
                            required property int modelData
                            bridge: root.bridge
                            stripIndex: strip.index
                            chord: strip.chord
                            width: strip.width
                            zone: modelData
                            objectName: (zone === 1 ? "chordStripChord" : zone === 2 ? "chordStripHigh" : "chordStripLow") + strip.index
                            height: (parent.height - 2 * 3) / 3
                        }
                    }
                }

                StripArea {
                    bridge: root.bridge
                    stripIndex: strip.index
                    chord: strip.chord
                    width: strip.width
                    zone: -1
                    objectName: "chordStripBass" + strip.index
                    height: (strip.height - 34 - 12) * 0.3
                }
            }
        }
    }
}
