import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The Chords track's page in the FX panel: autoplay, the key and ready-made progressions, then
// the eight chord strips (ChordStrips). A press plays the strip's chord in the autoplay pattern
// at the project's tempo until another strip is pressed or the same one again; with autoplay
// off a strip sounds while held. A MIDI keyboard or KEYS plays the strips too (chords on the
// white keys C4 .. C5, bass on C2 .. C3), and recording keeps the presses. Everything goes
// through ChordsWrapper.
Item {
    id: root
    property var trackModel
    property int trackId: -1
    property string trackName: ""
    property int editing: -1                                   // the strip whose chord is being changed

    ChordsWrapper {
        id: bridge
        objectName: "chordsBridge"
        trackModel: root.trackModel ?? null
        trackId: root.trackId
        engine: EngineController
        watchActive: root.visible
    }

    component SectionTitle: Text {
        color: Theme.textMuted
        font.pixelSize: Theme.captionSize
        font.bold: true
        font.letterSpacing: 0.6
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text { text: "Chords"; color: Theme.text; font.pixelSize: 15; font.bold: true }
                Text {
                    text: "Press a strip: it plays at the project's tempo until you press another, or the same one again"
                    color: Theme.textMuted; font.pixelSize: 11
                    Layout.fillWidth: true; elide: Text.ElideRight
                }
            }
            PanelButton {
                objectName: "chordsReset"
                text: "Reset"
                tooltip: "Back to the key chords of C major, arpeggio"
                onClicked: bridge.resetToDefaults()
            }
        }

        // Autoplay, key, progressions.
        Flow {
            Layout.fillWidth: true
            spacing: 18
            RowLayout {
                spacing: 6
                SectionTitle { text: "AUTOPLAY" }
                Repeater {
                    model: bridge.autoplayModes
                    delegate: ToggleChip {
                        required property var modelData
                        objectName: "chordsAutoplay" + modelData.mode
                        text: modelData.mode === 0 ? "Off" : modelData.mode + " " + modelData.title
                        textSize: 11
                        leftPadding: 10; rightPadding: 10
                        implicitHeight: 28
                        hoverHighlight: true
                        active: bridge.autoplay === modelData.mode
                        tooltip: modelData.mode === 0 ? "Autoplay off: a strip sounds while you hold it"
                                                      : "Autoplay " + modelData.mode + ": " + modelData.title
                        onClicked: bridge.autoplay = modelData.mode
                    }
                }
            }
            RowLayout {
                spacing: 6
                SectionTitle { text: "KEY" }
                ThemedComboBox {
                    objectName: "chordsKeyTonic"
                    implicitWidth: 76
                    model: bridge.tonicNames
                    currentIndex: bridge.keyTonic
                    onActivated: index => bridge.setKey(index, bridge.keyMinor)
                    ToolTip.visible: hovered
                    ToolTip.text: "The key the degree buttons and progressions use"
                }
                ToggleChip {
                    objectName: "chordsKeyMajor"
                    text: "Major"; textSize: 11; leftPadding: 10; rightPadding: 10; implicitHeight: 28
                    hoverHighlight: true
                    active: !bridge.keyMinor
                    onClicked: bridge.setKey(bridge.keyTonic, false)
                }
                ToggleChip {
                    objectName: "chordsKeyMinor"
                    text: "Minor"; textSize: 11; leftPadding: 10; rightPadding: 10; implicitHeight: 28
                    hoverHighlight: true
                    active: bridge.keyMinor
                    onClicked: bridge.setKey(bridge.keyTonic, true)
                }
            }
            RowLayout {
                spacing: 6
                SectionTitle { text: "PROGRESSION" }
                ThemedComboBox {
                    id: progressionBox
                    objectName: "chordsProgression"
                    implicitWidth: 230
                    model: bridge.progressions
                    textRole: "name"
                    ToolTip.visible: hovered
                    ToolTip.text: currentIndex >= 0 ? bridge.progressions[currentIndex].description : ""
                }
                PanelButton {
                    objectName: "chordsFill"
                    text: "Fill strips"
                    tooltip: "Put this progression on the eight strips, in " + bridge.tonicNames[bridge.keyTonic]
                    onClicked: bridge.applyProgression(progressionBox.currentIndex)
                }
            }
        }

        ChordStrips {
            objectName: "chordStrips"
            bridge: bridge
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 220
            onEditRequested: strip => chordDialog.edit(strip)
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text {
                text: "MIDI keyboard / KEYS: chords on the white keys C4–C5, bass on C2–C3 · arm the track (REC) to record what you play"
                color: Theme.textMuted; font.pixelSize: Theme.captionSize
                Layout.fillWidth: true; elide: Text.ElideRight
            }
            PanelButton {
                objectName: "chordsConvert"
                text: "Convert clip to piano notes"
                enabled: root.trackModel && root.trackModel.selectedClipId >= 0
                tooltip: "Plays the selected clip of this track out as notes on a new Piano track, to edit them one by one"
                onClicked: bridge.convertClipToPianoTrack(root.trackModel.selectedClipId)
            }
        }
        Text {
            objectName: "chordsError"
            visible: bridge.lastError.length > 0
            text: bridge.lastError
            color: Theme.meterWarning
            font.pixelSize: Theme.captionSize
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    // Changes one strip's chord: typed, or picked from the key's own chords.
    Popup {
        id: chordDialog
        objectName: "chordEditDialog"
        property bool seventh: false
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 420
        modal: true
        padding: 14
        background: Rectangle { color: Theme.popupBg; border.color: Theme.popupBorder; radius: 6 }
        function edit(strip) {
            root.editing = strip
            chordField.text = bridge.chords[strip]
            open()
            chordField.forceActiveFocus()
            chordField.selectAll()
        }
        function accept() {
            if (bridge.setChord(root.editing, chordField.text)) close()
        }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                text: "Strip " + (root.editing + 1)
                color: Theme.text; font.pixelSize: Theme.bodySize; font.bold: true
            }
            TextField {
                id: chordField
                objectName: "chordEditField"
                Layout.fillWidth: true
                maximumLength: 16
                selectByMouse: true
                color: Theme.text
                font.pixelSize: 13
                placeholderText: "A chord: C, Am7, G/B, F#m7b5, Dsus4 … (empty: no chord)"
                placeholderTextColor: Theme.textDisabled
                background: Rectangle { color: Theme.field; border.color: chordField.activeFocus ? Theme.accent : Theme.fieldBorder; radius: 3 }
                onAccepted: chordDialog.accept()
                Keys.onEscapePressed: chordDialog.close()
            }
            RowLayout {
                spacing: 6
                Text { text: "From " + bridge.keyName + ":"; color: Theme.textMuted; font.pixelSize: Theme.captionSize }
                Item { Layout.fillWidth: true }
                ToggleChip {
                    objectName: "chordEditSeventh"
                    text: "7th"; textSize: 11; leftPadding: 10; rightPadding: 10; implicitHeight: 26
                    hoverHighlight: true
                    active: chordDialog.seventh
                    tooltip: "Four-note chords (maj7, m7, 7, m7b5)"
                    onClicked: chordDialog.seventh = !chordDialog.seventh
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: 7
                    delegate: PanelButton {
                        required property int index
                        objectName: "chordEditDegree" + (index + 1)
                        text: bridge.degreeLabels[index] + "  " + bridge.degreeChord(index + 1, chordDialog.seventh)
                        tooltip: "Use " + bridge.degreeChord(index + 1, chordDialog.seventh)
                        onClicked: chordField.text = bridge.degreeChord(index + 1, chordDialog.seventh)
                    }
                }
            }
            Text {
                visible: bridge.lastError.length > 0 && chordDialog.visible
                text: bridge.lastError
                color: Theme.meterWarning; font.pixelSize: Theme.captionSize
                wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                PanelButton { text: "Clear"; tooltip: "Leave this strip empty"; onClicked: chordField.text = "" }
                Item { Layout.fillWidth: true }
                PanelButton { text: "Cancel"; onClicked: chordDialog.close() }
                PanelButton { objectName: "chordEditOk"; text: "OK"; onClicked: chordDialog.accept() }
            }
        }
    }
}
