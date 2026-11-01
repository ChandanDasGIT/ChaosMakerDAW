import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Popup {
    id: root
    objectName: "guitarSuitePanel"
    property string trackName: ""
    parent: Overlay.overlay
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    width: Math.min(920, parent.width - 32)
    height: Math.min(580, parent.height - 32)
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 0
    background: Rectangle { radius: 9; color: "#25272c"; border.color: "#4a4d56" }
    contentItem: ColumnLayout {
        enabled: !EngineController.guitarSuite.busy
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "GUITARSUITE"; color: Theme.text; font.pixelSize: 14; font.bold: true; font.letterSpacing: 1 }
                Text { text: trackName + " · live monitor chain"; color: Theme.textMuted; font.pixelSize: 10 }
            }
            ToolButton {
                text: "Save preset"
                onClicked: savePresetDialog.open()
                contentItem: Text { text: "Save preset"; color: parent.hovered ? Theme.text : Theme.textMuted; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: 4; color: parent.hovered ? "#383838" : "#303030"; border.color: "#424242" }
            }
            ToolButton {
                text: "Load preset"
                onClicked: loadPresetDialog.open()
                contentItem: Text { text: "Load preset"; color: parent.hovered ? Theme.text : Theme.textMuted; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: 4; color: parent.hovered ? "#383838" : "#303030"; border.color: "#424242" }
            }
            ToolButton {
                text: "×"
                onClicked: root.close()
                contentItem: Text { text: "×"; color: parent.hovered ? Theme.text : Theme.textMuted; font.pixelSize: 18; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text { text: "FACTORY PRESET"; color: Theme.textMuted; font.pixelSize: 9; font.bold: true }
            ComboBox {
                id: factoryPresetPicker
                objectName: "guitarFactoryPreset"
                Layout.preferredWidth: 140
                model: EngineController.guitarSuite.factoryPresetNames
            }
            Button {
                objectName: "applyGuitarFactoryPreset"
                text: "Apply preset"
                onClicked: EngineController.guitarSuite.applyFactoryPreset(factoryPresetPicker.currentText)
            }
            Text {
                Layout.fillWidth: true
                text: "Replaces pedals · keeps input trim and loaded cabinet"
                color: Theme.textMuted; font.pixelSize: 10
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
                    text: "INPUT TRIM  " + (EngineController.guitarSuite.inputTrimDb > 0 ? "+" : "") + Number(EngineController.guitarSuite.inputTrimDb).toFixed(1) + " dB"
                    color: Theme.text; font.pixelSize: 10
                }
                RowLayout {
                    Slider {
                        objectName: "guitarInputTrim"
                        Layout.fillWidth: true
                        from: -24; to: 24; stepSize: 0.1
                        value: EngineController.guitarSuite.inputTrimDb
                        onMoved: EngineController.guitarSuite.inputTrimDb = value
                        ToolTip.visible: hovered
                        ToolTip.text: "Adjust hard picking to peak near −12 dBFS on Pre-FX"
                    }
                    ToolButton {
                        text: "0 dB"
                        onClicked: EngineController.guitarSuite.inputTrimDb = 0
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
                    levelDb: EngineController.monitorLevels[modelData.db] ?? -120
                    clipped: EngineController.monitorLevels[modelData.clip] ?? false
                    showTarget: modelData.db === "preFxDb"
                }
            }
            ToolButton {
                text: "Clear clips"
                onClicked: EngineController.clearMonitorClips()
                ToolTip.visible: hovered
                ToolTip.text: "Reset the latched red clip indicators"
            }
        }
        Text {
            Layout.fillWidth: true
            text: "Hard picking: aim near −12 dBFS on Pre-FX. If Raw input clips, lower the interface gain."
            color: Theme.textMuted; font.pixelSize: 10
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text { text: "ADD PEDAL"; color: Theme.textMuted; font.pixelSize: 9; font.bold: true; font.letterSpacing: 0.7 }
            ComboBox {
                id: pedalPicker
                Layout.preferredWidth: 170
                model: EngineController.guitarSuite.availablePedals()
            }
            Button {
                text: "Add pedal"
                onClicked: EngineController.guitarSuite.addPedal(pedalPicker.currentText)
            }
            Text {
                text: "Scroll inside a pedal for all controls"
                color: Theme.textMuted; font.pixelSize: 10
                Layout.fillWidth: true; horizontalAlignment: Text.AlignRight
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
        Flickable {
            id: pedalScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: Math.max(width, pedalCards.count * 254)
            contentHeight: height
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
            Text {
                visible: EngineController.guitarSuite.count === 0
                anchors.centerIn: parent
                text: "Choose a pedal above to build your chain."
                color: Theme.textMuted
                font.pixelSize: 11
            }
            Repeater {
                id: pedalCards
                model: EngineController.guitarSuite
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
                    Component.onCompleted: {
                        controlDescriptors = parameters.map(function(parameter) {
                            return {id: parameter.id, name: parameter.name,
                                    min: parameter.min, max: parameter.max,
                                    value: parameter.value, unit: parameter.unit}
                        })
                    }
                    function parameterValue(id, fallback) {
                        for (const parameter of parameters)
                            if (parameter.id === id) return parameter.value
                        return fallback
                    }
                    x: index * 254
                    y: 4
                    width: 242
                    height: pedalScroll.height - 12
                    radius: 7
                    color: "#2c2e34"
                    border.color: dragArea.drag.active ? Theme.accent : "#454851"
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
                                drag.target: pedalCard
                                drag.axis: Drag.XAxis
                                onReleased: {
                                    pedalCard.Drag.drop()
                                    pedalCard.x = Qt.binding(function() { return pedalCard.index * 254 })
                                }
                                Text { anchors.centerIn: parent; text: "⠿"; color: Theme.textMuted; font.pixelSize: 18 }
                                ToolTip.visible: containsMouse
                                ToolTip.text: "Drag to reorder"
                                ToolTip.delay: 450
                            }
                            Text { text: pedalCard.pedalName.toUpperCase(); color: Theme.text; font.pixelSize: 11; font.bold: true; font.letterSpacing: 0.7; Layout.fillWidth: true }
                            ToolButton {
                                text: pedalCard.isBypassed ? "BYP" : "ON"
                                onClicked: EngineController.guitarSuite.setBypassed(pedalCard.pedalId, !pedalCard.isBypassed)
                                contentItem: Text { text: parent.text; color: pedalCard.isBypassed ? Theme.textMuted : Theme.accent; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { radius: 4; color: pedalCard.isBypassed ? "#303030" : "#203a45"; border.color: pedalCard.isBypassed ? "#424242" : Theme.accent }
                            }
                            ToolButton {
                                text: "×"
                                onClicked: EngineController.guitarSuite.removePedal(pedalCard.pedalId)
                                contentItem: Text { text: "×"; color: parent.hovered ? Theme.record : Theme.textMuted; font.pixelSize: 16; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
                        ScrollView {
                            id: controlScroll
                            ScrollBar.vertical.policy: contentHeight > availableHeight + 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            contentWidth: availableWidth
                            clip: true
                            ColumnLayout {
                                width: controlScroll.availableWidth
                                Text {
                                    visible: pedalCard.pedalType === "pedal.octaver" || pedalCard.pedalType === "pedal.pitchshift"
                                    text: "Shifted voices have a short processing delay. Best with clear single notes."
                                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                                    color: Theme.textMuted; font.pixelSize: 10
                                }
                                ColumnLayout {
                                    visible: pedalCard.pedalType === "pedal.cabinet"
                                    Layout.fillWidth: true
                                    Button {
                                        text: "Load cabinet IR…"
                                        Layout.fillWidth: true
                                        onClicked: cabinetDialog.open()
                                    }
                                    Button {
                                        text: "Clear IR"
                                        enabled: pedalCard.resourcePath.length > 0
                                        Layout.fillWidth: true
                                        onClicked: EngineController.guitarSuite.clearCabinetIr(pedalCard.pedalId)
                                    }
                                    Text {
                                        text: pedalCard.resourcePath.length ? pedalCard.resourcePath.split("/").pop() : "No IR loaded · dry signal"
                                        color: Theme.textMuted; font.pixelSize: 10
                                        Layout.fillWidth: true; elide: Text.ElideMiddle
                                    }
                                }
                                ColumnLayout {
                                    id: tunerDisplay
                                    visible: pedalCard.pedalType === "pedal.tuner"
                                    Layout.fillWidth: true
                                    spacing: 10
                                    property var reading: ({})
                                    property int lastRevision: -1
                                    property int staleTicks: 0
                                    property bool hasPitch: !pedalCard.isBypassed && staleTicks <= 4 && Number(reading.frequency) > 0
                                    property real cents: hasPitch ? Number(reading.cents) : 0
                                    property bool inTune: hasPitch && Math.abs(cents) <= 3
                                    property int selection: {
                                        for (const parameter of pedalCard.parameters)
                                            if (parameter.id === "string") return Math.round(parameter.value)
                                        return 0
                                    }
                                    Text {
                                        text: "Standard tuning · A4 = 440 Hz"
                                        color: Theme.textMuted; font.pixelSize: 11
                                        Layout.alignment: Qt.AlignHCenter
                                    }
                                    Button {
                                        text: "Auto-detect string"
                                        checkable: true
                                        checked: tunerDisplay.selection === 0
                                        Layout.fillWidth: true
                                        onClicked: EngineController.guitarSuite.setParameter(pedalCard.pedalId, "string", 0)
                                    }
                                    GridLayout {
                                        columns: 3
                                        Layout.fillWidth: true
                                        Repeater {
                                            model: ["6 · E2", "5 · A2", "4 · D3", "3 · G3", "2 · B3", "1 · E4"]
                                            delegate: Button {
                                                required property int index
                                                required property string modelData
                                                text: modelData
                                                Layout.fillWidth: true
                                                checkable: true
                                                checked: tunerDisplay.selection === index + 1
                                                onClicked: EngineController.guitarSuite.setParameter(pedalCard.pedalId, "string", index + 1)
                                            }
                                        }
                                    }
                                    Text {
                                        text: tunerDisplay.hasPitch ? tunerDisplay.reading.note : "—"
                                        color: tunerDisplay.inTune ? "#74d69a" : Theme.text
                                        font.pixelSize: 34; font.bold: true
                                        Layout.alignment: Qt.AlignHCenter
                                    }
                                    Text {
                                        text: !tunerDisplay.hasPitch ? (pedalCard.isBypassed ? "Tuner bypassed" : "Play one open string")
                                            : tunerDisplay.inTune ? "In tune"
                                            : tunerDisplay.cents < 0 ? "LOW / FLAT · Tune up ↑" : "HIGH / SHARP · Tune down ↓"
                                        color: tunerDisplay.inTune ? "#74d69a" : Theme.text
                                        font.pixelSize: 11
                                        Layout.alignment: Qt.AlignHCenter
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 32
                                        color: "#202227"; radius: 4
                                        Rectangle {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: parent.width * 0.06; height: parent.height
                                            color: "#294b39"
                                        }
                                        Rectangle {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: 1; height: parent.height; color: Theme.textMuted
                                        }
                                        Rectangle {
                                            visible: tunerDisplay.hasPitch
                                            x: (parent.width - width) * (0.5 + Math.max(-50, Math.min(50, tunerDisplay.cents)) / 100)
                                            width: 4; height: parent.height
                                            color: tunerDisplay.inTune ? "#74d69a" : "#efbd70"
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: "−50 · LOW"; color: Theme.textMuted; font.pixelSize: 9 }
                                        Item { Layout.fillWidth: true }
                                        Text { text: "0"; color: Theme.textMuted; font.pixelSize: 9 }
                                        Item { Layout.fillWidth: true }
                                        Text { text: "HIGH · +50"; color: Theme.textMuted; font.pixelSize: 9 }
                                    }
                                    Text {
                                        text: tunerDisplay.hasPitch ? (tunerDisplay.cents > 0 ? "+" : "") + tunerDisplay.cents.toFixed(1) + " cents" : ""
                                        color: Theme.text; font.pixelSize: 16
                                        Layout.alignment: Qt.AlignHCenter
                                    }
                                    Text {
                                        text: tunerDisplay.hasPitch ? Number(tunerDisplay.reading.frequency).toFixed(1) + " Hz → " + Number(tunerDisplay.reading.targetHz).toFixed(2) + " Hz" : ""
                                        color: Theme.textMuted; font.pixelSize: 10
                                        Layout.alignment: Qt.AlignHCenter
                                    }
                                    Timer {
                                        interval: 100
                                        running: root.visible && tunerDisplay.visible
                                        repeat: true
                                        onTriggered: {
                                            const reading = EngineController.guitarSuite.tunerReading(pedalCard.pedalId)
                                            tunerDisplay.staleTicks = reading.revision === tunerDisplay.lastRevision ? tunerDisplay.staleTicks + 1 : 0
                                            tunerDisplay.lastRevision = reading.revision
                                            tunerDisplay.reading = reading
                                        }
                                    }
                                }
                                CheckBox {
                                    objectName: "tremoloSustainToggle"
                                    visible: pedalCard.pedalType === "pedal.tremolo"
                                    text: "Sustain only"
                                    palette.windowText: Theme.text
                                    checked: pedalCard.parameterValue("sustain", 0) >= 0.5
                                    onClicked: EngineController.guitarSuite.setParameter(pedalCard.pedalId, "sustain", checked ? 1 : 0)
                                }
                                ColumnLayout {
                                    id: modulationSync
                                    visible: pedalCard.pedalType === "pedal.tremolo" || pedalCard.pedalType === "pedal.phaser"
                                    Layout.fillWidth: true
                                    spacing: 6
                                    function parameterValue(id, fallback) {
                                        for (const parameter of pedalCard.parameters)
                                            if (parameter.id === id) return parameter.value
                                        return fallback
                                    }
                                    readonly property bool synced: parameterValue("sync", 0) >= 0.5
                                    readonly property int division: Math.round(parameterValue("division", 2))
                                    Switch {
                                        objectName: pedalCard.pedalType === "pedal.phaser" ? "phaserBpmSync" : "tremoloBpmSync"
                                        text: "Sync to BPM"
                                        contentItem: Text {
                                            text: parent.text; color: Theme.text
                                            leftPadding: 48; font.pixelSize: 11
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        checked: modulationSync.synced
                                        onClicked: EngineController.guitarSuite.setParameter(pedalCard.pedalId, "sync", checked ? 1 : 0)
                                    }
                                    ComboBox {
                                        objectName: pedalCard.pedalType === "pedal.phaser" ? "phaserBeatDivision" : "tremoloBeatDivision"
                                        Layout.fillWidth: true
                                        enabled: modulationSync.synced
                                        model: ["1/1 · Whole note", "1/2 · Half note", "1/4 · Quarter note", "1/8 · Eighth note", "1/16 · Sixteenth note", "1/8 · Dotted eighth", "1/8 · Eighth triplet"]
                                        currentIndex: modulationSync.division
                                        onActivated: EngineController.guitarSuite.setParameter(pedalCard.pedalId, "division", currentIndex)
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: modulationSync.synced ? Number(EngineController.bpm).toString() + " BPM · follows bottom-bar tempo"
                                                                  : "Free rate · adjust Rate below"
                                        color: Theme.textMuted
                                        font.pixelSize: 10
                                    }
                                }
                                Repeater {
                                    model: pedalCard.pedalType === "pedal.tuner" ? []
                                        : pedalCard.controlDescriptors.filter(function(parameter) {
                                            return (pedalCard.pedalType !== "pedal.tremolo" && pedalCard.pedalType !== "pedal.phaser") || (parameter.id !== "sync" && parameter.id !== "division" && parameter.id !== "sustain")
                                        })
                                    delegate: ColumnLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 84
                                        spacing: 2
                                        Text { text: modelData.name + ((pedalCard.pedalType === "pedal.tremolo" || pedalCard.pedalType === "pedal.phaser") && modelData.id === "rate" && modulationSync.synced ? " (manual)" : ""); color: Theme.textMuted; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
                                        Dial {
                                            objectName: pedalCard.pedalType + "." + modelData.id
                                            Layout.alignment: Qt.AlignHCenter
                                            implicitWidth: 52
                                            implicitHeight: 52
                                            enabled: !((pedalCard.pedalType === "pedal.tremolo" || pedalCard.pedalType === "pedal.phaser") && modelData.id === "rate" && modulationSync.synced)
                                            opacity: enabled ? 1 : 0.35
                                            stepSize: pedalCard.pedalType === "pedal.pitchshift" && modelData.id === "semitones" ? 1 : 0
                                            from: modelData.min
                                            to: modelData.max
                                            value: pedalCard.parameterValue(modelData.id, modelData.value)
                                            onMoved: EngineController.guitarSuite.setParameter(pedalCard.pedalId, modelData.id, value)
                                        }
                                        Text {
                                            text: Number(pedalCard.parameterValue(modelData.id, modelData.value)).toFixed((modelData.unit === "ms" || modelData.unit === "st") ? 0 : 2) + (modelData.unit.length ? " " + modelData.unit : "")
                                            color: Theme.text
                                            font.pixelSize: 9
                                            horizontalAlignment: Text.AlignHCenter
                                            Layout.fillWidth: true
                                        }
                                    }
                                }
                            }
                        }
                    }
                    FileDialog {
                        id: cabinetDialog
                        title: "Load cabinet impulse response"
                        fileMode: FileDialog.OpenFile
                        nameFilters: ["WAV impulse responses (*.wav *.WAV)"]
                        onAccepted: EngineController.guitarSuite.loadCabinetIr(pedalCard.pedalId, selectedFile)
                    }
                    DropArea {
                        enabled: !dragArea.drag.active
                        anchors.fill: parent
                        keys: ["guitarsuite-pedal"]
                        onDropped: function(drop) {
                            if (drop.source && drop.source.index !== pedalCard.index)
                                EngineController.guitarSuite.movePedal(drop.source.index, pedalCard.index)
                        }
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            text: EngineController.guitarSuite.busy ? "Loading and preparing cabinet IR…" : EngineController.guitarSuite.lastError.length > 0
                ? EngineController.guitarSuite.lastError
                : "Preset file: " + EngineController.guitarSuite.presetFilePath
            color: EngineController.guitarSuite.lastError.length > 0 ? Theme.record : Theme.textMuted
            font.pixelSize: 9
            elide: Text.ElideMiddle
        }
    }

    FileDialog {
        id: savePresetDialog
        title: "Save GuitarSuite preset"
        fileMode: FileDialog.SaveFile
        nameFilters: ["GuitarSuite presets (*.gspreset)", "All files (*)"]
        defaultSuffix: "gspreset"
        onAccepted: EngineController.guitarSuite.savePresetTo(selectedFile)
    }

    FileDialog {
        id: loadPresetDialog
        title: "Load GuitarSuite preset"
        fileMode: FileDialog.OpenFile
        nameFilters: ["GuitarSuite presets (*.gspreset)", "All files (*)"]
        onAccepted: EngineController.guitarSuite.loadPresetFrom(selectedFile)
    }
}
