import QtQuick
import QtQuick.Controls

// A tick box in the app's style: a dark box with a light outline, lit in the accent with a
// tick when on. The label follows in the app's text colour.
CheckBox {
    id: root
    property int textSize: Theme.bodySize
    property bool wrap: false   // long labels wrap (give the control a width) instead of eliding
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45
    spacing: 8
    indicator: Rectangle {
        objectName: "checkBoxIndicator"
        x: root.leftPadding
        y: root.topPadding + Math.round((root.availableHeight - height) / 2)
        implicitWidth: 16; implicitHeight: 16
        radius: 3
        color: root.checked ? Theme.accentBg : root.down ? Theme.fieldHover : Theme.field
        border.color: root.checked || root.hovered || root.visualFocus ? Theme.accent : Theme.fieldBorder
        Text {
            anchors.centerIn: parent
            visible: root.checked
            text: Icons.check
            color: Theme.accent
            font.pixelSize: Theme.smallSize
            font.bold: true
        }
    }
    contentItem: Text {
        text: root.text
        color: Theme.text
        font.pixelSize: root.textSize
        leftPadding: root.indicator.width + root.spacing
        verticalAlignment: Text.AlignVCenter
        elide: root.wrap ? Text.ElideNone : Text.ElideRight
        wrapMode: root.wrap ? Text.WordWrap : Text.NoWrap
    }
}
