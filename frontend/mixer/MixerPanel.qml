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

    RowLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        // Pinned Master Bus channel strip on the absolute left
        Rectangle {
            id: masterStrip
            objectName: "mixerMasterStrip"
            property real masterChannelWidth: 124
            Layout.fillHeight: true
            Layout.preferredWidth: masterChannelWidth
            color: "#181818"
            border.color: Theme.divider

            TapHandler {
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: {
                    if (root.trackModel) {
                        root.trackModel.selectTrack(-1)
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 3
                color: Theme.accent
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                Text {
                    text: "MASTER"
                    color: Theme.accent
                    font.pixelSize: Theme.smallSize
                    font.bold: true
                    font.letterSpacing: 1
                    Layout.alignment: Qt.AlignHCenter
                }

                Text {
                    objectName: "mixerMasterVolumeText"
                    text: panelWrapper.formatMasterVolume(panelWrapper.masterVolume)
                    color: panelWrapper.masterMuted ? Theme.record : Theme.text
                    font.pixelSize: Theme.captionSize
                    font.family: Theme.monospaceFontFamily
                    Layout.alignment: Qt.AlignHCenter
                }

                RowLayout {
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    spacing: 4

                    Slider {
                        id: masterVolumeSlider
                        objectName: "mixerMasterFader"
                        Layout.fillHeight: true
                        Layout.preferredWidth: 24
                        Layout.alignment: Qt.AlignHCenter
                        orientation: Qt.Vertical
                        from: panelWrapper.minVolume
                        to: panelWrapper.maxVolume
                        stepSize: 0.01
                        value: panelWrapper.masterVolume
                        onMoved: panelWrapper.setMasterVolume(value)

                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onDoubleTapped: {
                                Qt.callLater(() => {
                                    panelWrapper.resetMasterVolume()
                                })
                            }
                        }

                        ToolTip.visible: hovered
                        ToolTip.text: panelWrapper.formatMasterVolumeTooltip(value)
                    }

                    Binding {
                        target: masterVolumeSlider
                        property: "value"
                        value: panelWrapper.masterVolume
                        when: !masterVolumeSlider.pressed
                    }

                    // dB scale for the meter, each mark at its real height on the meter bar
                    Item {
                        id: masterScale
                        Layout.fillHeight: true
                        Layout.preferredWidth: 20
                        Repeater {
                            model: [0, -6, -12, -24, -48, -60]
                            delegate: Text {
                                required property int modelData
                                objectName: "mixerMasterScale" + modelData
                                text: String(modelData)
                                color: Theme.textMuted
                                font.pixelSize: 8
                                font.family: Theme.monospaceFontFamily
                                width: masterScale.width
                                horizontalAlignment: Text.AlignRight
                                y: masterMeter.barItem.y + (1 - masterMeter.normalizedFor(modelData)) * masterMeter.barItem.height - height / 2
                            }
                        }
                    }

                    StereoLevelMeter {
                        id: masterMeter
                        objectName: "mixerMasterMeter"
                        label: "OUT"
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        leftDb: EngineController.monitorLevels.leftDb ?? -120
                        rightDb: EngineController.monitorLevels.rightDb ?? -120
                        leftClipped: EngineController.monitorLevels.leftClip ?? false
                        rightClipped: EngineController.monitorLevels.rightClip ?? false
                        onClearClipsRequested: EngineController.clearMonitorClips()
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 6

                    ToolButton {
                        id: masterMuteBtn
                        objectName: "mixerMasterMute"
                        property bool active: panelWrapper.masterMuted
                        text: "M"
                        implicitWidth: 32; implicitHeight: 22
                        onClicked: panelWrapper.setMasterMuted(!active)
                        contentItem: Text {
                            text: parent.text
                            color: parent.active ? "#ffb0a8" : Theme.textMuted
                            font.pixelSize: 10
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: parent.active ? "#663238" : Theme.controlBg
                            border.color: parent.active ? Theme.record : Theme.controlBorder
                            radius: 3
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: panelWrapper.formatMasterMuteTooltip(active)
                    }

                    ToolButton {
                        text: "CLR"
                        implicitWidth: 34
                        implicitHeight: 22
                        onClicked: EngineController.clearMonitorClips()
                        contentItem: Text {
                            text: parent.text
                            color: (EngineController.monitorLevels.leftClip || EngineController.monitorLevels.rightClip) ? Theme.meterClip : Theme.textMuted
                            font.pixelSize: 9
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 3
                            color: parent.down ? Theme.controlBgActive : parent.hovered ? Theme.controlBgHover : Theme.controlBg
                            border.color: Theme.controlBorder
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: "Clear master clip indicators"
                    }
                }
            }

            Rectangle {
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                width: 7
                color: masterResizeHandle.containsMouse || masterResizeHandle.pressed ? "#676767" : "transparent"
                z: 2

                Rectangle {
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    width: 1
                    color: Theme.divider
                }

                MouseArea {
                    id: masterResizeHandle
                    objectName: "mixerMasterResize"
                    property real startX: 0
                    property real startWidth: 0
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.SizeHorCursor
                    onPressed: mouse => {
                        startX = mapToItem(root, mouse.x, mouse.y).x
                        startWidth = masterStrip.masterChannelWidth
                    }
                    onPositionChanged: mouse => {
                        if (pressed) {
                            const pointerX = mapToItem(root, mouse.x, mouse.y).x
                            masterStrip.masterChannelWidth = panelWrapper.clampStripWidth(startWidth + pointerX - startX)
                        }
                    }
                }
            }
        }

        // Horizontally scrolling track strips
        Flickable {
            id: mixerScroll
            objectName: "mixerScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: stripRow.width
            contentHeight: height
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick

            Text {
                anchors.centerIn: parent
                visible: root.stripCount === 0
                text: "Add a track to show its mixer controls"
                color: Theme.textMuted
                font.pixelSize: 12
            }

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
                        required property var clips
                        readonly property var levels: root.levels[String(trackId)] ?? ({})
                        property real channelWidth: panelWrapper.defaultStripWidth
                        objectName: "mixerStrip" + trackId
                        width: channelWidth
                        height: stripRow.height
                        color: index % 2 ? "#202020" : "#222222"
                        border.color: Theme.divider

                        readonly property var targetClip: {
                            if (!clips || clips.length === 0) return null
                            if (root.trackModel && root.trackModel.selectedClipId >= 0) {
                                for (let i = 0; i < clips.length; ++i) {
                                    if (clips[i].clipId === root.trackModel.selectedClipId) {
                                        return clips[i]
                                    }
                                }
                            }
                            return clips[0]
                        }
                        readonly property bool hasClip: targetClip !== null
                        readonly property int targetClipId: hasClip ? targetClip.clipId : -1
                        readonly property real targetClipGain: hasClip ? (targetClip.gainDb ?? 0.0) : 0.0

                        TapHandler {
                            gesturePolicy: TapHandler.ReleaseWithinBounds
                            onTapped: {
                                if (root.trackModel) {
                                    root.trackModel.selectTrack(strip.trackId)
                                    if (strip.hasClip) {
                                        root.trackModel.selectClip(strip.trackId, strip.targetClipId)
                                    }
                                }
                            }
                        }
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
                                font.pixelSize: Theme.captionSize
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                objectName: "mixerClipGainText" + strip.trackId
                                text: strip.hasClip ? panelWrapper.formatClipGain(clipGainSlider.value) : "—"
                                color: strip.hasClip ? Theme.text : Theme.textMuted
                                font.pixelSize: Theme.captionSize
                                font.family: Theme.monospaceFontFamily
                                Layout.alignment: Qt.AlignHCenter
                            }
                            RowLayout {
                                Layout.fillHeight: true
                                Layout.fillWidth: true
                                spacing: 4
                                // Channel fader: gain of the selected clip on this track (or its first
                                // clip). Track volume lives in the track list.
                                Slider {
                                    id: clipGainSlider
                                    objectName: "mixerClipGain" + strip.trackId
                                    Layout.fillHeight: true
                                    Layout.preferredWidth: 24
                                    Layout.alignment: Qt.AlignHCenter
                                    orientation: Qt.Vertical
                                    enabled: strip.hasClip
                                    opacity: strip.hasClip ? 1.0 : 0.35
                                    from: panelWrapper.minClipGain; to: panelWrapper.maxClipGain; stepSize: panelWrapper.clipGainStep
                                    value: strip.targetClipGain
                                    // One mouse drag = one undo step: the first move starts it,
                                    // later moves of the same drag extend it. Audio follows live.
                                    property bool dragStepStarted: false
                                    onPressedChanged: if (pressed) dragStepStarted = false
                                    onMoved: {
                                        if (strip.hasClip) {
                                            panelWrapper.setClipGain(strip.trackId, strip.targetClipId, value, pressed && dragStepStarted)
                                            dragStepStarted = pressed
                                        }
                                    }
                                    TapHandler {
                                        acceptedButtons: Qt.LeftButton
                                        // Apply after the slider handles the mouse release.
                                        onDoubleTapped: {
                                            if (strip.hasClip) {
                                                Qt.callLater(() => {
                                                    panelWrapper.resetClipGain(strip.trackId, strip.targetClipId)
                                                })
                                            }
                                        }
                                    }
                                    ToolTip.visible: hovered
                                    ToolTip.text: strip.hasClip ? panelWrapper.formatClipGainTooltip(value) : "No clip on track"
                                }
                                Binding {
                                    target: clipGainSlider
                                    property: "value"
                                    value: strip.targetClipGain
                                    when: !clipGainSlider.pressed
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
                            // Names the fader above it: this strip's fader is clip gain
                            Text {
                                objectName: "mixerClipLabel" + strip.trackId
                                text: "CLIP"
                                color: Theme.textMuted
                                font.pixelSize: 8
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                Layout.preferredWidth: clipGainSlider.width
                                Layout.alignment: Qt.AlignLeft
                                Layout.leftMargin: clipGainSlider.x
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
}
