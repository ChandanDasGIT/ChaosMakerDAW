import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The harmonium's page in the track's FX panel (like GuitarSuite for guitars): style, stops,
// bellows, tone, scale changer and drones. Every change goes straight to the track through
// HarmoniumWrapper.
Item {
    id: root
    property var trackModel
    property int trackId: -1
    property string trackName: ""

    readonly property bool indian: bridge.style === "indian"

    // MIDI key -> name, middle C (60) = C4.
    function noteName(key) {
        const names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
        return names[key % 12] + (Math.floor(key / 12) - 1)
    }
    function signed(value, unit) {
        return (value > 0 ? "+" : "") + value + unit
    }
    // Drone choices: off, then C2 .. C5.
    readonly property int droneLow: 36
    readonly property int droneHigh: 72
    readonly property var droneChoices: {
        const list = ["Off"]
        for (let key = droneLow; key <= droneHigh; ++key) list.push(noteName(key))
        return list
    }

    HarmoniumWrapper {
        id: bridge
        objectName: "harmoniumBridge"
        trackModel: root.trackModel ?? null
        trackId: root.trackId
    }

    // A labelled 0..1 (or from..to) slider showing its value.
    component ValueSlider: ColumnLayout {
        id: valueSlider
        property string label
        property string valueText
        property real from: 0
        property real to: 1
        property real value: 0
        property real stepSize: 0
        property string tooltip: ""
        signal moved(real value)
        Layout.fillWidth: true
        spacing: 2
        RowLayout {
            Layout.fillWidth: true
            Text { text: valueSlider.label; color: Theme.textMuted; font.pixelSize: Theme.captionSize; Layout.fillWidth: true }
            Text { text: valueSlider.valueText; color: Theme.text; font.pixelSize: Theme.smallSize }
        }
        Slider {
            id: slider
            Layout.fillWidth: true
            from: valueSlider.from
            to: valueSlider.to
            stepSize: valueSlider.stepSize
            value: valueSlider.value
            onMoved: valueSlider.moved(value)
            ToolTip.visible: hovered && valueSlider.tooltip.length > 0
            ToolTip.text: valueSlider.tooltip
            ToolTip.delay: 600
        }
    }

    component SectionTitle: Text {
        color: Theme.text
        font.pixelSize: Theme.smallSize
        font.bold: true
        font.letterSpacing: 0.6
    }

    component Stop: ToggleChip {
        implicitHeight: 30
        implicitWidth: Math.max(86, contentItem.implicitWidth + 18)
        textSize: Theme.smallSize
        cornerRadius: 6
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.bottomMargin: 4
            spacing: 10
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text { text: "Harmonium"; color: Theme.text; font.pixelSize: 15; font.bold: true }
                Text {
                    text: root.indian ? "Hand harmonium: bass, male and female reeds, drones"
                                      : "Reed organ: stops in feet, steady foot bellows"
                    color: Theme.textMuted; font.pixelSize: 11
                    Layout.fillWidth: true; elide: Text.ElideRight
                }
            }
            ThemedComboBox {
                objectName: "harmoniumStyle"
                implicitWidth: 190
                model: bridge.styles
                textRole: "title"
                valueRole: "id"
                currentIndex: root.indian ? 0 : 1
                onActivated: index => bridge.style = bridge.styles[index].id
                ToolTip.visible: hovered
                ToolTip.text: "Indian hand harmonium or Western reed organ"
            }
            PanelButton {
                objectName: "harmoniumReset"
                text: "Reset"
                tooltip: "Back to the default harmonium"
                onClicked: bridge.resetToDefaults()
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: body.implicitHeight + 32
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                id: body
                x: 16; y: 16
                width: parent.width - 32
                spacing: 16

                // ---- Stops
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    SectionTitle { text: "STOPS" }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: root.indian
                        Stop { objectName: "harmoniumBass"; text: "Bass"; active: bridge.bass; onClicked: bridge.bass = !active; tooltip: "Bass reeds, an octave down" }
                        Stop { objectName: "harmoniumMale"; text: "Male"; active: bridge.male; onClicked: bridge.male = !active; tooltip: "Male reeds, the main set" }
                        Stop { objectName: "harmoniumFemale"; text: "Female"; active: bridge.female; onClicked: bridge.female = !active; tooltip: "Female reeds, an octave up" }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: !root.indian
                        Stop { objectName: "harmoniumBourdon"; text: "Bourdon 16'"; active: bridge.bourdon16; onClicked: bridge.bourdon16 = !active; tooltip: "Deep, an octave down" }
                        Stop { objectName: "harmoniumDiapason"; text: "Diapason 8'"; active: bridge.diapason8; onClicked: bridge.diapason8 = !active; tooltip: "Full, round main stop" }
                        Stop { objectName: "harmoniumDulciana"; text: "Dulciana 8'"; active: bridge.dulciana8; onClicked: bridge.dulciana8 = !active; tooltip: "Soft main stop" }
                        Stop { objectName: "harmoniumPrincipal"; text: "Principal 4'"; active: bridge.principal4; onClicked: bridge.principal4 = !active; tooltip: "Bright, an octave up" }
                        Stop { objectName: "harmoniumVoxHumana"; text: "Vox Humana"; active: bridge.voxHumana; onClicked: bridge.voxHumana = !active; tooltip: "Tremolo" }
                    }
                }

                // ---- Bellows and tone, two columns
                GridLayout {
                    Layout.fillWidth: true
                    columns: root.width > 560 ? 2 : 1
                    columnSpacing: 24
                    rowSpacing: 10

                    SectionTitle { text: "BELLOWS"; Layout.columnSpan: parent.columns }
                    ValueSlider {
                        objectName: "harmoniumBellows"
                        label: "Air pressure"
                        valueText: Math.round(value * 100) + " %"
                        value: bridge.bellows
                        tooltip: "More air: louder, brighter, a little flat. MIDI CC 11 / CC 2 work it too."
                        onMoved: v => bridge.bellows = v
                    }
                    ValueSlider {
                        objectName: "harmoniumPumping"
                        label: "Pumping"
                        valueText: Math.round(value * 100) + " %"
                        value: bridge.pumping
                        tooltip: "How unevenly the bellows are worked: the swell of each stroke"
                        onMoved: v => bridge.pumping = v
                    }

                    SectionTitle { text: "TONE"; Layout.columnSpan: parent.columns; Layout.topMargin: 6 }
                    ValueSlider {
                        objectName: "harmoniumBeating"
                        label: "Beating"
                        valueText: value.toFixed(1) + " cents"
                        to: bridge.maxBeating
                        stepSize: 0.5
                        value: bridge.beating
                        tooltip: "How far apart the reed sets are tuned: the shimmer between them"
                        onMoved: v => bridge.beating = v
                    }
                    ValueSlider {
                        objectName: "harmoniumCase"
                        label: "Case"
                        valueText: Math.round(value * 100) + " %"
                        value: bridge.caseResonance
                        tooltip: "Wooden-box colour and the short reflections"
                        onMoved: v => bridge.caseResonance = v
                    }
                    ValueSlider {
                        objectName: "harmoniumAirNoise"
                        label: "Air noise"
                        valueText: Math.round(value * 100) + " %"
                        value: bridge.airNoise
                        onMoved: v => bridge.airNoise = v
                    }
                    ValueSlider {
                        objectName: "harmoniumKeyClick"
                        label: "Key click"
                        valueText: Math.round(value * 100) + " %"
                        value: bridge.keyClick
                        onMoved: v => bridge.keyClick = v
                    }
                    ValueSlider {
                        objectName: "harmoniumVolume"
                        label: "Volume"
                        valueText: (value > 0 ? "+" : "") + value.toFixed(1) + " dB"
                        from: bridge.minVolume
                        to: bridge.maxVolume
                        stepSize: 0.5
                        value: bridge.volume
                        onMoved: v => bridge.volume = v
                    }
                }

                // ---- Scale changer
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    SectionTitle { text: "SCALE CHANGER" }
                    RowLayout {
                        spacing: 8
                        PanelButton {
                            objectName: "harmoniumTransposeDown"
                            text: "−"
                            enabled: bridge.transpose > -bridge.maxTranspose
                            tooltip: "Down a semitone"
                            onClicked: bridge.transpose = bridge.transpose - 1
                        }
                        Text {
                            objectName: "harmoniumTranspose"
                            Layout.preferredWidth: 150
                            horizontalAlignment: Text.AlignHCenter
                            text: bridge.transpose === 0 ? "C plays C (in place)"
                                                         : "C plays " + root.noteName(60 + bridge.transpose).replace(/-?\d+$/, "")
                                                           + " (" + root.signed(bridge.transpose, "") + ")"
                            color: Theme.text
                            font.pixelSize: Theme.smallSize
                        }
                        PanelButton {
                            objectName: "harmoniumTransposeUp"
                            text: "+"
                            enabled: bridge.transpose < bridge.maxTranspose
                            tooltip: "Up a semitone"
                            onClicked: bridge.transpose = bridge.transpose + 1
                        }
                    }
                }

                // ---- Drones
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    SectionTitle { text: "DRONES" }
                    Text {
                        text: "Notes that keep sounding with the stops in use, e.g. Sa and Pa."
                        color: Theme.textMuted
                        font.pixelSize: Theme.captionSize
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: bridge.droneCount
                            ThemedComboBox {
                                required property int index
                                objectName: "harmoniumDrone" + index
                                implicitWidth: 96
                                model: root.droneChoices
                                readonly property int key: bridge.drones[index] ?? -1
                                currentIndex: key < root.droneLow || key > root.droneHigh ? 0 : key - root.droneLow + 1
                                onActivated: choice => bridge.setDrone(index, choice === 0 ? -1 : root.droneLow + choice - 1)
                                ToolTip.visible: hovered
                                ToolTip.text: "Drone " + (index + 1)
                            }
                        }
                    }
                }
            }
        }
    }
}
