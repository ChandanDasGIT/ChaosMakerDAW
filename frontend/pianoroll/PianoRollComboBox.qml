import QtQuick
import QtQuick.Controls
import ".."

// Compact dark drop-down matching the piano roll's blue-grey palette.
ComboBox {
    id: control

    implicitHeight: 22
    font.pixelSize: 11

    background: Rectangle {
        radius: 2
        color: control.pressed ? "#454955" : (control.hovered ? "#3c404b" : "#32353e")
        border.color: control.visualFocus || control.popup.visible ? "#60cdff" : "#1d1f24"
    }

    contentItem: Text {
        leftPadding: 6
        rightPadding: control.indicator.width + 4
        text: control.displayText
        font: control.font
        color: "#d8dce4"
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: control.width - width - 6
        y: (control.height - height) / 2
        text: "▾"
        color: "#8a909d"
        font.pixelSize: 9
    }

    delegate: ItemDelegate {
        id: itemDelegate
        required property var modelData
        required property int index
        width: control.width
        height: 22
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: itemDelegate.modelData
            font: control.font
            color: control.currentIndex === itemDelegate.index ? "#60cdff" : "#d8dce4"
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: itemDelegate.highlighted ? "#3c404b" : "transparent"
        }
    }

    popup: Popup {
        y: control.height + 1
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 260)
        padding: 1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            color: "#26282f"
            border.color: "#454a56"
            radius: 2
        }
    }
}
