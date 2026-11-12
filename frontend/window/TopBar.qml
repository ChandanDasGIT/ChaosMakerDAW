import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../session"
import "../settings"

Rectangle {
    id: root
    property var trackModel: null
    property var timeline: null
    property bool mixerShown: true
    property bool pianoRollShown: false
    property bool pianoRollAvailable: false
    readonly property alias appSettings: appSettings
    readonly property var importJob: projectActions.importJob   // import / export (E2, F2)
    signal closeRequested()
    signal exportRequested()
    signal toggleMixerRequested()
    signal togglePianoRollRequested()
    // Options > Tap tempo: the window passes the tap on to the transport bar's BPM field.
    property real bpm: 120
    property int tapCount: 0
    signal tapTempoRequested()
    signal bpmEdited(real value)

    function requestProjectAction(action) {
        projectActions.requestAction(action)
    }

    Shortcut { sequence: Shortcuts.keys["edit.undo"]; enabled: !!root.trackModel && root.trackModel.canUndo; onActivated: root.trackModel.undo() }
    Shortcut { sequence: Shortcuts.keys["edit.redo"]; enabled: !!root.trackModel && root.trackModel.canRedo; onActivated: root.trackModel.redo() }
    Shortcut { sequence: Shortcuts.keys["edit.history"]; enabled: !!root.trackModel; onActivated: editMenu.openUndoHistory() }

    height: 48
    color: Theme.panelBg
    border.color: Theme.divider
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 6

        ProjectActions {
            id: projectActions
            trackModel: root.trackModel
            onCloseRequested: root.closeRequested()
            onExportRequested: root.exportRequested()
        }

        EditMenu {
            id: editMenu
            trackModel: root.trackModel
            timeline: root.timeline
        }

        ViewMenu {
            id: viewMenu
            mixerShown: root.mixerShown
            pianoRollShown: root.pianoRollShown
            pianoRollAvailable: root.pianoRollAvailable
            onToggleMixerRequested: root.toggleMixerRequested()
            onTogglePianoRollRequested: root.togglePianoRollRequested()
        }

        OptionsMenu {
            id: optionsMenu
            bpm: root.bpm
            tapCount: root.tapCount
            onTapTempoRequested: root.tapTempoRequested()
            onBpmEdited: value => root.bpmEdited(value)
        }
        AppSettings {
            id: appSettings
        }
        PanelTabs {
            id: panelTabs
            mixerShown: root.mixerShown
            pianoRollShown: root.pianoRollShown
            pianoRollAvailable: root.pianoRollAvailable
            onToggleMixerRequested: root.toggleMixerRequested()
            onTogglePianoRollRequested: root.togglePianoRollRequested()
        }
        Item { Layout.fillWidth: true }
    }
}
