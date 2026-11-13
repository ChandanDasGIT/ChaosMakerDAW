import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../common"

// Tools > Drum pattern analyzer… (restMap G20): finds the BPM, the time signature and the kick,
// snare and hi-hat hits of the selected audio clip (a drum loop or a whole song), then puts the
// pattern as a clip on a drum track under the audio, or saves its most typical two bars as a
// groove. The analysis is DrumAnalyzerWrapper's; the project's BPM and meter are set here, the
// way the transport bar sets them.
ToolWindow {
    id: root
    property var trackModel: null
    // Where "Make clip" puts the pattern: -1 a new Acoustic Drums track, -2 a new Electronic
    // Drums track, else the id of a drum track in the project. Kept while the app runs.
    property int target: -1
    property var existingTracks: []

    function refreshTargets() {
        existingTracks = bridge.targets().filter(t => t.id >= 0)
        if (target >= 0 && !existingTracks.some(t => t.id === target)) target = -1
        targetBox.currentIndex = existingTracks.findIndex(t => t.id === target)
    }

    objectName: "drumAnalyzerDialog"
    title: "Drum pattern analyzer"
    width: 560
    busy: bridge.busy                                       // no closing while it analyses

    onAboutToShow: {
        bridge.clipId = root.trackModel ? root.trackModel.selectedClipId : -1
        refreshTargets()
        groovePanel.visible = false
    }

    DrumAnalyzerWrapper {
        id: bridge
        objectName: "drumAnalyzerBridge"
        trackModel: root.trackModel ? root.trackModel : null
    }

    readonly property var rowNames: ["Kick", "Snare", "Hi-hat"]

    ColumnLayout {
        width: parent.width
        spacing: 12

        // Title and the clip.
        ColumnLayout {
            spacing: 3
            Layout.fillWidth: true
            Text { text: "Drum pattern analyzer"; color: Theme.text; font.pixelSize: 16; font.bold: true }
            Text {
                objectName: "drumAnalyzerClip"
                Layout.fillWidth: true
                text: bridge.unavailableReason.length > 0 ? bridge.unavailableReason
                    : "Clip: " + (bridge.clipLabel.length > 0 ? bridge.clipLabel : "audio clip")
                color: bridge.unavailableReason.length > 0 ? Theme.textMuted : Theme.text
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }

        // Analyse, progress, cancel.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            PanelButton {
                objectName: "drumAnalyzerAnalyse"
                text: bridge.hasResult ? "Analyse again" : "Analyse"
                enabled: !bridge.busy && bridge.unavailableReason.length === 0
                onClicked: bridge.analyse()
            }
            ProgressBar {
                objectName: "drumAnalyzerProgress"
                Layout.fillWidth: true
                visible: bridge.busy
                from: 0; to: 1
                value: bridge.progress
            }
            PanelButton {
                objectName: "drumAnalyzerCancel"
                visible: bridge.busy
                text: "Cancel"
                onClicked: bridge.cancel()
            }
            Item { Layout.fillWidth: true; visible: !bridge.busy }
        }
        Text {
            objectName: "drumAnalyzerMessage"
            Layout.fillWidth: true
            visible: text.length > 0
            text: bridge.message
            color: Theme.textMuted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        // The result.
        ColumnLayout {
            objectName: "drumAnalyzerResult"
            visible: bridge.hasResult && !bridge.busy
            Layout.fillWidth: true
            spacing: 10

            RowLayout {
                spacing: 8
                Text { text: "Tempo"; color: Theme.textMuted; font.pixelSize: 12 }
                Text {
                    objectName: "drumAnalyzerBpm"
                    text: (Math.round(bridge.bpm * 10) / 10) + " BPM"
                    color: Theme.text; font.pixelSize: 14; font.bold: true
                }
                PanelButton {
                    objectName: "drumAnalyzerHalve"
                    text: "Half"
                    tooltip: "Half the tempo (when the beat was counted twice too fast)"
                    onClicked: bridge.retempo(0.5)
                }
                PanelButton {
                    objectName: "drumAnalyzerDouble"
                    text: "Double"
                    tooltip: "Double the tempo (when the beat was counted at half speed)"
                    onClicked: bridge.retempo(2)
                }
                Item { width: 12 }
                Text { text: "Time signature"; color: Theme.textMuted; font.pixelSize: 12 }
                Text {
                    objectName: "drumAnalyzerMeter"
                    text: bridge.numerator + "/" + bridge.denominator
                    color: Theme.text; font.pixelSize: 14; font.bold: true
                }
            }
            Text {
                objectName: "drumAnalyzerCounts"
                text: bridge.bars + " bars · " + bridge.kicks + " kicks · " + bridge.snares + " snares · " + bridge.hats + " hi-hats"
                color: Theme.textMuted; font.pixelSize: 11
            }

            // The first two bars.
            Rectangle {
                objectName: "drumAnalyzerPreview"
                Layout.fillWidth: true
                Layout.preferredHeight: 3 * 18 + 10
                radius: 6
                color: Theme.cardBgAlt
                border.color: Theme.divider
                Item {
                    id: grid
                    anchors.fill: parent
                    anchors.margins: 5
                    readonly property real labelWidth: 46
                    readonly property real cell: bridge.previewColumns > 0 ? (width - labelWidth) / bridge.previewColumns : 0
                    Repeater {
                        model: 3
                        Text {
                            required property int index
                            x: 0; y: index * 18
                            height: 18
                            text: root.rowNames[index]
                            color: Theme.textMuted; font.pixelSize: 10
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    Repeater {                                  // beat lines
                        model: bridge.columnsPerBeat > 0 ? Math.ceil(bridge.previewColumns / bridge.columnsPerBeat) : 0
                        Rectangle {
                            required property int index
                            x: grid.labelWidth + index * bridge.columnsPerBeat * grid.cell
                            y: 0; width: 1; height: 54
                            color: Theme.divider
                        }
                    }
                    Repeater {
                        model: bridge.preview
                        Rectangle {
                            required property var modelData
                            x: grid.labelWidth + modelData.column * grid.cell + 1
                            y: modelData.row * 18 + 3
                            width: Math.max(2, grid.cell - 2); height: 12
                            radius: 2
                            color: Theme.accent
                            opacity: 0.35 + 0.65 * modelData.velocity / 127
                        }
                    }
                }
            }

            // Where it goes, and what else to set.
            RowLayout {
                spacing: 8
                Layout.fillWidth: true
                Text { text: "Put on a new"; color: Theme.textMuted; font.pixelSize: 12 }
                PanelButton {
                    objectName: "drumAnalyzerAcoustic"
                    text: "Acoustic"
                    tooltip: "A new Drums track (the acoustic kit)"
                    checked: root.target === -1
                    onClicked: { root.target = -1; targetBox.currentIndex = -1 }
                }
                PanelButton {
                    objectName: "drumAnalyzerElectronic"
                    text: "Electronic"
                    tooltip: "A new Electronic Drums track"
                    checked: root.target === -2
                    onClicked: { root.target = -2; targetBox.currentIndex = -1 }
                }
                Text {
                    visible: targetBox.visible
                    text: "drum track, or on"
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                ThemedComboBox {
                    id: targetBox
                    objectName: "drumAnalyzerTarget"
                    visible: root.existingTracks.length > 0
                    Layout.fillWidth: true
                    model: root.existingTracks
                    popup.margins: 4                    // the list stays inside this window
                    textRole: "name"
                    valueRole: "id"
                    displayText: currentIndex >= 0 ? currentText : "a drum track in the project…"
                    onActivated: root.target = currentValue
                }
                Text {
                    visible: !targetBox.visible
                    text: "drum track"
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                Item { visible: !targetBox.visible; Layout.fillWidth: true }
            }
            RowLayout {
                spacing: 14
                CheckBox {
                    id: setBpm
                    objectName: "drumAnalyzerSetBpm"
                    checked: true
                    text: "Set the project BPM to " + (Math.round(bridge.bpm * 10) / 10)
                    palette.windowText: Theme.text
                }
                CheckBox {
                    id: setMeter
                    objectName: "drumAnalyzerSetMeter"
                    checked: true
                    text: "Set the time signature to " + bridge.numerator + "/" + bridge.denominator
                    palette.windowText: Theme.text
                }
            }
            RowLayout {
                spacing: 8
                PanelButton {
                    objectName: "drumAnalyzerMakeClip"
                    text: "Make clip"
                    tooltip: "Put the pattern on the chosen drum track, under the analysed audio"
                    onClicked: {
                        if (setBpm.checked) EngineController.bpm = bridge.bpm
                        if (setMeter.checked) EngineController.setMeter(bridge.numerator, bridge.denominator)
                        if (bridge.makeClip(root.target) >= 0) root.refreshTargets()
                    }
                }
                PanelButton {
                    objectName: "drumAnalyzerSaveGroove"
                    text: "Save as groove…"
                    tooltip: "Save the two bars that repeat most as a groove for the drum pages"
                    onClicked: {
                        grooveName.text = bridge.suggestedGrooveName()
                        groovePanel.visible = true
                        grooveName.forceActiveFocus()
                        grooveName.selectAll()
                    }
                }
            }
            RowLayout {
                id: groovePanel
                objectName: "drumAnalyzerGroovePanel"
                visible: false
                spacing: 8
                Layout.fillWidth: true
                TextField {
                    id: grooveName
                    objectName: "drumAnalyzerGrooveName"
                    Layout.fillWidth: true
                    maximumLength: 48
                    color: Theme.text
                    placeholderText: "Groove name"
                    background: Rectangle { radius: 5; color: Theme.field; border.color: grooveName.activeFocus ? Theme.accent : Theme.fieldBorder }
                    onAccepted: saveGrooveButton.clicked()
                }
                PanelButton {
                    id: saveGrooveButton
                    objectName: "drumAnalyzerSaveGrooveConfirm"
                    text: "Save"
                    onClicked: if (bridge.saveGroove(grooveName.text)) groovePanel.visible = false
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PanelButton {
                objectName: "drumAnalyzerClose"
                text: "Close"
                enabled: !bridge.busy
                onClicked: root.close()
            }
        }
    }
}
