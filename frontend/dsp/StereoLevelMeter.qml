import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property alias label: meter.label
    property alias leftDb: meter.leftDb
    property alias rightDb: meter.rightDb
    property alias leftClipped: meter.leftClipped
    property alias rightClipped: meter.rightClipped
    signal clearClipsRequested()
    // The bar itself, and the bar height (0..1) for a dB value: lets a scale line up with it.
    readonly property alias barItem: bar
    function normalizedFor(db) { return meter.normalizedFor(db) }

    StereoLevelMeterWrapper {
        id: meter
        onClearClipsRequested: root.clearClipsRequested()
    }

    spacing: 3
    Text {
        text: root.label
        color: Theme.textMuted
        font.pixelSize: Theme.microSize
        Layout.alignment: Qt.AlignHCenter
    }
    Rectangle {
        id: bar
        Layout.fillWidth: true
        Layout.fillHeight: true
        implicitWidth: 14
        implicitHeight: 50
        color: Theme.meterBackdrop
        radius: 2
        clip: true
        Row {
            anchors.fill: parent
            spacing: 1
            Repeater {
                model: 2
                delegate: Item {
                    required property int index
                    readonly property real normalized: index === 0 ? meter.leftNormalized : meter.rightNormalized
                    readonly property int classification: index === 0 ? meter.leftClassification : meter.rightClassification
                    readonly property bool clipped: index === 0 ? meter.leftClipped : meter.rightClipped
                    width: (parent.width - 1) / 2
                    height: parent.height
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: parent.height * parent.normalized
                        color: parent.classification === 2 ? Theme.meterClip : parent.classification === 1 ? Theme.meterWarning : Theme.meterNormal
                    }
                    Rectangle {
                        width: parent.width; height: 3
                        color: parent.clipped ? Theme.meterClip : Theme.meterClipOff
                    }
                }
            }
        }
        HoverHandler { id: hover }
        TapHandler { onTapped: root.clearClipsRequested() }
        ToolTip.visible: hover.hovered
        ToolTip.text: meter.tooltipText
    }
}
