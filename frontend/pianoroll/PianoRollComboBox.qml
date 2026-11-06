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
        color: control.pressed ? Theme.pianoRoll.comboPressed : (control.hovered ? Theme.pianoRoll.comboHover : Theme.pianoRoll.combo)
        border.color: control.visualFocus || control.popup.visible ? Theme.accent : Theme.pianoRoll.panelBorder
    }

    contentItem: Text {
        leftPadding: 6
        rightPadding: control.indicator.width + 4
        text: control.displayText
        font: control.font
        color: Theme.pianoRoll.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: control.width - width - 6
        y: (control.height - height) / 2
        text: Icons.menuArrow
        color: Theme.pianoRoll.textMuted
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
            color: control.currentIndex === itemDelegate.index ? Theme.accent : Theme.pianoRoll.text
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: itemDelegate.highlighted ? Theme.pianoRoll.comboHover : "transparent"
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
            color: Theme.pianoRoll.comboPopup
            border.color: Theme.pianoRoll.readoutBorder
            radius: 2
        }
    }
}
