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

    TenBandEqWrapper {
        id: eqBridge
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Text { text: root.trackName + " · EQUALIZER"; color: Theme.text; font.pixelSize: 14; font.bold: true; Layout.fillWidth: true }
            Text { text: eqBridge.bandDescriptors.length + " bands  ·  ±" + eqBridge.maxGain + " dB"; color: Theme.textMuted; font.pixelSize: 10 }
            PanelButton {
                text: "Reset"
                onClicked: root.trackModel.resetTrackEq(root.trackId)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            ThemedComboBox { id: eqFactory; Layout.fillWidth: true; model: root.trackModel ? root.trackModel.eqPresetNames : eqBridge.presetNames }
            PanelButton { text: "Apply preset"; onClicked: root.trackModel.applyEqPreset(root.trackId, eqFactory.currentIndex) }
            PanelButton { text: "Save custom…"; onClicked: eqSaveDialog.open() }
            PanelButton { text: "Load custom…"; onClicked: eqLoadDialog.open() }
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
                model: eqBridge.bandDescriptors
                delegate: ColumnLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 5
                    Text {
                        text: eqBridge.formatGain(Number(root.eqBands[index] || 0))
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
                        from: modelData.min; to: modelData.max; stepSize: modelData.stepSize
                        orientation: Qt.Vertical
                        value: Number(root.eqBands[index] || 0)
                        onMoved: root.trackModel.setEqBand(root.trackId, index, value)
                    }
                    Text { text: modelData.shortLabel; color: Theme.textMuted; font.pixelSize: 9; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
                }
            }
        }
        Text { text: "Frequency (Hz)"; color: Theme.textMuted; font.pixelSize: 9; Layout.alignment: Qt.AlignHCenter }
    }

    FileDialog {

        // Inside the main window: a separate dialog window keeps app focus after closing,

        // which left the app without keyboard input (plan item 1.11).

        popupType: Popup.Item
        id: eqSaveDialog
        title: "Save equalizer preset"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Equalizer presets (*.eqpreset)"]
        defaultSuffix: "eqpreset"
        onAccepted: root.trackModel.saveEqPreset(root.trackId, selectedFile)
    }
    FileDialog {
        // Inside the main window: a separate dialog window keeps app focus after closing,
        // which left the app without keyboard input (plan item 1.11).
        popupType: Popup.Item
        id: eqLoadDialog
        title: "Load equalizer preset"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Equalizer presets (*.eqpreset)"]
        onAccepted: root.trackModel.loadEqPreset(root.trackId, selectedFile)
    }
}
