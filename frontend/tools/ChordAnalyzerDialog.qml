import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../common"

// Tools > Chord analyzer… (restMap G21): finds the key and the chords over time of the selected
// audio clip (a whole song), then puts the most played chords on a Chords track's strips, with
// a clip that plays them under the song when asked, or copies the chart as text. The analysis
// is ChordAnalyzerWrapper's; the project's BPM and meter are set here, the way the transport
// bar sets them.
ToolWindow {
    id: root
    property var trackModel: null
    // Where "Put on track" puts the chords: -1 a new Chords track, else the id of a Chords track
    // in the project. Kept while the app runs.
    property int target: -1
    property var targets: []

    function refreshTargets() {
        targets = bridge.targets()
        if (!targets.some(t => t.id === target)) target = -1
        targetBox.currentIndex = targets.findIndex(t => t.id === target)
    }
    function clock(seconds) {
        const s = Math.floor(seconds)
        return Math.floor(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + (s % 60)
    }

    objectName: "chordAnalyzerDialog"
    title: "Chord analyzer"
    width: 620
    busy: bridge.busy                                       // no closing while it analyses

    onAboutToShow: {
        bridge.clipId = root.trackModel ? root.trackModel.selectedClipId : -1
        refreshTargets()
    }

    ChordAnalyzerWrapper {
        id: bridge
        objectName: "chordAnalyzerBridge"
        trackModel: root.trackModel ? root.trackModel : null
    }

    ColumnLayout {
        width: parent.width
        spacing: 12

        // Title and the clip.
        ColumnLayout {
            spacing: 3
            Layout.fillWidth: true
            Text { text: "Chord analyzer"; color: Theme.text; font.pixelSize: Theme.headingSize; font.bold: true }
            Text {
                objectName: "chordAnalyzerClip"
                Layout.fillWidth: true
                text: bridge.unavailableReason.length > 0 ? bridge.unavailableReason
                    : "Clip: " + (bridge.clipLabel.length > 0 ? bridge.clipLabel : "audio clip")
                color: bridge.unavailableReason.length > 0 ? Theme.textMuted : Theme.text
                font.pixelSize: Theme.bodySize
                elide: Text.ElideRight
            }
        }

        // Analyse, progress, cancel.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            PanelButton {
                objectName: "chordAnalyzerAnalyse"
                primary: !bridge.hasResult   // the main action until there is a result
                text: bridge.hasResult ? "Analyse again" : "Analyse"
                enabled: !bridge.busy && bridge.unavailableReason.length === 0
                onClicked: bridge.analyse()
            }
            ProgressBar {
                objectName: "chordAnalyzerProgress"
                Layout.fillWidth: true
                visible: bridge.busy
                from: 0; to: 1
                value: bridge.progress
            }
            PanelButton {
                objectName: "chordAnalyzerCancel"
                visible: bridge.busy
                text: "Cancel"
                onClicked: bridge.cancel()
            }
            Item { Layout.fillWidth: true; visible: !bridge.busy }
            ThemedCheckBox {
                objectName: "chordAnalyzerSevenths"
                visible: !bridge.busy
                checked: bridge.sevenths
                text: "Name sevenths too (7, maj7, m7)"
                onToggled: bridge.sevenths = checked
                ToolTip.visible: hovered
                ToolTip.delay: 450
                ToolTip.text: "Sevenths are found less surely than major and minor chords"
            }
        }
        Text {
            objectName: "chordAnalyzerMessage"
            Layout.fillWidth: true
            visible: text.length > 0
            text: bridge.message
            color: Theme.textMuted
            font.pixelSize: Theme.smallSize
            wrapMode: Text.WordWrap
        }

        // The result.
        ColumnLayout {
            objectName: "chordAnalyzerResult"
            visible: bridge.hasResult && !bridge.busy
            Layout.fillWidth: true
            spacing: 10

            RowLayout {
                spacing: 8
                Text { text: "Key"; color: Theme.textMuted; font.pixelSize: Theme.bodySize }
                Text {
                    objectName: "chordAnalyzerKey"
                    text: bridge.keyName
                    color: Theme.text; font.pixelSize: Theme.headingSize; font.bold: true
                }
                Item { width: 12 }
                Text { visible: bridge.hasBeat; text: "Tempo"; color: Theme.textMuted; font.pixelSize: Theme.bodySize }
                Text {
                    objectName: "chordAnalyzerBpm"
                    visible: bridge.hasBeat
                    text: (Math.round(bridge.bpm * 10) / 10) + " BPM · " + bridge.numerator + "/" + bridge.denominator
                    color: Theme.text; font.pixelSize: Theme.headingSize; font.bold: true
                }
                Text {
                    objectName: "chordAnalyzerNoBeat"
                    visible: !bridge.hasBeat
                    text: "No steady beat was found: the chords are read every half second."
                    color: Theme.textMuted; font.pixelSize: Theme.smallSize
                }
            }
            Text {
                objectName: "chordAnalyzerTuning"
                visible: Math.abs(bridge.tuningCents) >= 8
                text: "The song is tuned " + Math.round(Math.abs(bridge.tuningCents)) + " cents "
                      + (bridge.tuningCents > 0 ? "sharp" : "flat") + " of A = 440 Hz; the chords are named at its own pitch."
                color: Theme.textMuted; font.pixelSize: Theme.smallSize
            }
            Text {
                objectName: "chordAnalyzerCommon"
                Layout.fillWidth: true
                text: bridge.commonChords.length > 0 ? "Most played: " + bridge.commonChords.slice(0, 8).join("  ·  ")
                                                     : "No chords were found."
                color: Theme.text; font.pixelSize: Theme.bodySize
                elide: Text.ElideRight
            }

            // The chords over the song: one block per chord, as wide as it is long; a chord the
            // analyzer was not sure of is dimmed.
            Rectangle {
                objectName: "chordAnalyzerChart"
                Layout.fillWidth: true
                Layout.preferredHeight: 168
                radius: Theme.panelRadius
                color: Theme.field
                border.color: Theme.fieldBorder
                clip: true
                Flickable {
                    id: chartView
                    anchors.fill: parent
                    anchors.margins: 6
                    contentWidth: width
                    contentHeight: chartFlow.height
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: chartView.contentHeight > chartView.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
                    Flow {
                        id: chartFlow
                        width: chartView.width - 12
                        spacing: 3
                        Repeater {
                            model: bridge.chords
                            Rectangle {
                                required property var modelData
                                objectName: "chordAnalyzerBlock"
                                width: Math.max(30, Math.min(chartFlow.width, modelData.beats * 9))
                                height: 34
                                radius: Theme.controlRadius
                                color: modelData.name.length === 0 ? "transparent" : Theme.chip
                                border.color: modelData.name.length === 0 ? Theme.chipBorder : Theme.accent
                                opacity: modelData.name.length === 0 || modelData.sure < 0.3 ? 0.55 : 1
                                Text {
                                    anchors.left: parent.left; anchors.leftMargin: 5
                                    anchors.top: parent.top; anchors.topMargin: 2
                                    text: modelData.name.length > 0 ? modelData.name : "–"
                                    color: Theme.text; font.pixelSize: Theme.bodySize; font.bold: true
                                }
                                Text {
                                    anchors.left: parent.left; anchors.leftMargin: 5
                                    anchors.bottom: parent.bottom; anchors.bottomMargin: 2
                                    text: root.clock(modelData.from)
                                    color: Theme.textMuted; font.pixelSize: Theme.microSize
                                }
                            }
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                text: "A chart to correct by ear, not a finished transcription. Dimmed chords are the ones it was least sure of."
                color: Theme.textMuted; font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap
            }

            // Where it goes, and what else to set.
            RowLayout {
                spacing: 8
                Layout.fillWidth: true
                Text { text: "Put on"; color: Theme.textMuted; font.pixelSize: Theme.bodySize }
                ThemedComboBox {
                    id: targetBox
                    objectName: "chordAnalyzerTarget"
                    Layout.fillWidth: true
                    model: root.targets
                    popup.margins: 4                    // the list stays inside this window
                    textRole: "name"
                    valueRole: "id"
                    onActivated: root.target = currentValue
                }
                ThemedCheckBox {
                    id: withClip
                    objectName: "chordAnalyzerWithClip"
                    checked: true
                    text: "with a clip that plays them"
                }
            }
            RowLayout {
                spacing: 14
                visible: bridge.hasBeat
                ThemedCheckBox {
                    id: setBpm
                    objectName: "chordAnalyzerSetBpm"
                    checked: true
                    text: "Set the project BPM to " + (Math.round(bridge.bpm * 10) / 10)
                    ToolTip.visible: hovered
                    ToolTip.delay: 450
                    ToolTip.text: "Also slides the song by less than a bar, so its bars sit on the project's bar lines"
                }
                ThemedCheckBox {
                    id: setMeter
                    objectName: "chordAnalyzerSetMeter"
                    checked: true
                    text: "Set the time signature to " + bridge.numerator + "/" + bridge.denominator
                }
            }
            RowLayout {
                spacing: 8
                PanelButton {
                    objectName: "chordAnalyzerPut"
                    primary: true
                    text: "Put on track"
                    enabled: bridge.commonChords.length > 0
                    tooltip: "The most played chords onto the Chords track's eight strips, with the key"
                    onClicked: {
                        if (bridge.hasBeat && setBpm.checked) EngineController.bpm = bridge.bpm
                        if (bridge.hasBeat && setMeter.checked) EngineController.setMeter(bridge.numerator, bridge.denominator)
                        if (bridge.hasBeat && setBpm.checked) bridge.alignSong()     // the song onto the bars of that tempo
                        const id = bridge.putOnTrack(root.target, withClip.checked)
                        if (id >= 0) { root.target = id; root.refreshTargets() }
                    }
                }
                PanelButton {
                    objectName: "chordAnalyzerCopy"
                    text: "Copy chart"
                    tooltip: "The chords as text, to paste anywhere"
                    onClicked: bridge.copyChart()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PanelButton {
                objectName: "chordAnalyzerClose"
                text: "Close"
                enabled: !bridge.busy
                onClicked: root.close()
            }
        }
    }
}
