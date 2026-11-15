import QtQuick
import QtQuick.Controls

// The audio device line at the far right of the top bar; click for the audio settings.
ToolButton {
    id: root
    objectName: "deviceSummaryButton"
    signal preferencesRequested()

    DeviceLineWrap {
        id: device
        objectName: "deviceLineWrap"
    }

    implicitHeight: 30
    leftPadding: 8
    rightPadding: 8
    onClicked: root.preferencesRequested()
    contentItem: Text {
        objectName: "deviceSummaryText"
        text: "[" + device.text + "]"
        color: root.hovered ? Theme.text : Theme.textMuted
        font.pixelSize: Theme.smallSize
        font.family: Theme.monospaceFontFamily
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideLeft
    }
    background: Rectangle {
        radius: Theme.controlRadius
        color: root.down ? Theme.controlBgActive : root.hovered ? Theme.controlBgHover : "transparent"
    }
    ToolTip.visible: hovered
    ToolTip.delay: 450
    ToolTip.text: device.tooltip
}
