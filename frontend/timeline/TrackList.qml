import QtQuick
import QtQuick.Dialogs
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var trackModel
    property int laneHeight: 106
    property real scrollOffset: 0
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
            color: "#252525"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 8
                Text { text: "TRACKS"; color: Theme.textMuted; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1; Layout.fillWidth: true }
                ToolButton {
                    id: addTrackButton
                    text: "+"
                    implicitWidth: 28; implicitHeight: 28
                    onClicked: addTrackPopup.open()
                    contentItem: Text { text: "+"; color: Theme.accent; font.pixelSize: 20; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { radius: 4; color: parent.down ? "#383838" : "transparent" }
                    ToolTip.visible: hovered
                    ToolTip.text: "Add audio track"
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
            contentHeight: root.trackModel ? root.trackModel.count * root.laneHeight : 0
            contentY: Math.max(0, Math.min(contentHeight - height, root.scrollOffset))
            interactive: false
            boundsBehavior: Flickable.StopAtBounds

            Repeater {
                model: root.trackModel
                delegate: Rectangle {
                    id: trackCard
                    required property int index
                    required property int trackId
                    required property string name
                    required property string kind
                    required property string trackType
                    required property bool isSolo
                    required property bool isMuted
                    required property bool isRecordArmed
                    required property string inputPort
                    required property real volume
                    required property var eqBands

                    y: index * root.laneHeight
                    width: trackScroll.width
                    height: root.laneHeight
                    color: index % 2 ? "#222222" : "#252525"
                    border.color: Theme.divider

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
                            Text { text: trackCard.name; color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                            Text { text: trackCard.trackType.toUpperCase(); color: Theme.textMuted; font.pixelSize: 8; font.letterSpacing: 0.7 }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            ToolButton {
                                id: soloButton
                                property bool active: trackCard.isSolo
                                text: "S"; implicitWidth: 22; implicitHeight: 24
                                onClicked: root.trackModel.setSolo(trackCard.trackId, !active)
                                contentItem: Text { text: "S"; color: parent.active ? "#ffe08a" : Theme.textMuted; font.pixelSize: 10; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { radius: 4; color: parent.active ? "#665322" : "#303030"; border.color: parent.active ? "#c49d39" : "#424242" }
                                ToolTip.visible: hovered
                                ToolTip.text: active ? "Solo enabled" : "Solo disabled"
                                ToolTip.delay: 450
                            }
                            ToolButton {
                                id: muteButton
                                property bool active: trackCard.isMuted
                                text: "M"; implicitWidth: 22; implicitHeight: 24
                                onClicked: root.trackModel.setMuted(trackCard.trackId, !active)
                                contentItem: Text { text: "M"; color: parent.active ? "#ffc08a" : Theme.textMuted; font.pixelSize: 10; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { radius: 4; color: parent.active ? "#653f28" : "#303030"; border.color: parent.active ? "#c87943" : "#424242" }
                                ToolTip.visible: hovered
                                ToolTip.text: active ? "Muted" : "Mute track"
                                ToolTip.delay: 450
                            }
                            ToolButton {
                                id: inputButton
                                text: "IN"
                                implicitWidth: 26; implicitHeight: 24
                                onClicked: inputPopup.open()
                                contentItem: Text { text: "IN"; color: trackCard.inputPort.length > 0 ? Theme.accent : Theme.textMuted; font.pixelSize: 8; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { radius: 4; color: inputButton.hovered ? "#383838" : "#303030"; border.color: trackCard.inputPort.length > 0 ? Theme.accent : (inputButton.hovered ? "#626671" : "#424242") }
                                ToolTip.visible: hovered
                                ToolTip.text: trackCard.inputPort.length > 0 ? trackCard.inputPort : "Choose this track's input"
                                ToolTip.delay: 450
                            }
                            ToolButton {
                                id: recordButton
                                property bool active: trackCard.isRecordArmed
                                implicitWidth: 54; implicitHeight: 24
                                text: active ? "REC ON" : "REC OFF"
                                onClicked: root.trackModel.setRecordArmed(trackCard.trackId, !active)
                                contentItem: Text { text: parent.text; color: parent.active ? "#ffffff" : Theme.textMuted; font.pixelSize: 8; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { radius: 4; color: parent.active ? "#a9363c" : "#303030"; border.color: parent.active ? Theme.record : "#424242" }
                                ToolTip.visible: hovered
                                ToolTip.text: active
                                    ? (trackCard.trackType === "Electric Guitar" ? "Clean input monitoring enabled" : "Recording armed")
                                    : (trackCard.trackType === "Electric Guitar" ? "Arm for clean input monitoring" : "Arm track for recording")
                                ToolTip.delay: 450
                            }
                            ToolButton {
                                id: fxButton
                                text: "FX"
                                implicitWidth: 28; implicitHeight: 24
                                onClicked: fxPopup.open()
                                contentItem: Text { text: "FX"; color: fxButton.hovered ? Theme.text : Theme.textMuted; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { radius: 4; color: fxButton.hovered ? "#383838" : "#303030"; border.color: fxButton.hovered ? "#626671" : "#424242" }
                                ToolTip.visible: hovered
                                ToolTip.text: "Browse track effects"
                                ToolTip.delay: 450
                            }
                            Item { Layout.fillWidth: true }
                            ToolButton {
                                id: closeTrackButton
                                text: "×"
                                implicitWidth: 20; implicitHeight: 24
                                onClicked: root.trackModel.removeTrack(trackCard.trackId)
                                contentItem: Text { text: "×"; color: closeTrackButton.hovered ? "#ffffff" : Theme.textMuted; font.pixelSize: 17; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { radius: 4; color: closeTrackButton.hovered ? "#a9363c" : "transparent"; border.color: closeTrackButton.hovered ? Theme.record : "transparent" }
                                ToolTip.visible: hovered
                                ToolTip.text: "Remove track"
                                ToolTip.delay: 450
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            Text { text: "VOL"; color: Theme.textMuted; font.pixelSize: 8; font.bold: true; Layout.preferredWidth: 24 }
                            Slider {
                                id: volumeSlider
                                Layout.fillWidth: true
                                from: 0; to: 2; value: trackCard.volume
                                onMoved: root.trackModel.setVolume(trackCard.trackId, value)
                                ToolTip.visible: hovered
                                ToolTip.text: "Track volume / monitor gain " + Math.round(value * 100) + "%"
                                ToolTip.delay: 450
                            }
                            Text { text: Math.round(volumeSlider.value * 100) + "%"; color: Theme.textMuted; font.pixelSize: 9; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 29 }
                        }
                    }

                    Connections {
                        target: trackCard
                        function onVolumeChanged() { volumeSlider.value = trackCard.volume }
                    }

                    Popup {
                        id: fxPopup
                        parent: Overlay.overlay
                        x: (parent.width - width) / 2
                        y: (parent.height - height) / 2
                        width: 340
                        height: trackCard.trackType === "Electric Guitar" ? 242 : 176
                        modal: true
                        focus: true
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                        padding: 0
                        background: Rectangle { radius: 8; color: "#25272c"; border.color: "#4a4d56" }
                        contentItem: ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: trackCard.name + " · FX"; color: Theme.text; font.pixelSize: 13; font.bold: true; Layout.fillWidth: true }
                                ToolButton {
                                    text: "×"
                                    onClicked: fxPopup.close()
                                    contentItem: Text { text: "×"; color: parent.hovered ? Theme.text : Theme.textMuted; font.pixelSize: 18; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                }
                            }
                            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
                            Rectangle {
                                Layout.fillWidth: true
                                height: 58
                                radius: 5
                                color: equalizerArea.containsMouse ? "#363941" : "#2b2d33"
                                border.color: equalizerArea.containsMouse ? Theme.accent : "#42454d"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 10
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 3
                                        Text { text: "Equalizer"; color: Theme.text; font.pixelSize: 12; font.bold: true }
                                        Text { text: "10-band graphic EQ"; color: Theme.textMuted; font.pixelSize: 10 }
                                    }
                                    Text { text: "›"; color: Theme.textMuted; font.pixelSize: 20 }
                                }
                                MouseArea {
                                    id: equalizerArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        fxPopup.close()
                                        eqPopup.open()
                                    }
                                }
                            }
                            Rectangle {
                                visible: trackCard.trackType === "Electric Guitar"
                                Layout.fillWidth: true
                                height: 58
                                radius: 5
                                color: guitarSuiteArea.containsMouse ? "#363941" : "#2b2d33"
                                border.color: guitarSuiteArea.containsMouse ? Theme.accent : "#42454d"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 10
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 3
                                        Text { text: "GuitarSuite pedalboard"; color: Theme.text; font.pixelSize: 12; font.bold: true }
                                        Text { text: "Guitar pedalboard / signal chain"; color: Theme.textMuted; font.pixelSize: 10 }
                                    }
                                    Text { text: "›"; color: Theme.textMuted; font.pixelSize: 20 }
                                }
                                MouseArea {
                                    id: guitarSuiteArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        fxPopup.close()
                                        guitarSuitePopup.open()
                                    }
                                }
                            }
                            Item { Layout.fillHeight: true }
                        }
                    }

                    GuitarSuitePanel {
                        id: guitarSuitePopup
                        trackName: trackCard.name
                    }

                    Popup {
                        id: eqPopup
                        parent: Overlay.overlay
                        x: (parent.width - width) / 2
                        y: (parent.height - height) / 2
                        width: Math.min(760, parent.width - 32)
                        height: Math.min(460, parent.height - 32)
                        modal: true
                        focus: true
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                        padding: 0
                        background: Rectangle { radius: 9; color: "#25272c"; border.color: "#4a4d56" }
                        contentItem: ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 10
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: trackCard.name + " · EQUALIZER"; color: Theme.text; font.pixelSize: 14; font.bold: true; Layout.fillWidth: true }
                                Text { text: "10 bands  ·  ±12 dB"; color: Theme.textMuted; font.pixelSize: 10 }
                                ToolButton {
                                    text: "Reset"
                                    onClicked: root.trackModel.resetTrackEq(trackCard.trackId)
                                    contentItem: Text { text: "Reset"; color: parent.hovered ? Theme.text : Theme.textMuted; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                    background: Rectangle { radius: 4; color: parent.hovered ? "#383838" : "#303030"; border.color: "#424242" }
                                }
                                ToolButton {
                                    text: "×"
                                    onClicked: eqPopup.close()
                                    contentItem: Text { text: "×"; color: parent.hovered ? Theme.text : Theme.textMuted; font.pixelSize: 18; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                ComboBox { id: eqFactory; Layout.fillWidth: true; model: root.trackModel.eqPresetNames }
                                Button { text: "Apply preset"; onClicked: root.trackModel.applyEqPreset(trackCard.trackId, eqFactory.currentIndex) }
                                Button { text: "Save custom…"; onClicked: eqSaveDialog.open() }
                                Button { text: "Load custom…"; onClicked: eqLoadDialog.open() }
                            }
                            Text {
                                visible: text.length > 0
                                text: root.trackModel.eqPresetError
                                color: Theme.record; font.pixelSize: 10
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                            }
                            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                spacing: 8
                                Repeater {
                                    model: ["31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"]
                                    delegate: ColumnLayout {
                                        required property string modelData
                                        required property int index
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        spacing: 5
                                        Text {
                                            text: Number(trackCard.eqBands[index] || 0).toFixed(1) + " dB"
                                            color: Math.abs(Number(trackCard.eqBands[index] || 0)) > 0.01 ? Theme.accent : Theme.textMuted
                                            font.pixelSize: 9
                                            horizontalAlignment: Text.AlignHCenter
                                            Layout.fillWidth: true
                                        }
                                        Slider {
                                            Layout.fillHeight: true
                                            Layout.alignment: Qt.AlignHCenter
                                            implicitWidth: 28
                                            from: -12; to: 12; stepSize: 0.5
                                            orientation: Qt.Vertical
                                            value: Number(trackCard.eqBands[index] || 0)
                                            onMoved: root.trackModel.setEqBand(trackCard.trackId, index, value)
                                        }
                                        Text { text: modelData; color: Theme.textMuted; font.pixelSize: 9; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
                                    }
                                }
                            }
                            Text { text: "Frequency (Hz)"; color: Theme.textMuted; font.pixelSize: 9; Layout.alignment: Qt.AlignHCenter }
                        }
                    }

                    FileDialog {
                        id: eqSaveDialog
                        title: "Save equalizer preset"
                        fileMode: FileDialog.SaveFile
                        nameFilters: ["Equalizer presets (*.eqpreset)"]
                        defaultSuffix: "eqpreset"
                        onAccepted: root.trackModel.saveEqPreset(trackCard.trackId, selectedFile)
                    }
                    FileDialog {
                        id: eqLoadDialog
                        title: "Load equalizer preset"
                        fileMode: FileDialog.OpenFile
                        nameFilters: ["Equalizer presets (*.eqpreset)"]
                        onAccepted: root.trackModel.loadEqPreset(trackCard.trackId, selectedFile)
                    }

                    Popup {
                        id: inputPopup
                        parent: Overlay.overlay
                        x: trackCard.mapToItem(Overlay.overlay, trackCard.width - width, 34).x
                        y: trackCard.mapToItem(Overlay.overlay, 0, 34).y
                        width: 300
                        height: Math.min(340, Math.max(150, 100 + EngineController.inputDevices.length * 34))
                        padding: 0
                        background: Rectangle { radius: 8; color: "#25272c"; border.color: "#4a4d56" }
                        contentItem: ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6
                            Text { text: trackCard.name + " · Input"; color: Theme.text; font.pixelSize: 12; font.bold: true }
                            Rectangle {
                                Layout.fillWidth: true
                                height: 32
                                radius: 5
                                color: defaultInputArea.containsMouse ? "#343841" : "#2c2e34"
                                border.color: trackCard.inputPort.length === 0 ? Theme.accent : "#40434b"
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
                                        root.trackModel.setTrackInputPort(trackCard.trackId, "")
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
                                            color: inputRowArea.containsMouse ? "#343841" : "transparent"
                                            border.color: trackCard.inputPort === EngineController.inputPortForDevice(index)
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
                                                        trackCard.trackId, EngineController.inputPortForDevice(index))
                                                    inputPopup.close()
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: addTrackPopup
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
        background: Rectangle { radius: 10; color: "#24262b"; border.color: "#454851" }

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
                    text: "‹"
                    onClicked: addTrackPopup.addTrackStage = "kind"
                    contentItem: Text { text: "‹"; color: Theme.text; font.pixelSize: 21; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { radius: 5; color: parent.hovered ? "#383b42" : "transparent" }
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
                model: addTrackPopup.addTrackStage === "kind"
                       ? ["Vocal", "Instrument"]
                       : ["Guitar", "Acoustic Guitar", "Electric Guitar", "Strings", "Drums"]
                delegate: Rectangle {
                    required property string modelData
                    width: parent.width
                    height: 40
                    radius: 6
                    color: choiceArea.containsMouse ? "#343841" : "#2c2e34"
                    border.color: choiceArea.containsMouse ? Theme.accent : "#40434b"
                    Item {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        Text {
                            text: modelData
                            color: Theme.text
                            font.pixelSize: 12
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: "›"
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
                            if (addTrackPopup.addTrackStage === "kind" && modelData === "Instrument") {
                                addTrackPopup.addTrackStage = "instrument"
                            } else if (root.trackModel && modelData === "Vocal") {
                                root.trackModel.addVocalTrack()
                                addTrackPopup.close()
                            } else if (root.trackModel) {
                                root.trackModel.addInstrumentTrackOfType(modelData)
                                addTrackPopup.close()
                            }
                        }
                    }
                }
            }
        }
    }
}
