import QtQuick

// The eight chord strips of a Chords track, GarageBand style: each has its chord's name on top
// (click it to change the chord), a large chord area (both hands) and a bass area below (the
// bass alone). Pressing an area plays it on the track through the bridge (ChordsWrapper); lower
// on an area is louder. The strip the player is playing lights up.
Item {
    id: root
    property var bridge
    signal editRequested(int strip)

    readonly property int count: 8
    readonly property real gap: 8

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

                // The chord's name: click to choose another.
                Rectangle {
                    objectName: "chordStripName" + strip.index
                    width: parent.width
                    height: 34
                    radius: 5
                    color: nameArea.containsMouse ? Theme.chipHover : Theme.chip
                    border.color: nameArea.containsMouse ? Theme.chipBorderHover : Theme.chipBorder
                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 8
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        text: strip.empty ? "+" : strip.chord
                        color: strip.empty ? Theme.textMuted : Theme.text
                        font.pixelSize: 15
                        font.bold: true
                    }
                    MouseArea {
                        id: nameArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.editRequested(strip.index)
                    }
                    Accessible.role: Accessible.Button
                    Accessible.name: "Change the chord of strip " + (strip.index + 1)
                }

                Repeater {
                    model: [false, true]                       // the chord area, then the bass area
                    delegate: Rectangle {
                        id: area
                        required property bool modelData
                        readonly property bool bass: modelData
                        readonly property bool lit: root.bridge && root.bridge.activeStrip === strip.index
                                                    && root.bridge.activeBass === bass
                        objectName: (bass ? "chordStripBass" : "chordStripChord") + strip.index
                        width: strip.width
                        height: bass ? (strip.height - 34 - 12) * 0.3 : (strip.height - 34 - 12) * 0.7
                        radius: 6
                        opacity: strip.empty ? 0.4 : 1
                        color: lit ? Theme.accentBg : touch.pressed ? Theme.controlBgActive
                             : touch.containsMouse ? Theme.controlBgHover : Theme.cardBgAlt
                        border.color: lit ? Theme.accent : Theme.chipBorder
                        border.width: lit ? 2 : 1
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 8
                            text: area.bass ? "BASS" : ""
                            color: area.lit ? Theme.accent : Theme.textMuted
                            font.pixelSize: Theme.microSize
                            font.bold: true
                            font.letterSpacing: 0.8
                        }
                        MouseArea {
                            id: touch
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: !strip.empty
                            onPressed: mouse => root.bridge.press(strip.index, area.bass,
                                                                  Math.round(50 + 77 * Math.max(0, Math.min(1, mouse.y / height))))
                            onReleased: root.bridge.release(strip.index, area.bass)
                            onCanceled: root.bridge.release(strip.index, area.bass)
                        }
                        Accessible.role: Accessible.Button
                        Accessible.name: (area.bass ? "Bass of " : "Play ") + (strip.empty ? "empty strip" : strip.chord)
                    }
                }
            }
        }
    }
}
