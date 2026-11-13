import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root
    spacing: 2

    property var transport: EngineController
    property bool playing: root.transport ? root.transport.playing : false
    property bool recording: root.transport ? root.transport.recording : false
    property bool repeating: root.transport ? root.transport.repeating : false
    property bool punching: root.transport ? !!root.transport.punch : false
    readonly property var focusedItem: Window.window ? Window.window.activeFocusItem : null
    readonly property bool editingText: focusedItem instanceof TextInput || focusedItem instanceof TextEdit

    signal transportAction(string action)

    PlaybackControlsWrapper {
        id: playbackBridge
        onActionRequested: action => root.transportAction(action)
    }

    Shortcut {
        sequence: Shortcuts.keys["transport.playPause"]
        context: Qt.WindowShortcut
        enabled: root.visible && root.enabled && !root.editingText && !EngineController.projectLoading
        autoRepeat: false
        onActivated: playbackBridge.transportAction(root.playing ? "pause" : "play")
    }

    Shortcut {
        sequence: Shortcuts.keys["transport.record"]
        context: Qt.WindowShortcut
        enabled: root.visible && root.enabled && !root.editingText && !EngineController.projectLoading
        autoRepeat: false
        onActivated: playbackBridge.transportAction("record")
    }

    // Punch on / off (restMap G11; no key by default), like the button below.
    Shortcut {
        sequence: Shortcuts.keys["transport.punch"]
        context: Qt.WindowShortcut
        enabled: root.visible && root.enabled && !root.editingText && !EngineController.projectLoading
        autoRepeat: false
        onActivated: playbackBridge.transportAction("punch")
    }

    // Go to start (restMap G16; W by default), like the button below.
    Shortcut {
        sequence: Shortcuts.keys["transport.goToStart"]
        context: Qt.WindowShortcut
        enabled: root.visible && root.enabled && !root.editingText && !EngineController.projectLoading
        autoRepeat: false
        onActivated: playbackBridge.transportAction("rewind")
    }

    Repeater {
        model: [
            {glyph: Icons.goToStart, hintText: "Go to start" + (Shortcuts.texts["transport.goToStart"] ? " (" + Shortcuts.texts["transport.goToStart"] + ")" : ""), command: "rewind"},
            {glyph: Icons.skipForward, hintText: "Skip forward one bar", command: "forward"},
            {glyph: Icons.record, hintText: "Start/stop recording" + (Shortcuts.texts["transport.record"] ? " (" + Shortcuts.texts["transport.record"] + ")" : ""), command: "record"},
            {glyph: Icons.play, hintText: "Play" + (Shortcuts.texts["transport.playPause"] ? " (" + Shortcuts.texts["transport.playPause"] + ")" : ""), command: "play"},
            {glyph: Icons.pause, hintText: "Pause" + (Shortcuts.texts["transport.playPause"] ? " (" + Shortcuts.texts["transport.playPause"] + ")" : ""), command: "pause"},
            {glyph: Icons.stop, hintText: "Stop", command: "stop"},
            {glyph: Icons.loop, hintText: "Toggle repeat", command: "repeat"},
            {glyph: Icons.punch, hintText: "Punch in / out: record only inside the time selection"
                                           + (Shortcuts.texts["transport.punch"] ? " (" + Shortcuts.texts["transport.punch"] + ")" : ""),
             command: "punch"}
        ]
        delegate: ToolButton {
            id: transportButton
            objectName: "transport_" + command
            required property int index
            required property string glyph
            required property string hintText
            required property string command
            readonly property bool active: command === "record" ? root.recording || (root.transport && root.transport.countingIn)
                                         : command === "play" ? root.playing && !root.recording
                                         : command === "repeat" ? root.repeating
                                         : command === "punch" ? root.punching : false
            text: glyph
            Accessible.name: hintText
            Layout.preferredWidth: 32
            Layout.preferredHeight: 30
            contentItem: Text {
                text: transportButton.glyph
                color: index === 2 ? Theme.record : (transportButton.active ? Theme.accent : (transportButton.hovered ? Theme.text : Theme.textMuted))
                font.pixelSize: index === 2 ? 20 : 14
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: Theme.controlRadius
                color: index === 2 && transportButton.active ? Theme.recordActiveBg
                     : transportButton.down ? Theme.transport.buttonPressed
                     : transportButton.active ? Theme.accentBg
                     : transportButton.hovered ? Theme.transport.buttonHover : "transparent"
                // Flat: an outline only while the button is on.
                border.color: index === 2 && transportButton.active ? Theme.record
                            : transportButton.active ? Theme.accent : "transparent"
            }
            onClicked: playbackBridge.transportAction(command)
            ToolTip.visible: hovered
            ToolTip.text: hintText
            ToolTip.delay: 450
        }
    }

    // The readout: the time and the musical position side by side in one dark panel.
    Rectangle {
        objectName: "transportReadout"
        Layout.leftMargin: 10
        Layout.preferredHeight: 32
        Layout.preferredWidth: readoutRow.implicitWidth + 24
        radius: Theme.controlRadius
        color: Theme.transport.field
        border.color: Theme.transport.fieldBorder
        Row {
            id: readoutRow
            anchors.centerIn: parent
            spacing: 12
            // Transport clock, formatted by the backend (PlaybackControls::formatTime)
            Text {
                objectName: "transportClock"
                anchors.verticalCenter: parent.verticalCenter
                text: playbackBridge.formatTime(root.transport ? root.transport.positionSeconds : 0)
                color: Theme.text
                font.pixelSize: 18
                font.family: "monospace"
            }
            Rectangle { width: 1; height: 18; anchors.verticalCenter: parent.verticalCenter; color: Theme.transport.fieldBorder }
            // Musical position (bars.beats.hundredths) in the transport's meter, formatted by the backend
            Text {
                objectName: "transportBarsBeats"
                anchors.verticalCenter: parent.verticalCenter
                text: root.transport ? playbackBridge.formatBarsBeats(root.transport.positionBeats,
                                                                      root.transport.numerator,
                                                                      root.transport.denominator)
                                     : playbackBridge.formatBarsBeats(0, 4, 4)
                color: Theme.accent
                font.pixelSize: 18
                font.family: "monospace"
                ToolTip.visible: barsBeatsHover.hovered
                ToolTip.text: "Position in bars.beats"
                HoverHandler { id: barsBeatsHover }
            }
        }
    }
}
