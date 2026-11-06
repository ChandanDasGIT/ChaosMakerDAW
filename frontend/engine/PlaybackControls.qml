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
            {glyph: "|◀", hintText: "Go to start", command: "rewind"},
            {glyph: "▶|", hintText: "Skip forward one bar", command: "forward"},
            {glyph: "●", hintText: "Start/stop recording (R)", command: "record"},
            {glyph: "▶", hintText: "Play (Space)", command: "play"},
            {glyph: "Ⅱ", hintText: "Pause (Space)", command: "pause"},
            {glyph: "■", hintText: "Stop", command: "stop"},
            {glyph: "⟳", hintText: "Toggle repeat", command: "repeat"}
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
                color: index === 2 && transportButton.active ? "#633238"
                     : transportButton.down ? "#454851"
                     : transportButton.active ? "#203a45"
                     : transportButton.hovered ? "#363941" : "#2b2d32"
                border.color: index === 2 && transportButton.active ? Theme.record
                            : transportButton.active ? Theme.accent
                            : transportButton.hovered ? "#5a5e68" : "#41444c"
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
}
