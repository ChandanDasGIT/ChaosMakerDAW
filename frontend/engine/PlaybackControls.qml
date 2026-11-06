import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root
    spacing: 8

    property var transport: EngineController
    property bool playing: root.transport ? root.transport.playing : false
    property bool recording: root.transport ? root.transport.recording : false
    property bool repeating: root.transport ? root.transport.repeating : false
    readonly property var focusedItem: Window.window ? Window.window.activeFocusItem : null
    readonly property bool editingText: focusedItem instanceof TextInput || focusedItem instanceof TextEdit

    signal transportAction(string action)

    PlaybackControlsWrapper {
        id: playbackBridge
        onActionRequested: action => root.transportAction(action)
    }

    Shortcut {
        sequence: "Space"
        context: Qt.WindowShortcut
        enabled: root.visible && root.enabled && !root.editingText && !EngineController.projectLoading
        autoRepeat: false
        onActivated: playbackBridge.transportAction(root.playing ? "pause" : "play")
    }

    Shortcut {
        sequence: "R"
        context: Qt.WindowShortcut
        enabled: root.visible && root.enabled && !root.editingText && !EngineController.projectLoading
        autoRepeat: false
        onActivated: playbackBridge.transportAction("record")
    }

    Repeater {
        model: [
            {glyph: Icons.goToStart, hintText: "Go to start", command: "rewind"},
            {glyph: Icons.skipForward, hintText: "Skip forward one bar", command: "forward"},
            {glyph: Icons.record, hintText: "Start/stop recording (R)", command: "record"},
            {glyph: Icons.play, hintText: "Play (Space)", command: "play"},
            {glyph: Icons.pause, hintText: "Pause (Space)", command: "pause"},
            {glyph: Icons.stop, hintText: "Stop", command: "stop"},
            {glyph: Icons.loop, hintText: "Toggle repeat", command: "repeat"}
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
                                         : command === "repeat" ? root.repeating : false
            text: glyph
            Layout.preferredWidth: index < 2 ? 42 : 36
            Layout.preferredHeight: 36
            contentItem: Text {
                text: transportButton.glyph
                color: index === 2 ? Theme.record : (transportButton.active ? Theme.accent : (transportButton.hovered ? Theme.text : Theme.textMuted))
                font.pixelSize: index === 2 ? 18 : 12
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: 6
                color: index === 2 && transportButton.active ? Theme.recordActiveBg
                     : transportButton.down ? Theme.transport.buttonPressed
                     : transportButton.active ? Theme.accentBg
                     : transportButton.hovered ? Theme.transport.buttonHover : Theme.transport.button
                border.color: index === 2 && transportButton.active ? Theme.record
                            : transportButton.active ? Theme.accent
                            : transportButton.hovered ? Theme.transport.buttonBorderHover : Theme.transport.buttonBorder
            }
            onClicked: playbackBridge.transportAction(command)
            ToolTip.visible: hovered
            ToolTip.text: hintText
            ToolTip.delay: 450
        }
    }

    Rectangle {
        Layout.preferredWidth: 1
        Layout.preferredHeight: 34
        color: Theme.divider
        Layout.leftMargin: 8
        Layout.rightMargin: 8
    }

    // Transport clock, formatted by the backend (PlaybackControls::formatTime)
    Text {
        objectName: "transportClock"
        text: playbackBridge.formatTime(root.transport ? root.transport.positionSeconds : 0)
        color: Theme.text
        font.pixelSize: 15
        font.family: "monospace"
    }

    // Musical position (bars.beats.hundredths) in the transport's meter, formatted by the backend
    Text {
        objectName: "transportBarsBeats"
        Layout.leftMargin: 10
        text: root.transport ? playbackBridge.formatBarsBeats(root.transport.positionBeats,
                                                              root.transport.numerator,
                                                              root.transport.denominator)
                             : playbackBridge.formatBarsBeats(0, 4, 4)
        color: Theme.accent
        font.pixelSize: 15
        font.family: "monospace"
        ToolTip.visible: barsBeatsHover.hovered
        ToolTip.text: "Position in bars.beats"
        HoverHandler { id: barsBeatsHover }
    }
}
