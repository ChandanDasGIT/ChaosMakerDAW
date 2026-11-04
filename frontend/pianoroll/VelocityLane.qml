import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Item {
    id: root

    property real beatWidth: 96
    property real contentWidth: 1000
    property real contentX: 0 // horizontal scroll offset shared with the note grid
    property real laneHeight: 90
    property var notes: []
    property int headerWidth: 86

    implicitHeight: laneHeight

    signal heightChangeRequested(real newHeight)

    Rectangle {
        anchors.fill: parent
        color: "#282a30"
        border.color: "#1d1f24"
        border.width: 1

        // Resizable Splitter Header with dotted grips (::::::::)
        Rectangle {
            id: splitterBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 8
            color: splitterMouse.pressed || splitterHover.hovered ? "#4a4f5b" : "#2f323a"
            border.color: "#1e2026"
            border.width: 1

            HoverHandler { id: splitterHover; cursorShape: Qt.SplitVCursor }

            // Texture dotted grip handles
            Row {
                anchors.centerIn: parent
                spacing: 3
                Repeater {
                    model: 8
                    delegate: Rectangle {
                        width: 2
                        height: 2
                        radius: 1
                        color: "#6b7280"
                    }
                }
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3
                Repeater {
                    model: 8
                    delegate: Rectangle {
                        width: 2
                        height: 2
                        radius: 1
                        color: "#6b7280"
                    }
                }
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: root.headerWidth + 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3
                Repeater {
                    model: 8
                    delegate: Rectangle {
                        width: 2
                        height: 2
                        radius: 1
                        color: "#6b7280"
                    }
                }
            }

            MouseArea {
                id: splitterMouse
                anchors.fill: parent
                cursorShape: Qt.SplitVCursor
                // Track the drag in scene coordinates: the bar itself moves as the lane resizes,
                // so local mouse.y would feed back into the delta and jitter.
                property real startY: 0
                property real startHeight: 0
                onPressed: mouse => {
                    startY = mapToItem(null, mouse.x, mouse.y).y
                    startHeight = root.laneHeight
                }
                onPositionChanged: mouse => {
                    if (pressed) {
                        const diff = startY - mapToItem(null, mouse.x, mouse.y).y
                        root.laneHeight = Math.max(45, Math.min(260, startHeight + diff))
                    }
                }
            }
        }

        // Left controller selector panel (aligned with PianoKeys)
        Rectangle {
            id: leftControlArea
            anchors.left: parent.left
            anchors.top: splitterBar.bottom
            anchors.bottom: parent.bottom
            width: root.headerWidth
            color: "#32353e"
            border.color: "#212329"

            Column {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4

                // Add CC / lane (+)
                Rectangle {
                    width: 16
                    height: 16
                    color: plusHover.containsMouse ? "#454a56" : "#2a2c33"
                    border.color: "#1e2025"
                    radius: 2

                    Text {
                        anchors.centerIn: parent
                        text: "+"
                        color: "#a0a6b2"
                        font.pixelSize: 11
                        font.bold: true
                    }

                    MouseArea {
                        id: plusHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                    }
                }

                // [ Velocity ▾ ] selector button
                Rectangle {
                    width: parent.width
                    height: 22
                    color: velBtnHover.containsMouse ? "#454a56" : "#2a2c33"
                    border.color: "#1a1b20"
                    radius: 3

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        spacing: 2

                        Text {
                            text: "Velocity"
                            color: "#c2c7d0"
                            font.pixelSize: 11
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }

                        Text {
                            text: "▾"
                            color: "#8a909d"
                            font.pixelSize: 9
                        }
                    }

                    MouseArea {
                        id: velBtnHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                    }
                }
            }
        }

        // Velocity stalks view area
        Item {
            anchors.left: leftControlArea.right
            anchors.right: parent.right
            anchors.top: splitterBar.bottom
            anchors.bottom: parent.bottom
            clip: true

            // Scrolled content, offset to stay aligned with the note grid above
            Item {
                id: stalkContent
                x: -root.contentX
                y: 0
                width: root.contentWidth
                height: parent.height

                // Vertical beat grid lines in velocity lane
                Repeater {
                    model: Math.ceil(root.contentWidth / root.beatWidth) + 1
                    delegate: Rectangle {
                        required property int index
                        x: index * root.beatWidth
                        y: 0
                        width: (index % 4 === 0) ? 2 : 1
                        height: parent.height
                        color: (index % 4 === 0) ? "#3b3f4a" : "#2f333c"
                    }
                }

                // Note velocity stalks
                Repeater {
                    model: root.notes
                    delegate: Item {
                        id: stalkDelegate
                        required property var modelData
                        readonly property real startBeat: modelData.startBeat ?? 0
                        readonly property int velocity: modelData.velocity ?? 100

                        // Centre the 2px stalk on the note's left edge (note blocks start at x + 1)
                        x: startBeat * root.beatWidth + 2 - width / 2
                        y: 0
                        width: 10
                        height: parent.height

                        readonly property real barHeight: Math.max(4, (velocity / 127.0) * (height - 8))

                        // Vertical stalk line
                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 2
                            height: stalkDelegate.barHeight
                            color: stalkHover.containsMouse ? "#60cdff" : "#3883a8"
                        }

                        // Stalk pin handle
                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: Math.max(0, stalkDelegate.barHeight - 3)
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 7
                            height: 7
                            radius: 3.5
                            color: stalkHover.containsMouse ? "#ffffff" : "#45a3d0"
                            border.color: "#1e2229"
                            border.width: 1
                        }

                        MouseArea {
                            id: stalkHover
                            anchors.fill: parent
                            hoverEnabled: true
                            ToolTip.visible: containsMouse
                            ToolTip.text: "Velocity: " + stalkDelegate.velocity
                        }
                    }
                }
            }
        }
    }
}
