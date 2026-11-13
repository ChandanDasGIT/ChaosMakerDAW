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

    color: Theme.mixer.background
    border.color: Theme.panelEdge

    RowLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        // Pinned Master Bus channel strip on the absolute left
        Rectangle {
            id: masterStrip
            objectName: "mixerMasterStrip"
            property real masterChannelWidth: 132
            Layout.fillHeight: true
            Layout.preferredWidth: masterChannelWidth
            color: Theme.mixer.master
            border.color: Theme.divider

            TapHandler {
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: {
                    if (root.trackModel) {
                        root.trackModel.selectTrack(-1)
                    }
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                anchors.topMargin: 6
                spacing: 4

                Text {
                    objectName: "mixerMasterVolumeText"
                    text: panelWrapper.formatMasterVolume(panelWrapper.masterVolume)
                    color: panelWrapper.masterMuted ? Theme.muteText : Theme.text
                    font.pixelSize: Theme.captionSize
                    font.family: Theme.monospaceFontFamily
                    Layout.alignment: Qt.AlignHCenter
                }

                RowLayout {
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    spacing: 4

                    HardwareSlider {
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
                        Layout.fillWidth: true
                        Repeater {
                            model: [0, -6, -12, -24, -48, -60]
                            delegate: Text {
                                required property int modelData
                                objectName: "mixerMasterScale" + modelData
                                text: String(modelData)
                                color: Theme.textMuted
                                font.pixelSize: Theme.microSize
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
                        Layout.preferredWidth: 20
                        Layout.fillWidth: false
                        leftDb: EngineController.monitorLevels.leftDb ?? -120
                        rightDb: EngineController.monitorLevels.rightDb ?? -120
                        leftClipped: EngineController.monitorLevels.leftClip ?? false
                        rightClipped: EngineController.monitorLevels.rightClip ?? false
                        onClearClipsRequested: EngineController.clearMonitorClips()
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 2

                    ToolButton {
                        id: masterMuteBtn
                        objectName: "mixerMasterMute"
                        property bool active: panelWrapper.masterMuted
                        text: "M"
                        implicitWidth: 28; implicitHeight: 20
                        onClicked: panelWrapper.setMasterMuted(!active)
                        contentItem: Text {
                            text: parent.text
                            color: parent.active ? Theme.muteText : Theme.textMuted
                            font.pixelSize: Theme.captionSize
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: parent.active ? Theme.muteBg : parent.hovered ? Theme.chipHover : Theme.chip
                            border.color: parent.active ? Theme.muteBorder : "transparent"
                            radius: Theme.controlRadius
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: panelWrapper.formatMasterMuteTooltip(active)
                    }

                    ToolButton {
                        objectName: "mixerMasterFx"
                        text: "FX"
                        implicitWidth: 30
                        implicitHeight: 20
                        enabled: !!root.trackModel
                        onClicked: masterEffects.open()
                        contentItem: Text {
                            text: parent.text
                            color: root.trackModel && root.trackModel.masterEffects.length > 0 ? Theme.accent : Theme.textMuted
                            font.pixelSize: Theme.microSize
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: Theme.controlRadius
                            color: parent.down ? Theme.controlBgActive : parent.hovered ? Theme.chipHover : Theme.chip
                            border.color: root.trackModel && root.trackModel.masterEffects.length > 0 ? Theme.accent : "transparent"
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: "Effects on the whole mix (master)"
                    }

                    ToolButton {
                        text: "CLR"
                        implicitWidth: 34
                        implicitHeight: 20
                        onClicked: EngineController.clearMonitorClips()
                        contentItem: Text {
                            text: parent.text
                            color: (EngineController.monitorLevels.leftClip || EngineController.monitorLevels.rightClip) ? Theme.meterClip : Theme.textMuted
                            font.pixelSize: Theme.microSize
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: Theme.controlRadius
                            color: parent.down ? Theme.controlBgActive : parent.hovered ? Theme.chipHover : Theme.chip
                            border.color: "transparent"
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: "Clear master clip indicators"
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: -6
                    Layout.rightMargin: -6
                    Layout.preferredHeight: 22
                    color: Theme.mixer.nameBand
                    Rectangle {
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        height: 2
                        color: Theme.accent
                    }
                    Text {
                        anchors.centerIn: parent
                        text: "MASTER"
                        color: Theme.accent
                        font.pixelSize: Theme.captionSize
                        font.bold: true
                        font.letterSpacing: 1
                    }
                }
            }

            Rectangle {
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                width: 7
                color: masterResizeHandle.containsMouse || masterResizeHandle.pressed ? Theme.mixer.resizeHandle : "transparent"
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
                // Centred in the visible area: a Flickable's content has no width while empty.
                x: Math.round((mixerScroll.width - width) / 2)
                y: Math.round((mixerScroll.height - height) / 2)
                visible: root.stripCount === 0
                text: "Add a track to show its mixer controls"
                color: Theme.textMuted
                font.pixelSize: Theme.bodySize
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
                        required property string trackColour   // "" = none
                        required property bool isMuted
                        required property bool isSolo
                        required property real volume
                        required property real pan
                        required property real stripWidth
                        required property var clips
                        readonly property var levels: root.levels[String(trackId)] ?? ({})
                        // Saved per track (stripWidth role); a resize drag changes it live
                        // and stores it once on release.
                        property real channelWidth: stripWidth
                        Binding on channelWidth { value: strip.stripWidth; when: !resizeHandle.pressed }
                        objectName: "mixerStrip" + trackId
                        width: channelWidth
                        height: stripRow.height
                        color: index % 2 ? Theme.mixer.stripAlt : Theme.mixer.strip
                        border.color: Theme.divider

                        // The clip this strip's fader controls: chosen by the backend
                        // (selected clip on this track, else its first clip)
                        readonly property int targetClipId: panelWrapper.targetClipId(clips ?? [], root.trackModel ? root.trackModel.selectedClipId : -1)
                        readonly property bool hasClip: targetClipId >= 0
                        readonly property real targetClipGain: panelWrapper.clipGainOf(clips ?? [], targetClipId)

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
                        // Top to bottom: clip gain and pan, the fader with its meters (the full
                        // height), CLIP / M / S, and the name on a band in the track colour.
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            anchors.topMargin: 6
                            spacing: 4
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text {
                                    objectName: "mixerClipGainText" + strip.trackId
                                    text: strip.hasClip ? panelWrapper.formatClipGain(clipGainSlider.value) : "—"
                                    color: strip.hasClip ? Theme.text : Theme.textMuted
                                    font.pixelSize: Theme.captionSize
                                    font.family: Theme.monospaceFontFamily
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                // Track pan knob: drag up/down, double-click to centre; value in the tooltip.
                                Dial {
                                    id: panKnob
                                    objectName: "mixerPan" + strip.trackId
                                    implicitWidth: 18; implicitHeight: 18
                                    from: panelWrapper.minPan; to: panelWrapper.maxPan; stepSize: panelWrapper.panStep
                                    inputMode: Dial.Vertical
                                    value: strip.pan
                                    readonly property string readout: panelWrapper.formatPan(value)
                                    onMoved: panelWrapper.setPan(strip.trackId, value)
                                    background: Rectangle {
                                        radius: width / 2
                                        color: panKnob.pressed ? Theme.controlBgActive : Theme.chip
                                        border.color: panKnob.readout === "C" ? Theme.mixer.knobBorder : Theme.accent
                                    }
                                    handle: Rectangle {
                                        // Pointer from the centre; straight up is centre pan
                                        x: panKnob.background.width / 2 - width / 2
                                        y: 2
                                        width: 2; height: panKnob.background.height / 2 - 2
                                        radius: 1
                                        color: panKnob.readout === "C" ? Theme.text : Theme.accent
                                        transform: Rotation {
                                            origin.x: 1; origin.y: panKnob.background.height / 2 - 2
                                            angle: panKnob.angle
                                        }
                                    }
                                    TapHandler {
                                        acceptedButtons: Qt.LeftButton
                                        // Apply after the dial handles the mouse release.
                                        onDoubleTapped: Qt.callLater(() => panelWrapper.resetPan(strip.trackId))
                                    }
                                    ToolTip.visible: hovered || pressed
                                    ToolTip.text: panelWrapper.formatPanTooltip(value)
                                    ToolTip.delay: pressed ? 0 : 450
                                    Accessible.name: "Pan " + readout
                                }
                                Binding {
                                    target: panKnob
                                    property: "value"
                                    value: strip.pan
                                    when: !panKnob.pressed
                                }
                            }
                            RowLayout {
                                Layout.fillHeight: true
                                Layout.fillWidth: true
                                spacing: 3
                                // Channel fader: gain of the selected clip on this track (or its first
                                // clip). Track volume lives in the track list.
                                ThemedSlider {
                                    id: clipGainSlider
                                    objectName: "mixerClipGain" + strip.trackId
                                    tint: strip.trackColour.length > 0 ? strip.trackColour : "transparent"
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
                                // dB scale, each mark at its real height on the meters beside it.
                                Item {
                                    id: stripScale
                                    Layout.fillHeight: true
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    Repeater {
                                        model: [0, -12, -24, -48]
                                        delegate: Text {
                                            required property int modelData
                                            text: String(modelData)
                                            color: Theme.textMuted
                                            font.pixelSize: Theme.microSize
                                            font.family: Theme.monospaceFontFamily
                                            width: stripScale.width
                                            horizontalAlignment: Text.AlignRight
                                            visible: stripScale.width >= 12
                                            y: inputMeter.barItem.y + (1 - inputMeter.normalizedFor(modelData)) * inputMeter.barItem.height - height / 2
                                        }
                                    }
                                }
                                StereoLevelMeter {
                                    id: inputMeter
                                    objectName: "mixerInput" + strip.trackId
                                    label: "IN"
                                    Layout.fillHeight: true
                                    Layout.preferredWidth: 14
                                    Layout.fillWidth: false
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
                                    Layout.preferredWidth: 14
                                    Layout.fillWidth: false
                                    leftDb: strip.levels.outputLeftDb ?? -120
                                    rightDb: strip.levels.outputRightDb ?? -120
                                    leftClipped: strip.levels.outputLeftClip ?? false
                                    rightClipped: strip.levels.outputRightClip ?? false
                                    onClearClipsRequested: EngineController.clearTrackClips(strip.trackId)
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                // Names the fader above it: this strip's fader is clip gain
                                Text {
                                    objectName: "mixerClipLabel" + strip.trackId
                                    text: "CLIP"
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.microSize
                                    font.bold: true
                                    horizontalAlignment: Text.AlignHCenter
                                    Layout.preferredWidth: clipGainSlider.width
                                }
                                Item { Layout.fillWidth: true }
                                ToggleChip {
                                    objectName: "mixerMute" + strip.trackId
                                    active: strip.isMuted
                                    activeBg: Theme.muteBg; activeBorder: Theme.muteBorder; activeText: Theme.muteText
                                    text: "M"; plain: true; implicitWidth: 24; implicitHeight: 20
                                    onClicked: root.trackModel ? root.trackModel.setMuted(strip.trackId, !active) : panelWrapper.setMuted(strip.trackId, !active)
                                    tooltip: panelWrapper.formatMuteTooltip(active)
                                }
                                ToggleChip {
                                    objectName: "mixerSolo" + strip.trackId
                                    active: strip.isSolo
                                    activeBg: Theme.soloBg; activeBorder: Theme.soloBorder; activeText: Theme.soloText
                                    text: "S"; plain: true; implicitWidth: 24; implicitHeight: 20
                                    onClicked: root.trackModel ? root.trackModel.setSolo(strip.trackId, !active) : panelWrapper.setSolo(strip.trackId, !active)
                                    tooltip: panelWrapper.formatSoloTooltip(active)
                                }
                            }
                            // The name band: the track colour, dimmed; grey for a track without one.
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.leftMargin: -6
                                Layout.rightMargin: -6
                                Layout.preferredHeight: 22
                                color: strip.trackColour.length > 0 ? Qt.darker(strip.trackColour, 2.2) : Theme.mixer.nameBand
                                Rectangle {
                                    objectName: "mixerColour" + strip.trackId
                                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                                    height: 2
                                    color: strip.trackColour.length > 0 ? strip.trackColour : "transparent"
                                }
                                Text {
                                    objectName: "mixerName" + strip.trackId
                                    anchors.fill: parent
                                    anchors.leftMargin: 6; anchors.rightMargin: 6
                                    text: strip.name
                                    color: Theme.text
                                    font.pixelSize: Theme.captionSize
                                    font.bold: true
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                        Rectangle {
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.right: parent.right
                            width: 7
                            color: resizeHandle.containsMouse || resizeHandle.pressed ? Theme.mixer.resizeHandle : "transparent"
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
                                onReleased: panelWrapper.setStripWidth(strip.trackId, strip.channelWidth)
                            }
                        }
                    }
                }
            }
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
        }
    }

    // The master track's FX window (restMap G9): the same window as a track's.
    TrackEffectsPanel {
        id: masterEffects
        objectName: "masterEffectsPanel"
        trackModel: root.trackModel
        trackId: root.trackModel ? root.trackModel.masterTrackId : -1
        trackName: "Master"
        trackType: "Master"
        eqBands: root.trackModel ? root.trackModel.masterEqBands : []
        effects: root.trackModel ? root.trackModel.masterEffects : []
        hasLiveInput: false
        recordDry: false
    }
}
