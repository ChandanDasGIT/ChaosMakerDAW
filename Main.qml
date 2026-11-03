import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: window
    width: 1440
    height: 900
    minimumWidth: 980
    minimumHeight: 640
    visible: true
    title: (EngineController.projectDirty ? "* " : "") + (EngineController.projectFile.toString().split("/").pop() || "Untitled") + " · ChaosMaker"
    property bool closeApproved: false
    color: Theme.windowBg
    palette.toolTipBase: Theme.tooltipBg
    palette.toolTipText: Theme.tooltipText

    TrackListWrapper { id: trackModel }

    onClosing: event => {
        if (!closeApproved && EngineController.projectDirty) {
            event.accepted = false
            topBar.requestProjectAction("Close")
        } else EngineController.shutdownSession()
    }

    Component.onCompleted: {
        EngineController.attachProject(trackModel)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TopBar { id: topBar; trackModel: trackModel; Layout.fillWidth: true; onCloseRequested: { window.closeApproved = true; window.close() } }

        RowLayout {
            enabled: !EngineController.projectLoading
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            TrackList {
                trackModel: trackModel
                laneHeight: timeline.laneHeight
                scrollOffset: timeline.verticalScrollPosition
                onVerticalScrollRequested: amount => timeline.scrollVertically(amount)
                Layout.preferredWidth: 220
                Layout.fillHeight: true
            }
            Timeline {
                id: timeline
                objectName: "projectTimeline"
                trackModel: trackModel
                bpm: EngineController.bpm
                timeSignature: EngineController.numerator
                timeSignatureDenominator: EngineController.denominator
                laneCount: trackModel.count
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }

        TransportBar {
            snapMode: timeline.snapMode
            onSnapModeEdited: value => timeline.snapMode = value
            Layout.fillWidth: true
            timeSignature: timeline.timeSignature
            timeSignatureDenominator: timeline.timeSignatureDenominator
            bpm: EngineController.bpm
            playing: timeline.playing
            recording: timeline.recording
            repeating: timeline.repeating
            onNumeratorEdited: value => EngineController.setMeter(value, EngineController.denominator)
            onDenominatorEdited: value => EngineController.setMeter(EngineController.numerator, value)
            onBpmEdited: value => EngineController.bpm = value
            onTransportAction: action => EngineController.transportAction(action)
        }
        MixerPanel { trackModel: trackModel; Layout.fillWidth: true; Layout.preferredHeight: 166 }
        StatusStrip { Layout.fillWidth: true; Layout.preferredHeight: Theme.stripHeight }
    }
}
