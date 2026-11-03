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

    StereoLevelMeterWrapper {
        id: meter
        onClearClipsRequested: root.clearClipsRequested()
    }

    spacing: 3
    Text {
        text: root.label
        color: Theme.textMuted
        font.pixelSize: 8
        Layout.alignment: Qt.AlignHCenter
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        implicitWidth: 14
        implicitHeight: 50
        color: "#17191e"
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
                        color: parent.classification === 2 ? Theme.record : parent.classification === 1 ? "#e5b56b" : "#74d69a"
                    }
                    Rectangle {
                        width: parent.width; height: 3
                        color: parent.clipped ? Theme.record : "#393b42"
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
