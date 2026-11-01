import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property real value: 120
    signal valueEdited(real value)
    implicitWidth: 124
    implicitHeight: 29
    radius: 6
    color: "#191a1e"
    border.color: field.activeFocus && !Number.isFinite(parse(field.text)) ? Theme.record
                  : field.activeFocus ? Theme.accent : "#3a3d45"

    function parse(text) {
        const trimmed = text.trim()
        // No validator on the TextField: temporary empty/partial text must remain editable.
        if (!/^\d+(?:[.,]\d{0,2})?$/.test(trimmed)) return NaN
        const number = Number(trimmed.replace(",", "."))
        return Number.isFinite(number) && number >= 0.01 && number <= 10000 ? number : NaN
    }
    function commit() {
        const candidate = parse(field.text)
        if (Number.isFinite(candidate) && candidate !== root.value)
            root.valueEdited(candidate)
        field.text = Number(root.value).toString()
    }
    function adjust(delta) {
        const candidate = parse(field.text)
        const next = Math.max(0.01, Math.min(10000, Math.round(((Number.isFinite(candidate) ? candidate : root.value) + delta) * 100) / 100))
        if (next !== root.value) root.valueEdited(next)
        field.text = Number(root.value).toString()
    }
    onValueChanged: {
        if (!field.activeFocus) field.text = Number(root.value).toString()
    }
    RowLayout {
        anchors.fill: parent
        anchors.margins: 2
        spacing: 0
        ToolButton {
            objectName: "bpmDecrease"
            Layout.preferredWidth: 23; Layout.fillHeight: true
            text: "−"
            onClicked: root.adjust(-1)
            contentItem: Text { text: "−"; color: Theme.text; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: parent.down ? "#414650" : parent.hovered ? "#2d3038" : "transparent"; radius: 4 }
            ToolTip.visible: hovered; ToolTip.text: "Decrease BPM by 1"
        }
        TextField {
            id: field
            objectName: "bpmField"
            Layout.fillWidth: true; Layout.fillHeight: true
            padding: 0
            text: Number(root.value).toString()
            color: Theme.text
            font.pixelSize: 12; font.bold: true
            horizontalAlignment: TextInput.AlignHCenter
            verticalAlignment: TextInput.AlignVCenter
            selectByMouse: true
            activeFocusOnTab: true
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            background: Item {}
            Accessible.name: "Tempo in BPM"
            onActiveFocusChanged: {
                if (activeFocus) Qt.callLater(function() { if (field.activeFocus) field.selectAll() })
            }
            onAccepted: { root.commit(); focus = false }
            onEditingFinished: root.commit()
            ToolTip.visible: hovered
            ToolTip.text: "Enter 0.01–10000 BPM; press Enter or click elsewhere to apply"
            ToolTip.delay: 450
        }
        ToolButton {
            objectName: "bpmIncrease"
            Layout.preferredWidth: 23; Layout.fillHeight: true
            text: "+"
            onClicked: root.adjust(1)
            contentItem: Text { text: "+"; color: Theme.text; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: parent.down ? "#414650" : parent.hovered ? "#2d3038" : "transparent"; radius: 4 }
            ToolTip.visible: hovered; ToolTip.text: "Increase BPM by 1"
        }
    }
}
