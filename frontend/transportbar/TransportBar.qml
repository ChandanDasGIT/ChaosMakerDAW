import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../settings"
import "../engine"
import "../tempo"

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
    height: 72
    color: Theme.transport.bar
    border.color: Theme.divider

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 8

        PlaybackControls {
            id: playbackControls
            transport: root.transport
            playing: root.playing
            recording: root.recording
            repeating: root.repeating
            onTransportAction: action => root.transportAction(action)
        }

        Item { Layout.fillWidth: true }

        // KEYS: the computer keyboard and the floating on-screen piano play the monitored or
        // armed instrument tracks. One switch for both; closing the piano turns KEYS off too.
        TypingKeyboardWrapper {
            id: typingKeys
            objectName: "typingKeyboard"
            engine: root.transport
        }
        VirtualKeyboard {
            id: virtualKeyboard
            engine: root.transport
            typingKeys: typingKeys
            keyShift: root.trackModel ? root.trackModel.selectedKeyShift : 0
            onVisibleChanged: if (!visible) typingKeys.enabled = false
        }
        ColumnLayout {
            spacing: 2
            Text { text: "KEYS"; color: Theme.textMuted; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
            ToggleChip {
                objectName: "typingKeyboardButton"
                Layout.preferredHeight: 29
                implicitWidth: 64
                active: typingKeys.enabled
                hoverHighlight: true
                textSize: Theme.smallSize
                cornerRadius: 5
                text: typingKeys.enabled ? "Sa = " + typingKeys.saNote : "Off"
                onClicked: {
                    const on = !typingKeys.enabled
                    typingKeys.enabled = on
                    virtualKeyboard.visible = on
                    // The piano never takes the focus: the main window stays the active one.
                    const main = root.Window.window
                    if (on && main) Qt.callLater(() => main.requestActivate())
                }
                tooltip: typingKeys.enabled
                    ? "Keys on (velocity " + typingKeys.velocity + "): click the on-screen piano, or type "
                      + "Tab Q W E R T Y U I O P [ ] Backspace for lower Pa to upper Ma (E = Sa), the number "
                      + "row and \\ for the black keys. Z/X octave, C/V velocity. Plays tracks with MON or REC on. "
                      + "Click to turn off."
                    : "Play instruments from the on-screen piano and the computer keyboard"
            }
        }

        ColumnLayout {
            spacing: 2
            enabled: !(root.transport && root.transport.countingIn)
            Text { text: "BPM"; color: Theme.textMuted; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
            RowLayout {
                spacing: 4
                TempoEditor {
                    id: tempoEditor
                    Layout.preferredWidth: 124
                    Layout.preferredHeight: 29
                    value: root.bpm
                    onValueEdited: value => root.bpmEdited(value)
                }
            }
        }

        ColumnLayout {
            spacing: 2
            enabled: !(root.transport && root.transport.countingIn)
            Text { text: "TIME SIG"; color: Theme.textMuted; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
            MeterEditor {
                Layout.preferredWidth: 87
                Layout.preferredHeight: 29
                numerator: root.timeSignature
                denominator: root.timeSignatureDenominator
                onNumeratorEdited: value => root.numeratorEdited(value)
                onDenominatorEdited: value => root.denominatorEdited(value)
            }
        }

        ColumnLayout {
            spacing: 2
            // Empty label row so the button lines up with the BPM and time signature fields.
            Text { text: " "; font.pixelSize: 9 }
            QuickSettings {
                id: quickSettings
                Layout.preferredWidth: 34
                Layout.preferredHeight: 29
                transport: root.transport
                snapMode: root.snapMode
                onSnapModeEdited: value => root.snapModeEdited(value)
                ripple: root.ripple
                onRippleEdited: on => root.rippleEdited(on)
                onTransportAction: action => root.transportAction(action)
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
