import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Export the song (restMap F2): the range, the format (WAV 16 / 24 / 32-bit float, MP3 with a
// bitrate), how long the tail rings on after the end, normalising, and the file. While it runs
// the progress shows and Cancel stops it; live audio is paused meanwhile (ExportDialogWrap).
Popup {
    id: root
    objectName: "exportDialog"

    // The timeline's time selection, offered as a range when there is one.
    property bool hasTimeSelection: false
    property real selectionStartBeat: 0
    property real selectionEndBeat: 0

    readonly property var formats: [
        { key: "wav16", label: "WAV 16-bit (dithered)", ext: "wav" },
        { key: "wav24", label: "WAV 24-bit", ext: "wav" },
        { key: "wav32f", label: "WAV 32-bit float", ext: "wav" },
        { key: "mp3", label: "MP3", ext: "mp3" }
    ]
    readonly property var mp3Rates: [128, 160, 192, 256, 320]
    readonly property var format: formats[formatBox.currentIndex]
    property url file: ""
    property string message: ""
    property bool lastOk: false

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: 56
    width: 460
    padding: 14
    modal: true
    closePolicy: exporter.exporting ? Popup.NoAutoClose : Popup.CloseOnEscape
    background: Rectangle { color: Theme.panelBg; border.color: Theme.divider; radius: Theme.panelRadius }

    onAboutToShow: {
        message = ""
        if (file.toString().length === 0) file = exporter.defaultFile(EngineController.projectFile, format.ext)
        rangeBox.currentIndex = hasTimeSelection ? 1 : 0
    }
    onFormatChanged: if (file.toString().length > 0) file = exporter.withExtension(file, format.ext)

    function startExport() {
        message = ""
        const selection = rangeBox.currentIndex === 1 && hasTimeSelection
        const started = exporter.startExport(file,
                                             selection ? selectionStartBeat : 0,
                                             selection ? selectionEndBeat : 0,
                                             format.key,
                                             mp3Rates[bitrateBox.currentIndex],
                                             tailSlider.value,
                                             normaliseBox.currentIndex > 0,
                                             normaliseBox.currentIndex === 1 ? 0 : -1)
        if (!started) {
            lastOk = false
            message = exporter.cannotStartMessage(selection)
        }
    }

    ExportDialogWrap {
        id: exporter
        objectName: "exportDialogWrap"
        onExportFinished: (ok, text) => {
            root.lastOk = ok
            root.message = text
        }
    }

    FilePicker {
        id: fileDialog
        objectName: "exportFileDialog"
        title: "Export to"
        fileMode: FilePicker.SaveFile
        acceptLabel: "Choose"
        currentFolder: exporter.folder(EngineController.projectFile)
        selectedFile: root.file
        nameFilters: root.format.ext === "mp3" ? ["MP3 audio (*.mp3)"] : ["WAV audio (*.wav)"]
        onAccepted: root.file = exporter.withExtension(selectedFile, root.format.ext)
    }

    contentItem: ColumnLayout {
        spacing: 10

        Text {
            text: "Export"
            color: Theme.text
            font.pixelSize: Theme.bodySize
            font.bold: true
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 8
            enabled: !exporter.exporting

            Text { text: "Range"; color: Theme.textMuted; font.pixelSize: Theme.smallSize }
            ThemedComboBox {
                id: rangeBox
                objectName: "exportRange"
                Layout.fillWidth: true
                model: root.hasTimeSelection ? ["Whole song", "Time selection"] : ["Whole song"]
            }

            Text { text: "Format"; color: Theme.textMuted; font.pixelSize: Theme.smallSize }
            ThemedComboBox {
                id: formatBox
                objectName: "exportFormat"
                Layout.fillWidth: true
                model: root.formats.map(f => f.label)
            }

            Text { text: "Bitrate"; color: Theme.textMuted; font.pixelSize: Theme.smallSize; visible: bitrateBox.visible }
            ThemedComboBox {
                id: bitrateBox
                objectName: "exportBitrate"
                Layout.fillWidth: true
                visible: root.format.key === "mp3"
                model: root.mp3Rates.map(r => r + " kbps")
                currentIndex: root.mp3Rates.length - 1
            }

            Text { text: "Tail"; color: Theme.textMuted; font.pixelSize: Theme.smallSize }
            RowLayout {
                Layout.fillWidth: true
                ThemedSlider {
                    id: tailSlider
                    objectName: "exportTail"
                    Layout.fillWidth: true
                    from: 0; to: 10; stepSize: 0.5
                    value: 2
                }
                Text {
                    text: tailSlider.value.toFixed(1) + " s"
                    color: Theme.text
                    font.pixelSize: Theme.smallSize
                    Layout.preferredWidth: 40
                }
            }

            Text { text: "Normalise"; color: Theme.textMuted; font.pixelSize: Theme.smallSize }
            ThemedComboBox {
                id: normaliseBox
                objectName: "exportNormalise"
                Layout.fillWidth: true
                model: ["Off", "Peak to 0 dBFS", "Peak to -1 dBFS"]
            }

            Text { text: "File"; color: Theme.textMuted; font.pixelSize: Theme.smallSize }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    objectName: "exportFileLabel"
                    Layout.fillWidth: true
                    text: exporter.fileLabel(root.file)
                    color: Theme.text
                    font.pixelSize: Theme.smallSize
                    elide: Text.ElideMiddle
                }
                PanelButton {
                    objectName: "exportChooseFile"
                    text: "Choose…"
                    onClicked: fileDialog.open()
                }
            }
        }

        ProgressBar {
            objectName: "exportProgress"
            Layout.fillWidth: true
            visible: exporter.exporting
            value: exporter.exportProgress
        }

        Text {
            objectName: "exportMessage"
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.message
            color: root.lastOk ? Theme.text : Theme.textMuted
            font.pixelSize: Theme.smallSize
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PanelButton {
                objectName: "exportCancel"
                text: "Cancel"
                visible: exporter.exporting
                onClicked: exporter.cancelExport()
            }
            PanelButton {
                objectName: "exportClose"
                text: "Close"
                visible: !exporter.exporting
                onClicked: root.close()
            }
            PanelButton {
                objectName: "exportStart"
                primary: true
                text: "Export"
                visible: !exporter.exporting
                enabled: root.file.toString().length > 0
                onClicked: root.startExport()
            }
        }
    }
}
