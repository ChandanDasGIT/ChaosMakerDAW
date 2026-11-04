import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root

    property real keyHeight: 18
    property int totalNotes: 128
    property int activeNote: 60 // C4 (Middle C) active by default

    signal keyClicked(int note)
    signal keyHovered(int note, string noteName)
    signal wheelScrolled(real deltaY)

    readonly property var noteNames: ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

    function isBlackKey(pitchClass) {
        return pitchClass === 1 || pitchClass === 3 || pitchClass === 6 || pitchClass === 8 || pitchClass === 10
    }

    function noteLabel(note) {
        const pitchClass = note % 12
        const octave = Math.floor(note / 12) - 1
        return noteNames[pitchClass] + octave
    }

    width: 90
    height: totalNotes * keyHeight
    implicitWidth: 90
    implicitHeight: totalNotes * keyHeight

    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        target: null
        onWheel: event => {
            const delta = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y
            root.wheelScrolled(delta)
        }
    }

    Column {
        width: parent.width
        height: parent.height

        // 128 semitones from 127 down to 0
        Repeater {
            model: root.totalNotes
            delegate: Item {
                id: keyRow
                required property int index
                readonly property int note: 127 - index
                readonly property int pitchClass: note % 12
                readonly property int octave: Math.floor(note / 12) - 1
                readonly property bool black: root.isBlackKey(pitchClass)
                readonly property bool isActive: (keyRow.note === root.activeNote)

                width: root.width
                height: root.keyHeight

                // Full-width natural key background (ivory white)
                Rectangle {
                    anchors.fill: parent
                    color: {
                        if (keyMouse.pressed && !keyRow.black) return "#bcc5d3"
                        if (keyMouse.containsMouse && !keyRow.black) return "#ffffff"
                        if (keyRow.isActive && !keyRow.black) return "#dbe0ea"
                        return "#f2f4f8"
                    }
                    border.color: "#8a909d"
                    border.width: 1

                    // Octave note name on C keys (C1, C2, C3, C4, C5)
                    Text {
                        visible: keyRow.pitchClass === 0
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: "C" + keyRow.octave
                        color: "#181a1f"
                        font.pixelSize: 11
                        font.bold: true
                    }
                }

                // Black key overlay on the left portion (x: 0 to 54px)
                Rectangle {
                    visible: keyRow.black
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 54
                    color: {
                        if (keyMouse.pressed) return "#484d59"
                        if (keyRow.isActive) return "#42454f" // active illuminated key like D#2 in demo
                        if (keyMouse.containsMouse) return "#2c2f38"
                        return "#0f1013"
                    }
                    border.color: "#050608"
                    border.width: 1

                    // Subtle 3D shine line on top
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: 1
                        color: keyRow.isActive ? "#636875" : "#2a2d36"
                    }
                }

                // Mouse interaction area
                MouseArea {
                    id: keyMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton
                    onEntered: {
                        root.activeNote = keyRow.note
                        root.keyHovered(keyRow.note, root.noteLabel(keyRow.note))
                    }
                    onClicked: {
                        root.activeNote = keyRow.note
                        root.keyClicked(keyRow.note)
                    }
                }
            }
        }
    }
}
