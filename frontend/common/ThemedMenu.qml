import QtQuick
import QtQuick.Controls

// A menu in the app's style: the panel frame, and ThemedMenuItem for entries made from a
// title (e.g. a submenu's own entry). Callers set position, width and, where needed, popupType.
Menu {
    padding: 4
    delegate: ThemedMenuItem {}
    background: Rectangle {
        color: Theme.panelBg
        border.color: Theme.divider
        radius: 6
    }
}
