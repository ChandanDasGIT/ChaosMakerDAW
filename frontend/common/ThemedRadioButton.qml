import QtQuick
import QtQuick.Controls

// A radio button in the app's style: a dark ring with a light outline, an accent dot when
// chosen. The label follows in the app's text colour.
RadioButton {
    id: root
    property int textSize: Theme.bodySize
    property bool wrap: false   // long labels wrap (give the control a width) instead of eliding
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45
    spacing: 8
    indicator: Rectangle {
        objectName: "radioButtonIndicator"
        x: root.leftPadding
        y: root.topPadding + Math.round((root.availableHeight - height) / 2)
        implicitWidth: 16; implicitHeight: 16
        radius: width / 2
        color: root.down ? Theme.fieldHover : Theme.field
        border.color: root.checked || root.hovered || root.visualFocus ? Theme.accent : Theme.fieldBorder
        Rectangle {
            anchors.centerIn: parent
            visible: root.checked
            width: 8; height: 8; radius: width / 2
            color: Theme.accent
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
