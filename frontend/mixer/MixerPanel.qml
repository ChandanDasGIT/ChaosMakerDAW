import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var trackModel: null
    property var levels: EngineController.trackLevels
    readonly property int stripCount: trackStrips.count

    MixerPanelWrapper {
        id: panelWrapper
        trackList: root.trackModel instanceof TrackListWrapper ? root.trackModel : null
    }

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
                    property real channelWidth: panelWrapper.defaultStripWidth
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
                            text: panelWrapper.formatVolume(strip.volume)
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
                                from: panelWrapper.minVolume; to: panelWrapper.maxVolume; stepSize: 0.01
                                value: strip.volume
                                onMoved: root.trackModel ? root.trackModel.setVolume(strip.trackId, value) : panelWrapper.setVolume(strip.trackId, value)
                                TapHandler {
                                    acceptedButtons: Qt.LeftButton
                                    // Apply after the slider handles the mouse release.
                                    onDoubleTapped: Qt.callLater(() => root.trackModel ? root.trackModel.setVolume(strip.trackId, panelWrapper.defaultVolume) : panelWrapper.resetVolume(strip.trackId))
                                }
                                ToolTip.visible: hovered
                                ToolTip.text: panelWrapper.formatVolumeTooltip(value)
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
                                onClicked: root.trackModel ? root.trackModel.setMuted(strip.trackId, !active) : panelWrapper.setMuted(strip.trackId, !active)
                                contentItem: Text { text: parent.text; color: parent.active ? "#ffb0a8" : Theme.textMuted; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { color: parent.active ? "#663238" : "#303030"; radius: 3 }
                                ToolTip.visible: hovered
                                ToolTip.text: panelWrapper.formatMuteTooltip(active)
                            }
                            ToolButton {
                                objectName: "mixerSolo" + strip.trackId
                                property bool active: strip.isSolo
                                text: "S"; implicitWidth: 30; implicitHeight: 24
                                onClicked: root.trackModel ? root.trackModel.setSolo(strip.trackId, !active) : panelWrapper.setSolo(strip.trackId, !active)
                                contentItem: Text { text: parent.text; color: parent.active ? "#ffe08a" : Theme.textMuted; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { color: parent.active ? "#665322" : "#303030"; radius: 3 }
                                ToolTip.visible: hovered
                                ToolTip.text: panelWrapper.formatSoloTooltip(active)
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
                                    strip.channelWidth = panelWrapper.clampStripWidth(startWidth + pointerX - startX)
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
