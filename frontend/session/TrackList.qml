import QtQuick
import QtQuick.Dialogs
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var trackModel
    property int laneHeight: 106
    property real scrollOffset: 0
    property int dropRow: -1
    signal verticalScrollRequested(real amount)
    color: Theme.panelBg
    border.color: Theme.divider

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
                Text { text: "TRACKS"; color: Theme.textMuted; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1; Layout.fillWidth: true }
                ToolButton {
                    id: addTrackButton
                    objectName: "addTrackButton"
                    text: Icons.plus
                    Accessible.name: "Add track"
                    implicitWidth: 28; implicitHeight: 28
                    onClicked: addTrackPopup.open()
                    contentItem: Text { text: Icons.plus; color: Theme.accent; font.pixelSize: 20; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { radius: 4; color: parent.down ? Theme.chipHover : "transparent" }
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
            contentHeight: Math.max(height, root.trackModel ? root.trackModel.count * root.laneHeight : 0)
            contentY: Math.max(0, Math.min(contentHeight - height, root.scrollOffset))
            interactive: false
            boundsBehavior: Flickable.StopAtBounds

            MouseArea {
                objectName: "emptyTrackArea"
                x: 0
                y: root.trackModel ? root.trackModel.count * root.laneHeight : 0
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
                    font.pixelSize: 11
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
                    required property color trackColour
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

                    y: index * root.laneHeight
                    width: trackScroll.width
                    height: root.laneHeight
                    color: isSelected ? Theme.trackList.rowSelected : (index % 2 ? Theme.trackList.rowAlt : Theme.trackList.row)
                    border.color: isSelected ? Theme.accent : (root.dropRow === index ? Theme.accent : Theme.divider)
                    border.width: isSelected || root.dropRow === index ? 2 : 1
                    Rectangle {
                        objectName: "trackColourAccent" + trackCard.trackId
                        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                        width: isSelected ? 5 : 3
                        // Always the track colour; selection shows through the border/background
                        color: trackCard.trackColour
                    }

                    MouseArea {
                        anchors.fill: parent
                        onPressed: if (root.trackModel) root.trackModel.selectTrack(trackCard.trackId)
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        anchors.topMargin: 5
                        anchors.bottomMargin: 4
                        spacing: 2
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Item {
                                Layout.fillWidth: true
                                implicitHeight: 22
                                ToolButton {
                                    objectName: "trackColourButton" + trackCard.trackId
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 14; height: 14
                                    onClicked: root.openColourPopup(trackCard)
                                    background: Rectangle { color: trackCard.trackColour; border.color: parent.hovered ? Theme.text : Theme.trackList.swatchBorder; radius: 3 }
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Choose track colour"
                                }
                                Text {
                                    objectName: "trackNameLabel" + trackCard.trackId
                                    anchors.fill: parent
                                    anchors.leftMargin: 20
                                    verticalAlignment: Text.AlignVCenter
                                    text: trackCard.name
                                    color: Theme.text
                                    font.pixelSize: 12
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
                                                Math.floor(mapToItem(trackScroll.contentItem, mouse.x, mouse.y).y / root.laneHeight)))
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
                                    font.pixelSize: 12
                                    padding: 2
                                    background: Rectangle { color: Theme.trackList.nameEditor; border.color: Theme.accent; radius: 3 }
                                    onEditingFinished: trackCard.finishRename(true)
                                    Keys.onEscapePressed: event => {
                                        trackCard.finishRename(false)
                                        event.accepted = true
                                    }
                                }
                            }
                            Text { text: trackCard.trackType.toUpperCase(); color: Theme.textMuted; font.pixelSize: Theme.microSize; font.letterSpacing: 0.7 }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            // At the narrowest track list the chips wrap onto a second, shorter line
                            // so the card still fits its timeline lane; the remove button stays top right.
                            Flow {
                                id: chipFlow
                                readonly property real oneLineWidth: muteButton.implicitWidth + soloButton.implicitWidth
                                    + inputButton.implicitWidth + monitorButton.implicitWidth
                                    + recordButton.implicitWidth + fxButton.implicitWidth + 5 * spacing
                                readonly property bool wrapped: width < oneLineWidth
                                readonly property real chipHeight: wrapped ? 18 : 24
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignTop
                                Layout.preferredHeight: implicitHeight
                                spacing: 2
                                ToggleChip {
                                    id: muteButton
                                    objectName: "trackMuteButton" + trackCard.trackId
                                    active: trackCard.isMuted
                                    activeBg: Theme.muteBg; activeBorder: Theme.muteBorder; activeText: Theme.muteText
                                    text: "M"; implicitWidth: 22; implicitHeight: chipFlow.chipHeight
                                    onClicked: root.trackModel.setMuted(trackCard.trackId, !active)
                                    tooltip: active ? "Mute enabled" : "Mute disabled"
                                }
                                ToggleChip {
                                    id: soloButton
                                    objectName: "trackSoloButton" + trackCard.trackId
                                    active: trackCard.isSolo
                                    activeBg: Theme.soloBg; activeBorder: Theme.soloBorder; activeText: Theme.soloText
                                    text: "S"; implicitWidth: 22; implicitHeight: chipFlow.chipHeight
                                    onClicked: root.trackModel.setSolo(trackCard.trackId, !active)
                                    tooltip: active ? "Solo enabled" : "Solo disabled"
                                }
                                ToggleChip {
                                    id: inputButton
                                    objectName: "trackInputButton" + trackCard.trackId
                                    // Lit (accent text and border) once an input is chosen; still lightens on hover.
                                    active: trackCard.inputPort.length > 0
                                    activeBg: hovered ? Theme.chipHover : Theme.chip
                                    hoverHighlight: true
                                    text: "IN"; textSize: Theme.microSize
                                    implicitWidth: 26; implicitHeight: chipFlow.chipHeight
                                    onClicked: root.openInputPopup(trackCard)
                                    tooltip: trackCard.inputPort.length > 0 ? trackCard.inputPort : "Choose this track's input"
                                }
                                ToggleChip {
                                    id: monitorButton
                                    objectName: "trackMonitorButton" + trackCard.trackId
                                    active: trackCard.monitorEnabled
                                    enabled: trackCard.canMonitor
                                    implicitWidth: 26; implicitHeight: chipFlow.chipHeight
                                    text: "MON"; textSize: Theme.microSize
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
                                    implicitWidth: 50; implicitHeight: chipFlow.chipHeight
                                    text: active ? "REC ON" : "REC OFF"; textSize: Theme.microSize
                                    onClicked: root.trackModel.setRecordArmed(trackCard.trackId, !active)
                                    tooltip: active ? "Recording armed" : "Arm track for recording"
                                }
                                ToggleChip {
                                    id: fxButton
                                    objectName: "trackFxButton" + trackCard.trackId
                                    hoverHighlight: true
                                    text: "FX"; textSize: 9
                                    implicitWidth: 28; implicitHeight: chipFlow.chipHeight
                                    onClicked: root.openEffects(trackCard)
                                    tooltip: "Browse track effects"
                                }
                            }
                            ToolButton {
                                id: closeTrackButton
                                Layout.alignment: Qt.AlignTop
                                objectName: "trackRemoveButton" + trackCard.trackId
                                text: Icons.close
                                Accessible.name: "Remove track"
                                implicitWidth: 20; implicitHeight: 24
                                onClicked: root.requestRemoveTrack(trackCard.trackId, trackCard.name, trackCard.clips ? trackCard.clips.length : 0)
                                contentItem: Text { text: Icons.close; color: closeTrackButton.hovered ? Theme.textStrong : Theme.textMuted; font.pixelSize: 17; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { radius: 4; color: closeTrackButton.hovered ? Theme.recordBg : "transparent"; border.color: closeTrackButton.hovered ? Theme.record : "transparent" }
                                ToolTip.visible: hovered
                                ToolTip.text: "Remove track"
                                ToolTip.delay: 450
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            Text { text: "VOL"; color: Theme.textMuted; font.pixelSize: Theme.microSize; font.bold: true; Layout.preferredWidth: 24 }
                            Slider {
                                id: volumeSlider
                                objectName: "trackVolume" + trackCard.trackId
                                Layout.fillWidth: true
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
                            Text { text: Math.round(volumeSlider.value * 100) + "%"; color: Theme.textMuted; font.pixelSize: 9; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 29 }
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
        readonly property string trackColour: card ? card.trackColour.toString() : ""
        onCardChanged: if (!card) close()
        objectName: "trackColourPopup" + trackId
        parent: Overlay.overlay
        width: 232
        padding: 12
        x: card ? Math.max(8, Math.min(parent.width - width - 8, card.mapToItem(parent, 0, 0).x)) : 0
        y: card ? Math.max(8, Math.min(parent.height - height - 8, card.mapToItem(parent, 0, 28).y)) : 0
        background: Rectangle { color: Theme.popupBg; border.color: Theme.popupBorder; radius: 6 }
        contentItem: Column {
            spacing: 10
            Text { text: "Track colour"; color: Theme.text; font.pixelSize: 12 }
            Grid {
                columns: 6
                spacing: 7
                Repeater {
                    model: Theme.trackColors
                    delegate: Rectangle {
                        required property int index
                        required property string modelData
                        objectName: "trackColourChoice" + colourPopup.trackId + "_" + index
                        width: 28; height: 28; radius: 4
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
            PanelButton {
                objectName: "customTrackColour" + colourPopup.trackId
                text: "Custom colour…"
                onClicked: {
                    colourDialog.card = colourPopup.card
                    colourPopup.close()
                    colourDialog.selectedColor = colourDialog.card.trackColour
                    colourDialog.open()
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
        onCardChanged: if (!card) close()
        objectName: "trackInputPopup" + trackId
        parent: Overlay.overlay
        x: card ? card.mapToItem(Overlay.overlay, card.width - width, 34).x : 0
        y: card ? card.mapToItem(Overlay.overlay, 0, 34).y : 0
        width: 300
        height: Math.min(340, Math.max(150, 100 + EngineController.inputDevices.length * 34))
        padding: 0
        background: Rectangle { radius: 8; color: Theme.popupBg; border.color: Theme.popupBorder }
        contentItem: ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 6
            Text { text: inputPopup.trackName + " · Input"; color: Theme.text; font.pixelSize: 12; font.bold: true }
            Rectangle {
                Layout.fillWidth: true
                height: 32
                radius: 5
                color: defaultInputArea.containsMouse ? Theme.trackList.optionHover : Theme.trackList.option
                border.color: inputPopup.inputPort.length === 0 ? Theme.accent : Theme.trackList.optionBorder
                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 9
                    verticalAlignment: Text.AlignVCenter
                    text: "Use default input"
                    color: Theme.text
                    font.pixelSize: 10
                }
                MouseArea {
                    id: defaultInputArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        root.trackModel.setTrackInputPort(inputPopup.trackId, "")
                        inputPopup.close()
                    }
                }
            }
            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentHeight: Math.max(inputRows.implicitHeight,
                                        EngineController.inputDevices.length === 0 ? 34 : 0)
                boundsBehavior: Flickable.StopAtBounds
                Text {
                    visible: EngineController.inputDevices.length === 0
                    width: parent.width
                    height: 34
                    text: "No physical input devices found"
                    color: Theme.textMuted
                    font.pixelSize: 10
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 8
                }
                Column {
                    id: inputRows
                    width: parent.width
                    Repeater {
                        model: EngineController.inputDevices
                        delegate: Rectangle {
                            required property string modelData
                            required property int index
                            width: inputRows.width
                            height: 34
                            radius: 5
                            color: inputRowArea.containsMouse ? Theme.trackList.optionHover : "transparent"
                            border.color: inputPopup.inputPort === EngineController.inputPortForDevice(index)
                                ? Theme.accent : "transparent"
                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 9
                                anchors.rightMargin: 8
                                text: modelData
                                color: Theme.text
                                font.pixelSize: 10
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideMiddle
                            }
                            MouseArea {
                                id: inputRowArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    root.trackModel.setTrackInputPort(
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

    Dialog {
        id: removeTrackDialog
        objectName: "removeTrackDialog"
        property int trackId: -1
        property string trackName: ""
        property int clipCount: 0
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(420, parent.width - 32)
        modal: true
        title: "Delete track?"
        standardButtons: Dialog.Ok | Dialog.Cancel
        background: Rectangle { color: Theme.panelBg; border.color: Theme.divider; radius: 8 }
        palette.windowText: Theme.text
        palette.text: Theme.text
        contentItem: Text {
            text: "Delete \u201c" + removeTrackDialog.trackName + "\u201d and its " + removeTrackDialog.clipCount
                  + (removeTrackDialog.clipCount === 1 ? " clip" : " clips")
                  + "?\nYou can bring it back with Undo (Ctrl+Z)."
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
        height: addTrackStage === "kind" ? 222 : 382
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        property string addTrackStage: "kind"
        padding: 0
        onClosed: addTrackStage = "kind"
        background: Rectangle { radius: 10; color: Theme.popupBg; border.color: Theme.popupBorder }

        contentItem: Column {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 10

            Row {
                width: parent.width
                spacing: 8
                ToolButton {
                    visible: addTrackPopup.addTrackStage === "instrument"
                    width: visible ? 26 : 0; height: 26
                    text: Icons.back
                    Accessible.name: "Back to track types"
                    onClicked: addTrackPopup.addTrackStage = "kind"
                    contentItem: Text { text: Icons.back; color: Theme.text; font.pixelSize: 21; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { radius: 5; color: parent.hovered ? Theme.trackList.backHover : "transparent" }
                }
                Column {
                    spacing: 3
                    Text {
                        text: addTrackPopup.addTrackStage === "kind" ? "Create a track" : "Choose an instrument"
                        color: Theme.text; font.pixelSize: 16; font.bold: true
                    }
                    Text {
                        text: addTrackPopup.addTrackStage === "kind" ? "Select the kind of track to add." : "Select the instrument type."
                        color: Theme.textMuted; font.pixelSize: 11
                    }
                }
            }

            Repeater {
                model: !root.trackModel ? [] : addTrackPopup.addTrackStage === "kind"
                       ? root.trackModel.trackChoices : root.trackModel.instrumentChoices
                delegate: Rectangle {
                    required property var modelData
                    objectName: "trackChoice_" + modelData.id
                    width: parent.width
                    height: 40
                    radius: 6
                    color: choiceArea.containsMouse ? Theme.trackList.optionHover : Theme.trackList.option
                    border.color: choiceArea.containsMouse ? Theme.accent : Theme.trackList.optionBorder
                    Item {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        Text {
                            text: modelData.name
                            color: Theme.text
                            font.pixelSize: 12
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
                            if (modelData.hasChildren) {
                                addTrackPopup.addTrackStage = "instrument"
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
}
