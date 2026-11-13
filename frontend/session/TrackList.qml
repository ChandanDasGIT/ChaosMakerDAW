import QtQuick
import QtQuick.Dialogs
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var trackModel
    property int laneHeight: 64
    property real scrollOffset: 0
    property int dropRow: -1
    // Audio files dropped here (restMap E2): onto the card's track, or a new track per file
    // below the cards, at the playhead. Needs the app's import job.
    property var importJob: null
    signal verticalScrollRequested(real amount)
    color: Theme.panelBg
    border.color: Theme.panelEdge

    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        blocking: true
        target: null
        onWheel: event => {
            const amount = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y * 0.75
            root.verticalScrollRequested(-amount)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            height: 38
            color: Theme.panelHeaderBg
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 8
                Text { text: "TRACKS"; color: Theme.textMuted; font.pixelSize: Theme.smallSize; font.bold: true; font.letterSpacing: 1; Layout.fillWidth: true }
                ToolButton {
                    id: addTrackButton
                    objectName: "addTrackButton"
                    text: Icons.plus
                    Accessible.name: "Add track"
                    implicitWidth: 28; implicitHeight: 28
                    onClicked: addTrackPopup.open()
                    contentItem: Text { text: Icons.plus; color: Theme.accent; font.pixelSize: 20; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { radius: Theme.controlRadius; color: parent.down ? Theme.chipHover : "transparent" }
                    ToolTip.visible: hovered
                    ToolTip.text: "Add track…"
                    ToolTip.delay: 450
                }
            }
        }
        Flickable {
            id: trackScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: Math.max(height, root.trackModel ? root.trackModel.layoutHeight : 0)
            contentY: Math.max(0, Math.min(contentHeight - height, root.scrollOffset))
            interactive: false
            boundsBehavior: Flickable.StopAtBounds

            MouseArea {
                objectName: "emptyTrackArea"
                x: 0
                y: root.trackModel ? root.trackModel.layoutHeight : 0
                width: trackScroll.width
                height: Math.max(0, trackScroll.contentHeight - y)
                enabled: !!root.trackModel
                acceptedButtons: Qt.LeftButton
                onDoubleClicked: root.trackModel.addVocalTrack()
                Text {
                    anchors.centerIn: parent
                    width: Math.max(0, parent.width - 24)
                    visible: !!root.trackModel && root.trackModel.count === 0
                    objectName: "emptyTrackHint"
                    text: "Click + to add a track, or double-click here for a Vocal track"
                    color: Theme.textMuted
                    font.pixelSize: Theme.smallSize
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }

            Repeater {
                model: root.trackModel
                delegate: Rectangle {
                    id: trackCard
                    required property int index
                    required property int trackId
                    required property string name
                    required property string kind
                    required property string trackType
                    required property string trackColour   // "" = none: the track is drawn plain
                    readonly property bool hasColour: trackColour.length > 0
                    required property bool isSolo
                    required property bool isMuted
                    required property bool isRecordArmed
                    required property bool monitorEnabled
                    required property string inputPort
                    required property real volume
                    required property bool hasLiveInput
                    required property bool canMonitor
                    required property bool recordDry
                    required property bool recordsWithFx
                    required property var eqBands
                    required property var effects
                    required property var clips
                    required property bool envelopeShown
                    required property bool envelopeOverClips
                    required property bool envelopeBypassed
                    property bool renaming: false
                    readonly property bool isSelected: !!root.trackModel && root.trackModel.selectedTrackId === trackCard.trackId

                    function beginRename() {
                        nameEditor.text = trackCard.name
                        renaming = true
                        nameEditor.forceActiveFocus()
                        nameEditor.selectAll()
                    }
                    function finishRename(commit) {
                        if (!renaming) return
                        const candidate = nameEditor.text
                        renaming = false
                        if (commit) root.trackModel.renameTrack(trackId, candidate)
                        if (nameEditor.activeFocus) trackCard.forceActiveFocus()
                    }

                    // Rows follow the backend layout: a shown volume lane (restMap G6) sits under
                    // the card and pushes the rows below it down. A card being removed has
                    // index -1 for a moment; it keeps a plain position then.
                    y: root.trackModel && index >= 0 && index < root.trackModel.rowTops.length
                       ? root.trackModel.rowTops[index] : Math.max(0, index) * root.laneHeight
                    width: trackScroll.width
                    height: root.laneHeight
                    color: isSelected ? Theme.trackList.rowSelected : (index % 2 ? Theme.trackList.rowAlt : Theme.trackList.row)
                    // Selected: the lighter background and a thin dim edge. Bright only as a drop target.
                    border.color: root.dropRow === index ? Theme.accent : isSelected ? Theme.trackList.rowSelectedBorder : Theme.divider
                    border.width: root.dropRow === index ? 2 : 1
                    Rectangle {
                        objectName: "trackColourAccent" + trackCard.trackId
                        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                        width: 3
                        // Always the track colour (nothing when it has none); selection shows
                        // through the border / background.
                        color: trackCard.hasColour ? trackCard.trackColour : "transparent"
                    }

                    MouseArea {
                        anchors.fill: parent
                        onPressed: if (root.trackModel) root.trackModel.selectTrack(trackCard.trackId)
                    }

                    // The volume lane's header, beside the lane in the timeline (restMap G6).
                    Rectangle {
                        objectName: "envelopeLaneHeader" + trackCard.trackId
                        visible: trackCard.envelopeShown && !trackCard.envelopeOverClips
                        y: trackCard.height
                        width: trackCard.width
                        height: root.trackModel ? root.trackModel.envelopeLaneHeight : 72
                        color: trackCard.color
                        border.color: Theme.divider
                        Rectangle {
                            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                            width: 3
                            color: trackCard.hasColour ? trackCard.trackColour : "transparent"
                        }
                        MouseArea {
                            anchors.fill: parent
                            onPressed: if (root.trackModel) root.trackModel.selectTrack(trackCard.trackId)
                        }
                        Text {
                            anchors.left: parent.left; anchors.leftMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Volume"
                            color: trackCard.envelopeBypassed ? Theme.textMuted : Theme.text
                            font.pixelSize: Theme.smallSize
                        }
                        ToggleChip {
                            objectName: "envelopeBypassButton" + trackCard.trackId
                            anchors.right: parent.right; anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            active: trackCard.envelopeBypassed
                            text: "BYPASS"; textSize: Theme.microSize
                            implicitWidth: 50; implicitHeight: 22
                            onClicked: root.trackModel.setEnvelopeBypassed(trackCard.trackId, !active)
                            tooltip: active ? "Envelope bypassed: the track plays at its fader level" : "Bypass the volume envelope"
                        }
                    }

                    // Three lines in a 64 px row: name (with the type and the remove cross), the buttons,
                    // the volume slider.
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 6
                        anchors.topMargin: 4
                        anchors.bottomMargin: 4
                        spacing: 3
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Item {
                                id: nameItem
                                Layout.fillWidth: true
                                // The name goes first: the type beside it gives way when both do not fit.
                                readonly property real wanted: Math.max(40, Math.min(20 + nameLabel.implicitWidth, trackCard.width - 60))
                                Layout.minimumWidth: wanted
                                implicitHeight: 18
                                ToolButton {
                                    objectName: "trackColourButton" + trackCard.trackId
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 12; height: 12
                                    onClicked: root.openColourPopup(trackCard)
                                    // An empty box while the track has no colour.
                                    background: Rectangle { color: trackCard.hasColour ? trackCard.trackColour : "transparent"; border.color: parent.hovered ? Theme.text : Theme.trackList.swatchBorder; radius: 3 }
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Choose track colour"
                                }
                                Text {
                                    id: nameLabel
                                    objectName: "trackNameLabel" + trackCard.trackId
                                    anchors.fill: parent
                                    anchors.leftMargin: 20
                                    verticalAlignment: Text.AlignVCenter
                                    text: trackCard.name
                                    color: Theme.text
                                    font.pixelSize: Theme.bodySize
                                    elide: Text.ElideRight
                                    visible: !trackCard.renaming
                                }
                                MouseArea {
                                    objectName: "trackHeader" + trackCard.trackId
                                    anchors.fill: parent
                                    anchors.leftMargin: 20
                                    visible: !trackCard.renaming
                                    hoverEnabled: true
                                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                    property real pressY: 0
                                    property bool reordering: false
                                    onPressed: mouse => {
                                        if (root.trackModel) root.trackModel.selectTrack(trackCard.trackId)
                                        pressY = mapToItem(root, mouse.x, mouse.y).y
                                        reordering = false
                                    }
                                    onPositionChanged: mouse => {
                                        if (!pressed) return
                                        if (Math.abs(mapToItem(root, mouse.x, mouse.y).y - pressY) > Qt.styleHints.startDragDistance)
                                            reordering = true
                                        if (reordering)
                                            root.dropRow = Math.max(0, Math.min(root.trackModel.count - 1,
                                                root.trackModel.rowAtY(mapToItem(trackScroll.contentItem, mouse.x, mouse.y).y)))
                                    }
                                    onReleased: {
                                        if (reordering && root.dropRow >= 0)
                                            root.trackModel.moveTrack(trackCard.trackId, root.dropRow)
                                        root.dropRow = -1
                                        reordering = false
                                    }
                                    onCanceled: { root.dropRow = -1; reordering = false }
                                    onDoubleClicked: trackCard.beginRename()
                                    ToolTip.visible: containsMouse
                                    ToolTip.text: "Drag to reorder; double-click to rename track"
                                    ToolTip.delay: 450
                                }
                                TextField {
                                    id: nameEditor
                                    objectName: "trackNameEditor" + trackCard.trackId
                                    anchors.fill: parent
                                    anchors.leftMargin: 20
                                    visible: trackCard.renaming
                                    maximumLength: 256
                                    selectByMouse: true
                                    color: Theme.text
                                    font.pixelSize: Theme.bodySize
                                    padding: 2
                                    background: Rectangle { color: Theme.trackList.nameEditor; border.color: Theme.accent; radius: 3 }
                                    onEditingFinished: trackCard.finishRename(true)
                                    Keys.onEscapePressed: event => {
                                        trackCard.finishRename(false)
                                        event.accepted = true
                                    }
                                }
                            }
                            Text {
                                objectName: "trackTypeLabel" + trackCard.trackId
                                text: trackCard.trackType.toUpperCase()
                                color: Theme.textMuted
                                font.pixelSize: Theme.microSize; font.letterSpacing: 0.7
                                elide: Text.ElideRight
                                // What the name leaves of the line (card margins 14, the cross 18, gaps 8).
                                Layout.maximumWidth: Math.max(0, Math.min(trackCard.width * 0.4, trackCard.width - 40 - nameItem.wanted))
                            }
                            CloseButton {
                                id: closeTrackButton
                                objectName: "trackRemoveButton" + trackCard.trackId
                                destructive: true
                                glyphSize: 14
                                implicitWidth: 18; implicitHeight: 18
                                Accessible.name: "Remove track"
                                tooltip: "Remove track"
                                onClicked: root.requestRemoveTrack(trackCard.trackId, trackCard.name, trackCard.clips ? trackCard.clips.length : 0)
                            }
                        }
                        // One line of buttons, also at the narrowest track list.
                        Row {
                            id: chipFlow
                            readonly property real chipHeight: 20
                            Layout.fillWidth: true
                            spacing: 2
                            ToggleChip {
                                id: muteButton
                                objectName: "trackMuteButton" + trackCard.trackId
                                active: trackCard.isMuted
                                activeBg: Theme.muteBg; activeBorder: Theme.muteBorder; activeText: Theme.muteText
                                text: "M"; plain: true; implicitWidth: 22; implicitHeight: chipFlow.chipHeight
                                onClicked: root.trackModel.setMuted(trackCard.trackId, !active)
                                tooltip: active ? "Mute enabled" : "Mute disabled"
                            }
                            ToggleChip {
                                id: soloButton
                                objectName: "trackSoloButton" + trackCard.trackId
                                active: trackCard.isSolo
                                activeBg: Theme.soloBg; activeBorder: Theme.soloBorder; activeText: Theme.soloText
                                text: "S"; plain: true; implicitWidth: 22; implicitHeight: chipFlow.chipHeight
                                onClicked: root.trackModel.setSolo(trackCard.trackId, !active)
                                tooltip: active ? "Solo enabled" : "Solo disabled"
                            }
                            ToggleChip {
                                id: inputButton
                                objectName: "trackInputButton" + trackCard.trackId
                                // Lit (accent text and border) once an input is chosen; still lightens on hover.
                                // Instrument tracks without an audio input show the shared MIDI input.
                                readonly property string shownInput: trackCard.hasLiveInput ? trackCard.inputPort
                                                                                            : EngineController.connectedMidiPort
                                active: shownInput.length > 0
                                activeBg: hovered ? Theme.chipHover : Theme.chip
                                hoverHighlight: true
                                text: "IN"; textSize: Theme.microSize; plain: true
                                implicitWidth: 24; implicitHeight: chipFlow.chipHeight
                                onClicked: root.openInputPopup(trackCard)
                                tooltip: shownInput.length > 0 ? shownInput
                                         : trackCard.hasLiveInput ? "Choose this track's input" : "Choose the MIDI input"
                            }
                            ToggleChip {
                                id: monitorButton
                                objectName: "trackMonitorButton" + trackCard.trackId
                                active: trackCard.monitorEnabled
                                enabled: trackCard.canMonitor
                                implicitWidth: 30; implicitHeight: chipFlow.chipHeight
                                text: "MON"; textSize: Theme.microSize; plain: true
                                onClicked: root.trackModel.setMonitorEnabled(trackCard.trackId, !active)
                                tooltip: trackCard.hasLiveInput
                                         ? (active ? "Disable live input monitoring" : "Monitor this track's input without arming recording")
                                         : (active ? "Stop playing this instrument from MIDI" : "Play this instrument live from your MIDI keyboard")
                            }
                            ToggleChip {
                                id: recordButton
                                objectName: "trackRecordButton" + trackCard.trackId
                                active: trackCard.isRecordArmed
                                activeBg: Theme.recordBg; activeBorder: Theme.record; activeText: Theme.textStrong
                                implicitWidth: 30; implicitHeight: chipFlow.chipHeight
                                text: "REC"; textSize: Theme.microSize; plain: true
                                onClicked: root.trackModel.setRecordArmed(trackCard.trackId, !active)
                                tooltip: active ? "Recording armed" : "Arm track for recording"
                            }
                            ToggleChip {
                                id: fxButton
                                objectName: "trackFxButton" + trackCard.trackId
                                hoverHighlight: true
                                text: "FX"; textSize: Theme.microSize; plain: true
                                implicitWidth: 24; implicitHeight: chipFlow.chipHeight
                                onClicked: root.openEffects(trackCard)
                                tooltip: "Browse track effects"
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            ThemedSlider {
                                id: volumeSlider
                                objectName: "trackVolume" + trackCard.trackId
                                tint: trackCard.hasColour ? trackCard.trackColour : "transparent"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 12
                                from: 0; to: 2; value: trackCard.volume
                                onMoved: root.trackModel.setVolume(trackCard.trackId, value)
                                TapHandler {
                                    acceptedButtons: Qt.LeftButton
                                    // Apply after the slider handles the mouse release.
                                    onDoubleTapped: Qt.callLater(() => root.trackModel.setVolume(trackCard.trackId, 1.0))
                                }
                                ToolTip.visible: hovered
                                ToolTip.text: "Track volume / monitor gain " + Math.round(value * 100) + "%; double-click for 0 dB"
                                ToolTip.delay: 450
                            }
                            Text { text: Math.round(volumeSlider.value * 100) + "%"; color: Theme.textMuted; font.pixelSize: Theme.microSize; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 29 }
                        }
                    }

                    Connections {
                        target: trackCard
                        function onVolumeChanged() { volumeSlider.value = trackCard.volume }
                    }
                }
            }
        }
    }

    // One shared copy of each per-track popup (plan 5.1) instead of one per track card.
    // Each follows the card that opened it and closes if that track is removed.
    property var effectsPageByTrack: ({})

    function openColourPopup(card) {
        colourPopup.card = card
        colourPopup.open()
    }
    function openInputPopup(card) {
        inputPopup.card = card
        inputPopup.open()
    }
    // The FX panel keeps the effect last shown for each track.
    function openEffects(card) {
        rememberEffectsPage()
        fxPopup.card = card
        // A track opened for the first time shows its chain's first effect (backend rule).
        fxPopup.selectedEffect = effectsPageByTrack[card.trackId] ?? ""
        fxPopup.open()
    }
    function rememberEffectsPage() {
        if (fxPopup.card) effectsPageByTrack[fxPopup.card.trackId] = fxPopup.selectedEffect
    }

    Popup {
        id: colourPopup
        property Item card: null
        readonly property int trackId: card ? card.trackId : -1
        readonly property string trackColour: card ? card.trackColour : ""
        onCardChanged: if (!card) close()
        objectName: "trackColourPopup" + trackId
        parent: Overlay.overlay
        width: 232
        padding: 12
        x: card ? Math.max(8, Math.min(parent.width - width - 8, card.mapToItem(parent, 0, 0).x)) : 0
        y: card ? Math.max(8, Math.min(parent.height - height - 8, card.mapToItem(parent, 0, 28).y)) : 0
        background: Rectangle { color: Theme.popupBg; border.color: Theme.popupBorder; radius: Theme.panelRadius }
        contentItem: Column {
            spacing: 10
            Text { text: "Track colour"; color: Theme.text; font.pixelSize: Theme.bodySize }
            Grid {
                columns: 6
                spacing: 7
                Repeater {
                    model: Theme.trackColors
                    delegate: Rectangle {
                        required property int index
                        required property string modelData
                        objectName: "trackColourChoice" + colourPopup.trackId + "_" + index
                        width: 28; height: 28; radius: Theme.controlRadius
                        color: modelData
                        border.width: colourPopup.trackColour === modelData ? 2 : 1
                        border.color: colourPopup.trackColour === modelData ? Theme.text : Theme.trackList.swatchChoiceBorder
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                root.trackModel.setTrackColour(colourPopup.trackId, modelData)
                                colourPopup.close()
                            }
                        }
                    }
                }
            }
            Row {
                spacing: 6
                PanelButton {
                    objectName: "customTrackColour" + colourPopup.trackId
                    text: "Custom colour…"
                    onClicked: {
                        colourDialog.card = colourPopup.card
                        colourPopup.close()
                        colourDialog.selectedColor = colourDialog.card.hasColour ? colourDialog.card.trackColour : Theme.trackColors[0]
                        colourDialog.open()
                    }
                }
                // Back to a plain track, as a new one is.
                PanelButton {
                    objectName: "noTrackColour" + colourPopup.trackId
                    text: "No colour"
                    enabled: colourPopup.trackColour.length > 0
                    onClicked: {
                        root.trackModel.clearTrackColour(colourPopup.trackId)
                        colourPopup.close()
                    }
                }
            }
        }
    }
    ColorDialog {
        // Inside the main window: a separate dialog window keeps app focus after closing,
        // which left the app without keyboard input (plan item 1.11).
        popupType: Popup.Item
        id: colourDialog
        property Item card: null
        readonly property int trackId: card ? card.trackId : -1
        onCardChanged: if (!card && visible) reject()
        objectName: "trackColourDialog" + trackId
        title: "Choose track colour"
        onAccepted: if (card) root.trackModel.setTrackColour(trackId, selectedColor.toString())
    }

    TrackEffectsPanel {
        id: fxPopup
        property Item card: null
        onCardChanged: if (!card) close()
        onClosed: root.rememberEffectsPage()
        objectName: "trackEffectsPanel" + trackId
        trackModel: root.trackModel
        trackId: card ? card.trackId : -1
        trackName: card ? card.name : ""
        trackType: card ? card.trackType : ""
        eqBands: card ? card.eqBands : []
        effects: card ? card.effects : []
        hasLiveInput: card ? card.hasLiveInput : true
        recordDry: card ? card.recordDry : false
    }

    Popup {
        id: inputPopup
        property Item card: null
        readonly property int trackId: card ? card.trackId : -1
        readonly property string trackName: card ? card.name : ""
        readonly property string inputPort: card ? card.inputPort : ""
        // Instrument tracks with no audio input (harmonium, piano...) take MIDI: one MIDI input
        // for the app, shared by every instrument track; MON or REC picks which ones play.
        readonly property bool midi: card ? !card.hasLiveInput : false
        readonly property var choices: midi ? EngineController.midiInputPorts : EngineController.inputDevices
        onCardChanged: if (!card) close()
        onAboutToShow: if (midi) EngineController.refreshMidiInputs()
        objectName: "trackInputPopup" + trackId
        parent: Overlay.overlay
        // Below the card's IN corner, but always inside the window.
        x: card ? Math.max(8, Math.min(card.mapToItem(Overlay.overlay, card.width - width, 34).x,
                                       Overlay.overlay.width - width - 8)) : 0
        y: card ? Math.max(8, Math.min(card.mapToItem(Overlay.overlay, 0, 34).y,
                                       Overlay.overlay.height - height - 8)) : 0
        width: Math.min(300, Overlay.overlay.width - 16)
        height: Math.min(Math.min(340, Overlay.overlay.height - 16),
                         Math.max(150, (midi ? 120 : 100) + choices.length * 34))
        padding: 0
        background: Rectangle { radius: Theme.panelRadius; color: Theme.popupBg; border.color: Theme.popupBorder }
        contentItem: ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 6
            Text {
                text: inputPopup.trackName + (inputPopup.midi ? " · MIDI input" : " · Input")
                color: Theme.text; font.pixelSize: Theme.bodySize; font.bold: true
                Layout.fillWidth: true; elide: Text.ElideRight
            }
            Text {
                visible: inputPopup.midi
                text: "One MIDI input for all instrument tracks. MON or REC picks which tracks play it."
                color: Theme.textMuted; font.pixelSize: Theme.captionSize
                Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
            Rectangle {
                objectName: inputPopup.midi ? "midiInputOff" : "defaultInput"
                Layout.fillWidth: true
                height: 32
                radius: Theme.controlRadius
                color: defaultInputArea.containsMouse ? Theme.trackList.optionHover : Theme.trackList.option
                border.color: (inputPopup.midi ? EngineController.connectedMidiPort.length === 0
                                               : inputPopup.inputPort.length === 0)
                              ? Theme.accent : Theme.trackList.optionBorder
                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 9
                    verticalAlignment: Text.AlignVCenter
                    text: inputPopup.midi ? "No MIDI input" : "Use default input"
                    color: Theme.text
                    font.pixelSize: Theme.captionSize
                }
                MouseArea {
                    id: defaultInputArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (inputPopup.midi) EngineController.connectMidiInput("")
                        else root.trackModel.setTrackInputPort(inputPopup.trackId, "")
                        inputPopup.close()
                    }
                }
            }
            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentHeight: Math.max(inputRows.implicitHeight, inputPopup.choices.length === 0 ? 34 : 0)
                boundsBehavior: Flickable.StopAtBounds
                Text {
                    visible: inputPopup.choices.length === 0
                    width: parent.width
                    height: 34
                    wrapMode: Text.WordWrap
                    text: inputPopup.midi ? "No MIDI devices found. Switch the keyboard on, then open this again."
                                          : "No physical input devices found"
                    color: Theme.textMuted
                    font.pixelSize: Theme.captionSize
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 8
                }
                Column {
                    id: inputRows
                    width: parent.width
                    Repeater {
                        model: inputPopup.choices
                        delegate: Rectangle {
                            required property string modelData
                            required property int index
                            objectName: (inputPopup.midi ? "midiInput" : "audioInput") + index
                            width: inputRows.width
                            height: 34
                            radius: Theme.controlRadius
                            color: inputRowArea.containsMouse ? Theme.trackList.optionHover : "transparent"
                            border.color: (inputPopup.midi ? EngineController.connectedMidiPort === modelData
                                                           : inputPopup.inputPort === EngineController.inputPortForDevice(index))
                                ? Theme.accent : "transparent"
                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 9
                                anchors.rightMargin: 8
                                text: modelData
                                color: Theme.text
                                font.pixelSize: Theme.captionSize
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideMiddle
                            }
                            MouseArea {
                                id: inputRowArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    if (inputPopup.midi) EngineController.connectMidiInput(modelData)
                                    else root.trackModel.setTrackInputPort(
                                        inputPopup.trackId, EngineController.inputPortForDevice(index))
                                    inputPopup.close()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Deleting a track with clips asks first (it can be undone with Ctrl+Z, which restores the
    // track, its clips and its pedalboard). Empty tracks are removed straight away.
    function requestRemoveTrack(trackId, name, clipCount) {
        if (!root.trackModel) return
        if (clipCount <= 0) { root.trackModel.removeTrack(trackId); return }
        removeTrackDialog.trackId = trackId
        removeTrackDialog.trackName = name
        removeTrackDialog.clipCount = clipCount
        removeTrackDialog.open()
    }

    ThemedDialog {
        id: removeTrackDialog
        objectName: "removeTrackDialog"
        property int trackId: -1
        property string trackName: ""
        property int clipCount: 0
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(420, parent.width - 32)
        title: "Delete track?"
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Text {
            text: "Delete \u201c" + removeTrackDialog.trackName + "\u201d and its " + removeTrackDialog.clipCount
                  + (removeTrackDialog.clipCount === 1 ? " clip" : " clips")
                  + "?\nYou can bring it back with Undo" + (Shortcuts.texts["edit.undo"] ? " (" + Shortcuts.texts["edit.undo"] + ")" : "") + "."
            color: Theme.text
            wrapMode: Text.WordWrap
            padding: 16
        }
        Component.onCompleted: {
            const ok = standardButton(Dialog.Ok)
            if (ok) ok.text = "Delete"
        }
        onAccepted: if (root.trackModel) root.trackModel.removeTrack(trackId)
    }

    Popup {
        id: addTrackPopup
        objectName: "addTrackPopup"
        parent: Overlay.overlay
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        width: 330
        // As tall as its rows need (three kinds, two drum kits), up to the long instrument list.
        height: Math.min(382, 36 + addTrackHeader.height + 10 + addTrackChoices.count * 50 - 10)
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        property string addTrackStage: "kind"               // "kind", "instrument", "variant"
        property var group: null                            // the group choice "variant" lists
        padding: 0
        onClosed: { addTrackStage = "kind"; group = null }
        background: Rectangle { radius: Theme.panelRadius; color: Theme.popupBg; border.color: Theme.popupBorder }

        contentItem: Column {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 10

            Row {
                id: addTrackHeader
                width: parent.width
                spacing: 8
                ToolButton {
                    objectName: "addTrackBack"
                    visible: addTrackPopup.addTrackStage !== "kind"
                    width: visible ? 26 : 0; height: 26
                    text: Icons.back
                    Accessible.name: addTrackPopup.addTrackStage === "variant" ? "Back to instruments" : "Back to track types"
                    onClicked: addTrackPopup.addTrackStage = addTrackPopup.addTrackStage === "variant" ? "instrument" : "kind"
                    contentItem: Text { text: Icons.back; color: Theme.text; font.pixelSize: 21; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { radius: Theme.controlRadius; color: parent.hovered ? Theme.trackList.backHover : "transparent" }
                }
                Column {
                    spacing: 3
                    Text {
                        text: addTrackPopup.addTrackStage === "kind" ? "Create a track"
                            : addTrackPopup.addTrackStage === "variant" ? "Choose " + addTrackPopup.group.name.toLowerCase()
                            : "Choose an instrument"
                        color: Theme.text; font.pixelSize: Theme.headingSize; font.bold: true
                    }
                    Text {
                        text: addTrackPopup.addTrackStage === "kind" ? "Select the kind of track to add."
                            : addTrackPopup.addTrackStage === "variant" ? "Select the kind of " + addTrackPopup.group.name.toLowerCase() + "."
                            : "Select the instrument type."
                        color: Theme.textMuted; font.pixelSize: Theme.smallSize
                    }
                }
            }

            ListView {
                id: addTrackChoices
                objectName: "addTrackChoices"
                width: parent.width
                height: parent.height - addTrackHeader.height - parent.spacing
                clip: true
                spacing: 10
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { id: addTrackScroll; objectName: "addTrackScrollBar"; policy: ScrollBar.AlwaysOn }
                model: !root.trackModel ? [] : addTrackPopup.addTrackStage === "kind" ? root.trackModel.trackChoices
                       : addTrackPopup.addTrackStage === "variant" ? root.trackModel.variantChoices(addTrackPopup.group.id)
                       : root.trackModel.instrumentChoices
                delegate: Rectangle {
                    required property var modelData
                    objectName: "trackChoice_" + modelData.id
                    width: ListView.view.width - addTrackScroll.width - 6   // clear of the scrollbar
                    height: 40
                    radius: Theme.panelRadius
                    color: choiceArea.containsMouse ? Theme.trackList.optionHover : Theme.trackList.option
                    border.color: choiceArea.containsMouse ? Theme.accent : Theme.trackList.optionBorder
                    Item {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        Text {
                            text: modelData.name
                            color: Theme.text
                            font.pixelSize: Theme.bodySize
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: Icons.submenu
                            color: Theme.textMuted
                            font.pixelSize: 18
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    MouseArea {
                        id: choiceArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            if (modelData.hasChildren && addTrackPopup.addTrackStage === "kind") {
                                addTrackPopup.addTrackStage = "instrument"
                            } else if (modelData.hasChildren) {
                                addTrackPopup.group = modelData
                                addTrackPopup.addTrackStage = "variant"
                            } else if (root.trackModel) {
                                root.trackModel.addTrackChoice(modelData.id)
                                addTrackPopup.close()
                            }
                        }
                    }
                }
            }
        }
    }

    DropArea {
        objectName: "trackListFileDrop"
        anchors.fill: parent
        keys: ["text/uri-list"]
        enabled: !!root.importJob
        onDropped: drop => {
            const files = drop.urls.filter(url => root.importJob.isImportable(url))
            if (files.length === 0 || !root.trackModel) return
            const y = drop.y - 38 + root.scrollOffset   // under the header, in the scrolled list
            const row = root.trackModel.rowAtY(y)
            const onTrack = row >= 0 && row < root.trackModel.count
            root.importJob.importFiles(files, EngineController.positionBeats, onTrack ? root.trackModel.trackIdAt(row) : -1)
            drop.acceptProposedAction()
        }
    }
}
