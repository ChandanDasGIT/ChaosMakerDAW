import QtQuick
import QtQuick.Controls

// A drop-down box in the app's style: face, arrow, list and entries all themed (Qt's default
// list is light). The colours default to the app's fields; an area can pass its own palette
// (the piano roll does, see PianoRollComboBox).
ComboBox {
    id: control
    property color face: Theme.field
    property color faceHover: Theme.fieldHover
    property color facePressed: Theme.fieldHover
    property color faceDisabled: Theme.fieldDisabled
    property color border: Theme.fieldBorder
    property color textColour: Theme.text
    property color textDisabled: Theme.textDisabled
    property color arrowColour: Theme.textMuted
    property color listBg: Theme.popupBg
    property color listBorder: Theme.popupBorder
    property color itemHighlight: Theme.tooltipBg
    property real cornerRadius: 5
    property real itemHeight: 26

    hoverEnabled: true
    implicitHeight: 28
    font.pixelSize: Theme.smallSize

    background: Rectangle {
        radius: control.cornerRadius
        color: !control.enabled ? control.faceDisabled
             : control.pressed ? control.facePressed
             : control.hovered ? control.faceHover : control.face
        border.color: control.visualFocus || control.activeFocus || control.popup.visible ? Theme.accent : control.border
    }

    contentItem: Text {
        leftPadding: 8
        rightPadding: control.indicator.width + 8
        text: control.displayText
        font: control.font
        color: control.enabled ? control.textColour : control.textDisabled
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: control.width - width - 8
        y: (control.height - height) / 2
        text: Icons.menuArrow
        color: control.arrowColour
        font.pixelSize: 9
    }

    delegate: ItemDelegate {
        id: entry
        required property var modelData
        required property int index
        width: control.width
        height: control.itemHeight
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            // Plain string lists and lists of objects with the box's textRole both work.
            text: control.textRole && entry.modelData && typeof entry.modelData === "object"
                  ? entry.modelData[control.textRole] : entry.modelData
            font: control.font
            color: control.currentIndex === entry.index ? Theme.accent : control.textColour
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: entry.highlighted ? control.itemHighlight : "transparent"
            radius: 3
        }
    }

    popup: Popup {
        y: control.height + 2
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 4, 300)
        padding: 2
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            color: control.listBg
            border.color: control.listBorder
            radius: control.cornerRadius
        }
    }
}
