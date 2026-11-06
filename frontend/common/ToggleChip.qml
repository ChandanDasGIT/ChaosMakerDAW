import QtQuick
import QtQuick.Controls

// A small chip button: track card M / S / IN / MON / REC / FX, mixer M / S, pedal ON,
// metronome CLICK / COUNT. "active" lights it in its own colours (accent by default).
// Action chips (IN, FX) set hoverHighlight so they lighten under the mouse; toggles don't.
ToolButton {
    id: chip
    property bool active: false
    property color activeBg: Theme.accentBg
    property color activeBorder: Theme.accent
    property color activeText: Theme.accent
    property color idleBg: Theme.chip
    property color idleBorder: Theme.chipBorder
    property bool hoverHighlight: false
    property int textSize: 10
    property real cornerRadius: 4
    property string tooltip: ""

    hoverEnabled: true
    readonly property bool lit: hoverHighlight && hovered && !active
    opacity: enabled ? 1 : 0.45
    leftPadding: 2
    rightPadding: 2

    contentItem: Text {
        text: chip.text
        color: chip.active ? chip.activeText : chip.lit ? Theme.text : Theme.textMuted
        font.pixelSize: chip.textSize
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: chip.cornerRadius
        color: chip.active ? chip.activeBg : chip.lit ? Theme.chipHover : chip.idleBg
        border.color: chip.active ? chip.activeBorder : chip.lit ? Theme.chipBorderHover : chip.idleBorder
    }
    // Screen readers: the tooltip says what the chip is and its state ("Mute enabled").
    Accessible.name: tooltip.length > 0 ? tooltip : text
    ToolTip.visible: hovered && tooltip.length > 0
    ToolTip.text: tooltip
    ToolTip.delay: 450
}
