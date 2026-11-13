import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The grand piano's page in the track's FX panel (like the drum kits'): the drawn piano (click
// the keys to play, the pedal to hold; every note the track plays lights up), then its tone,
// pedal and level settings and the scale changer. Every change goes straight to the track
// through PianoWrapper; the piano rebuilds in the background.
Item {
    id: root
    property var trackModel
    property int trackId: -1
    property string trackName: ""

    function percent(value) {
        return Math.round(value * 100) + " %"
    }

    PianoWrapper {
        id: bridge
        objectName: "pianoBridge"
        trackModel: root.trackModel ?? null
        trackId: root.trackId
        engine: EngineController
        watchKeys: root.visible
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
        HardwareSlider {
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
                Text { text: "Grand piano"; color: Theme.text; font.pixelSize: Theme.headingSize; font.bold: true }
                Text {
                    text: "Concert grand, four dynamic layers, sustain pedal with string resonance"
                    color: Theme.textMuted; font.pixelSize: Theme.smallSize
                    Layout.fillWidth: true; elide: Text.ElideRight
                }
            }
            PanelButton {
                objectName: "pianoReset"
                text: "Reset"
                tooltip: "Back to the default piano"
                onClicked: bridge.resetToDefaults()
            }
        }

        // Loading, or where the piano was looked for.
        Text {
            objectName: "pianoStatus"
            visible: bridge.status.length > 0
            text: bridge.status
            color: bridge.missing ? Theme.meterWarning : Theme.textMuted
            font.pixelSize: Theme.captionSize
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
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

                PianoPicture {
                    objectName: "pianoPicture"
                    bridge: bridge
                    Layout.fillWidth: true
                    Layout.maximumWidth: 760
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredHeight: implicitHeight
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.width > 440 ? 2 : 1
                    columnSpacing: 24
                    rowSpacing: 10

                    SectionTitle { text: "TONE"; Layout.columnSpan: parent.columns }
                    ValueSlider {
                        objectName: "pianoBrightness"
                        label: "Brightness"
                        value: bridge.brightness
                        stepSize: 0.01
                        valueText: value >= 1 ? "As recorded" : root.percent(value)
                        tooltip: "Lower is darker and softer-edged"
                        onMoved: v => bridge.brightness = v
                    }
                    ValueSlider {
                        objectName: "pianoVelocity"
                        label: "Touch"
                        value: bridge.velocity
                        stepSize: 0.01
                        valueText: root.percent(value)
                        tooltip: "How much louder harder playing is (the tone changes either way)"
                        onMoved: v => bridge.velocity = v
                    }

                    SectionTitle { text: "PEDAL AND RELEASE"; Layout.columnSpan: parent.columns; Layout.topMargin: 6 }
                    ValueSlider {
                        objectName: "pianoResonance"
                        label: "String resonance"
                        value: bridge.resonance
                        stepSize: 0.01
                        valueText: root.percent(value)
                        tooltip: "Strings ringing along while the pedal is down"
                        onMoved: v => bridge.resonance = v
                    }
                    ValueSlider {
                        objectName: "pianoRelease"
                        label: "Release"
                        value: bridge.release
                        stepSize: 0.01
                        valueText: root.percent(value)
                        tooltip: "How long a note rings after its key is let go"
                        onMoved: v => bridge.release = v
                    }

                    SectionTitle { text: "LEVEL AND SCALE"; Layout.columnSpan: parent.columns; Layout.topMargin: 6 }
                    ValueSlider {
                        objectName: "pianoVolume"
                        label: "Volume"
                        from: bridge.minVolume
                        to: bridge.maxVolume
                        stepSize: 0.5
                        value: bridge.volume
                        valueText: (value > 0 ? "+" : "") + value.toFixed(1) + " dB"
                        onMoved: v => bridge.volume = v
                    }
                    ValueSlider {
                        objectName: "pianoTranspose"
                        label: "Scale changer"
                        from: -bridge.maxTranspose
                        to: bridge.maxTranspose
                        stepSize: 1
                        value: bridge.transpose
                        valueText: value === 0 ? "C4 plays C4"
                                 : "C4 plays " + bridge.noteName(60 + Math.round(value))
                                   + " (" + (value > 0 ? "+" : "") + Math.round(value) + ")"
                        tooltip: "Every key sounds this many semitones higher or lower"
                        onMoved: v => bridge.transpose = Math.round(v)
                    }
                }
            }
        }
    }
}
