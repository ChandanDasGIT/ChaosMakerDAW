import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    property string label: ""
    property real levelDb: -120
    property bool clipped: false
    property bool showTarget: false
    spacing: 4
    RowLayout {
        Layout.fillWidth: true
        Text { text: root.label; color: Theme.textMuted; font.pixelSize: 9; Layout.fillWidth: true }
        Text {
            text: root.levelDb <= -90 ? "−∞" : root.levelDb.toFixed(1)
            color: root.clipped ? Theme.record : Theme.text
            font.pixelSize: 9
        }
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 10
        radius: 2; color: "#17191e"
        Rectangle {
            width: parent.width * Math.max(0, Math.min(1, (root.levelDb + 60) / 60))
            height: parent.height; radius: 2
            color: root.levelDb >= 0 ? Theme.record : root.levelDb > -12 ? "#e5b56b" : "#74d69a"
        }
        Rectangle {
            visible: root.showTarget
            x: parent.width * 0.8
            width: 1; height: parent.height
            color: Theme.text
        }
        Rectangle {
            anchors.right: parent.right
            width: 4; height: parent.height
            color: root.clipped ? Theme.record : "#393b42"
        }
    }
}
