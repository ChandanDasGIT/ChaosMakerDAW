import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../common"
import "../dialogs"

// Tools > Guitar tone analyzer… (restMap G19): finds a GuitarSuite pedalboard that sounds close
// to the selected audio clip's guitar tone (chords or single notes), shows how close it got,
// and saves it as a GuitarSuite preset. The analysis and the saving are ToneAnalyzerWrapper's.
Popup {
    id: root
    property var trackModel: null

    objectName: "toneAnalyzerDialog"
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: 600
    padding: 18
    modal: true
    focus: true
    closePolicy: bridge.busy ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { radius: 10; color: Theme.popupBg; border.color: Theme.popupBorder }

    onAboutToShow: {
        bridge.clipId = root.trackModel ? root.trackModel.selectedClipId : -1
        presetName.text = bridge.suggestedName()
    }

    ToneAnalyzerWrapper {
        id: bridge
        objectName: "toneAnalyzerBridge"
        trackModel: root.trackModel ? root.trackModel : null
        onClipChanged: if (!hasResult) presetName.text = suggestedName()
    }

    contentItem: ColumnLayout {
        spacing: 12

        // Title and the clip.
        ColumnLayout {
            spacing: 3
            Layout.fillWidth: true
            Text { text: "Guitar tone analyzer"; color: Theme.text; font.pixelSize: 16; font.bold: true }
            Text {
                objectName: "toneAnalyzerClip"
                Layout.fillWidth: true
                text: bridge.unavailableReason.length > 0 ? bridge.unavailableReason
                    : "Clip: " + (bridge.clipLabel.length > 0 ? bridge.clipLabel : "audio clip")
                color: bridge.unavailableReason.length > 0 ? Theme.textMuted : Theme.text
                font.pixelSize: 12
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: "Best on a guitar alone: drums, bass or a voice in the clip are heard as part of the tone."
                color: Theme.textMuted; font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }

        // What the clip plays.
        RowLayout {
            spacing: 8
            visible: !bridge.busy
            Text { text: "The clip plays"; color: Theme.textMuted; font.pixelSize: 12 }
            Repeater {
                model: [{ text: "Find out", name: "toneAnalyzerPlayingAuto" }, { text: "Chords", name: "toneAnalyzerPlayingChords" },
                        { text: "Single notes", name: "toneAnalyzerPlayingSolo" }]
                PanelButton {
                    required property var modelData
                    required property int index
                    objectName: modelData.name
                    text: modelData.text
                    checked: bridge.playing === index
                    onClicked: bridge.playing = index
                }
            }
        }

        // Analyse, progress, cancel.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            PanelButton {
                objectName: "toneAnalyzerAnalyse"
                text: bridge.hasResult ? "Analyse again" : "Analyse"
                enabled: !bridge.busy && bridge.unavailableReason.length === 0
                onClicked: bridge.analyse()
            }
            ProgressBar {
                objectName: "toneAnalyzerProgress"
                Layout.fillWidth: true
                visible: bridge.busy
                from: 0; to: 1
                value: bridge.progress
            }
            PanelButton {
                objectName: "toneAnalyzerCancel"
                visible: bridge.busy
                text: "Cancel"
                onClicked: bridge.cancel()
            }
            Item { Layout.fillWidth: true; visible: !bridge.busy }
        }
        Text {
            objectName: "toneAnalyzerMessage"
            Layout.fillWidth: true
            visible: text.length > 0
            text: bridge.message
            color: Theme.textMuted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        // The result.
        ColumnLayout {
            objectName: "toneAnalyzerResult"
            visible: bridge.hasResult && !bridge.busy
            Layout.fillWidth: true
            spacing: 10

            Text {
                objectName: "toneAnalyzerSummary"
                Layout.fillWidth: true
                text: bridge.playingName + "  ·  " + bridge.gainName + "  ·  " + bridge.cabinetName + "  ·  " + bridge.roomName
                color: Theme.text; font.pixelSize: 14; font.bold: true
                elide: Text.ElideRight
            }
            Text {
                objectName: "toneAnalyzerCloseness"
                Layout.fillWidth: true
                text: bridge.closeness + ": the tone's colour is " + (Math.round(bridge.balanceErrorDb * 10) / 10) + " dB off on average."
                color: Theme.textMuted; font.pixelSize: 12
            }

            // The colour, low to high: the bars are the found tone, the marks the clip's.
            Rectangle {
                objectName: "toneAnalyzerBands"
                Layout.fillWidth: true
                Layout.preferredHeight: 96
                radius: 6
                color: Theme.field
                border.color: Theme.fieldBorder
                Row {
                    id: bandRow
                    anchors.fill: parent
                    anchors.margins: 8
                    anchors.bottomMargin: 18
                    readonly property real span: 18                  // dB above and below the middle
                    Repeater {
                        model: bridge.bands
                        Item {
                            required property var modelData
                            width: bandRow.width / Math.max(1, bridge.bands.length)
                            height: bandRow.height
                            readonly property real mid: height / 2
                            function yOf(db) { return mid - Math.max(-bandRow.span, Math.min(bandRow.span, db)) / bandRow.span * mid }
                            Rectangle {                              // the found tone
                                x: 2; width: parent.width - 4
                                y: Math.min(parent.mid, parent.yOf(modelData.got))
                                height: Math.max(1, Math.abs(parent.yOf(modelData.got) - parent.mid))
                                color: Theme.accent; opacity: 0.55; radius: 1
                            }
                            Rectangle {                              // the clip
                                x: 1; width: parent.width - 2; height: 2
                                y: parent.yOf(modelData.want) - 1
                                color: Theme.text
                            }
                        }
                    }
                }
                Text {
                    anchors.left: parent.left; anchors.leftMargin: 8
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 3
                    text: "100 Hz"; color: Theme.textMuted; font.pixelSize: 9
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 3
                    text: "white marks: the clip · bars: the preset"; color: Theme.textMuted; font.pixelSize: 9
                }
                Text {
                    anchors.right: parent.right; anchors.rightMargin: 8
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 3
                    text: "6.3 kHz"; color: Theme.textMuted; font.pixelSize: 9
                }
            }

            // The pedalboard.
            ColumnLayout {
                objectName: "toneAnalyzerPedals"
                Layout.fillWidth: true
                spacing: 3
                Repeater {
                    model: bridge.pedals
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            Layout.preferredWidth: 96
                            text: modelData.name
                            color: Theme.text; font.pixelSize: 12; font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.settings
                            color: Theme.textMuted; font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                text: "Fitted to a typical electric guitar. With hotter or thinner pickups, nudge the amp's Drive and tone controls after loading it."
                color: Theme.textMuted; font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            // Save it.
            RowLayout {
                spacing: 8
                Layout.fillWidth: true
                Text { text: "Preset name"; color: Theme.textMuted; font.pixelSize: 12 }
                TextField {
                    id: presetName
                    objectName: "toneAnalyzerName"
                    Layout.fillWidth: true
                    maximumLength: 60
                    color: Theme.text
                    placeholderText: "Preset name"
                    background: Rectangle { radius: 5; color: Theme.field; border.color: presetName.activeFocus ? Theme.accent : Theme.fieldBorder }
                    onAccepted: saveButton.clicked()
                }
                PanelButton {
                    id: saveButton
                    objectName: "toneAnalyzerSave"
                    text: "Save as preset"
                    tooltip: "A GuitarSuite preset file in the default folder below"
                    onClicked: bridge.savePreset(presetName.text)
                }
                PanelButton {
                    objectName: "toneAnalyzerSaveAs"
                    text: "Save as…"
                    tooltip: "Choose where this preset goes"
                    onClicked: {
                        saveAsDialog.currentFolder = bridge.presetFolderUrl
                        saveAsDialog.selectedFile = bridge.fileFor(presetName.text)
                        saveAsDialog.open()
                    }
                }
            }
            // Where "Save as preset" saves, and how to change it.
            RowLayout {
                spacing: 8
                Layout.fillWidth: true
                Text {
                    objectName: "toneAnalyzerFolder"
                    Layout.fillWidth: true
                    text: "Default folder: " + bridge.presetFolder
                    color: Theme.textMuted; font.pixelSize: 11
                    elide: Text.ElideMiddle
                }
                CheckBox {
                    id: makeDefault
                    objectName: "toneAnalyzerMakeDefault"
                    text: "Save as… also sets the default folder"
                    palette.windowText: Theme.text
                    font.pixelSize: 11
                }
                PanelButton {
                    objectName: "toneAnalyzerResetFolder"
                    visible: bridge.hasOwnDefaultFolder
                    text: "App folder"
                    tooltip: "Back to the app's own GuitarSuite presets folder"
                    onClicked: bridge.setDefaultFolder("")
                }
            }
        }

        FilePicker {
            id: saveAsDialog
            objectName: "toneAnalyzerSaveAsDialog"
            title: "Save GuitarSuite preset"
            fileMode: FilePicker.SaveFile
            nameFilters: ["GuitarSuite presets (*.gspreset)", "All files (*)"]
            defaultSuffix: "gspreset"
            onAccepted: bridge.savePresetTo(selectedFile, makeDefault.checked)
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PanelButton {
                objectName: "toneAnalyzerClose"
                text: "Close"
                enabled: !bridge.busy
                onClicked: root.close()
            }
        }
    }
}
