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

    LevelMeterWrapper {
        id: meter
    }

    spacing: 4
    RowLayout {
        Layout.fillWidth: true
        Text { text: root.label; color: Theme.textMuted; font.pixelSize: Theme.microSize; Layout.fillWidth: true }
        Text {
            text: root.levelText
            color: root.clipped ? Theme.record : Theme.text
            font.pixelSize: Theme.microSize
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
