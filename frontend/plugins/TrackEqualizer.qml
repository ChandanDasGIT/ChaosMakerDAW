import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root
    property var trackModel
    property int trackId: -1
    property string trackName: ""
    property var eqBands: []
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Text { text: root.trackName + " · EQUALIZER"; color: Theme.text; font.pixelSize: 14; font.bold: true; Layout.fillWidth: true }
            Text { text: "10 bands  ·  ±12 dB"; color: Theme.textMuted; font.pixelSize: 10 }
            ToolButton {
                text: "Reset"
                onClicked: root.trackModel.resetTrackEq(root.trackId)
                contentItem: Text { text: "Reset"; color: parent.hovered ? Theme.text : Theme.textMuted; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: 4; color: parent.hovered ? "#383838" : "#303030"; border.color: "#424242" }
            }

        }
        RowLayout {
            Layout.fillWidth: true
            ComboBox { id: eqFactory; Layout.fillWidth: true; model: root.trackModel ? root.trackModel.eqPresetNames : [] }
            Button { text: "Apply preset"; onClicked: root.trackModel.applyEqPreset(root.trackId, eqFactory.currentIndex) }
            Button { text: "Save custom…"; onClicked: eqSaveDialog.open() }
            Button { text: "Load custom…"; onClicked: eqLoadDialog.open() }
        }
        Text {
            visible: text.length > 0
            text: root.trackModel ? root.trackModel.eqPresetError : ""
            color: Theme.record; font.pixelSize: 10
            Layout.fillWidth: true; wrapMode: Text.WordWrap
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            Repeater {
                model: ["31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"]
                delegate: ColumnLayout {
                    required property string modelData
                    required property int index
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 5
                    Text {
                        text: Number(root.eqBands[index] || 0).toFixed(1) + " dB"
                        color: Math.abs(Number(root.eqBands[index] || 0)) > 0.01 ? Theme.accent : Theme.textMuted
                        font.pixelSize: 9
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }
                    Slider {
                        objectName: "trackEqBand" + index
                        Layout.fillHeight: true
                        Layout.alignment: Qt.AlignHCenter
                        implicitWidth: 28
                        from: -12; to: 12; stepSize: 0.5
                        orientation: Qt.Vertical
                        value: Number(root.eqBands[index] || 0)
                        onMoved: root.trackModel.setEqBand(root.trackId, index, value)
                    }
                    Text { text: modelData; color: Theme.textMuted; font.pixelSize: 9; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
                }
            }
        }
        Text { text: "Frequency (Hz)"; color: Theme.textMuted; font.pixelSize: 9; Layout.alignment: Qt.AlignHCenter }
    }

    FileDialog {
        id: eqSaveDialog
        title: "Save equalizer preset"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Equalizer presets (*.eqpreset)"]
        defaultSuffix: "eqpreset"
        onAccepted: root.trackModel.saveEqPreset(root.trackId, selectedFile)
    }
    FileDialog {
        id: eqLoadDialog
        title: "Load equalizer preset"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Equalizer presets (*.eqpreset)"]
        onAccepted: root.trackModel.loadEqPreset(root.trackId, selectedFile)
    }
}
