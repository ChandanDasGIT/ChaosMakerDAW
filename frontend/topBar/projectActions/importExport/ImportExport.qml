import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Importing audio (File > Import audio and file drops) and the export request.
Item {
    id: root
    property var trackModel: null
    readonly property alias job: importJob
    signal exportRequested()

    function openImportDialog() {
        importDialog.open()
    }

    ImportExportWrap {
        id: importJob
        objectName: "importJob"
        trackModel: root.trackModel ?? null
        onFinished: importStatus.open()
        onBusyChanged: if (busy) importStatus.open()
    }

    FilePicker {
        id: importDialog
        objectName: "importAudioDialog"
        title: "Import audio"
        fileMode: FilePicker.OpenFiles
        acceptLabel: "Import"
        nameFilters: ["Audio files (*.wav *.WAV *.flac *.FLAC *.mp3 *.MP3)", "All files (*)"]
        onAccepted: importJob.importFiles(selectedFiles, EngineController.positionBeats, -1)
    }

    // What an import is doing, and what went wrong with which file.
    Popup {
        id: importStatus
        objectName: "importStatusPopup"
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: 56
        width: 420
        padding: 12
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: Theme.panelBg; border.color: Theme.divider; radius: Theme.panelRadius }
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                objectName: "importStatusText"
                Layout.fillWidth: true
                text: importJob.status
                color: Theme.text
                font.pixelSize: Theme.bodySize
                wrapMode: Text.WordWrap
            }
            ProgressBar {
                Layout.fillWidth: true
                visible: importJob.busy
                value: importJob.progress
            }
            Repeater {
                model: importJob.busy ? [] : importJob.problems
                delegate: Text {
                    required property string modelData
                    Layout.fillWidth: true
                    text: "• " + modelData
                    color: Theme.textMuted
                    font.pixelSize: Theme.smallSize
                    wrapMode: Text.WordWrap
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PanelButton {
                    objectName: "importCancelButton"
                    visible: importJob.busy
                    text: "Cancel"
                    onClicked: importJob.cancel()
                }
                PanelButton {
                    objectName: "importStatusClose"
                    visible: !importJob.busy
                    text: "Close"
                    onClicked: importStatus.close()
                }
            }
        }
    }

    Shortcut {
        sequence: Shortcuts.keys["file.import"]
        enabled: !EngineController.takeBusy && !EngineController.projectLoading && !importJob.busy
        onActivated: importDialog.open()
    }
    Shortcut {
        sequence: Shortcuts.keys["file.export"]
        enabled: !EngineController.takeBusy && !EngineController.projectLoading
        onActivated: root.exportRequested()
    }
}
