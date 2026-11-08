import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The floating on-screen piano (a virtual MIDI keyboard) for every instrument: all 88 keys,
// A0 to C8. Click a key to play it: the lower on the key, the louder. Drag across the keys to
// play them in turn. Keys light up while held here and when the KEYS typing keyboard plays
// them; the band above marks the keys KEYS reaches (Tab to backslash) at its current octave.
// The notes go where a MIDI keyboard's go: the instrument tracks with MON or REC on.
// keyShift is the selected track's scale changer (TrackListWrapper.selectedKeyShift): the piano
// shows what sounds. The band, "E = ..." and the lit keys move by it, and a clicked key sends
// the note that makes the instrument sound that key.
Window {
    id: root
    property var engine: null
    property var typingKeys: null                          // TypingKeyboardWrapper: its range and notes
    property int keyShift: 0                               // semitones the selected track moves its notes

    readonly property int lowestNote: 21                   // A0
    readonly property int highestNote: 108                 // C8
    // The KEYS typing keyboard's reach: Tab (lower Pa) to backslash (upper tivra Ma).
    readonly property int typingOctave: typingKeys ? typingKeys.octave : 4
    // Where E (Sa) and the reach sound: the keys KEYS sends, moved by the scale changer.
    readonly property int typingSa: 12 * (typingOctave + 1) + keyShift
    readonly property int typingLow: typingSa - 5
    readonly property int typingHigh: typingSa + 18
    property var lit: ({})                                 // MIDI note sent -> true while it sounds
    property int heldNote: -1                              // the key the mouse holds
    property int heldSent: -1                              // the note sent for it (ends it, whatever the shift does)
    function isLit(key) { return !!lit[key - keyShift] }

    objectName: "virtualKeyboardWindow"
    title: "Virtual piano"
    // Floats over the app and never takes the keyboard focus: shortcuts, KEYS and the main
    // window's controls keep working while it is open.
    flags: Qt.Tool | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    width: 1240
    height: 200
    minimumWidth: 620
    minimumHeight: 150
    color: Theme.panelBg
    // A window of its own does not inherit the main window's palette: without these the
    // tooltips fall back to the system's black on yellow.
    palette.toolTipBase: Theme.tooltipBg
    palette.toolTipText: Theme.tooltipText

    VirtualKeyboardWrapper {
        id: bridge
        objectName: "virtualKeyboardBridge"
        engine: root.engine
        onNoteSent: (note, velocity, on) => root.setLit(note, on)
    }
    Connections {
        target: root.typingKeys
        function onNoteSent(note, velocity, on) { root.setLit(note, on) }
    }

    onVisibleChanged: if (!visible) { bridge.releaseAll(); root.heldNote = -1; root.heldSent = -1; root.lit = ({}) }

    function setLit(note, on) {
        const next = Object.assign({}, root.lit)
        if (on) next[note] = true; else delete next[note]
        root.lit = next
    }

    function isBlack(note) { return [1, 3, 6, 8, 10].indexOf(note % 12) >= 0 }
    // The white keys, low to high, and the black keys.
    readonly property var whiteNotes: {
        const list = []
        for (let n = lowestNote; n <= highestNote; ++n) if (!isBlack(n)) list.push(n)
        return list
    }
    readonly property var blackNotes: {
        const list = []
        for (let n = lowestNote; n <= highestNote; ++n) if (isBlack(n)) list.push(n)
        return list
    }
    // A black key sits on the line after the white key just below it.
    function whiteIndex(note) { return whiteNotes.indexOf(isBlack(note) ? note - 1 : note) }
    function keyLeft(note) {
        return isBlack(note) ? (whiteIndex(note) + 1) * keys.whiteWidth - keys.blackWidth / 2
                             : whiteIndex(note) * keys.whiteWidth
    }
    function keyRight(note) { return keyLeft(note) + (isBlack(note) ? keys.blackWidth : keys.whiteWidth) }
    function inTypingRange(note) { return note >= typingLow && note <= typingHigh }

    // The note under a point of the keys, black keys first; -1 outside.
    function noteAt(x, y) {
        if (x < 0 || y < 0 || x >= keys.width || y >= keys.height) return -1
        if (y < keys.blackHeight) {
            for (const n of blackNotes) {
                const left = keyLeft(n)
                if (x >= left && x < left + keys.blackWidth) return n
            }
        }
        return whiteNotes[Math.min(whiteNotes.length - 1, Math.floor(x / keys.whiteWidth))]
    }
    // Lower on the key, louder: 30 at the top to 127 at the bottom.
    function velocityAt(y, note) {
        const length = isBlack(note) ? keys.blackHeight : keys.height
        return Math.max(30, Math.min(127, Math.round(30 + 97 * y / length)))
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text {
                objectName: "virtualKeyboardRangeLabel"
                text: "KEYS reach  " + bridge.noteName(root.typingLow) + " – " + bridge.noteName(root.typingHigh)
                      + "   E = " + bridge.noteName(root.typingSa)
                      + "   (Tab … \\, Z / X move it)"
                color: Theme.text
                font.pixelSize: Theme.smallSize
            }
            Text {
                text: "Plays the instrument tracks with MON or REC on"
                color: Theme.textMuted
                font.pixelSize: Theme.captionSize
                elide: Text.ElideRight
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
            }
        }

        // The band over the keys KEYS reaches; full strength while KEYS is on.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 6
            Rectangle {
                objectName: "virtualKeyboardTypingBand"
                x: root.keyLeft(Math.max(root.typingLow, root.lowestNote))
                width: root.keyRight(Math.min(root.typingHigh, root.highestNote)) - x
                height: parent.height
                radius: 3
                color: Theme.accent
                opacity: root.typingKeys && root.typingKeys.enabled ? 1.0 : 0.45
            }
        }

        Item {
            id: keys
            objectName: "virtualKeyboardKeys"
            Layout.fillWidth: true
            Layout.fillHeight: true
            readonly property real whiteWidth: width / root.whiteNotes.length
            readonly property real blackWidth: whiteWidth * 0.6
            readonly property real blackHeight: height * 0.62

            Repeater {
                model: root.whiteNotes
                Rectangle {
                    required property int modelData
                    readonly property int note: modelData
                    objectName: "virtualKey" + note
                    x: root.keyLeft(note)
                    width: keys.whiteWidth + 1
                    height: keys.height
                    radius: 3
                    color: root.isLit(note) ? Theme.accent
                         : root.inTypingRange(note) ? Theme.pianoRoll.whiteKeyActive : Theme.pianoRoll.whiteKey
                    border.color: Theme.pianoRoll.whiteKeyBorder
                    Text {
                        visible: parent.note % 12 === 0 && keys.whiteWidth >= 14
                        text: bridge.noteName(parent.note)
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 4
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: Theme.pianoRoll.whiteKeyLabel
                        font.pixelSize: 9
                    }
                }
            }
            Repeater {
                model: root.blackNotes
                Rectangle {
                    required property int modelData
                    readonly property int note: modelData
                    objectName: "virtualKey" + note
                    x: root.keyLeft(note)
                    width: keys.blackWidth
                    height: keys.blackHeight
                    radius: 2
                    color: root.isLit(note) ? Qt.darker(Theme.accent, 1.3)
                         : root.inTypingRange(note) ? Theme.pianoRoll.blackKeyActive : Theme.pianoRoll.blackKey
                    border.color: Theme.pianoRoll.blackKeyBorder
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                function press(mx, my) {
                    const note = root.noteAt(mx, my)
                    if (note === root.heldNote) return
                    if (root.heldSent >= 0) bridge.noteOff(root.heldSent)
                    root.heldNote = note
                    const sent = note - root.keyShift            // the instrument moves it back to this key
                    root.heldSent = note >= 0 && sent >= 0 && sent <= 127 ? sent : -1
                    if (root.heldSent >= 0) bridge.noteOn(root.heldSent, root.velocityAt(my, note))
                }
                function lift() {
                    if (root.heldSent >= 0) bridge.noteOff(root.heldSent)
                    root.heldNote = -1
                    root.heldSent = -1
                }
                onPressed: mouse => press(mouse.x, mouse.y)
                onPositionChanged: mouse => { if (pressed) press(mouse.x, mouse.y) }
                onReleased: lift()
                onCanceled: lift()
            }
        }

        Text {
            objectName: "virtualKeyboardShortcutHint"
            text: "Z / X octave down / up  ·  C / V velocity down / up"
            color: Theme.textMuted
            font.pixelSize: Theme.captionSize
        }
    }
}
