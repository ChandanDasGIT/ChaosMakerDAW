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
    // The board is the signal chain: IN jack, a cable, each pedal with a cable after it, OUT jack.
    readonly property real jackWidth: 44
    readonly property real cableLength: 34
    readonly property real cardStep: cardWidth + cableLength
    readonly property real cardTop: 24          // room above the cards for the bypass arcs
    readonly property real cableY: cardTop + 19 // cables run at the height of a card's title
    function slotX(slot) { return jackWidth + cableLength + slot * cardStep }
    // A pedal being dragged (restMap U8 H4): the others slide aside to show where it lands.
    property int dragFrom: -1
    property int dragTo: -1
    function visualSlot(index) {
        if (dragFrom < 0 || index === dragFrom) return dragFrom < 0 ? index : dragTo
        if (dragFrom < dragTo && index > dragFrom && index <= dragTo) return index - 1
        if (dragTo < dragFrom && index >= dragTo && index < dragFrom) return index + 1
        return index
    }
    // Bring a pedal into view on the board (from the chain bar), with a short highlight.
    property int flashIndex: -1
    function showPedal(index) {
        boardScroll.to = Math.max(0, Math.min(pedalScroll.contentWidth - pedalScroll.width,
                                              slotX(index) - cableLength - 12))
        boardScroll.restart()
        flashIndex = index
        flashTimer.restart()
    }
    function slotAt(x) {
        const count = root.effectsModel ? root.effectsModel.count : 0
        return Math.max(0, Math.min(count - 1, Math.round((x - slotX(0)) / cardStep)))
    }
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
                Layout.fillWidth: true; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight
            }
        }
        SignalChainBar {
            id: chainBar
            objectName: "guitarChainBar"
            Layout.fillWidth: true
            effectsModel: root.effectsModel
            boardFrom: root.dragFrom
            boardTo: root.dragTo
            onPedalClicked: index => root.showPedal(index)
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
        Flickable {
            id: pedalScroll
            NumberAnimation { id: boardScroll; target: pedalScroll; property: "contentX"; duration: 280; easing.type: Easing.OutCubic }
            // A shorter chain (pedals removed): never stay scrolled past its end.
            // (A scroll still running towards a pedal that is gone stops first.)
            onContentWidthChanged: {
                if (boardScroll.running && boardScroll.to > contentWidth - width) boardScroll.stop()
                contentX = Math.max(0, Math.min(contentX, contentWidth - width))
            }
            Timer { id: flashTimer; interval: 700; onTriggered: root.flashIndex = -1 }
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: Math.max(width, root.slotX(pedalCards.count) + root.jackWidth + 8)
            contentHeight: height
            boundsBehavior: Flickable.StopAtBounds
            // More pedals than fit: the bar stays on screen, so a cut-off pedal is seen to scroll.
            readonly property bool overflowing: contentWidth > width + 1
            ScrollBar.horizontal: ScrollBar { id: pedalScrollBar; objectName: "guitarPedalScrollBar"; policy: pedalScroll.overflowing ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
            Text {
                visible: root.effectsModel.count === 0
                x: root.slotX(0) + 8
                y: root.cableY + 24
                text: "Choose a pedal above to build your chain."
                color: Theme.textMuted
                font.pixelSize: Theme.smallSize
            }
            // The guitar goes in here ...
            component Jack: Rectangle {
                property string label
                width: root.jackWidth; height: 30
                y: root.cableY - height / 2
                radius: Theme.controlRadius
                color: Theme.guitarSuite.jack
                border.color: Theme.guitarSuite.cable
                Row {
                    anchors.centerIn: parent
                    spacing: 4
                    Rectangle { width: 8; height: 8; radius: 4; anchors.verticalCenter: parent.verticalCenter; color: Theme.guitarSuite.jackSocket; border.color: Theme.guitarSuite.cable }
                    Text { text: parent.parent.label; color: Theme.text; font.pixelSize: Theme.microSize; font.bold: true; anchors.verticalCenter: parent.verticalCenter }
                }
            }
            Jack { objectName: "guitarChainIn"; label: "IN"; x: 0 }
            Jack { objectName: "guitarChainOut"; label: "OUT"; x: root.slotX(pedalCards.count) }
            // ... and through a cable before each pedal and one after the last, out to OUT.
            Repeater {
                model: pedalCards.count + 1
                delegate: Item {
                    required property int index
                    objectName: "guitarChainCable" + index
                    x: root.slotX(index) - root.cableLength
                    y: root.cableY - 5
                    width: root.cableLength; height: 10
                    Rectangle { x: 2; width: parent.width - 8; height: 2; anchors.verticalCenter: parent.verticalCenter; color: Theme.guitarSuite.cable }
                    Text {   // arrow head: the way the signal goes
                        anchors.right: parent.right; anchors.rightMargin: 1
                        anchors.verticalCenter: parent.verticalCenter
                        text: Icons.signalFlow; color: Theme.guitarSuite.cable; font.pixelSize: Theme.microSize
                    }
                }
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
                    objectName: "guitarPedal" + index
                    readonly property bool isAmp: pedalType === "pedal.amp"
                    readonly property bool isCabinet: pedalType === "pedal.cabinet"
                    readonly property bool dragging: dragArea.drag.active
                    // Its place in the chain; while another pedal is dragged, where it slides to.
                    x: root.slotX(root.visualSlot(index))
                    Behavior on x {
                        enabled: !pedalCard.dragging
                        NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
                    }
                    y: root.cardTop
                    z: dragging ? 10 : 1
                    width: root.cardWidth
                    height: pedalScroll.height - root.cardTop - 8 - (pedalScroll.overflowing ? pedalScrollBar.height + 2 : 0)
                    radius: Theme.panelRadius
                    color: Theme.guitarSuite.card
                    border.color: dragging || root.flashIndex === index ? Theme.accent : Theme.guitarSuite.cardBorder
                    Behavior on border.color { ColorAnimation { duration: 200 } }
                    // Lifted while dragged.
                    scale: dragging ? 1.02 : 1
                    Behavior on scale { NumberAnimation { duration: 120 } }
                    // The amp and the cabinet stand out in the chain: a band in their own colour.
                    Rectangle {
                        objectName: "guitarPedalBand" + pedalCard.pedalId
                        visible: pedalCard.isAmp || pedalCard.isCabinet
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        anchors.margins: 1
                        height: 4
                        radius: 2
                        color: pedalCard.isAmp ? Theme.guitarSuite.ampBand : Theme.guitarSuite.cabinetBand
                    }
                    // A bypassed pedal: the signal goes over it (a dashed arc from cable to cable).
                    Canvas {
                        objectName: "guitarBypassArc" + pedalCard.pedalId
                        visible: pedalCard.isBypassed && !pedalCard.dragging
                        x: -root.cableLength / 2
                        y: -root.cardTop + 2
                        width: pedalCard.width + root.cableLength
                        height: root.cableY - 2 + 6
                        onVisibleChanged: requestPaint()
                        onWidthChanged: requestPaint()
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.strokeStyle = Theme.guitarSuite.cable
                            ctx.lineWidth = 2
                            ctx.setLineDash([4, 3])
                            const bottom = height - 6, top = 4, r = 14
                            ctx.beginPath()
                            ctx.moveTo(1, bottom)
                            ctx.lineTo(1, top + r)
                            ctx.quadraticCurveTo(1, top, 1 + r, top)
                            ctx.lineTo(width - 1 - r, top)
                            ctx.quadraticCurveTo(width - 1, top, width - 1, top + r)
                            ctx.lineTo(width - 1, bottom)
                            ctx.stroke()
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 7
                        opacity: pedalCard.isBypassed ? 0.55 : 1
                        Behavior on opacity { NumberAnimation { duration: 150 } }
                        RowLayout {
                            Layout.fillWidth: true
                            MouseArea {
                                id: dragArea
                                objectName: "guitarPedalGrip" + pedalCard.index
                                Layout.preferredWidth: 22
                                Layout.preferredHeight: 26
                                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                Accessible.role: Accessible.Grip
                                Accessible.name: "Drag to reorder pedal"
                                drag.target: pedalCard
                                drag.axis: Drag.XAxis
                                drag.minimumX: root.slotX(0) - root.cableLength
                                drag.maximumX: root.slotX(Math.max(0, pedalCards.count - 1)) + root.cableLength
                                drag.threshold: 4
                                onPressed: { root.dragFrom = pedalCard.index; root.dragTo = pedalCard.index }
                                // The others slide as the card passes their middle.
                                onPositionChanged: if (drag.active) root.dragTo = root.slotAt(pedalCard.x)
                                // After the drag has ended, so the card glides (its animation is off while dragging).
                                onReleased: Qt.callLater(pedalCard.finishDrag)
                                onCanceled: Qt.callLater(pedalCard.finishDrag)
                                Text { anchors.centerIn: parent; text: Icons.dragHandle; color: Theme.textMuted; font.pixelSize: 18 }
                                ToolTip.visible: containsMouse
                                ToolTip.text: "Drag to reorder"
                                ToolTip.delay: 450
                            }
                            Text { text: pedalCard.pedalName.toUpperCase(); color: Theme.text; font.pixelSize: Theme.smallSize; font.bold: true; font.letterSpacing: 0.7; Layout.fillWidth: true }
                            ToggleChip {
                                objectName: "guitarPedalOn" + pedalCard.index
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
                    // Drop: one move in the chain; the card glides from where it was let go into
                    // its slot (its x binding comes back and animates).
                    function finishDrag() {
                        const from = root.dragFrom, to = root.dragTo
                        root.dragFrom = -1; root.dragTo = -1
                        pedalCard.x = Qt.binding(function() { return root.slotX(root.visualSlot(pedalCard.index)) })
                        if (from >= 0 && to >= 0 && from !== to) root.effectsModel.movePedal(from, to)
                    }
                    // Near the board's edge while dragging: the board scrolls, the card stays under the mouse.
                    Timer {
                        interval: 16
                        repeat: true
                        running: pedalCard.dragging && pedalScroll.overflowing
                        onTriggered: {
                            const left = pedalCard.x - pedalScroll.contentX
                            const right = left + pedalCard.width - pedalScroll.width
                            let step = left < 30 ? -12 : right > -30 ? 12 : 0
                            const target = Math.max(0, Math.min(pedalScroll.contentWidth - pedalScroll.width, pedalScroll.contentX + step))
                            step = target - pedalScroll.contentX
                            if (step === 0) return
                            pedalScroll.contentX = target
                            pedalCard.x = Math.max(dragArea.drag.minimumX, Math.min(dragArea.drag.maximumX, pedalCard.x + step))
                            root.dragTo = root.slotAt(pedalCard.x)
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
