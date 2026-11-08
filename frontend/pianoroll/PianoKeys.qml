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
    // Raw wheel deltas, modifiers and the y position (in this item) so the view can scroll or zoom.
    signal wheelScrolled(real pixelY, real angleY, int modifiers, real keyY)

    // PianoRollWrapper: note names and black keys come from the backend (notenames)
    property var noteSource: null
    // A drum clip: the keys the kit plays show the part's name instead of the note name
    property bool drumMode: false
    property string drumKit: ""                          // which kit names the keys: switching kits renames them

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
        onWheel: event => root.wheelScrolled(event.pixelDelta.y, event.angleDelta.y, event.modifiers, point.position.y)
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
                readonly property string drumName: root.drumMode && root.drumKit !== "" && root.noteSource ? root.noteSource.drumName(note) : ""
                readonly property bool isActive: (keyRow.note === root.activeNote)

                width: root.width
                height: root.keyHeight

                // Full-width natural key background (ivory white)
                Rectangle {
                    anchors.fill: parent
                    color: {
                        if (keyMouse.pressed && !keyRow.black) return Theme.pianoRoll.whiteKeyPressed
                        if (keyMouse.containsMouse && !keyRow.black) return Theme.pianoRoll.whiteKeyHover
                        if (keyRow.isActive && !keyRow.black) return Theme.pianoRoll.whiteKeyActive
                        return Theme.pianoRoll.whiteKey
                    }
                    border.color: Theme.pianoRoll.whiteKeyBorder
                    border.width: 1

                    // Note name on white keys (C4, D4, E4, F4, G4, A4, B4)
                    Text {
                        // Short rows: only C labels (none below 9 px) so labels never overlap
                        visible: keyRow.drumName === "" && !keyRow.black && root.keyHeight >= (keyRow.isC ? 9 : 12)
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.noteLabel(keyRow.note)
                        color: keyRow.isC ? Theme.pianoRoll.whiteKeyLabelC : Theme.pianoRoll.whiteKeyLabel
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
                        if (keyMouse.pressed) return Theme.pianoRoll.blackKeyPressed
                        if (keyRow.isActive) return Theme.pianoRoll.blackKeyActive // active illuminated key like D#2 in demo
                        if (keyMouse.containsMouse) return Theme.pianoRoll.blackKeyHover
                        return Theme.pianoRoll.blackKey
                    }
                    border.color: Theme.pianoRoll.blackKeyBorder
                    border.width: 1

                    // Note name on black keys (C#4, D#4, F#4, G#4, A#4)
                    Text {
                        visible: keyRow.drumName === "" && root.keyHeight >= 12
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.noteLabel(keyRow.note)
                        color: Theme.pianoRoll.blackKeyLabel
                        font.pixelSize: 9
                        font.bold: false
                    }

                    // Subtle 3D shine line on top
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: 1
                        color: keyRow.isActive ? Theme.pianoRoll.blackKeyShineActive : Theme.pianoRoll.blackKeyShine
                    }
                }

                // Drum clip: the kit's part on this key, where the note name would be
                Text {
                    objectName: "drumKeyLabel" + keyRow.note
                    visible: keyRow.drumName !== "" && root.keyHeight >= 9
                    x: 5
                    width: (keyRow.black ? 54 : root.width) - 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: keyRow.drumName
                    elide: Text.ElideRight
                    color: keyRow.black ? Theme.pianoRoll.blackKeyLabel : Theme.pianoRoll.whiteKeyLabelC
                    font.pixelSize: keyRow.black ? 8 : Math.min(10, Math.max(8, root.keyHeight - 8))
                    font.bold: !keyRow.black
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
