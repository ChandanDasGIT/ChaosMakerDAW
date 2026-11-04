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

    property real trackListWidth: 230
    property bool mixerVisible: true
    property real mixerHeight: 180

    Shortcut {
        sequence: "M"
        onActivated: window.mixerVisible = !window.mixerVisible
    }

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

        TopBar {
            id: topBar
            trackModel: trackModel
            mixerVisible: window.mixerVisible
            onToggleMixerRequested: window.mixerVisible = !window.mixerVisible
            Layout.fillWidth: true
            onCloseRequested: { window.closeApproved = true; window.close() }
        }

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
                Layout.preferredWidth: window.trackListWidth
                Layout.fillHeight: true
            }
            Rectangle {
                id: trackSplitter
                Layout.fillHeight: true
                Layout.preferredWidth: 4
                color: trackSplitterHover.hovered || trackSplitterMouse.pressed ? Theme.accent : Theme.divider
                HoverHandler { id: trackSplitterHover; cursorShape: Qt.SplitHCursor }
                MouseArea {
                    id: trackSplitterMouse
                    anchors.fill: parent
                    anchors.margins: -2
                    cursorShape: Qt.SplitHCursor
                    property real startGlobalX: 0
                    property real startWidth: 0
                    onPressed: mouse => {
                        let pt = mapToItem(window.contentItem, mouse.x, mouse.y)
                        startGlobalX = pt.x
                        startWidth = window.trackListWidth
                    }
                    onPositionChanged: mouse => {
                        if (pressed) {
                            let pt = mapToItem(window.contentItem, mouse.x, mouse.y)
                            let diff = pt.x - startGlobalX
                            window.trackListWidth = Math.max(180, Math.min(460, startWidth + diff))
                        }
                    }
                }
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

        Rectangle {
            id: mixerSplitter
            visible: window.mixerVisible
            Layout.fillWidth: true
            Layout.preferredHeight: 4
            color: mixerSplitterHover.hovered || mixerSplitterMouse.pressed ? Theme.accent : Theme.divider
            HoverHandler { id: mixerSplitterHover; cursorShape: Qt.SplitVCursor }
            MouseArea {
                id: mixerSplitterMouse
                anchors.fill: parent
                anchors.margins: -2
                cursorShape: Qt.SplitVCursor
                property real startGlobalY: 0
                property real startHeight: 0
                onPressed: mouse => {
                    let pt = mapToItem(window.contentItem, mouse.x, mouse.y)
                    startGlobalY = pt.y
                    startHeight = window.mixerHeight
                }
                onPositionChanged: mouse => {
                    if (pressed) {
                        let pt = mapToItem(window.contentItem, mouse.x, mouse.y)
                        let diff = startGlobalY - pt.y
                        window.mixerHeight = Math.max(120, Math.min(480, startHeight + diff))
                    }
                }
            }
        }

        MixerPanel {
            trackModel: trackModel
            visible: window.mixerVisible
            Layout.fillWidth: true
            Layout.preferredHeight: window.mixerVisible ? window.mixerHeight : 0
        }
        StatusStrip { Layout.fillWidth: true; Layout.preferredHeight: Theme.stripHeight }
    }
}
