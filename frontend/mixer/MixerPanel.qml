import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var channelWidths: [108, 108, 108, 108, 108, 108]
    property int resizingIndex: -1
    property real resizeStartX: 0
    property real resizeStartWidth: 0

    function totalChannelWidth() {
        var total = 0
        for (var i = 0; i < channelWidths.length; ++i)
            total += channelWidths[i]
        return total
    }

    function setChannelWidth(index, width) {
        var updatedWidths = channelWidths.slice()
        updatedWidths[index] = Math.max(82, Math.min(220, width))
        channelWidths = updatedWidths
    }

    color: "#1d1d1d"
    border.color: Theme.divider
    Flickable {
        id: mixerScroll
        anchors.fill: parent
        anchors.margins: 1
        clip: true
        contentWidth: root.totalChannelWidth()
        contentHeight: height
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick

        Row {
            id: stripRow
            width: root.totalChannelWidth()
            height: mixerScroll.height
            spacing: 0
            Repeater {
                model: ["MASTER", "DRUMS", "BASS", "KEYS", "AUDIO 1", "AUDIO 2"]
                delegate: Rectangle {
                width: root.channelWidths[index]
                height: stripRow.height
                color: index === 0 ? "#252525" : (index % 2 ? "#202020" : "#222222")
                border.color: Theme.divider
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6
                    Text { text: modelData; color: index === 0 ? Theme.accent : Theme.text; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1 }
                    Text { text: "−∞    0 dB"; color: Theme.textMuted; font.pixelSize: 10; Layout.alignment: Qt.AlignHCenter }
                    Slider { Layout.fillHeight: true; Layout.alignment: Qt.AlignHCenter; orientation: Qt.Vertical; from: 0; to: 1; value: index === 0 ? 0.78 : 0.68 }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 4
                        ToolButton {
                            text: "M"; implicitWidth: 30; implicitHeight: 24
                            contentItem: Text { text: parent.text; color: Theme.textMuted; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { color: "#303030"; radius: 3 }
                        }
                        ToolButton {
                            text: "S"; implicitWidth: 30; implicitHeight: 24
                            contentItem: Text { text: parent.text; color: Theme.textMuted; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { color: "#303030"; radius: 3 }
                        }
                    }
                    ComboBox { Layout.fillWidth: true; model: ["Stereo", "Mono"]; currentIndex: 0; implicitHeight: 25; font.pixelSize: 10 }
                }

                Rectangle {
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    width: 7
                    color: resizeHandle.containsMouse || root.resizingIndex === index ? "#676767" : "transparent"
                    z: 2
                    MouseArea {
                        id: resizeHandle
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.SizeHorCursor
                        onPressed: mouse => {
                            root.resizingIndex = index
                            root.resizeStartX = mapToItem(root, mouse.x, mouse.y).x
                            root.resizeStartWidth = root.channelWidths[index]
                        }
                        onPositionChanged: mouse => {
                            if (pressed && root.resizingIndex === index) {
                                var pointerX = mapToItem(root, mouse.x, mouse.y).x
                                root.setChannelWidth(index, root.resizeStartWidth + pointerX - root.resizeStartX)
                            }
                        }
                        onReleased: root.resizingIndex = -1
                    }
                }
            }
        }
        }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
    }
}
