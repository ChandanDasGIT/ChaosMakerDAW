import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../settings"
import "../engine"
import "../tempo"

Rectangle {
    id: root
    property var transport: EngineController
    property int timeSignature: 4
    property int timeSignatureDenominator: 4
    property real bpm: 120
    property int snapMode: 1
    property bool playing: false
    property bool recording: false
    property bool repeating: false
    readonly property var focusedItem: Window.window ? Window.window.activeFocusItem : null
    readonly property bool editingText: focusedItem instanceof TextInput || focusedItem instanceof TextEdit
    signal numeratorEdited(int value)
    signal denominatorEdited(int value)
    signal bpmEdited(real value)
    signal snapModeEdited(int value)
    signal transportAction(string action)
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

        // KEYS: the computer keyboard plays the monitored or armed instrument tracks.
        TypingKeyboardWrapper {
            id: typingKeys
            objectName: "typingKeyboard"
            engine: root.transport
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
                onClicked: typingKeys.enabled = !typingKeys.enabled
                tooltip: typingKeys.enabled
                    ? "Typing keyboard on (velocity " + typingKeys.velocity + "): Tab Q W E R T Y U I O P [ ] \\ "
                      + "play lower Pa to upper Ma (E = Sa), the number row and Backspace the black keys. "
                      + "Z/X octave, C/V velocity. Plays tracks with MON or REC on. Click to turn off."
                    : "Play instruments from the computer keyboard"
            }
        }

        ColumnLayout {
            spacing: 2
            enabled: !(root.transport && root.transport.countingIn)
            Text { text: "BPM"; color: Theme.textMuted; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
            TempoEditor {
                Layout.preferredWidth: 124
                Layout.preferredHeight: 29
                value: root.bpm
                onValueEdited: value => root.bpmEdited(value)
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
                onTransportAction: action => root.transportAction(action)
            }
        }
    }
}
