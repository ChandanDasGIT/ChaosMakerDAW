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

    function requestProjectAction(action) {
        projectActions.requestAction(action)
    }

    Shortcut { sequences: [StandardKey.Undo]; enabled: !!root.trackModel && root.trackModel.canUndo; onActivated: root.trackModel.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: !!root.trackModel && root.trackModel.canRedo; onActivated: root.trackModel.redo() }

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
