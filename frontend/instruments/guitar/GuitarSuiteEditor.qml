import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var effectsModel
    property string trackName: ""
    property bool monitoring: true
    property var levels: EngineController.monitorLevels
    // Pedal card size: wide enough for a name | slider | value row per parameter.
    readonly property real cardWidth: 276
    readonly property real cardStep: cardWidth + 12
    signal clearClipsRequested()
    ColumnLayout {
        enabled: root.effectsModel && !root.effectsModel.busy
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            PanelButton {
                text: "Save preset"
                onClicked: savePresetDialog.open()
            }
            PanelButton {
                text: "Load preset"
                onClicked: loadPresetDialog.open()
            }
            // The preset that is playing now.
            Text {
                text: "PRESET"
                color: Theme.textMuted; font.pixelSize: Theme.microSize; font.bold: true
                Layout.leftMargin: 10
            }
            Text {
                objectName: "guitarPresetName"
                Layout.fillWidth: true
                text: root.effectsModel.presetName.length === 0 ? "None (your own board)"
                    : root.effectsModel.presetName + (root.effectsModel.presetEdited ? "  ·  edited" : "")
                color: root.effectsModel.presetName.length === 0 ? Theme.textMuted : Theme.text
                font.pixelSize: Theme.bodySize
                font.bold: root.effectsModel.presetName.length > 0
                elide: Text.ElideRight
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text { text: "FACTORY PRESET"; color: Theme.textMuted; font.pixelSize: Theme.microSize; font.bold: true }
            ThemedComboBox {
                id: factoryPresetPicker
                objectName: "guitarFactoryPreset"
                Layout.preferredWidth: 140
                model: root.effectsModel.factoryPresetNames
            }
            PanelButton {
                objectName: "applyGuitarFactoryPreset"
                text: "Apply preset"
                onClicked: root.effectsModel.applyFactoryPreset(factoryPresetPicker.currentText)
            }
            Text {
                Layout.fillWidth: true
                text: "Replaces pedals · keeps input trim and loaded cabinet"
                color: Theme.textMuted; font.pixelSize: Theme.captionSize
                wrapMode: Text.WordWrap
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            ColumnLayout {
                Layout.preferredWidth: 210
                spacing: 0
                Text {
                    text: "INPUT TRIM  " + root.effectsModel.formatTrim(root.effectsModel.inputTrimDb)
                    color: Theme.text; font.pixelSize: Theme.captionSize
                }
                RowLayout {
                    HardwareSlider {
                        objectName: "guitarInputTrim"
                        Layout.fillWidth: true
                        from: -24; to: 24; stepSize: 0.1
                        value: root.effectsModel.inputTrimDb
                        onMoved: root.effectsModel.inputTrimDb = value
                        ToolTip.visible: hovered
                        ToolTip.text: "Adjust input peaks near −12 dBFS on Pre-FX"
                    }
                    ToolButton {
                        text: "0 dB"
                        onClicked: root.effectsModel.inputTrimDb = 0
                    }
                }
            }
            Repeater {
                model: [
                    {label: "Raw input", db: "rawDb", clip: "rawClip"},
                    {label: "Pre-FX", db: "preFxDb", clip: "preFxClip"},
                    {label: "Output L", db: "leftDb", clip: "leftClip"},
                    {label: "Output R", db: "rightDb", clip: "rightClip"}
                ]
                delegate: LevelMeter {
                    required property var modelData
                    Layout.fillWidth: true
                    label: modelData.label
                    levelDb: root.monitoring ? (root.levels[modelData.db] ?? -120) : -120
                    clipped: root.monitoring && (root.levels[modelData.clip] ?? false)
                    showTarget: modelData.db === "preFxDb"
                }
            }
            PanelButton {
                text: "Clear clips"
                enabled: root.monitoring
                onClicked: { EngineController.clearMonitorClips(); root.clearClipsRequested() }
                tooltip: "Reset the latched red clip indicators"
            }
        }
        Text {
            Layout.fillWidth: true
            text: root.monitoring ? "Input peaks: aim near −12 dBFS on Pre-FX. If Raw input clips, lower the interface gain."
                : "Arm this track to hear and meter its effects."
            color: Theme.textMuted; font.pixelSize: Theme.captionSize
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text { text: "ADD PEDAL"; color: Theme.textMuted; font.pixelSize: Theme.microSize; font.bold: true; font.letterSpacing: 0.7 }
            ThemedComboBox {
                id: pedalPicker
                Layout.preferredWidth: 170
                model: root.effectsModel.availablePedals()
            }
            PanelButton {
                text: "Add pedal"
                onClicked: root.effectsModel.addPedal(pedalPicker.currentText)
            }
            Text {
                text: "Scroll inside a pedal for all controls"
                color: Theme.textMuted; font.pixelSize: Theme.captionSize
                Layout.fillWidth: true; horizontalAlignment: Text.AlignRight
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
        Flickable {
            id: pedalScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: Math.max(width, pedalCards.count * root.cardStep)
            contentHeight: height
            boundsBehavior: Flickable.StopAtBounds
            // More pedals than fit: the bar stays on screen, so a cut-off pedal is seen to scroll.
            readonly property bool overflowing: contentWidth > width + 1
            ScrollBar.horizontal: ScrollBar { id: pedalScrollBar; objectName: "guitarPedalScrollBar"; policy: pedalScroll.overflowing ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
            Text {
                visible: root.effectsModel.count === 0
                anchors.centerIn: parent
                text: "Choose a pedal above to build your chain."
                color: Theme.textMuted
                font.pixelSize: Theme.smallSize
            }
            Repeater {
                id: pedalCards
                model: root.effectsModel
                delegate: Rectangle {
                    id: pedalCard
                    required property int index
                    required property int pedalId
                    required property string pedalType
                    required property string resourcePath
                    required property string pedalName
                    required property bool isBypassed
                    required property var parameters
                    // Keep control descriptors stable: value updates must not rebuild
                    // the Repeater and destroy a dial holding keyboard/mouse focus.
                    property var controlDescriptors: []
                    // Which extra sections this pedal shows (backend pedaleditorrules)
                    readonly property var features: root.effectsModel.pedalFeatures(pedalType)
                    Component.onCompleted: {
                        controlDescriptors = parameters.map(function(parameter) {
                            return {id: parameter.id, name: parameter.name,
                                    min: parameter.min, max: parameter.max,
                                    value: parameter.value, unit: parameter.unit,
                                    defaultValue: parameter["default"]}
                        })
                    }
                    function parameterValue(id, fallback) {
                        for (const parameter of parameters)
                            if (parameter.id === id) return parameter.value
                        return fallback
                    }
                    x: index * root.cardStep
                    y: 4
                    width: root.cardWidth
                    height: pedalScroll.height - 12 - (pedalScroll.overflowing ? pedalScrollBar.height + 2 : 0)
                    radius: Theme.panelRadius
                    color: Theme.guitarSuite.card
                    border.color: dragArea.drag.active ? Theme.accent : Theme.guitarSuite.cardBorder
                    Drag.source: pedalCard
                    Drag.keys: ["guitarsuite-pedal"]
                    Drag.hotSpot.x: width / 2
                    Drag.hotSpot.y: 18
                    Drag.active: dragArea.drag.active

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 7
                        RowLayout {
                            Layout.fillWidth: true
                            MouseArea {
                                id: dragArea
                                Layout.preferredWidth: 22
                                Layout.preferredHeight: 26
                                cursorShape: Qt.OpenHandCursor
                                Accessible.role: Accessible.Grip
                                Accessible.name: "Drag to reorder pedal"
                                drag.target: pedalCard
                                drag.axis: Drag.XAxis
                                onReleased: {
                                    pedalCard.Drag.drop()
                                    pedalCard.x = Qt.binding(function() { return pedalCard.index * root.cardStep })
                                }
                                Text { anchors.centerIn: parent; text: Icons.dragHandle; color: Theme.textMuted; font.pixelSize: 18 }
                                ToolTip.visible: containsMouse
                                ToolTip.text: "Drag to reorder"
                                ToolTip.delay: 450
                            }
                            Text { text: pedalCard.pedalName.toUpperCase(); color: Theme.text; font.pixelSize: Theme.smallSize; font.bold: true; font.letterSpacing: 0.7; Layout.fillWidth: true }
                            ToggleChip {
                                active: !pedalCard.isBypassed
                                text: pedalCard.isBypassed ? "BYP" : "ON"; textSize: Theme.microSize
                                onClicked: root.effectsModel.setBypassed(pedalCard.pedalId, !pedalCard.isBypassed)
                            }
                            CloseButton {
                                destructive: true
                                Accessible.name: "Remove pedal"
                                onClicked: root.effectsModel.removePedal(pedalCard.pedalId)
                            }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
                        ScrollView {
                            id: controlScroll
                            // Mouse drags belong to the sliders: without this the card's
                            // Flickable steals the press and a double-click never resets a
                            // value. The wheel and the scroll bar still scroll the card.
                            Component.onCompleted: contentItem.acceptedButtons = Qt.NoButton
                            ScrollBar.vertical.policy: contentHeight > availableHeight + 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            contentWidth: availableWidth
                            clip: true
                            ColumnLayout {
                                // Leave room for the scroll bar so it never covers values.
                                width: controlScroll.availableWidth
                                       - (controlScroll.ScrollBar.vertical.policy === ScrollBar.AlwaysOn ? controlScroll.ScrollBar.vertical.width + 4 : 0)
                                Text {
                                    visible: pedalCard.features.pitchDelayNote
                                    text: "Shifted voices have a short processing delay. Best with clear single notes."
                                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                                    color: Theme.textMuted; font.pixelSize: Theme.captionSize
                                }
                                ColumnLayout {
                                    visible: pedalCard.features.cabinetIr
                                    Layout.fillWidth: true
                                    PanelButton {
                                        text: "Load cabinet IR…"
                                        Layout.fillWidth: true
                                        onClicked: cabinetDialog.open()
                                    }
                                    PanelButton {
                                        text: "Clear IR"
                                        enabled: pedalCard.resourcePath.length > 0
                                        Layout.fillWidth: true
                                        onClicked: root.effectsModel.clearCabinetIr(pedalCard.pedalId)
                                    }
                                    Text {
                                        text: pedalCard.resourcePath.length ? pedalCard.resourcePath.split("/").pop() : "No IR loaded · dry signal"
                                        color: Theme.textMuted; font.pixelSize: Theme.captionSize
                                        Layout.fillWidth: true; elide: Text.ElideMiddle
                                    }
                                }
                                ColumnLayout {
                                    id: tunerDisplay
                                    visible: pedalCard.features.tuner
                                    Layout.fillWidth: true
                                    spacing: 10
                                    property var reading: ({})
                                    property int lastRevision: -1
                                    property int staleTicks: 0
                                    property bool hasPitch: !pedalCard.isBypassed && staleTicks <= 4 && Number(reading.frequency) > 0
                                    property real cents: hasPitch ? Number(reading.cents) : 0
                                    property bool inTune: hasPitch && root.effectsModel.inTune(cents)
                                    property int selection: {
                                        for (const parameter of pedalCard.parameters)
                                            if (parameter.id === "string") return Math.round(parameter.value)
                                        return 0
                                    }
                                    Text {
                                        text: root.effectsModel.tunerReferenceText
                                        color: Theme.textMuted; font.pixelSize: Theme.smallSize
                                        Layout.alignment: Qt.AlignHCenter
                                    }
                                    PanelButton {
                                        text: "Auto-detect string"
                                        checkable: true
                                        checked: tunerDisplay.selection === 0
                                        Layout.fillWidth: true
                                        onClicked: root.effectsModel.setParameter(pedalCard.pedalId, "string", 0)
                                    }
                                    GridLayout {
                                        columns: 3
                                        Layout.fillWidth: true
                                        Repeater {
                                            model: root.effectsModel.tunerStringLabels
                                            delegate: PanelButton {
                                                required property int index
                                                required property string modelData
                                                text: modelData
                                                Layout.fillWidth: true
                                                checkable: true
                                                checked: tunerDisplay.selection === index + 1
                                                onClicked: root.effectsModel.setParameter(pedalCard.pedalId, "string", index + 1)
                                            }
                                        }
                                    }
                                    Text {
                                        text: tunerDisplay.hasPitch ? tunerDisplay.reading.note : "—"
                                        color: tunerDisplay.inTune ? Theme.guitarSuite.inTune : Theme.text
                                        font.pixelSize: 34; font.bold: true
                                        Layout.alignment: Qt.AlignHCenter
                                    }
                                    Text {
                                        text: root.effectsModel.tunerStatus(tunerDisplay.hasPitch, pedalCard.isBypassed, tunerDisplay.cents)
                                        color: tunerDisplay.inTune ? Theme.guitarSuite.inTune : Theme.text
                                        font.pixelSize: Theme.smallSize
                                        Layout.alignment: Qt.AlignHCenter
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 32
                                        color: Theme.guitarSuite.tunerScale; radius: Theme.controlRadius
                                        Rectangle {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: parent.width * 0.06; height: parent.height
                                            color: Theme.guitarSuite.tunerZone
                                        }
                                        Rectangle {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: 1; height: parent.height; color: Theme.textMuted
                                        }
                                        Rectangle {
                                            visible: tunerDisplay.hasPitch
                                            x: (parent.width - width) * (0.5 + Math.max(-50, Math.min(50, tunerDisplay.cents)) / 100)
                                            width: 4; height: parent.height
                                            color: tunerDisplay.inTune ? Theme.guitarSuite.inTune : Theme.guitarSuite.offTune
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: "−50 · LOW"; color: Theme.textMuted; font.pixelSize: Theme.microSize }
                                        Item { Layout.fillWidth: true }
                                        Text { text: "0"; color: Theme.textMuted; font.pixelSize: Theme.microSize }
                                        Item { Layout.fillWidth: true }
                                        Text { text: "HIGH · +50"; color: Theme.textMuted; font.pixelSize: Theme.microSize }
                                    }
                                    Text {
                                        text: tunerDisplay.hasPitch ? root.effectsModel.formatCents(tunerDisplay.cents) : ""
                                        color: Theme.text; font.pixelSize: Theme.headingSize
                                        Layout.alignment: Qt.AlignHCenter
                                    }
                                    Text {
                                        text: tunerDisplay.hasPitch ? root.effectsModel.formatTunerFrequency(Number(tunerDisplay.reading.frequency), Number(tunerDisplay.reading.targetHz)) : ""
                                        color: Theme.textMuted; font.pixelSize: Theme.captionSize
                                        Layout.alignment: Qt.AlignHCenter
                                    }
                                    Timer {
                                        interval: 100
                                        running: root.visible && tunerDisplay.visible
                                        repeat: true
                                        onTriggered: {
                                            const reading = root.effectsModel.tunerReading(pedalCard.pedalId)
                                            tunerDisplay.staleTicks = reading.revision === tunerDisplay.lastRevision ? tunerDisplay.staleTicks + 1 : 0
                                            tunerDisplay.lastRevision = reading.revision
                                            tunerDisplay.reading = reading
                                        }
                                    }
                                }
                                ThemedCheckBox {
                                    objectName: "tremoloSustainToggle"
                                    visible: pedalCard.features.sustainToggle
                                    text: "Sustain only"
                                    textSize: Theme.smallSize
                                    checked: pedalCard.parameterValue("sustain", 0) >= 0.5
                                    onClicked: root.effectsModel.setParameter(pedalCard.pedalId, "sustain", checked ? 1 : 0)
                                }
                                ColumnLayout {
                                    id: modulationSync
                                    visible: pedalCard.features.tempoSync
                                    Layout.fillWidth: true
                                    spacing: 6
                                    function parameterValue(id, fallback) {
                                        for (const parameter of pedalCard.parameters)
                                            if (parameter.id === id) return parameter.value
                                        return fallback
                                    }
                                    readonly property bool synced: parameterValue("sync", 0) >= 0.5
                                    readonly property int division: Math.round(parameterValue("division", 2))
                                    ThemedSwitch {
                                        objectName: pedalCard.pedalType === "pedal.phaser" ? "phaserBpmSync" : "tremoloBpmSync"
                                        text: "Sync to BPM"
                                        textSize: Theme.smallSize
                                        checked: modulationSync.synced
                                        onClicked: root.effectsModel.setParameter(pedalCard.pedalId, "sync", checked ? 1 : 0)
                                    }
                                    ThemedComboBox {
                                        objectName: pedalCard.pedalType === "pedal.phaser" ? "phaserBeatDivision" : "tremoloBeatDivision"
                                        Layout.fillWidth: true
                                        enabled: modulationSync.synced
                                        model: root.effectsModel.beatDivisionNames
                                        currentIndex: modulationSync.division
                                        onActivated: root.effectsModel.setParameter(pedalCard.pedalId, "division", currentIndex)
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: root.effectsModel.syncText(modulationSync.synced, EngineController.bpm)
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.captionSize
                                    }
                                }
                                Repeater {
                                    model: pedalCard.controlDescriptors.filter(function(parameter) {
                                            return root.effectsModel.isDial(pedalCard.pedalType, parameter.id)
                                        })
                                    // One row per parameter: name | slider | value. The plain
                                    // wheel scrolls the card (sliders ignore it); double-click
                                    // resets to the pedal's default.
                                    delegate: RowLayout {
                                        id: controlRow
                                        required property var modelData
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 26
                                        spacing: 4
                                        readonly property bool syncedOff: root.effectsModel.disabledWhenSynced(pedalCard.pedalType, modelData.id) && modulationSync.synced
                                        readonly property real modelValue: Number(pedalCard.parameterValue(modelData.id, modelData.value))
                                        opacity: syncedOff ? 0.4 : 1
                                        Text {
                                            text: controlRow.modelData.name + (controlRow.syncedOff ? " (manual)" : "")
                                            color: Theme.textMuted
                                            font.pixelSize: Theme.captionSize
                                            elide: Text.ElideRight
                                            Layout.preferredWidth: 78
                                        }
                                        HardwareSlider {
                                            id: parameterSlider
                                            objectName: pedalCard.pedalType + "." + controlRow.modelData.id
                                            Layout.fillWidth: true
                                            implicitHeight: 22
                                            padding: 0
                                            enabled: !controlRow.syncedOff
                                            stepSize: root.effectsModel.dialStep(pedalCard.pedalType, controlRow.modelData.id)
                                            from: controlRow.modelData.min
                                            to: controlRow.modelData.max
                                            value: controlRow.modelValue
                                            onMoved: root.effectsModel.setParameter(pedalCard.pedalId, controlRow.modelData.id, value)
                                            TapHandler {
                                                acceptedButtons: Qt.LeftButton
                                                enabled: controlRow.modelData.defaultValue !== undefined
                                                // Apply after the slider handles the mouse release.
                                                onDoubleTapped: Qt.callLater(() => root.effectsModel.setParameter(
                                                    pedalCard.pedalId, controlRow.modelData.id, controlRow.modelData.defaultValue))
                                            }
                                            ToolTip.visible: hovered || pressed
                                            ToolTip.delay: pressed ? 0 : 450
                                            ToolTip.text: controlRow.modelData.name + " " + root.effectsModel.formatValue(value, controlRow.modelData.unit)
                                                          + " · double-click for default"
                                        }
                                        // Follow model changes (presets, other views) except mid-drag.
                                        Binding {
                                            target: parameterSlider
                                            property: "value"
                                            value: controlRow.modelValue
                                            when: !parameterSlider.pressed
                                        }
                                        Text {
                                            objectName: pedalCard.pedalType + "." + controlRow.modelData.id + ".value"
                                            text: root.effectsModel.formatValue(parameterSlider.value, controlRow.modelData.unit)
                                            color: Theme.text
                                            font.pixelSize: Theme.captionSize
                                            font.family: Theme.monospaceFontFamily
                                            horizontalAlignment: Text.AlignRight
                                            elide: Text.ElideLeft
                                            Layout.preferredWidth: 60
                                        }
                                    }
                                }
                            }
                        }
                    }
                    FilePicker {
                        id: cabinetDialog
                        title: "Load cabinet impulse response"
                        fileMode: FilePicker.OpenFile
                        nameFilters: ["WAV impulse responses (*.wav *.WAV)"]
                        onAccepted: root.effectsModel.loadCabinetIr(pedalCard.pedalId, selectedFile)
                    }
                    DropArea {
                        enabled: !dragArea.drag.active
                        anchors.fill: parent
                        keys: ["guitarsuite-pedal"]
                        onDropped: function(drop) {
                            if (drop.source && drop.source.index !== pedalCard.index)
                                root.effectsModel.movePedal(drop.source.index, pedalCard.index)
                        }
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            text: root.effectsModel.busy ? "Loading and preparing cabinet IR…" : root.effectsModel.lastError.length > 0
                ? root.effectsModel.lastError
                : "Preset file: " + root.effectsModel.presetFilePath.split("/").pop()
            color: root.effectsModel.lastError.length > 0 ? Theme.record : Theme.textMuted
            font.pixelSize: Theme.microSize
            elide: Text.ElideMiddle
            // The whole path under the mouse.
            HoverHandler { id: presetPathHover }
            ToolTip.visible: presetPathHover.hovered && root.effectsModel.lastError.length === 0 && !root.effectsModel.busy
            ToolTip.text: root.effectsModel.presetFilePath
            ToolTip.delay: 450
        }
    }

    FilePicker {
        id: savePresetDialog
        title: "Save GuitarSuite preset"
        fileMode: FilePicker.SaveFile
        nameFilters: ["GuitarSuite presets (*.gspreset)", "All files (*)"]
        defaultSuffix: "gspreset"
        onAccepted: root.effectsModel.savePresetTo(selectedFile)
    }

    FilePicker {
        id: loadPresetDialog
        title: "Load GuitarSuite preset"
        fileMode: FilePicker.OpenFile
        nameFilters: ["GuitarSuite presets (*.gspreset)", "All files (*)"]
        onAccepted: root.effectsModel.loadPresetFrom(selectedFile)
    }
}
