import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    property alias label: meter.label
    property alias levelDb: meter.levelDb
    property alias clipped: meter.clipped
    property alias showTarget: meter.showTarget
    readonly property real normalized: meter.normalized
    readonly property string levelText: meter.levelText
    readonly property int classification: meter.classification
    // Room for a label of `labelWidth` beside the widest reading: a width that never changes.
    function widthFor(labelWidth) { return Math.ceil(labelWidth + widestReading.advanceWidth) + 6 }

    LevelMeterWrapper {
        id: meter
    }

    spacing: 4
    RowLayout {
        Layout.fillWidth: true
        Text {
            objectName: "levelMeterLabel"
            text: root.label; color: Theme.textMuted; font.pixelSize: Theme.microSize
            Layout.fillWidth: true
            Layout.preferredWidth: 1   // The meter's width never follows its text.
            elide: Text.ElideRight
        }
        // Readings are "−∞" or one decimal down to −89.9 (backend valueformat::meterLevel).
        TextMetrics { id: widestReading; font.pixelSize: Theme.microSize; text: "−88.8" }
        Text {
            objectName: "levelMeterReading"
            text: root.levelText
            color: root.clipped ? Theme.record : Theme.text
            font.pixelSize: Theme.microSize
            // Fixed to the widest reading, so a changing number never moves the label.
            Layout.preferredWidth: Math.ceil(widestReading.advanceWidth)
            horizontalAlignment: Text.AlignRight
        }
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 10
        radius: 2; color: Theme.meterBackdrop
        Rectangle {
            width: parent.width * root.normalized
            height: parent.height; radius: 2
            color: root.classification === 2 ? Theme.meterClip : root.classification === 1 ? Theme.meterWarning : Theme.meterNormal
        }
        Rectangle {
            visible: root.showTarget
            x: parent.width * meter.normalizedTarget
            width: 1; height: parent.height
            color: Theme.text
        }
        Rectangle {
            anchors.right: parent.right
            width: 4; height: parent.height
            color: root.clipped ? Theme.meterClip : Theme.meterClipOff
        }
    }
}
