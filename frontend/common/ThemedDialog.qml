import QtQuick
import QtQuick.Controls

// A dialog in the app's colours: the title, body and button row on the dark panel, the buttons
// in PanelButton's style. Use it like Dialog (title, contentItem, standardButtons, accepted ...).
Dialog {
    id: dialog
    modal: true
    background: Rectangle { color: Theme.panelBg; border.color: Theme.divider; radius: 8 }
    palette.windowText: Theme.text
    palette.text: Theme.text

    header: Text {
        objectName: "themedDialogTitle"
        text: dialog.title
        visible: text.length > 0
        color: Theme.text
        font.pixelSize: Theme.bodySize
        font.bold: true
        elide: Text.ElideRight
        padding: 16
        bottomPadding: 12
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 1; rightMargin: 1 }
            height: 1
            color: Theme.divider
        }
    }

    footer: DialogButtonBox {
        objectName: "themedDialogButtons"
        padding: 12
        spacing: 6
        background: Rectangle { color: "transparent" }
        delegate: PanelButton { implicitHeight: 28 }
    }
}
