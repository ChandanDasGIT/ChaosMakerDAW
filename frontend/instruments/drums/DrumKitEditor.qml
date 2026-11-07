import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The acoustic drum kit's page in the track's FX panel (like the harmonium's): the drawn kit
// (click a piece to play it; hits light up), room, part levels, tuning and the kit's
// character controls. Every change goes straight to the track
// through DrumKitWrapper; the kit rebuilds in the background.
Item {
    id: root
    property var trackModel
    property int trackId: -1
    property string trackName: ""

    function signedDb(value) {
        return (value > 0 ? "+" : "") + value.toFixed(1) + " dB"
    }
    function percent(value) {
        return Math.round(value * 100) + " %"
    }

    DrumKitWrapper {
        id: bridge
        objectName: "drumKitBridge"
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

    // A part level in dB, -24 .. +12.
    component LevelSlider: ValueSlider {
        from: bridge.minLevel
        to: bridge.maxLevel
        stepSize: 0.5
        valueText: root.signedDb(value)
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
                Text { text: "Drum kit"; color: Theme.text; font.pixelSize: 15; font.bold: true }
                Text {
                    text: "Acoustic kit: kick, snare, three toms and a hi-hat"
                    color: Theme.textMuted; font.pixelSize: 11
                    Layout.fillWidth: true; elide: Text.ElideRight
                }
            }
            ThemedComboBox {
                objectName: "drumKitRoom"
                implicitWidth: 190
                model: bridge.rooms
                textRole: "title"
                valueRole: "id"
                currentIndex: bridge.room === "close" ? 0 : bridge.room === "full" ? 2 : 1
                onActivated: index => bridge.room = bridge.rooms[index].id
                ToolTip.visible: hovered
                ToolTip.text: "Which microphones play: more room mics, a bigger space"
            }
            PanelButton {
                objectName: "drumKitReset"
                text: "Reset"
                tooltip: "Back to the default kit"
                onClicked: bridge.resetToDefaults()
            }
        }

        // Loading, or where the kit was looked for.
        Text {
            objectName: "drumKitStatus"
            visible: bridge.kitStatus.length > 0
            text: bridge.kitStatus
            color: bridge.kitMissing ? Theme.meterWarning : Theme.textMuted
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

                DrumKitPicture {
                    objectName: "drumKitPicture"
                    bridge: bridge
                    Layout.fillWidth: true
                    Layout.maximumWidth: 760
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredHeight: implicitHeight
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.width > 560 ? 2 : 1
                    columnSpacing: 24
                    rowSpacing: 10

                    SectionTitle { text: "LEVELS"; Layout.columnSpan: parent.columns }
                    LevelSlider {
                        objectName: "drumKitKick"
                        label: "Kick"
                        value: bridge.kick
                        onMoved: v => bridge.kick = v
                    }
                    LevelSlider {
                        objectName: "drumKitSnare"
                        label: "Snare and side stick"
                        value: bridge.snare
                        onMoved: v => bridge.snare = v
                    }
                    LevelSlider {
                        objectName: "drumKitHihat"
                        label: "Hi-hat"
                        value: bridge.hihat
                        onMoved: v => bridge.hihat = v
                    }
                    LevelSlider {
                        objectName: "drumKitToms"
                        label: "Toms"
                        value: bridge.toms
                        onMoved: v => bridge.toms = v
                    }
                    LevelSlider {
                        objectName: "drumKitVolume"
                        label: "Volume"
                        value: bridge.volume
                        tooltip: "The whole kit"
                        onMoved: v => bridge.volume = v
                    }
                    ValueSlider {
                        objectName: "drumKitTune"
                        label: "Tuning"
                        from: -bridge.maxTune
                        to: bridge.maxTune
                        stepSize: 0.5
                        value: bridge.tune
                        valueText: (value > 0 ? "+" : "") + value.toFixed(1) + " st"
                        tooltip: "The whole kit, in semitones"
                        onMoved: v => bridge.tune = v
                    }

                    SectionTitle { text: "CHARACTER"; Layout.columnSpan: parent.columns; Layout.topMargin: 6 }
                    ValueSlider {
                        objectName: "drumKitKickBeater"
                        label: "Kick beater"
                        value: bridge.kickBeater
                        valueText: root.percent(value)
                        tooltip: "The beater's click in the other mics"
                        onMoved: v => bridge.kickBeater = v
                    }
                    ValueSlider {
                        objectName: "drumKitSnareBody"
                        label: "Snare body"
                        value: bridge.snareBody
                        valueText: root.percent(value)
                        tooltip: "A fatter, lower snare"
                        onMoved: v => bridge.snareBody = v
                    }
                    ValueSlider {
                        objectName: "drumKitHihatTrash"
                        label: "Hi-hat trash"
                        value: bridge.hihatTrash
                        valueText: root.percent(value)
                        tooltip: "Trashier, higher hats"
                        onMoved: v => bridge.hihatTrash = v
                    }
                    ValueSlider {
                        objectName: "drumKitTomThud"
                        label: "Tom thud"
                        value: bridge.tomThud
                        valueText: root.percent(value)
                        tooltip: "An octave-down thud under the toms"
                        onMoved: v => bridge.tomThud = v
                    }
                    ValueSlider {
                        objectName: "drumKitMicTiming"
                        label: "Mic timing"
                        value: bridge.micTiming
                        valueText: root.percent(value)
                        tooltip: "Low: each mic arrives with its natural delay. High: the mics lined up tight."
                        onMoved: v => bridge.micTiming = v
                    }
                }
            }
        }
    }
}
