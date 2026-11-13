import QtQuick
import QtQuick.Controls

// Time signature input: numerator / denominator. Invalid input falls back to the current value.
Rectangle {
    id: root
    property int numerator: 4
    property int denominator: 4
    signal numeratorEdited(int value)
    signal denominatorEdited(int value)
    implicitWidth: 87
    implicitHeight: 26
    radius: Theme.controlRadius
    color: Theme.transport.field
    border.color: Theme.transport.fieldBorder

    Row {
        anchors.centerIn: parent
        spacing: 3
        TextField {
            id: numeratorField
            objectName: "meterNumerator"
            width: 32; height: 20
            text: String(root.numerator)
            color: Theme.text
            font.pixelSize: Theme.bodySize; font.bold: true
            horizontalAlignment: TextInput.AlignHCenter
            verticalAlignment: TextInput.AlignVCenter
            selectByMouse: true
            inputMethodHints: Qt.ImhDigitsOnly
            validator: IntValidator { bottom: 1; top: 2147483647 }
            Accessible.name: "Time signature numerator"
            background: Rectangle {
                radius: Theme.controlRadius
                color: numeratorField.activeFocus ? Theme.transport.fieldFocus : "transparent"
                border.color: numeratorField.activeFocus ? Theme.accent : "transparent"
            }
            onEditingFinished: {
                if (acceptableInput && Number(text) > 0)
                    root.numeratorEdited(Number(text))
                else
                    text = String(root.numerator)
            }
        }
        Text { text: "/"; anchors.verticalCenter: parent.verticalCenter; color: Theme.transport.separatorText; font.pixelSize: Theme.bodySize }
        TextField {
            id: denominatorField
            objectName: "meterDenominator"
            width: 32; height: 20
            text: String(root.denominator)
            color: Theme.text
            font.pixelSize: Theme.bodySize; font.bold: true
            horizontalAlignment: TextInput.AlignHCenter
            verticalAlignment: TextInput.AlignVCenter
            selectByMouse: true
            inputMethodHints: Qt.ImhDigitsOnly
            validator: IntValidator { bottom: 1; top: 2147483647 }
            Accessible.name: "Time signature denominator"
            background: Rectangle {
                radius: Theme.controlRadius
                color: denominatorField.activeFocus ? Theme.transport.fieldFocus : "transparent"
                border.color: denominatorField.activeFocus ? Theme.accent : "transparent"
            }
            onEditingFinished: {
                if (acceptableInput && Number(text) > 0)
                    root.denominatorEdited(Number(text))
                else
                    text = String(root.denominator)
            }
        }
    }

    // Typed text breaks the binding; put the project value back when it changes.
    onNumeratorChanged: numeratorField.text = String(root.numerator)
    onDenominatorChanged: denominatorField.text = String(root.denominator)
}
