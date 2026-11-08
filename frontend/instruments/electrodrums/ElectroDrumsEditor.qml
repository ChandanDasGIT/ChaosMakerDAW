import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The electronic drum kit's page in the track's FX panel (like the acoustic kit's): the drawn
// drum machine (style buttons, volume knob, pads: press one to play it and pick its drum), then
// the picked drum's tune, decay and level, and the kit's volume. Every change goes straight to
// the track through ElectroDrumsWrapper.
Item {
    id: root
    property var trackModel
    property int trackId: -1
    property string trackName: ""
    property string selectedDrum: "kick"                    // the drum the knobs edit

    readonly property var knobs: bridge.drumSettings[selectedDrum] ?? ({tune: 0, decay: 1, level: 0})

    function signedDb(value) {
        return (value > 0 ? "+" : "") + value.toFixed(1) + " dB"
    }

    ElectroDrumsWrapper {
        id: bridge
        objectName: "electroDrumsBridge"
        trackModel: root.trackModel ?? null
        trackId: root.trackId
        engine: EngineController
        watchHits: root.visible
    }

    // A labelled slider showing its value.
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
                Text { text: "Electronic drums"; color: Theme.text; font.pixelSize: 15; font.bold: true }
                Text {
                    text: "Synthesised kit: 808, 909 or Studio sounds on the drum keys"
                    color: Theme.textMuted; font.pixelSize: 11
                    Layout.fillWidth: true; elide: Text.ElideRight
                }
            }
            PanelButton {
                objectName: "electroDrumsReset"
                text: "Reset"
                tooltip: "Back to the default kit"
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

                ElectroDrumsPicture {
                    objectName: "electroDrumsPicture"
                    bridge: bridge
                    selectedDrum: root.selectedDrum
                    onDrumChosen: drum => root.selectedDrum = drum
                    Layout.fillWidth: true
                    Layout.maximumWidth: 760
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredHeight: implicitHeight
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.width > 560 ? 3 : 1
                    columnSpacing: 24
                    rowSpacing: 10

                    SectionTitle {
                        objectName: "electroDrumsSelected"
                        text: bridge.drumTitle(root.selectedDrum).toUpperCase()
                        Layout.columnSpan: parent.columns
                    }
                    ValueSlider {
                        objectName: "electroDrumsTune"
                        label: "Tune"
                        from: bridge.minTune
                        to: bridge.maxTune
                        stepSize: 0.5
                        value: root.knobs.tune
                        valueText: (value > 0 ? "+" : "") + value.toFixed(1) + " st"
                        tooltip: "This drum's pitch, in semitones"
                        onMoved: v => bridge.setTune(root.selectedDrum, v)
                    }
                    ValueSlider {
                        objectName: "electroDrumsDecay"
                        label: "Decay"
                        from: bridge.minDecay
                        to: bridge.maxDecay
                        stepSize: 0.05
                        value: root.knobs.decay
                        valueText: value.toFixed(2) + "x"
                        tooltip: "How long this drum rings: shorter or longer than the style's own"
                        onMoved: v => bridge.setDecay(root.selectedDrum, v)
                    }
                    ValueSlider {
                        objectName: "electroDrumsLevel"
                        label: "Level"
                        from: bridge.minLevel
                        to: bridge.maxLevel
                        stepSize: 0.5
                        value: root.knobs.level
                        valueText: root.signedDb(value)
                        tooltip: "This drum's loudness in the kit"
                        onMoved: v => bridge.setLevel(root.selectedDrum, v)
                    }

                    SectionTitle { text: "KIT"; Layout.columnSpan: parent.columns; Layout.topMargin: 6 }
                    ValueSlider {
                        objectName: "electroDrumsVolume"
                        label: "Volume"
                        from: bridge.minLevel
                        to: bridge.maxLevel
                        stepSize: 0.5
                        value: bridge.volume
                        valueText: root.signedDb(value)
                        tooltip: "The whole kit (the knob on the machine)"
                        onMoved: v => bridge.volume = v
                    }
                }
            }
        }
    }
}
