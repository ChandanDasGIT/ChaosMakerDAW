import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var transport: EngineController
    property var trackModel: null                          // the project's tracks: the piano follows the selected one's scale
    property int timeSignature: 4
    property int timeSignatureDenominator: 4
    property real bpm: 120
    property int snapMode: 1
    property bool ripple: false
    property bool playing: false
    property bool recording: false
    property bool repeating: false
    readonly property var focusedItem: Window.window ? Window.window.activeFocusItem : null
    readonly property bool editingText: focusedItem instanceof TextInput || focusedItem instanceof TextEdit
    signal numeratorEdited(int value)
    signal denominatorEdited(int value)
    signal bpmEdited(real value)
    signal snapModeEdited(int value)
    signal rippleEdited(bool on)
    signal transportAction(string action)
    // Tap tempo (restMap G17), asked for from Options > Tap tempo and Shift+T: tap in time;
    // a pause over 2 s starts a new count.
    readonly property int tapCount: tempoEditor.tapCount
    function tapTempo() { if (!(root.transport && root.transport.countingIn)) tempoEditor.tap() }
    height: 46
    color: Theme.transport.bar
    border.color: Theme.panelEdge

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 4

        PlaybackControls {
            id: playbackControls
            transport: root.transport
            playing: root.playing
            recording: root.recording
            repeating: root.repeating
            onTransportAction: action => root.transportAction(action)
        }

        // One KEYS switch for the computer keyboard and the floating on-screen piano;
        // closing the piano turns KEYS off too.
        VirtualKeyboard {
            id: virtualKeyboard
            engine: root.transport
            typingKeys: typingKey.keys
            keyShift: root.trackModel ? root.trackModel.selectedKeyShift : 0
            onVisibleChanged: if (!visible) typingKey.keys.enabled = false
        }
        // Tempo, time signature, quick settings and KEYS sit at the far right.
        Item { Layout.fillWidth: true }
        RowLayout {
            objectName: "transportTempoGroup"
            spacing: 6
            Layout.leftMargin: 10
            enabled: !(root.transport && root.transport.countingIn)
            Text { text: "BPM"; color: Theme.textMuted; font.pixelSize: Theme.microSize; font.bold: true; font.letterSpacing: 1 }
            TempoEditor {
                id: tempoEditor
                Layout.preferredWidth: 112
                Layout.preferredHeight: 26
                value: root.bpm
                onValueEdited: value => root.bpmEdited(value)
            }
        }
        RowLayout {
            spacing: 6
            Layout.leftMargin: 6
            enabled: !(root.transport && root.transport.countingIn)
            Text { text: "SIG"; color: Theme.textMuted; font.pixelSize: Theme.microSize; font.bold: true; font.letterSpacing: 1 }
            MeterEditor {
                Layout.preferredWidth: 84
                Layout.preferredHeight: 26
                numerator: root.timeSignature
                denominator: root.timeSignatureDenominator
                onNumeratorEdited: value => root.numeratorEdited(value)
                onDenominatorEdited: value => root.denominatorEdited(value)
            }
        }
        QuickSettings {
            id: quickSettings
            Layout.leftMargin: 2
            Layout.preferredWidth: 30
            Layout.preferredHeight: 26
            transport: root.transport
            snapMode: root.snapMode
            onSnapModeEdited: value => root.snapModeEdited(value)
            ripple: root.ripple
            onRippleEdited: on => root.rippleEdited(on)
            onTransportAction: action => root.transportAction(action)
        }
        TypingKey {
            id: typingKey
            Layout.leftMargin: 6
            engine: root.transport
            onToggled: on => {
                virtualKeyboard.visible = on
                // The piano never takes the focus: the main window stays the active one.
                const main = root.Window.window
                if (on && main) Qt.callLater(() => main.requestActivate())
            }
        }
    }

    // Shift+T taps the tempo (plain T plays a note when the typing keyboard is on).
    Shortcut {
        sequence: Shortcuts.keys["transport.tapTempo"]
        enabled: !(root.transport && root.transport.countingIn)
        autoRepeat: false
        onActivated: tempoEditor.tap()
    }
}
