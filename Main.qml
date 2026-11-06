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

    property int activePianoRollTrackId: -1
    property int activePianoRollClipId: -1
    property string activePianoRollClipLabel: ""
    property real activePianoRollClipLength: 4.0
    property string bottomPanelMode: "mixer" // "mixer" or "pianoroll"
    readonly property string pianoRollPresentation: topBar.appSettings ? topBar.appSettings.pianoRollPresentation : "dock"

    function openPianoRoll(trackId, clipId, label, lengthBeats) {
        window.activePianoRollTrackId = trackId
        window.activePianoRollClipId = clipId
        window.activePianoRollClipLabel = label
        window.activePianoRollClipLength = lengthBeats

        if (window.pianoRollPresentation === "window") {
            floatingPianoRollWindow.show()
            floatingPianoRollWindow.raise()
            floatingPianoRollWindow.requestActivate()
            floatingPianoRollView.centerOnMiddleC()
            floatingPianoRollView.forceActiveFocus()
        } else {
            window.bottomPanelMode = "pianoroll"
            window.mixerVisible = true
            dockedPianoRoll.centerOnMiddleC()
            // Its editing keys (Delete, Ctrl+A, Esc) need keyboard focus.
            dockedPianoRoll.forceActiveFocus()
        }
    }

    property real trackListWidth: 230
    property bool mixerVisible: true
    property real mixerHeight: 180
    // The piano roll needs more room than the mixer, so it keeps its own dock height.
    property real pianoRollDockHeight: 380
    readonly property bool pianoRollDocked: window.bottomPanelMode === "pianoroll"

    Shortcut {
        sequence: "M"
        onActivated: {
            if (window.bottomPanelMode === "pianoroll") {
                window.bottomPanelMode = "mixer"
                window.mixerVisible = true
            } else {
                window.mixerVisible = !window.mixerVisible
            }
        }
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
            onToggleMixerRequested: {
                if (window.bottomPanelMode === "pianoroll") {
                    window.bottomPanelMode = "mixer"
                    window.mixerVisible = true
                } else {
                    window.mixerVisible = !window.mixerVisible
                }
            }
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
                onOpenPianoRollRequested: (trackId, clipId, label, lengthBeats) => {
                    window.openPianoRoll(trackId, clipId, label, lengthBeats)
                }
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
                    startHeight = window.pianoRollDocked ? window.pianoRollDockHeight : window.mixerHeight
                }
                onPositionChanged: mouse => {
                    if (pressed) {
                        let pt = mapToItem(window.contentItem, mouse.x, mouse.y)
                        let diff = startGlobalY - pt.y
                        if (window.pianoRollDocked) {
                            const minH = dockedPianoRoll.minimumDockHeight
                            const maxH = Math.max(minH, window.height - 220)
                            window.pianoRollDockHeight = Math.max(minH, Math.min(maxH, startHeight + diff))
                        } else {
                            window.mixerHeight = Math.max(120, Math.min(480, startHeight + diff))
                        }
                    }
                }
            }
        }

        Item {
            id: bottomWorkspace
            visible: window.mixerVisible
            Layout.fillWidth: true
            Layout.preferredHeight: !window.mixerVisible ? 0
                : (window.pianoRollDocked ? Math.max(dockedPianoRoll.minimumDockHeight, window.pianoRollDockHeight)
                                          : window.mixerHeight)
            clip: true

            MixerPanel {
                anchors.fill: parent
                trackModel: trackModel
                visible: window.bottomPanelMode === "mixer"
            }

            PianoRollView {
                id: dockedPianoRoll
                anchors.fill: parent
                trackModel: trackModel
                visible: window.bottomPanelMode === "pianoroll"
                trackId: window.activePianoRollTrackId
                clipId: window.activePianoRollClipId
                clipLabel: window.activePianoRollClipLabel
                clipLengthBeats: window.activePianoRollClipLength
                bpm: EngineController.bpm
                onCloseRequested: {
                    window.bottomPanelMode = "mixer"
                }
            }
        }

        StatusStrip { Layout.fillWidth: true; Layout.preferredHeight: Theme.stripHeight }
    }

    Window {
        id: floatingPianoRollWindow
        objectName: "floatingPianoRollWindow"
        width: 1040
        height: 640
        minimumWidth: 700
        minimumHeight: 450
        title: "Piano Roll · " + (window.activePianoRollClipLabel || "Clip") + " · ChaosMaker"
        color: Theme.windowBg
        visible: false

        PianoRollView {
            id: floatingPianoRollView
            width: floatingPianoRollWindow.width
            trackModel: trackModel
            height: floatingPianoRollWindow.height
            trackId: window.activePianoRollTrackId
            clipId: window.activePianoRollClipId
            clipLabel: window.activePianoRollClipLabel
            clipLengthBeats: window.activePianoRollClipLength
            bpm: EngineController.bpm
            onCloseRequested: floatingPianoRollWindow.close()
        }
    }
}
