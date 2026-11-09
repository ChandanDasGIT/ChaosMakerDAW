import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The Chords track's page in the FX panel: autoplay, the key and ready-made progressions, then
// the eight chord strips (ChordStrips). A press plays the strip's chord in the autoplay pattern
// at the project's tempo until another strip is pressed or the same one again; with autoplay
// off a strip sounds while held. A MIDI keyboard or KEYS plays the strips too (chords on the
// white keys C4 .. C5, their low / high zones on C0 .. C1 / C6 .. C7 or after the zone keys
// F#2 G#2 A#2, bass on C2 .. C3), and recording keeps the presses. Everything goes through
// ChordsWrapper.
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
                tooltip: "Back to the key chords of C major, comping"
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
                // A menu: Off, then the styles; hovering a style opens its variations.
                Button {
                    id: autoplayButton
                    objectName: "chordsAutoplay"
                    readonly property var current: bridge.autoplayModes[bridge.autoplayChoice] ?? null
                    readonly property string displayText: current ? current.title : ""
                    implicitWidth: 190
                    implicitHeight: 28
                    hoverEnabled: true
                    checkable: true
                    checked: autoplayMenu.visible
                    onClicked: autoplayMenu.visible ? autoplayMenu.close() : autoplayMenu.open()
                    contentItem: Text {
                        leftPadding: 8
                        rightPadding: 20
                        text: autoplayButton.displayText
                        color: Theme.text
                        font.pixelSize: Theme.smallSize
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        Text {
                            anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                            text: Icons.menuArrow
                            color: Theme.textMuted
                            font.pixelSize: 9
                        }
                    }
                    background: Rectangle {
                        radius: 5
                        color: autoplayButton.pressed || autoplayButton.hovered ? Theme.fieldHover : Theme.field
                        border.color: autoplayMenu.visible || autoplayButton.visualFocus ? Theme.accent : Theme.fieldBorder
                    }
                    ToolTip.visible: hovered && !autoplayMenu.visible && !!current
                    ToolTip.text: current ? current.description : ""
                    ToolTip.delay: 450

                    ThemedMenu {
                        id: autoplayMenu
                        objectName: "chordsAutoplayMenu"
                        y: parent.height + 4
                        width: 170
                        // The styles' entries: in the same tick column as Off, the playing style ticked.
                        delegate: ThemedMenuItem {
                            checkable: true
                            checked: !!subMenu && subMenu.objectName === "chordsAutoplayStyle" + bridge.autoplay
                        }

                        ThemedMenuItem {
                            objectName: "chordsAutoplayOff"
                            text: "Off"
                            checkable: true
                            checked: bridge.autoplay === 0
                            ToolTip.visible: hovered
                            ToolTip.text: bridge.autoplayModes[0].description
                            ToolTip.delay: 450
                            onTriggered: bridge.setAutoplayChoice(0)
                        }
                        ThemedMenuSeparator {}

                        Instantiator {
                            model: bridge.autoplayStyles
                            delegate: ThemedMenu {
                                id: styleMenu
                                required property var modelData
                                objectName: "chordsAutoplayStyle" + modelData.mode
                                title: modelData.title
                                width: 200
                                Instantiator {
                                    model: styleMenu.modelData.variations
                                    delegate: ThemedMenuItem {
                                        required property var modelData
                                        objectName: "chordsAutoplayChoice" + modelData.index
                                        text: modelData.name
                                        checkable: true
                                        checked: bridge.autoplayChoice === modelData.index
                                        ToolTip.visible: hovered
                                        ToolTip.text: modelData.description
                                        ToolTip.delay: 450
                                        onTriggered: {
                                            bridge.setAutoplayChoice(modelData.index)
                                            autoplayMenu.close()
                                        }
                                    }
                                    onObjectAdded: (index, object) => styleMenu.insertItem(index, object)
                                    onObjectRemoved: (index, object) => styleMenu.removeItem(object)
                                }
                            }
                            onObjectAdded: (index, object) => autoplayMenu.insertMenu(index + 2, object)
                            onObjectRemoved: (index, object) => autoplayMenu.removeMenu(object)
                        }
                    }
                }
            }
            RowLayout {
                spacing: 6
                SectionTitle { text: "VOLUME" }
                ToggleChip {
                    objectName: "chordsDynamicsEven"
                    text: "Same"; textSize: 11; leftPadding: 10; rightPadding: 10; implicitHeight: 28
                    hoverHighlight: true
                    active: bridge.dynamics === 0
                    tooltip: "Every note of every chord at the same volume"
                    onClicked: bridge.dynamics = 0
                }
                ToggleChip {
                    objectName: "chordsDynamicsTop"
                    text: "Top note"; textSize: 11; leftPadding: 10; rightPadding: 10; implicitHeight: 28
                    hoverHighlight: true
                    active: bridge.dynamics === 1
                    tooltip: "The chord's highest note louder, on every chord (in arpeggios, every note of that name)"
                    onClicked: bridge.dynamics = 1
                }
                ToggleChip {
                    objectName: "chordsDynamicsMeter"
                    text: "Meter accents"; textSize: 11; leftPadding: 10; rightPadding: 10; implicitHeight: 28
                    hoverHighlight: true
                    active: bridge.dynamics === 2
                    tooltip: "By the time signature: the bar's first beat loudest, then each group's first beat, the other beats, and the off-beats softest"
                    onClicked: bridge.dynamics = 2
                }
                ToggleChip {
                    objectName: "chordsBassWithChords"
                    text: "Bass with chords"; textSize: 11; leftPadding: 10; rightPadding: 10; implicitHeight: 28
                    hoverHighlight: true
                    active: bridge.bassWithChords
                    tooltip: bridge.bassWithChords ? "On: a chord (or its name) plays its bass part too. Click for the right hand only"
                                                   : "Off: a chord plays the right hand only. Click to add its bass part"
                    onClicked: bridge.bassWithChords = !bridge.bassWithChords
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
                    ToolTip.text: "The key: the strips follow it, with the progression in the new key"
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
                    objectName: "chordsProgression"
                    implicitWidth: 230
                    model: bridge.progressions
                    textRole: "name"
                    currentIndex: bridge.currentProgression
                    displayText: currentIndex >= 0 ? currentText : "Custom"
                    onActivated: index => bridge.applyProgression(index)
                    ToolTip.visible: hovered
                    ToolTip.text: currentIndex >= 0 ? bridge.progressions[currentIndex].description + ", in " + bridge.keyName
                                                    : "Chords of your own. Picking a progression, key or mode replaces them"
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
                text: "MIDI keyboard / KEYS: chords on the white keys C4–C5 (low zone C0–C1, high C6–C7, or zone keys F#2 G#2 A#2), bass on C2–C3 · arm the track (REC) to record what you play"
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
