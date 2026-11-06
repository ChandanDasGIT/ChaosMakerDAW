import QtQuick
import QtQuick.Controls

// A plain text button inside panels (presets, cabinet IR, tuner strings, EQ, "Custom colour…").
// Chip colours, lightens on hover; checkable buttons show their checked state in the accent.
Button {
    id: button
    property string tooltip: ""
    hoverEnabled: true
    implicitHeight: 26
    leftPadding: 10
    rightPadding: 10
    topPadding: 4
    bottomPadding: 4
    opacity: enabled ? 1 : 0.45

    contentItem: Text {
        text: button.text
        color: button.checked ? Theme.accent : button.hovered ? Theme.text : Theme.textMuted
        font.pixelSize: Theme.smallSize
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 4
        color: button.checked ? Theme.accentBg : button.down || button.hovered ? Theme.chipHover : Theme.chip
        border.color: button.checked ? Theme.accent : button.hovered ? Theme.chipBorderHover : Theme.chipBorder
    }
    ToolTip.visible: hovered && tooltip.length > 0
    ToolTip.text: tooltip
    ToolTip.delay: 450
}
