import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// KEYS: the computer keyboard plays the monitored or armed instrument tracks.
RowLayout {
    id: root
    property var engine: null
    readonly property alias keys: typingKeys
    signal toggled(bool on)
    spacing: 6

    TypingKeyWrap {
        id: typingKeys
        objectName: "typingKeyboard"
        engine: root.engine
    }

    Text { text: "KEYS"; color: Theme.textMuted; font.pixelSize: Theme.microSize; font.bold: true; font.letterSpacing: 1 }
    ToggleChip {
        objectName: "typingKeyboardButton"
        Layout.preferredHeight: 26
        implicitWidth: 64
        active: typingKeys.enabled
        hoverHighlight: true
        textSize: Theme.smallSize
        cornerRadius: Theme.controlRadius
        text: typingKeys.enabled ? "Sa = " + typingKeys.saNote : "Off"
        onClicked: {
            typingKeys.enabled = !typingKeys.enabled
            root.toggled(typingKeys.enabled)
        }
        tooltip: typingKeys.enabled
            ? "Keys on (velocity " + typingKeys.velocity + "): click the on-screen piano, or type "
              + "Tab Q W E R T Y U I O P [ ] Backspace for lower Pa to upper Ma (E = Sa), the number "
              + "row and \\ for the black keys. Z/X octave, C/V velocity. Plays tracks with MON or REC on. "
              + "Click to turn off."
            : "Play instruments from the on-screen piano and the computer keyboard"
    }
}
