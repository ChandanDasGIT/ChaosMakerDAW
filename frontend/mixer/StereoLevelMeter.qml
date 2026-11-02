import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property string label: ""
    property real leftDb: -120
    property real rightDb: -120
    property bool leftClipped: false
    property bool rightClipped: false
    signal clearClipsRequested()
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
                    readonly property real db: index === 0 ? root.leftDb : root.rightDb
                    readonly property bool clipped: index === 0 ? root.leftClipped : root.rightClipped
                    width: (parent.width - 1) / 2
                    height: parent.height
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: parent.height * Math.max(0, Math.min(1, (parent.db + 60) / 60))
                        color: parent.db >= 0 ? Theme.record : parent.db > -12 ? "#e5b56b" : "#74d69a"
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
        ToolTip.text: root.label + " L: " + (root.leftDb <= -90 ? "−∞" : root.leftDb.toFixed(1))
            + " dBFS / R: " + (root.rightDb <= -90 ? "−∞" : root.rightDb.toFixed(1)) + " dBFS\nClick to clear clip indicators"
    }
}
