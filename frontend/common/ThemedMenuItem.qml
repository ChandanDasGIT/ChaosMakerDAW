import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A menu entry in the app's style: label on the left, optional shortcut text on the right.
MenuItem {
    id: root
    property string shortcutText: ""
    implicitHeight: 30
    leftPadding: Theme.margin
    rightPadding: Theme.margin
    contentItem: RowLayout {
        Text {
            text: root.text
            color: !root.enabled ? Theme.textMuted : root.highlighted ? Theme.accent : Theme.text
            font.pixelSize: Theme.bodySize
            Layout.fillWidth: true
        }
        Text {
            visible: root.shortcutText.length > 0
            text: root.shortcutText
            color: Theme.textMuted
            font.pixelSize: Theme.captionSize
            font.family: Theme.monospaceFontFamily
        }
    }
    background: Rectangle {
        color: root.highlighted ? Theme.tooltipBg : "transparent"
        radius: 4
    }
}
