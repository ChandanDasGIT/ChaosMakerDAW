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
    title: "ChaosMaker"
    color: Theme.windowBg
    palette.toolTipBase: Theme.tooltipBg
    palette.toolTipText: Theme.tooltipText

    TrackListModel { id: trackModel }

    Connections {
        target: trackModel
        function onCleanMonitorChanged(enabled) {
            EngineController.setCleanMonitorEnabled(enabled)
        }
        function onCleanMonitorInputChanged() {
            EngineController.setCleanMonitorInput(trackModel.cleanMonitorInput)
        }
        function onCleanMonitorGainChanged(gain) {
            EngineController.setCleanMonitorGain(gain)
        }
        function onCleanMonitorEqChanged() {
            EngineController.setCleanMonitorEqBands(trackModel.cleanMonitorEqBands)
        }
    }

    onClosing: EngineController.shutdownSession()

    Component.onCompleted: {
        EngineController.attachProject(trackModel)
        EngineController.setCleanMonitorEnabled(trackModel.cleanMonitorArmed)
        EngineController.setCleanMonitorInput(trackModel.cleanMonitorInput)
        EngineController.setCleanMonitorGain(trackModel.cleanMonitorGain)
        EngineController.setCleanMonitorEqBands(trackModel.cleanMonitorEqBands)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TopBar { Layout.fillWidth: true }

        RowLayout {
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
        MixerPanel { Layout.fillWidth: true; Layout.preferredHeight: 166 }
        StatusStrip { Layout.fillWidth: true; Layout.preferredHeight: Theme.stripHeight }
    }
}
