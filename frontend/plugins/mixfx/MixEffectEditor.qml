import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A mixing plugin's FX page (restMap G7: compressor, de-esser, reverb, Chaos Verb), in the
// GuitarSuite's look: a row of pedal-style cards, each control a "name | slider | value" row
// (double-click resets it), choices as drop-downs. The cards and controls come from the backend
// spec through MixEffectWrapper; the page only lays them out.
Item {
    id: root
    property var trackModel
    property int trackId: -1
    property string trackName: ""
    property string effectType: ""
    property bool active: false
    readonly property real cardWidth: 276
    readonly property real cardStep: cardWidth + 12
    readonly property alias effect: fx

    MixEffectWrapper {
        id: fx
        trackModel: root.trackModel ?? null
        trackId: root.trackId
        effectType: root.effectType
        engine: EngineController
        metering: root.active && fx.hasMeter
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                text: fx.title.toUpperCase()
                color: Theme.text; font.pixelSize: 11; font.bold: true; font.letterSpacing: 0.7
            }
            Text {
                text: root.trackName
                color: Theme.textMuted; font.pixelSize: 10
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            ToggleChip {
                objectName: "mixEffectBypass"
                active: !fx.bypassed
                text: fx.bypassed ? "BYP" : "ON"; textSize: 9
                implicitWidth: 34; implicitHeight: 22
                onClicked: fx.bypassed = !fx.bypassed
                tooltip: fx.bypassed ? "Bypassed: the track passes through unchanged" : "Bypass this effect"
            }
        }
        Text {
            Layout.fillWidth: true
            text: "Double-click a slider for its default"
            color: Theme.textMuted; font.pixelSize: 10
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
        Flickable {
            id: cardScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: Math.max(width, (cardRepeater.count + (fx.hasMeter ? 1 : 0)) * root.cardStep)
            contentHeight: height
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

            Repeater {
                id: cardRepeater
                model: fx.cards
                delegate: Rectangle {
                    id: card
                    required property int index
                    required property var modelData
                    objectName: "mixEffectCard_" + modelData.title
                    x: index * root.cardStep
                    y: 4
                    width: root.cardWidth
                    height: cardScroll.height - 12
                    radius: 7
                    color: Theme.guitarSuite.card
                    border.color: Theme.guitarSuite.cardBorder
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 7
                        Text {
                            text: card.modelData.title.toUpperCase()
                            color: Theme.text; font.pixelSize: 11; font.bold: true; font.letterSpacing: 0.7
                            Layout.preferredHeight: 26
                            verticalAlignment: Text.AlignVCenter
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
                        ScrollView {
                            id: controlScroll
                            // Mouse drags belong to the sliders (as in the GuitarSuite cards).
                            Component.onCompleted: contentItem.acceptedButtons = Qt.NoButton
                            ScrollBar.vertical.policy: contentHeight > availableHeight + 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            contentWidth: availableWidth
                            clip: true
                            ColumnLayout {
                                width: controlScroll.availableWidth
                                       - (controlScroll.ScrollBar.vertical.policy === ScrollBar.AlwaysOn ? controlScroll.ScrollBar.vertical.width + 4 : 0)
                                Repeater {
                                    model: card.modelData.controls
                                    delegate: RowLayout {
                                        id: controlRow
                                        required property var modelData
                                        readonly property real modelValue: Number(fx.values[modelData.id] ?? modelData["default"])
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 26
                                        spacing: 4
                                        Text {
                                            text: controlRow.modelData.name
                                            color: Theme.textMuted
                                            font.pixelSize: 10
                                            elide: Text.ElideRight
                                            Layout.preferredWidth: 78
                                        }
                                        HardwareSlider {
                                            id: parameterSlider
                                            objectName: root.effectType + "." + controlRow.modelData.id
                                            visible: controlRow.modelData.kind === "slider"
                                            Layout.fillWidth: true
                                            implicitHeight: 22
                                            padding: 0
                                            stepSize: fx.step(controlRow.modelData.id)
                                            from: controlRow.modelData.min
                                            to: controlRow.modelData.max
                                            value: controlRow.modelValue
                                            onMoved: fx.setValue(controlRow.modelData.id, value)
                                            TapHandler {
                                                acceptedButtons: Qt.LeftButton
                                                // Apply after the slider handles the mouse release.
                                                onDoubleTapped: Qt.callLater(() => fx.resetValue(controlRow.modelData.id))
                                            }
                                            ToolTip.visible: hovered || pressed
                                            ToolTip.delay: pressed ? 0 : 450
                                            ToolTip.text: controlRow.modelData.name + " " + fx.formatValue(controlRow.modelData.id, value)
                                                          + " · double-click for default"
                                        }
                                        // Follow model changes (other views, chain imports) except mid-drag.
                                        Binding {
                                            target: parameterSlider
                                            property: "value"
                                            value: controlRow.modelValue
                                            when: !parameterSlider.pressed
                                        }
                                        ThemedComboBox {
                                            objectName: root.effectType + "." + controlRow.modelData.id + ".choice"
                                            visible: controlRow.modelData.kind === "choice"
                                            Layout.fillWidth: true
                                            model: controlRow.modelData.choices
                                            currentIndex: Math.round(controlRow.modelValue)
                                            onActivated: index => fx.setValue(controlRow.modelData.id, index)
                                        }
                                        Switch {
                                            objectName: root.effectType + "." + controlRow.modelData.id + ".switch"
                                            visible: controlRow.modelData.kind === "switch"
                                            Layout.fillWidth: true
                                            checked: controlRow.modelValue >= 0.5
                                            onClicked: fx.setValue(controlRow.modelData.id, checked ? 1 : 0)
                                        }
                                        Text {
                                            objectName: root.effectType + "." + controlRow.modelData.id + ".value"
                                            visible: controlRow.modelData.kind !== "choice"
                                            text: fx.formatValue(controlRow.modelData.id,
                                                                 controlRow.modelData.kind === "slider" ? parameterSlider.value : controlRow.modelValue)
                                            color: Theme.text
                                            font.pixelSize: 10
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
                }
            }

            // The dynamics plugins' gain-reduction meter, a card after the controls.
            Rectangle {
                objectName: "mixEffectMeter"
                visible: fx.hasMeter
                x: cardRepeater.count * root.cardStep
                y: 4
                width: root.cardWidth
                height: cardScroll.height - 12
                radius: 7
                color: Theme.guitarSuite.card
                border.color: Theme.guitarSuite.cardBorder
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 7
                    Text {
                        text: "GAIN REDUCTION"
                        color: Theme.text; font.pixelSize: 11; font.bold: true; font.letterSpacing: 0.7
                        Layout.preferredHeight: 26
                        verticalAlignment: Text.AlignVCenter
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 14
                        radius: 3
                        color: Theme.guitarSuite.tunerScale
                        // Reduction grows from the right, like a hardware GR meter; 24 dB full.
                        Rectangle {
                            objectName: "mixEffectMeterBar"
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: parent.width * Math.min(1, fx.reductionDb / 24)
                            radius: 3
                            color: Theme.accent
                        }
                    }
                    Text {
                        objectName: "mixEffectMeterText"
                        text: fx.reductionDb >= 0.1 ? "−" + fx.reductionDb.toFixed(1) + " dB" : "0.0 dB"
                        color: Theme.text
                        font.pixelSize: 18
                        font.family: Theme.monospaceFontFamily
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: root.active ? "How far the track is being turned down right now."
                                          : ""
                        color: Theme.textMuted; font.pixelSize: 10
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
