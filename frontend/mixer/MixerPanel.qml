import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var trackModel: null
    property var levels: EngineController.trackLevels
    readonly property int stripCount: trackStrips.count

    color: "#1d1d1d"
    border.color: Theme.divider
    Text {
        anchors.centerIn: parent
        visible: root.stripCount === 0
        text: "Add a track to show its mixer controls"
        color: Theme.textMuted
        font.pixelSize: 12
    }
    Flickable {
        id: mixerScroll
        objectName: "mixerScroll"
        anchors.fill: parent
        anchors.margins: 1
        clip: true
        contentWidth: stripRow.width
        contentHeight: height
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick

        Row {
            id: stripRow
            height: mixerScroll.height
            spacing: 0
            Repeater {
                id: trackStrips
                model: root.trackModel
                delegate: Rectangle {
                    id: strip
                    required property int index
                    required property int trackId
                    required property string name
                    required property color trackColour
                    required property bool isMuted
                    required property bool isSolo
                    required property real volume
                    readonly property var levels: root.levels[String(trackId)] ?? ({})
                    property real channelWidth: 108
                    objectName: "mixerStrip" + trackId
                    width: channelWidth
                    height: stripRow.height
                    color: index % 2 ? "#202020" : "#222222"
                    border.color: Theme.divider
                    Rectangle {
                        objectName: "mixerColour" + strip.trackId
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        height: 3
                        color: strip.trackColour
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6
                        Text {
                            objectName: "mixerName" + strip.trackId
                            text: strip.name
                            color: Theme.text
                            font.pixelSize: 10
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: strip.volume > 0 ? (20 * Math.log(strip.volume) / Math.LN10).toFixed(1) + " dB" : "−∞ dB"
                            color: Theme.textMuted
                            font.pixelSize: 10
                            Layout.alignment: Qt.AlignHCenter
                        }
                        RowLayout {
                            Layout.fillHeight: true
                            Layout.fillWidth: true
                            spacing: 4
                            Slider {
                                id: volumeSlider
                                objectName: "mixerVolume" + strip.trackId
                                Layout.fillHeight: true
                                Layout.preferredWidth: 24
                                Layout.alignment: Qt.AlignHCenter
                                orientation: Qt.Vertical
                                from: 0; to: 2; stepSize: 0.01
                                value: strip.volume
                                onMoved: root.trackModel.setVolume(strip.trackId, value)
                                ToolTip.visible: hovered
                                ToolTip.text: "Track volume " + Math.round(value * 100) + "%"
                            }
                            StereoLevelMeter {
                                objectName: "mixerInput" + strip.trackId
                                label: "IN"
                                Layout.fillHeight: true
                                Layout.fillWidth: true
                                leftDb: strip.levels.inputLeftDb ?? -120
                                rightDb: strip.levels.inputRightDb ?? -120
                                leftClipped: strip.levels.inputLeftClip ?? false
                                rightClipped: strip.levels.inputRightClip ?? false
                                onClearClipsRequested: EngineController.clearTrackClips(strip.trackId)
                            }
                            StereoLevelMeter {
                                objectName: "mixerOutput" + strip.trackId
                                label: "OUT"
                                Layout.fillHeight: true
                                Layout.fillWidth: true
                                leftDb: strip.levels.outputLeftDb ?? -120
                                rightDb: strip.levels.outputRightDb ?? -120
                                leftClipped: strip.levels.outputLeftClip ?? false
                                rightClipped: strip.levels.outputRightClip ?? false
                                onClearClipsRequested: EngineController.clearTrackClips(strip.trackId)
                            }
                        }
                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 4
                            ToolButton {
                                objectName: "mixerMute" + strip.trackId
                                property bool active: strip.isMuted
                                text: "M"; implicitWidth: 30; implicitHeight: 24
                                onClicked: root.trackModel.setMuted(strip.trackId, !active)
                                contentItem: Text { text: parent.text; color: parent.active ? "#ffb0a8" : Theme.textMuted; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { color: parent.active ? "#663238" : "#303030"; radius: 3 }
                                ToolTip.visible: hovered
                                ToolTip.text: active ? "Mute enabled" : "Mute disabled"
                            }
                            ToolButton {
                                objectName: "mixerSolo" + strip.trackId
                                property bool active: strip.isSolo
                                text: "S"; implicitWidth: 30; implicitHeight: 24
                                onClicked: root.trackModel.setSolo(strip.trackId, !active)
                                contentItem: Text { text: parent.text; color: parent.active ? "#ffe08a" : Theme.textMuted; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { color: parent.active ? "#665322" : "#303030"; radius: 3 }
                                ToolTip.visible: hovered
                                ToolTip.text: active ? "Solo enabled" : "Solo disabled"
                            }
                        }
                    }
                    Connections {
                        target: strip
                        function onVolumeChanged() { volumeSlider.value = strip.volume }
                    }
                    Rectangle {
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.right: parent.right
                        width: 7
                        color: resizeHandle.containsMouse || resizeHandle.pressed ? "#676767" : "transparent"
                        z: 2
                        MouseArea {
                            id: resizeHandle
                            objectName: "mixerResize" + strip.trackId
                            property real startX: 0
                            property real startWidth: 0
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.SizeHorCursor
                            onPressed: mouse => {
                                startX = mapToItem(root, mouse.x, mouse.y).x
                                startWidth = strip.channelWidth
                            }
                            onPositionChanged: mouse => {
                                if (pressed) {
                                    const pointerX = mapToItem(root, mouse.x, mouse.y).x
                                    strip.channelWidth = Math.max(82, Math.min(220, startWidth + pointerX - startX))
                                }
                            }
                        }
                    }
                }
            }
        }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
    }
}
