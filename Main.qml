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

    TrackListWrapper {
        id: trackModel
        // New tracks start in the recording mode chosen in Preferences (plan 6.6).
        newTrackRecordDry: window.layoutSettings.recordDryByDefault
    }

    // The project's tracks under a name no panel uses for its own property: inside a Loader's
    // component, "trackModel: trackModel" finds the panel's own (empty) property, not the id.
    readonly property TrackListWrapper projectTracks: trackModel
    property int activePianoRollTrackId: -1
    property int activePianoRollClipId: -1
    property string activePianoRollClipLabel: ""
    property real activePianoRollClipLength: 4.0
    property string bottomPanelMode: "mixer" // "mixer" or "pianoroll"
    readonly property string pianoRollPresentation: topBar.appSettings ? topBar.appSettings.pianoRollPresentation : "dock"
    // Plan 5.2: each piano roll is created the first time it is opened, and kept (with its tool,
    // zoom and grid settings) while it is the chosen presentation or still on screen.
    property bool dockedPianoRollUsed: false
    property bool floatingPianoRollUsed: false
    readonly property var dockedPianoRoll: dockedPianoRollLoader.item
    readonly property var floatingPianoRollView: floatingPianoRollLoader.item
    readonly property real pianoRollMinimumDockHeight: dockedPianoRoll ? dockedPianoRoll.minimumDockHeight : 0

    function openPianoRoll(trackId, clipId, label, lengthBeats) {
        window.activePianoRollTrackId = trackId
        window.activePianoRollClipId = clipId
        window.activePianoRollClipLabel = label
        window.activePianoRollClipLength = lengthBeats

        if (window.pianoRollPresentation === "window") {
            window.floatingPianoRollUsed = true
            floatingPianoRollWindow.show()
            floatingPianoRollWindow.raise()
            floatingPianoRollWindow.requestActivate()
            window.floatingPianoRollView.centerOnMiddleC()
            window.floatingPianoRollView.forceActiveFocus()
        } else {
            window.dockedPianoRollUsed = true
            window.bottomPanelMode = "pianoroll"
            window.mixerVisible = true
            window.dockedPianoRoll.centerOnMiddleC()
            // Its editing keys (Delete, Ctrl+A, Esc) need keyboard focus.
            window.dockedPianoRoll.forceActiveFocus()
        }
    }

    // Layout sizes start from the saved preferences (AppSettings); a splitter drag changes them
    // live and saves once on release.
    readonly property var layoutSettings: topBar.appSettings.bridge // AppSettingsWrapper
    property real trackListWidth: layoutSettings.trackListWidth
    property bool mixerVisible: true
    property real mixerHeight: layoutSettings.mixerHeight
    // The piano roll needs more room than the mixer, so it keeps its own dock height.
    property real pianoRollDockHeight: layoutSettings.pianoRollDockHeight
    readonly property bool pianoRollDocked: window.bottomPanelMode === "pianoroll"

    // Which bottom panel / piano roll is on screen (view state only).
    readonly property bool mixerShown: window.mixerVisible && window.bottomPanelMode === "mixer"
    readonly property bool pianoRollShown: (window.mixerVisible && window.bottomPanelMode === "pianoroll")
                                           || floatingPianoRollWindow.visible
    readonly property bool pianoRollAvailable: window.activePianoRollClipId >= 0

    // Mixer toggle (M key, View menu, top-bar button): from the docked piano roll it
    // switches back to the mixer, otherwise it shows/hides the bottom panel.
    function toggleMixer() {
        if (window.bottomPanelMode === "pianoroll") {
            window.bottomPanelMode = "mixer"
            window.mixerVisible = true
        } else {
            window.mixerVisible = !window.mixerVisible
        }
    }

    // Show the last opened piano roll clip again (docked or floating, as set in
    // Preferences), or hide the piano roll that is showing.
    function togglePianoRoll() {
        if (window.pianoRollShown) {
            if (floatingPianoRollWindow.visible) floatingPianoRollWindow.close()
            if (window.bottomPanelMode === "pianoroll") window.bottomPanelMode = "mixer"
        } else if (window.pianoRollAvailable) {
            window.openPianoRoll(window.activePianoRollTrackId, window.activePianoRollClipId,
                                 window.activePianoRollClipLabel, window.activePianoRollClipLength)
        }
    }

    Shortcut {
        sequence: Shortcuts.keys["view.mixer"]
        onActivated: window.toggleMixer()
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
            timeline: timeline
            mixerShown: window.mixerShown
            pianoRollShown: window.pianoRollShown
            pianoRollAvailable: window.pianoRollAvailable
            onToggleMixerRequested: window.toggleMixer()
            onTogglePianoRollRequested: window.togglePianoRoll()
            bpm: transportBar.bpm
            tapCount: transportBar.tapCount
            onTapTempoRequested: transportBar.tapTempo()
            onBpmEdited: value => EngineController.bpm = value
            Layout.fillWidth: true
            onCloseRequested: { window.closeApproved = true; window.close() }
            onExportRequested: exportDialog.open()
        }

        RowLayout {
            enabled: !EngineController.projectLoading
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            TrackList {
                trackModel: trackModel
                importJob: topBar.importJob
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
                color: trackSplitterHover.hovered || trackSplitterMouse.pressed ? Theme.accent : Theme.panelEdge
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
                            window.trackListWidth = window.layoutSettings.clampTrackListWidth(startWidth + diff)
                        }
                    }
                    onReleased: window.layoutSettings.trackListWidth = window.trackListWidth
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
                importJob: topBar.importJob
                snapPlayhead: window.layoutSettings.snapPlayhead
                snapSelection: window.layoutSettings.snapSelection
                ripple: window.layoutSettings.rippleEditing
                onRippleEdited: on => window.layoutSettings.rippleEditing = on
                Layout.fillWidth: true
                Layout.fillHeight: true
                onOpenPianoRollRequested: (trackId, clipId, label, lengthBeats) => {
                    window.openPianoRoll(trackId, clipId, label, lengthBeats)
                }
            }
        }

        TransportBar {
            id: transportBar
            trackModel: window.projectTracks
            snapMode: timeline.snapMode
            onSnapModeEdited: value => timeline.snapMode = value
            ripple: timeline.ripple
            onRippleEdited: on => timeline.rippleEdited(on)
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
            color: mixerSplitterHover.hovered || mixerSplitterMouse.pressed ? Theme.accent : Theme.panelEdge
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
                            const minH = window.pianoRollMinimumDockHeight
                            const maxH = Math.max(minH, window.height - 220)
                            window.pianoRollDockHeight = Math.max(minH, Math.min(maxH, startHeight + diff))
                        } else {
                            window.mixerHeight = window.layoutSettings.clampMixerHeight(startHeight + diff)
                        }
                    }
                }
                onReleased: {
                    if (window.pianoRollDocked) window.layoutSettings.pianoRollDockHeight = window.pianoRollDockHeight
                    else window.layoutSettings.mixerHeight = window.mixerHeight
                }
            }
        }

        Item {
            id: bottomWorkspace
            visible: window.mixerVisible
            Layout.fillWidth: true
            Layout.preferredHeight: !window.mixerVisible ? 0
                // (a saved dock height never pushes the timeline off a smaller window)
                : (window.pianoRollDocked ? Math.max(window.pianoRollMinimumDockHeight,
                                                     Math.min(window.height - 220, window.pianoRollDockHeight))
                                          : window.mixerHeight)
            clip: true

            MixerPanel {
                anchors.fill: parent
                trackModel: trackModel
                visible: window.bottomPanelMode === "mixer"
            }

            Loader {
                id: dockedPianoRollLoader
                objectName: "dockedPianoRollLoader"
                anchors.fill: parent
                visible: window.bottomPanelMode === "pianoroll"
                active: window.dockedPianoRollUsed
                        && (window.pianoRollPresentation === "dock" || window.bottomPanelMode === "pianoroll")
                sourceComponent: PianoRollView {
                    trackModel: window.projectTracks
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

        Loader {
            id: floatingPianoRollLoader
            objectName: "floatingPianoRollLoader"
            anchors.fill: parent
            active: window.floatingPianoRollUsed
                    && (window.pianoRollPresentation === "window" || floatingPianoRollWindow.visible)
            sourceComponent: PianoRollView {
                trackModel: window.projectTracks
                trackId: window.activePianoRollTrackId
                clipId: window.activePianoRollClipId
                clipLabel: window.activePianoRollClipLabel
                clipLengthBeats: window.activePianoRollClipLength
                bpm: EngineController.bpm
                onCloseRequested: floatingPianoRollWindow.close()
            }
        }
    }

    // File > Export (restMap F2); offers the timeline's time selection as a range.
    ExportDialog {
        id: exportDialog
        hasTimeSelection: timeline.hasTimeSelection
        selectionStartBeat: timeline.rangeStartBeat
        selectionEndBeat: timeline.rangeEndBeat
    }
}
