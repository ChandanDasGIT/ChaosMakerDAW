import QtQuick
import QtQuick.Controls

// A button that opens a menu, in the face of the app's drop-downs (ThemedComboBox): the text on
// the left, the menu arrow (Icons.menuArrow) on the right, the accent border while the menu is
// open. Set `menu` to a ThemedMenu (usually a child of the button, with y: parent.height + 4);
// a click opens or closes it.
Button {
    id: control
    property Menu menu: null
    hoverEnabled: true
    implicitHeight: 28
    checkable: true
    checked: !!menu && menu.visible
    font.pixelSize: Theme.smallSize
    onClicked: if (menu) menu.visible ? menu.close() : menu.open()

    contentItem: Text {
        leftPadding: 8
        rightPadding: 20
        text: control.text
        font: control.font
        color: control.enabled ? Theme.text : Theme.textDisabled
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        Text {
            anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
            text: Icons.menuArrow
            color: Theme.textMuted
            font.pixelSize: Theme.microSize
        }
    }
    background: Rectangle {
        radius: Theme.controlRadius
        color: !control.enabled ? Theme.fieldDisabled : control.pressed || control.hovered ? Theme.fieldHover : Theme.field
        border.color: control.checked || control.visualFocus ? Theme.accent : Theme.fieldBorder
    }
}
