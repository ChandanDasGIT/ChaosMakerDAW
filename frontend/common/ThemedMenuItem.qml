import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A menu entry in the app's style: label on the left, optional shortcut text on the right.
// Checkable entries get a tick column in front of the label; submenu entries an arrow.
MenuItem {
    id: root
    property string shortcutText: ""
    // A checkable entry drawn as a white-outlined box (ticked when on), so it is seen off too.
    property bool boxed: false
    implicitHeight: 26
    leftPadding: Theme.margin
    rightPadding: Theme.margin
    indicator: Item {}
    arrow: Item {}
    contentItem: RowLayout {
        Rectangle {
            objectName: "menuItemBox"
            visible: root.checkable && root.boxed
            Layout.preferredWidth: 14; Layout.preferredHeight: 14
            Layout.rightMargin: 2
            radius: 3
            color: "transparent"
            border.color: root.enabled ? Theme.textStrong : Theme.textMuted
            border.width: 1
            Text {
                anchors.centerIn: parent
                text: root.checked ? Icons.check : ""
                color: root.enabled ? Theme.accent : Theme.textMuted
                font.pixelSize: Theme.captionSize
            }
        }
        Text {
            visible: root.checkable && !root.boxed
            text: root.checked ? Icons.check : ""
            color: root.enabled ? Theme.accent : Theme.textMuted
            font.pixelSize: Theme.bodySize
            Layout.preferredWidth: 16
        }
        Text {
            text: root.text
            color: !root.enabled ? Theme.textMuted : root.highlighted ? Theme.accent : Theme.text
            font.pixelSize: Theme.bodySize
            Layout.fillWidth: true
        }
        Text {
            visible: !!root.subMenu
            text: Icons.submenu
            color: Theme.textMuted
            font.pixelSize: Theme.bodySize
        }
        Text {
            visible: root.shortcutText.length > 0
            text: root.shortcutText
            color: Theme.textMuted
            font.pixelSize: Theme.captionSize
            font.family: Theme.monospaceFontFamily
            horizontalAlignment: Text.AlignRight
            Layout.leftMargin: 16
        }
    }
    background: Rectangle {
        color: root.highlighted ? Theme.tooltipBg : "transparent"
        radius: Theme.controlRadius
    }
}
