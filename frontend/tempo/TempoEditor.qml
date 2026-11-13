import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property real value: 120
    signal valueEdited(real value)
    readonly property int tapCount: editor.tapCount
    // Tap tempo (restMap G17): from the second tap, the BPM follows the taps.
    function tap() { editor.tap() }
    implicitWidth: 124
    implicitHeight: 26
    radius: Theme.controlRadius
    color: Theme.transport.field
    border.color: field.activeFocus && !editor.acceptableInput ? Theme.record
                  : field.activeFocus ? Theme.accent : Theme.transport.fieldBorder

    TempoEditorWrapper {
        id: editor
        value: root.value
        onValueEdited: value => root.valueEdited(value)
    }
    RowLayout {
        anchors.fill: parent
        anchors.margins: 2
        spacing: 0
        ToolButton {
            objectName: "bpmDecrease"
            Layout.preferredWidth: 23; Layout.fillHeight: true
            text: Icons.minus
            Accessible.name: "Decrease BPM"
            onClicked: editor.adjust(-1)
            contentItem: Text { text: Icons.minus; color: Theme.text; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: parent.down ? Theme.transport.stepPressed : parent.hovered ? Theme.transport.stepHover : "transparent"; radius: Theme.controlRadius }
            ToolTip.visible: hovered; ToolTip.text: "Decrease BPM by 1"
        }
        TextField {
            id: field
            objectName: "bpmField"
            Layout.fillWidth: true; Layout.fillHeight: true
            padding: 0
            text: editor.text
            onTextEdited: editor.text = text
            color: Theme.text
            font.pixelSize: Theme.bodySize; font.bold: true
            horizontalAlignment: TextInput.AlignHCenter
            verticalAlignment: TextInput.AlignVCenter
            selectByMouse: true
            activeFocusOnTab: true
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            background: Item {}
            Accessible.name: "Tempo in BPM"
            onActiveFocusChanged: {
                editor.editing = activeFocus
                if (activeFocus) Qt.callLater(function() { if (field.activeFocus) field.selectAll() })
            }
            onAccepted: { editor.commit(); focus = false }
            onEditingFinished: editor.commit()
            ToolTip.visible: hovered
            ToolTip.text: "Enter 0.01–10000 BPM; press Enter or click elsewhere to apply"
            ToolTip.delay: 450
        }
        ToolButton {
            objectName: "bpmIncrease"
            Layout.preferredWidth: 23; Layout.fillHeight: true
            text: Icons.plus
            Accessible.name: "Increase BPM"
            onClicked: editor.adjust(1)
            contentItem: Text { text: Icons.plus; color: Theme.text; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: parent.down ? Theme.transport.stepPressed : parent.hovered ? Theme.transport.stepHover : "transparent"; radius: Theme.controlRadius }
            ToolTip.visible: hovered; ToolTip.text: "Increase BPM by 1"
        }
    }
}
