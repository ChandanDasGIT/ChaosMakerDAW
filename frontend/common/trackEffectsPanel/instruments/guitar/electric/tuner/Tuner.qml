import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The tuner pedal's display: string choice, note, needle and cents.
ColumnLayout {
    id: root
    property var effectsModel: null
    property var pedalId
    property var parameters: []
    property bool bypassed: false
    property bool active: true
    readonly property bool ready: !!effectsModel

    TunerWrap {
        id: tuner
        board: root.effectsModel
    }
    spacing: 10
    property var reading: ({})
    property int lastRevision: -1
    property int staleTicks: 0
    property bool hasPitch: !root.bypassed && staleTicks <= 4 && Number(reading.frequency) > 0
    property real cents: hasPitch ? Number(reading.cents) : 0
    property bool inTune: hasPitch && ready && tuner.inTune(cents)
    property int selection: {
        for (const parameter of root.parameters)
            if (parameter.id === "string") return Math.round(parameter.value)
        return 0
    }
    Text {
        text: root.ready ? tuner.referenceText : ""
        color: Theme.textMuted; font.pixelSize: Theme.smallSize
        Layout.alignment: Qt.AlignHCenter
    }
    PanelButton {
        text: "Auto-detect string"
        checkable: true
        checked: root.selection === 0
        Layout.fillWidth: true
        onClicked: root.effectsModel.setParameter(root.pedalId, "string", 0)
    }
    GridLayout {
        columns: 3
        Layout.fillWidth: true
        Repeater {
            model: root.ready ? tuner.stringLabels : []
            delegate: PanelButton {
                required property int index
                required property string modelData
                text: modelData
                Layout.fillWidth: true
                checkable: true
                checked: root.selection === index + 1
                onClicked: root.effectsModel.setParameter(root.pedalId, "string", index + 1)
            }
        }
    }
    Text {
        text: root.hasPitch ? root.reading.note : "—"
        color: root.inTune ? Theme.guitarSuite.inTune : Theme.text
        font.pixelSize: 34; font.bold: true
        Layout.alignment: Qt.AlignHCenter
    }
    Text {
        text: root.ready ? tuner.status(root.hasPitch, root.bypassed, root.cents) : ""
        color: root.inTune ? Theme.guitarSuite.inTune : Theme.text
        font.pixelSize: Theme.smallSize
        Layout.alignment: Qt.AlignHCenter
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 32
        color: Theme.guitarSuite.tunerScale; radius: Theme.controlRadius
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width * 0.06; height: parent.height
            color: Theme.guitarSuite.tunerZone
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 1; height: parent.height; color: Theme.textMuted
        }
        Rectangle {
            visible: root.hasPitch
            x: (parent.width - width) * (0.5 + Math.max(-50, Math.min(50, root.cents)) / 100)
            width: 4; height: parent.height
            color: root.inTune ? Theme.guitarSuite.inTune : Theme.guitarSuite.offTune
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Text { text: "−50 · LOW"; color: Theme.textMuted; font.pixelSize: Theme.microSize }
        Item { Layout.fillWidth: true }
        Text { text: "0"; color: Theme.textMuted; font.pixelSize: Theme.microSize }
        Item { Layout.fillWidth: true }
        Text { text: "HIGH · +50"; color: Theme.textMuted; font.pixelSize: Theme.microSize }
    }
    Text {
        text: root.hasPitch && root.ready ? tuner.formatCents(root.cents) : ""
        color: Theme.text; font.pixelSize: Theme.headingSize
        Layout.alignment: Qt.AlignHCenter
    }
    Text {
        text: root.hasPitch && root.ready ? tuner.formatFrequency(Number(root.reading.frequency), Number(root.reading.targetHz)) : ""
        color: Theme.textMuted; font.pixelSize: Theme.captionSize
        Layout.alignment: Qt.AlignHCenter
    }
    Timer {
        interval: 100
        running: root.active && root.visible && root.ready
        repeat: true
        onTriggered: {
            const reading = tuner.reading(root.pedalId)
            root.staleTicks = reading.revision === root.lastRevision ? root.staleTicks + 1 : 0
            root.lastRevision = reading.revision
            root.reading = reading
        }
    }
}
