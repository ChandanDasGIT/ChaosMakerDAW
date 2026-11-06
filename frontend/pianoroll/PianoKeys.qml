import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root

    property real keyHeight: 18
    property int totalNotes: 128
    property int activeNote: 60 // C4 (Middle C) active by default

    signal keyClicked(int note)
    signal keyPressed(int note, int velocity)
    signal keyReleased(int note)
    signal keyHovered(int note, string noteName)
    signal wheelScrolled(real deltaY)

    // PianoRollWrapper: note names and black keys come from the backend (notenames)
    property var noteSource: null

    function noteLabel(note) {
        return root.noteSource ? root.noteSource.noteName(note) : ""
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
                readonly property bool black: root.noteSource ? root.noteSource.isBlackKey(note) : false
                readonly property bool isC: root.noteSource ? root.noteSource.isOctaveStart(note) : false
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

                    // Note name on white keys (C4, D4, E4, F4, G4, A4, B4)
                    Text {
                        visible: !keyRow.black
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.noteLabel(keyRow.note)
                        color: keyRow.isC ? "#111317" : "#555b68"
                        font.pixelSize: keyRow.isC ? 11 : 10
                        font.bold: keyRow.isC
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

                    // Note name on black keys (C#4, D#4, F#4, G#4, A#4)
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.noteLabel(keyRow.note)
                        color: "#a6acb8"
                        font.pixelSize: 9
                        font.bold: false
                    }

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
                    onPressed: {
                        root.activeNote = keyRow.note
                        root.keyPressed(keyRow.note, 100)
                    }
                    onReleased: {
                        root.keyReleased(keyRow.note)
                    }
                    onCanceled: {
                        root.keyReleased(keyRow.note)
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
