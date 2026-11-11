import QtQuick
import QtQuick.Controls

Item {
    id: root
    property var transport: EngineController
    property int timeSignature: 4
    property int timeSignatureDenominator: 4
    property real bpm: 120
    property alias snapMode: editor.snapMode // Backend-owned bars/beats/subdivision/off choice.
    // The magnets for the playhead and the time selection (Preferences > Timeline; off by default).
    property alias snapPlayhead: editor.snapPlayhead
    property alias snapSelection: editor.snapSelection
    TimelineWrapper {
        id: editor
        trackModel: root.trackModel ?? null
        numerator: root.timeSignature
        denominator: root.timeSignatureDenominator
        bpm: root.bpm
        takeBusy: root.transport.takeBusy
        projectLoading: root.transport.projectLoading
        recording: root.recording
        onSeekRequested: beat => root.transport.seekBeat(beat)
        onRepeatRangeRequested: (start, end) => editor.acceptRepeatRange(root.transport.setRepeatRange(start, end))
    }
    readonly property real snapStepBeats: editor.snapStepBeats
    // Pixel geometry and scroll/zoom state are presentation concerns.
    property real pixelsPerSecond: 96
    readonly property real beatWidth: pixelsPerSecond * editor.secondsPerBeat
    readonly property real playhead: root.transport.positionBeats * beatWidth
    readonly property bool playing: root.transport.playing
    readonly property bool recording: root.transport.recording
    readonly property bool repeating: root.transport.repeating
    readonly property real recordingTailX: playhead + width * 0.2
    property bool followPlayhead: true
    property real lastTransportPlayhead: 0
    onPlayingChanged: { if (playing) followPlayhead = true }
    readonly property var selectedClipIds: editor.selectedClipIds
    readonly property int selectedClipId: editor.selectedClipId
    readonly property int selectedTrackId: editor.selectedTrackId
    readonly property var keyboardFocusItem: Window.window ? Window.window.activeFocusItem : null
    readonly property bool editingText: keyboardFocusItem instanceof TextInput || keyboardFocusItem instanceof TextEdit
    readonly property bool clipKeyboardActive: activeFocus && visible && enabled && !editingText && editor.keyboardAllowed
    readonly property bool canEditClipSelection: clipKeyboardActive && editor.canEditSelection
    readonly property bool canCopy: clipKeyboardActive && editor.canCopy
    readonly property bool canCut: clipKeyboardActive && editor.canCut
    readonly property bool canPaste: clipKeyboardActive && editor.canPaste
    readonly property bool canDuplicate: clipKeyboardActive && editor.canDuplicate
    readonly property string editOperation: editor.editOperation
    readonly property real editDelta: editor.editDelta
    readonly property int editLaneDelta: editor.editLaneDelta
    readonly property bool canMergeSelection: editor.canMergeSelection
    readonly property bool selecting: editor.selecting
    readonly property bool hasSelection: editor.hasSelection
    readonly property bool clipSelectionDrag: editor.clipSelectionDrag
    property alias timeRangeStartBeat: editor.timeRangeStartBeat
    property alias timeRangeEndBeat: editor.timeRangeEndBeat
    readonly property real rangeStartBeat: editor.rangeStartBeat
    readonly property real rangeEndBeat: editor.rangeEndBeat

    signal openPianoRollRequested(int trackId, int clipId, string label, real lengthBeats)
    // Audio files dropped here (restMap E2): onto the track under the drop, at the drop's beat;
    // below the tracks, a new track per file. Needs the app's import job.
    property var importJob: null

    // Clip edit actions, shared by the keyboard shortcuts below and the Edit menu.
    // The menu does not need keyboard focus on the timeline; the backend still decides
    // whether each action is allowed (not while recording, busy, dragging or loading).
    readonly property bool editActionsAvailable: visible && enabled && !!root.trackModel
    readonly property bool menuCanCut: editActionsAvailable && editor.canCut
    readonly property bool menuCanCopy: editActionsAvailable && editor.canCopy
    readonly property bool menuCanPaste: editActionsAvailable && editor.canPaste
    readonly property bool menuCanDuplicate: editActionsAvailable && editor.canDuplicate
    readonly property bool menuCanDelete: editActionsAvailable && editor.canEditSelection
    readonly property bool menuCanSelectAll: editActionsAvailable && editor.keyboardAllowed
    function cutSelection() { editor.cutSelectedClips() }
    function copySelection() { editor.copySelectedClips() }
    // Paste at the playhead, on the selected track.
    function pasteAtPlayhead() { editor.pasteClips(root.transport.positionBeats, editor.selectedTrackId) }
    function duplicateSelection() { editor.duplicateSelectedClips() }
    function deleteSelection() { root.trackModel.editClips(root.selectedClipIds, "delete", 0) }
    function selectAllClips() { editor.selectAllClips() }
    // Clip effects (restMap R2). With a time selection, the clip of `trackId` under it is split
    // at the selection's edges and the piece inside gets its effect window.
    readonly property bool hasTimeSelection: rangeEndBeat > rangeStartBeat
    readonly property bool menuCanEffectsOnSelection: editActionsAvailable && !recording && hasTimeSelection && selectedTrackId >= 0
    function effectsOnSelection(trackId) {
        if (!root.trackModel || !root.hasTimeSelection) return
        const clip = root.trackModel.splitForRangeEffects(trackId >= 0 ? trackId : root.selectedTrackId,
                                                          root.rangeStartBeat, root.rangeEndBeat)
        if (clip >= 0) root.openClipEffects(clip)
    }
    function openClipEffects(clipId) {
        if (!root.trackModel || !root.trackModel.canHaveClipEffects(clipId)) return
        clipEffectsWindow.clipId = clipId
        clipEffectsWindow.open()
    }

    Shortcut {
        sequence: "Delete"
        enabled: root.canEditClipSelection
        autoRepeat: false
        onActivated: root.deleteSelection()
    }
    Shortcut {
        sequences: [StandardKey.Cut]
        enabled: root.canCut
        autoRepeat: false
        onActivated: root.cutSelection()
    }
    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: root.canCopy
        autoRepeat: false
        onActivated: root.copySelection()
    }
    Shortcut {
        sequences: [StandardKey.Paste]
        enabled: root.canPaste
        autoRepeat: false
        onActivated: root.pasteAtPlayhead()
    }
    Shortcut {
        sequence: "Ctrl+D"
        enabled: root.canDuplicate
        autoRepeat: false
        onActivated: root.duplicateSelection()
    }
    Shortcut {
        sequence: "Left"
        enabled: root.canEditClipSelection
        autoRepeat: true
        onActivated: editor.nudgeClips(-1)
    }
    Shortcut {
        sequence: "Right"
        enabled: root.canEditClipSelection
        autoRepeat: true
        onActivated: editor.nudgeClips(1)
    }
    Shortcut {
        sequences: [StandardKey.SelectAll]
        enabled: root.clipKeyboardActive
        autoRepeat: false
        onActivated: root.selectAllClips()
    }
    Shortcut {
        sequence: "Escape"
        enabled: root.clipKeyboardActive && root.selectedClipIds.length > 0
        autoRepeat: false
        onActivated: editor.deselectClips()
    }
    function beginSelection(x, y, clips) {
        forceActiveFocus()
        selectionStartX = x; selectionEndX = x
        selectionStartY = y; selectionEndY = y
        editor.beginSelection(x / beatWidth, rowCoordinate(y), clips)
    }
    function updateSelection(x, y) {
        selectionEndX = x; selectionEndY = y
        editor.updateSelection(x / beatWidth, rowCoordinate(y), Math.abs(x - selectionStartX) > 2)
    }
    function finishSelection() {
        editor.finishSelection(8 / laneHeight, 10 / laneHeight, 1 / laneHeight)
    }
    property real selectionStartX: 0
    property real selectionEndX: 0
    property real selectionStartY: 0
    property real selectionEndY: 0
    property var trackModel: null
    property int laneCount: 5
    property int laneHeight: 106
    property int envelopeLaneHeight: 72   // a volume lane under its track (restMap G6)
    property int rulerHeight: 38
    // Rows can be taller than laneHeight when a volume lane is shown under the track; the
    // backend's row layout (TrackLayout via the track model) says where every row is.
    Binding { target: root.trackModel ? root.trackModel : null; property: "trackRowHeight"; value: root.laneHeight; when: !!root.trackModel }
    Binding { target: root.trackModel ? root.trackModel : null; property: "envelopeLaneHeight"; value: root.envelopeLaneHeight; when: !!root.trackModel }
    function rowTop(row) {
        const tops = root.trackModel ? root.trackModel.rowTops : []
        if (row < 0 || tops.length === 0) return row * root.laneHeight
        if (row < tops.length) return tops[row]
        return tops[tops.length - 1] + (row - tops.length + 1) * root.laneHeight
    }
    function rowHeight(row) { return rowTop(row + 1) - rowTop(row) }
    function rowAt(y) { return root.trackModel ? root.trackModel.rowAtY(y) : Math.floor(y / root.laneHeight) }
    function rowCoordinate(y) { return root.trackModel ? root.trackModel.rowCoordinate(y) : y / root.laneHeight }
    readonly property real rowsHeight: root.trackModel ? root.trackModel.layoutHeight : root.laneCount * root.laneHeight
    readonly property real verticalScrollPosition: scroll.contentY
    readonly property real beatUnitWidth: beatWidth * editor.beatUnit
    readonly property real barWidth: beatWidth * editor.beatsPerBar
    readonly property real contentEndBars: editor.contentEndBars
    readonly property real contentWidth: Math.max(width, (Math.ceil(contentEndBars) + 1) * barWidth, recordingTailX)
    // Which bar labels and grid lines to draw: backend rules (MusicalGrid via TimelineWrapper).
    readonly property real barLabelStride: editor.lineStride(barWidth, 48)
    readonly property real firstVisibleBar: editor.firstVisibleLine(scroll.contentX, barWidth, barLabelStride)
    readonly property int visibleBarCount: editor.visibleLineCount(width, barWidth, barLabelStride)
    readonly property real gridStepBeats: editor.gridStepBeats
    readonly property real gridUnitWidth: gridStepBeats * beatWidth
    readonly property real beatGridStride: editor.lineStride(gridUnitWidth, 8)
    readonly property real firstVisibleBeat: editor.firstVisibleLine(scroll.contentX, gridUnitWidth, beatGridStride)
    readonly property int visibleBeatCount: editor.visibleLineCount(width, gridUnitWidth, beatGridStride)

    function scrollVertically(amount) {
        scroll.contentY = Math.max(0, Math.min(scroll.contentHeight - scroll.height, scroll.contentY + amount))
    }

    function scrollHorizontally(amount) {
        followPlayhead = false
        scroll.contentX = Math.max(0, Math.min(scroll.contentWidth - scroll.width, scroll.contentX + amount))
    }

    function zoomAtCursor(factor, cursorX) {
        followPlayhead = false
        const anchor = scroll.contentX + cursorX
        const nextPixelsPerSecond = Math.max(0.000001, pixelsPerSecond * factor)
        const scale = nextPixelsPerSecond / pixelsPerSecond
        pixelsPerSecond = nextPixelsPerSecond
        scroll.contentX = Math.max(0, Math.min(scroll.contentWidth - scroll.width, anchor * scale - cursorX))
    }

    Shortcut {
        sequence: "S"
        enabled: root.activeFocus && editor.canSplitSelection
        onActivated: root.trackModel.splitClipAtBeat(root.selectedTrackId, root.selectedClipId, root.transport.positionBeats)
    }
    ThemedMenu {
        id: clipContextMenu
        parent: root
        popupType: Popup.Item
        width: 160
        property int targetTrackId: -1
        property int targetClipId: -1
        property real targetStart: 0
        property real targetLength: 0
        property bool targetIsRecorded: false
        property string targetLabel: ""

        ThemedMenuItem {
            id: openPianoRollAction
            objectName: "openPianoRollAction"
            text: "Open Piano Roll"
            visible: !clipContextMenu.targetIsRecorded
            enabled: visible
            implicitHeight: visible ? 30 : 0
            onTriggered: {
                root.openPianoRollRequested(clipContextMenu.targetTrackId, clipContextMenu.targetClipId,
                    clipContextMenu.targetLabel, clipContextMenu.targetLength)
            }
        }

        ThemedMenuSeparator {
            visible: openPianoRollAction.visible
        }

        ThemedMenuItem {
            id: cutClipAction
            text: "Cut"
            enabled: editor.canCut
            onTriggered: editor.cutSelectedClips()
        }
        ThemedMenuItem {
            id: copyClipAction
            text: "Copy"
            enabled: editor.canCopy
            onTriggered: editor.copySelectedClips()
        }
        ThemedMenuItem {
            id: pasteClipAction
            text: "Paste"
            enabled: editor.canPaste
            onTriggered: editor.pasteClips(root.transport.positionBeats, clipContextMenu.targetTrackId >= 0 ? clipContextMenu.targetTrackId : editor.selectedTrackId)
        }
        ThemedMenuItem {
            id: duplicateClipAction
            text: "Duplicate"
            enabled: editor.canDuplicate
            onTriggered: editor.duplicateSelectedClips()
        }
        ThemedMenuItem {
            id: splitClipAction
            text: "Split at playhead"
            enabled: editor.canEditSelection && editor.canSplit(clipContextMenu.targetTrackId, clipContextMenu.targetClipId, root.transport.positionBeats)
            onTriggered: root.trackModel.splitClipAtBeat(clipContextMenu.targetTrackId, clipContextMenu.targetClipId, root.transport.positionBeats)
        }
        ThemedMenuItem {
            objectName: "mergeSelectedClips"
            text: "Merge selected clips"
            enabled: root.canMergeSelection
            onTriggered: root.trackModel.mergeSelectedClips(root.selectedClipIds)
        }
        ThemedMenuSeparator {}
        // Clip effects (restMap R2).
        ThemedMenuItem {
            objectName: "clipEffectsAction"
            text: "Clip effects…"
            enabled: !!root.trackModel && !root.recording && root.trackModel.canHaveClipEffects(clipContextMenu.targetClipId)
            onTriggered: root.openClipEffects(clipContextMenu.targetClipId)
        }
        ThemedMenuItem {
            objectName: "effectsOnSelectionAction"
            text: "Effects on selection…"
            enabled: !!root.trackModel && !root.recording && root.hasTimeSelection
                     && root.trackModel.canHaveClipEffects(clipContextMenu.targetClipId)
            onTriggered: root.effectsOnSelection(clipContextMenu.targetTrackId)
        }
        ThemedMenuSeparator {}
        // Clip tools (restMap G14), on every selected audio clip; one undo step each.
        ThemedMenuItem {
            objectName: "clipReverseAction"
            text: "Reverse"
            enabled: !!root.trackModel && !root.recording && root.trackModel.canApplyClipTool(root.selectedClipIds)
            onTriggered: root.trackModel.applyClipTool(root.selectedClipIds, "reverse")
        }
        ThemedMenuItem {
            objectName: "clipNormaliseAction"
            text: "Normalise (peak to 0 dB)"
            enabled: !!root.trackModel && !root.recording && root.trackModel.canApplyClipTool(root.selectedClipIds)
            onTriggered: root.trackModel.applyClipTool(root.selectedClipIds, "normalise")
        }
        ThemedMenuItem {
            objectName: "clipPolarityAction"
            text: "Invert polarity"
            enabled: !!root.trackModel && !root.recording && root.trackModel.canApplyClipTool(root.selectedClipIds)
            onTriggered: root.trackModel.applyClipTool(root.selectedClipIds, "invertPolarity")
        }
        ThemedMenuSeparator {}
        ThemedMenuItem {
            id: deleteClipAction
            text: root.selectedClipIds.length > 1 ? "Delete selected clips" : "Delete"
            enabled: editor.canEditSelection
            onTriggered: {
                if (root.trackModel)
                    root.trackModel.editClips(root.selectedClipIds, "delete", 0)
            }
        }
    }

    // A clip's own effects (restMap R2): the same window as a track's, for the clip's effects id.
    TrackEffectsPanel {
        id: clipEffectsWindow
        objectName: "clipEffectsPanel"
        property int clipId: -1
        readonly property var info: clipId >= 0 && root.trackModel
                                    ? (root.trackModel.clips, root.trackModel.clipEffectsInfo(clipId)) : ({})
        onInfoChanged: if (visible && info.effectsId === undefined) close()   // the clip went away
        trackModel: root.trackModel
        trackId: info.effectsId ?? -1
        trackName: (info.name ?? "") + " (clip)"
        trackType: "Clip"
        eqBands: info.eqBands ?? []
        effects: info.effects ?? []
        hasLiveInput: false
        recordDry: false
    }

    // The volume envelope's right-click menu (restMap G6).
    ThemedMenu {
        id: envelopeMenu
        objectName: "envelopeMenu"
        parent: root
        popupType: Popup.Item
        width: 180
        property var lane: null
        ThemedMenuItem {
            objectName: "envelopePlacementAction"
            text: envelopeMenu.lane && envelopeMenu.lane.overClips ? "Show in own lane" : "Show over clips"
            onTriggered: root.trackModel.setEnvelopeOverClips(envelopeMenu.lane.envelopeTrackId, !envelopeMenu.lane.overClips)
        }
        ThemedMenuItem {
            objectName: "envelopeDeletePointAction"
            text: "Delete point"
            enabled: !!envelopeMenu.lane && envelopeMenu.lane.selectedPoint >= 0
            onTriggered: {
                const lane = envelopeMenu.lane, point = lane.selectedPoint
                lane.selectedPoint = -1
                root.trackModel.removeEnvelopePoint(lane.envelopeTrackId, point)
            }
        }
        ThemedMenuItem {
            objectName: "envelopeBypassAction"
            text: "Bypass"
            checkable: true
            checked: !!envelopeMenu.lane && envelopeMenu.lane.bypassed
            onTriggered: {
                root.trackModel.setEnvelopeBypassed(envelopeMenu.lane.envelopeTrackId, !envelopeMenu.lane.bypassed)
                checked = Qt.binding(() => !!envelopeMenu.lane && envelopeMenu.lane.bypassed)
            }
        }
    }
    // Shift+right-click on an envelope point: the curve from it to the next point (restMap G6b).
    // The last point has no curve after it, so its shapes are greyed out.
    ThemedMenu {
        id: envelopePointMenu
        objectName: "envelopePointMenu"
        parent: root
        popupType: Popup.Item
        width: 160
        property var lane: null
        property int point: -1
        readonly property var pointData: lane && point >= 0 && point < lane.points.length ? lane.points[point] : null
        readonly property bool hasCurve: !!lane && point >= 0 && point < lane.points.length - 1
        readonly property var shapes: root.trackModel ? root.trackModel.envelopeShapes : []
        function choose(i) {
            if (lane && i < shapes.length) root.trackModel.setEnvelopePointShape(lane.envelopeTrackId, point, shapes[i].id)
        }
        // One entry per shape, from the backend's list, in front of the separator.
        Instantiator {
            model: envelopePointMenu.shapes
            delegate: ThemedMenuItem {
                required property var modelData
                required property int index
                function current() { return !!envelopePointMenu.pointData && envelopePointMenu.pointData.shape === modelData.id }
                objectName: "envelopeShape_" + modelData.id
                text: modelData.name
                checkable: true
                enabled: envelopePointMenu.hasCurve
                checked: current()
                // A click toggles "checked" itself: put the binding back after choosing.
                onTriggered: { envelopePointMenu.choose(index); checked = Qt.binding(current) }
            }
            onObjectAdded: (index, object) => envelopePointMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => envelopePointMenu.removeItem(object)
        }
        ThemedMenuSeparator {}
        ThemedMenuItem {
            objectName: "envelopePointDeleteAction"
            text: "Delete point"
            onTriggered: {
                const lane = envelopePointMenu.lane, point = envelopePointMenu.point
                if (!lane) return
                lane.selectedPoint = -1
                root.trackModel.removeEnvelopePoint(lane.envelopeTrackId, point)
            }
        }
    }
    // V shows / hides the selected track's volume envelope.
    Shortcut {
        sequence: "V"
        enabled: root.visible && !root.editingText && !!root.trackModel && root.trackModel.selectedTrackId >= 0
        autoRepeat: false
        onActivated: root.trackModel.toggleEnvelopeShown(root.trackModel.selectedTrackId)
    }

    Connections {
        target: root.transport
        function onTransportChanged() {
            const returnedToStart = root.playhead === 0 && root.lastTransportPlayhead > 0
            if (returnedToStart && (!root.playing || root.followPlayhead)) {
                scroll.contentX = 0
                if (!root.playing) root.followPlayhead = true
            } else if (root.followPlayhead && root.playing && root.playhead > scroll.contentX + scroll.width * 0.82)
                scroll.contentX = Math.max(0, Math.min(root.playhead - scroll.width * 0.82,
                                                     scroll.contentWidth - scroll.width))
            root.lastTransportPlayhead = root.playhead
        }
    }

    Rectangle { anchors.fill: parent; color: Theme.windowBg }
    // Same wheel rules as the rest of the app: wheel = scroll the tracks, Shift = scroll
    // in time, Ctrl = zoom at the cursor. Vertical steps match the track list.
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        blocking: true
        target: null
        onWheel: event => {
            const pixels = event.pixelDelta.y !== 0
            const delta = pixels ? event.pixelDelta.y : event.angleDelta.y
            if (event.modifiers & Qt.ControlModifier)
                root.zoomAtCursor(Math.pow(1.12, (pixels ? event.angleDelta.y : delta) / 120), point.position.x)
            else if (event.modifiers & Qt.ShiftModifier) root.scrollHorizontally(-delta)
            else root.scrollVertically(-(pixels ? delta : delta * 0.75))
        }
    }
    WheelHandler {
        orientation: Qt.Horizontal
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        blocking: true
        target: null
        onWheel: event => root.scrollHorizontally(-(event.pixelDelta.x !== 0 ? event.pixelDelta.x : event.angleDelta.x))
    }

    Flickable {
        id: scroll
        objectName: "timelineScroll"
        anchors.fill: parent
        clip: true
        contentWidth: root.contentWidth
        contentHeight: Math.max(height, root.rulerHeight + root.rowsHeight)
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick
        interactive: false

        Rectangle { x: 0; y: 0; width: root.contentWidth; height: root.rulerHeight; color: Theme.timeline.ruler }
        Repeater {
            model: root.visibleBarCount
            delegate: Item {
                x: (root.firstVisibleBar + index * root.barLabelStride) * root.barWidth
                y: 0
                width: root.barWidth
                height: scroll.contentHeight
                Rectangle { width: 1; height: parent.height; color: Theme.timeline.rulerBarLine }
                Text {
                    x: 8; y: 11
                    text: editor.barLabel(root.firstVisibleBar + index * root.barLabelStride)
                    color: Theme.textMuted
                    font.pixelSize: 11
                }
            }
        }

        Repeater {
            model: root.laneCount
            delegate: Rectangle {
                readonly property bool isSelected: !!root.trackModel && root.trackModel.indexOfTrack(root.selectedTrackId) === index
                x: 0
                y: root.rulerHeight + root.rowTop(index)
                width: root.contentWidth
                height: root.rowHeight(index)
                color: isSelected ? Theme.timeline.laneSelected : (index % 2 ? Theme.timeline.laneAlt : Theme.timeline.lane)
                border.color: isSelected ? Theme.timeline.laneSelectedBorder : Theme.timeline.laneBorder
            }
        }

        // Draw beat dividers over the lane backgrounds so every track shows the grid.
        Repeater {
            model: root.trackModel
            delegate: Rectangle {
                required property int index
                required property int trackId
                required property color trackColour
                objectName: "timelineLaneColour" + trackId
                x: 0; y: root.rulerHeight + root.rowTop(index)
                width: 3; height: root.rowHeight(index)
                color: trackColour
                z: 2
            }
        }
        Repeater {
            model: root.visibleBeatCount
            delegate: Rectangle {
                property real beatNumber: root.firstVisibleBeat + index * root.beatGridStride
                x: beatNumber * root.gridUnitWidth
                y: root.rulerHeight
                width: 1
                height: scroll.contentHeight - root.rulerHeight
                color: editor.isBarLine(beatNumber * root.gridStepBeats, editor.beatsPerBar) ? Theme.timeline.gridBar : Theme.timeline.gridBeat
                z: 1
            }
        }

        // Only actual clips from the project model are drawn in the arrangement.
        Repeater {
            model: root.trackModel ? root.trackModel.clips : []
            delegate: Rectangle {
                id: clipItem
                required property int clipId
                required property int laneIndex
                required property int trackId
                required property real startBeat
                required property real lengthBeats
                required property string label
                required property bool effectsPrinted
                required property color trackColour
                required property bool isRecorded
                required property var notes
                required property bool reversed
                required property bool polarityInverted
                required property bool hasEffects
                required property real fadeInBeats
                required property real fadeOutBeats
                // Fade handle drags (restMap G8): the dragged length in beats, -1 when not dragging.
                property real dragFadeIn: -1
                property real dragFadeOut: -1
                readonly property real shownFadeIn: dragFadeIn >= 0 ? dragFadeIn : fadeInBeats
                readonly property real shownFadeOut: dragFadeOut >= 0 ? dragFadeOut : fadeOutBeats
                objectName: "clipBlock" + clipId
                readonly property var preview: editor.previews[String(clipId)] ?? ({})
                x: (preview.startBeat ?? startBeat) * root.beatWidth + 4
                y: root.rulerHeight + root.rowTop(preview.laneIndex ?? laneIndex) + 8
                width: Math.max(4, (preview.lengthBeats ?? lengthBeats) * root.beatWidth - 8)
                height: root.laneHeight - 18
                radius: 4
                color: Qt.darker(trackColour, 2.4)
                border.color: (preview.selected ?? false) ? Theme.accent : trackColour
                border.width: (preview.selected ?? false) ? 2 : 1
                z: 3
                Text {
                    anchors.left: parent.left; anchors.leftMargin: 10
                    anchors.top: parent.top; anchors.topMargin: 8
                    text: label + (clipItem.effectsPrinted ? "" : " · Dry") + (clipItem.reversed ? " · Reversed" : "")
                          + (clipItem.polarityInverted ? " · Ø" : ""); color: Theme.timeline.clipLabel; font.pixelSize: 11
                    elide: Text.ElideRight
                    width: Math.max(0, parent.width - 20)
                }
                // Clip effects badge (restMap R2): click it to open them.
                Rectangle {
                    objectName: "clipEffectsBadge" + clipItem.clipId
                    visible: clipItem.hasEffects
                    anchors.right: parent.right; anchors.rightMargin: 12
                    anchors.top: parent.top; anchors.topMargin: 6
                    width: 24; height: 15; radius: 3
                    color: Theme.accentBg
                    border.color: Theme.accent
                    z: 2
                    Text { anchors.centerIn: parent; text: "FX"; color: Theme.accent; font.pixelSize: 9; font.bold: true }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.openClipEffects(clipItem.clipId)
                        ToolTip.visible: containsMouse; ToolTip.delay: 450
                        ToolTip.text: "This clip has its own effects: click to open them"
                        hoverEnabled: true
                    }
                }
                Canvas {
                    id: waveform
                    objectName: "clipWaveform" + clipItem.clipId
                    // The drawn strip is two views wide and starts on a whole-view step, so while
                    // scrolling it just moves with the clip; new peaks are fetched (and painted)
                    // only when the view crosses a step, not on every frame (plan 5.3).
                    readonly property real step: Math.max(1, scroll.width)
                    readonly property bool onScreen: clipItem.x < scroll.contentX + scroll.width
                                                     && clipItem.x + clipItem.width > scroll.contentX
                    x: Math.max(0, Math.floor((scroll.contentX - clipItem.x) / step) * step)
                    y: 30
                    width: onScreen ? Math.max(0, Math.min(clipItem.width - x, 2 * step)) : 0
                    height: Math.max(0, clipItem.height - 38)
                    property var peaks: width > 0 && root.trackModel
                        ? root.trackModel.clipWaveform(clipItem.trackId, clipItem.clipId, Math.ceil(width),
                            x / clipItem.width, Math.min(1, (x + width) / clipItem.width)) : []
                    onPeaksChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        ctx.strokeStyle = Theme.timeline.waveform
                        ctx.lineWidth = 1
                        ctx.beginPath()
                        for (let i = 0; i < peaks.length; ++i) {
                            const px = (i + 0.5) * width / peaks.length
                            ctx.moveTo(px, height * (0.5 - Math.min(1, peaks[i][1]) * 0.48))
                            ctx.lineTo(px, height * (0.5 - Math.max(-1, peaks[i][0]) * 0.48) + 1)
                        }
                        ctx.stroke()
                    }
                }
                // Fades (restMap G8): the part a fade turns down is shaded above its curve.
                Repeater {
                    model: clipItem.isRecorded ? [false, true] : []
                    delegate: Canvas {
                        id: fadeCurve
                        required property bool modelData
                        readonly property bool atEnd: modelData
                        objectName: (atEnd ? "clipFadeOutCurve" : "clipFadeInCurve") + clipItem.clipId
                        readonly property real beats: atEnd ? clipItem.shownFadeOut : clipItem.shownFadeIn
                        width: Math.min(clipItem.width, Math.max(0, beats * root.beatWidth))
                        height: clipItem.height
                        x: atEnd ? clipItem.width - width : 0
                        visible: width >= 1
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            const steps = Math.max(2, Math.ceil(width / 3))
                            const yAt = i => {
                                const t = i / steps
                                return height * (1 - Math.sin(Math.PI / 2 * (atEnd ? 1 - t : t)))
                            }
                            ctx.beginPath()
                            ctx.moveTo(0, 0)
                            for (let i = 0; i <= steps; ++i) ctx.lineTo(i * width / steps, yAt(i))
                            ctx.lineTo(width, 0)
                            ctx.closePath()
                            ctx.fillStyle = Qt.rgba(0, 0, 0, 0.35)
                            ctx.fill()
                            ctx.beginPath()
                            for (let i = 0; i <= steps; ++i) {
                                if (i === 0) ctx.moveTo(0, yAt(0)); else ctx.lineTo(i * width / steps, yAt(i))
                            }
                            ctx.strokeStyle = Theme.timeline.waveform
                            ctx.lineWidth = 1
                            ctx.stroke()
                        }
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                    }
                }
                // Reaper-style outlines of the pattern's notes. Vertical scale fits the clip's own
                // pitch range; only the visible part of the clip is painted (like the waveform).
                Canvas {
                    id: noteOutlines
                    objectName: "clipNotes" + clipItem.clipId
                    visible: !clipItem.isRecorded && clipItem.notes.length > 0
                    x: Math.max(0, scroll.contentX - clipItem.x)
                    y: 26
                    width: visible ? Math.max(0, Math.min(clipItem.width - x, scroll.width)) : 0
                    height: Math.max(0, clipItem.height - 30)
                    // Left-edge trim previews keep notes fixed in time; moves carry them along.
                    readonly property real trimShift: root.editOperation === "trimStart"
                        ? (clipItem.preview.startBeat ?? clipItem.startBeat) - clipItem.startBeat : 0
                    readonly property real visibleLength: clipItem.preview.lengthBeats ?? clipItem.lengthBeats
                    readonly property color noteColour: Qt.lighter(clipItem.trackColour, 1.7)
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        const shown = []
                        let lo = 127, hi = 0
                        for (let i = 0; i < clipItem.notes.length; ++i) {
                            const n = clipItem.notes[i]
                            const start = n.startBeat - trimShift
                            if (start + n.lengthBeats <= 0 || start >= visibleLength) continue // hidden
                            shown.push({ start: start, length: n.lengthBeats, note: n.note })
                            lo = Math.min(lo, n.note); hi = Math.max(hi, n.note)
                        }
                        if (shown.length === 0) return
                        // At least an octave of rows so a single note doesn't fill the clip
                        const rows = Math.max(12, hi - lo + 1)
                        const base = lo - Math.floor((rows - (hi - lo + 1)) / 2)
                        const rowH = height / rows
                        const barH = Math.max(1.5, Math.min(6, rowH - 1))
                        ctx.fillStyle = Qt.rgba(noteColour.r, noteColour.g, noteColour.b, 0.55)
                        ctx.strokeStyle = noteColour
                        ctx.lineWidth = 1
                        for (const n of shown) {
                            // Clip-local x (the block is drawn 4px right of its start beat)
                            const x0 = Math.max(0, n.start * root.beatWidth - 4) - x
                            const x1 = Math.min(clipItem.width, (n.start + n.length) * root.beatWidth - 4) - x
                            if (x1 <= 0 || x0 >= width) continue
                            const ny = (rows - 1 - (n.note - base)) * rowH + (rowH - barH) / 2
                            const w = Math.max(2, x1 - x0)
                            ctx.fillRect(x0, ny, w, barH)
                            ctx.strokeRect(Math.round(x0) + 0.5, Math.round(ny) + 0.5, Math.max(1, Math.round(w) - 1), Math.max(1, Math.round(barH) - 1))
                        }
                    }
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onXChanged: requestPaint()
                    onTrimShiftChanged: requestPaint()
                    onVisibleLengthChanged: requestPaint()
                    onNoteColourChanged: requestPaint()
                    Connections {
                        target: clipItem
                        function onNotesChanged() { noteOutlines.requestPaint() }
                    }
                    Connections {
                        target: root
                        function onBeatWidthChanged() { noteOutlines.requestPaint() }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: pressedButtons & Qt.LeftButton ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    property real pointerStartX: 0
                    property real originalBeat: 0
                    property int originalLane: 0
                    onPressed: mouse => {
                        if (mouse.button === Qt.LeftButton && (mouse.modifiers & Qt.ShiftModifier)) {
                            const point = mapToItem(scroll.contentItem, mouse.x, mouse.y)
                            root.beginSelection(point.x, point.y-root.rulerHeight, true)
                            return
                        }
                        editor.selectClip(clipItem.trackId, clipItem.clipId,
                            mouse.button === Qt.LeftButton && !!(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)),
                            true)
                        root.forceActiveFocus()
                        pointerStartX = mapToItem(scroll.contentItem, mouse.x, mouse.y).x
                        originalBeat = clipItem.startBeat; originalLane = clipItem.laneIndex
                        if (mouse.button === Qt.LeftButton && !(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) {
                            editor.beginEdit("move")
                        }
                    }
                    onPositionChanged: mouse => {
                        if (root.selecting && root.clipSelectionDrag) {
                            const point = mapToItem(scroll.contentItem, mouse.x, mouse.y)
                            root.updateSelection(point.x, point.y-root.rulerHeight)
                            return
                        }
                        if (!(pressedButtons & Qt.LeftButton) || root.recording || (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) return
                        const delta = mapToItem(scroll.contentItem, mouse.x, mouse.y).x - pointerStartX
                        const pointerY = mapToItem(scroll.contentItem, mouse.x, mouse.y).y
                        editor.updateGroupLane(root.rowAt(pointerY-root.rulerHeight)-originalLane)
                        const verticalOnly = root.editLaneDelta !== 0 && Math.abs(delta) < Qt.styleHints.startDragDistance
                        editor.updateDrag(originalBeat, delta / root.beatWidth, verticalOnly)
                    }
                    onReleased: mouse => {
                        if (mouse.button === Qt.LeftButton) {
                            if (root.selecting) root.finishSelection()
                            else editor.commitGroupEdit()
                        }
                    }
                    onCanceled: { editor.cancelEdit(); editor.cancelSelection() }
                    onClicked: mouse => {
                        if (mouse.button !== Qt.RightButton) return
                        clipContextMenu.targetTrackId = clipItem.trackId
                        clipContextMenu.targetClipId = clipItem.clipId
                        clipContextMenu.targetStart = clipItem.startBeat
                        clipContextMenu.targetLength = clipItem.lengthBeats
                        clipContextMenu.targetIsRecorded = clipItem.isRecorded
                        clipContextMenu.targetLabel = clipItem.label
                        const position = mapToItem(root, mouse.x, mouse.y)
                        clipContextMenu.popup(position.x, position.y)
                    }
                    onDoubleClicked: mouse => {
                        if (mouse.button === Qt.LeftButton && !clipItem.isRecorded) {
                            root.openPianoRollRequested(clipItem.trackId, clipItem.clipId,
                                clipItem.label, clipItem.lengthBeats)
                        }
                    }
                }
                Rectangle {
                    anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                    width: 8; radius: 3; color: leftTrim.containsMouse ? Theme.accent : Theme.timeline.trimHandle
                    MouseArea {
                        id: leftTrim
                        objectName: "clipLeftTrim" + clipItem.clipId
                        anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.LeftButton
                        cursorShape: Qt.SizeHorCursor
                        property real pointerStart: 0
                        property real originalStart: 0
                        property real originalEnd: 0
                        onPressed: mouse => {
                            editor.selectClip(clipItem.trackId, clipItem.clipId, false, true)
                            pointerStart = mapToItem(scroll.contentItem, mouse.x, mouse.y).x
                            originalStart = clipItem.startBeat
                            editor.beginEdit("trimStart")
                        }
                        onPositionChanged: mouse => {
                            if (!pressed || root.recording) return
                            const delta = (mapToItem(scroll.contentItem, mouse.x, mouse.y).x - pointerStart) / root.beatWidth
                            editor.updateDrag(originalStart, delta)
                        }
                        onReleased: editor.commitGroupEdit()
                        onCanceled: { editor.cancelEdit() }
                        ToolTip.visible: containsMouse; ToolTip.text: "Drag to trim the start"
                    }
                }
                // Fade handles (restMap G8) in the top corners, at the end of each fade: drag them
                // sideways to set the fade. Audio clips only; one undo step per drag.
                Repeater {
                    model: clipItem.isRecorded ? [false, true] : []
                    delegate: Rectangle {
                        id: fadeHandle
                        required property bool modelData
                        readonly property bool atEnd: modelData
                        readonly property real fadePixels: (atEnd ? clipItem.shownFadeOut : clipItem.shownFadeIn) * root.beatWidth
                        width: 8; height: 8; radius: 2
                        y: 0; z: 4
                        x: atEnd ? Math.max(8, Math.min(clipItem.width - 16, clipItem.width - 16 - fadePixels))
                                 : Math.max(8, Math.min(clipItem.width - 16, 8 + fadePixels))
                        color: fadeArea.containsMouse || fadeArea.pressed ? Theme.accent : Theme.timeline.trimHandle
                        MouseArea {
                            id: fadeArea
                            objectName: (fadeHandle.atEnd ? "clipFadeOutHandle" : "clipFadeInHandle") + clipItem.clipId
                            anchors.fill: parent; anchors.margins: -3
                            hoverEnabled: true; acceptedButtons: Qt.LeftButton
                            cursorShape: Qt.SizeHorCursor
                            property real pointerStart: 0
                            property real original: 0
                            property bool moved: false
                            function dragged(mouse) {
                                const delta = (mapToItem(scroll.contentItem, mouse.x, mouse.y).x - pointerStart) / root.beatWidth
                                const other = fadeHandle.atEnd ? clipItem.fadeInBeats : clipItem.fadeOutBeats
                                return Math.max(0, Math.min(clipItem.lengthBeats - other,
                                                            original + (fadeHandle.atEnd ? -delta : delta)))
                            }
                            onPressed: mouse => {
                                editor.selectClip(clipItem.trackId, clipItem.clipId, false, true)
                                pointerStart = mapToItem(scroll.contentItem, mouse.x, mouse.y).x
                                original = fadeHandle.atEnd ? clipItem.fadeOutBeats : clipItem.fadeInBeats
                                moved = false
                            }
                            onPositionChanged: mouse => {
                                if (!pressed || root.recording) return
                                moved = true
                                if (fadeHandle.atEnd) clipItem.dragFadeOut = dragged(mouse)
                                else clipItem.dragFadeIn = dragged(mouse)
                            }
                            onReleased: mouse => {
                                const beats = dragged(mouse)
                                clipItem.dragFadeIn = -1; clipItem.dragFadeOut = -1
                                if (moved && !root.recording && root.trackModel)
                                    root.trackModel.setClipFade(clipItem.trackId, clipItem.clipId, fadeHandle.atEnd, beats)
                            }
                            onCanceled: { clipItem.dragFadeIn = -1; clipItem.dragFadeOut = -1 }
                            ToolTip.visible: containsMouse && !pressed; ToolTip.delay: 450
                            ToolTip.text: fadeHandle.atEnd ? "Drag to set the fade-out" : "Drag to set the fade-in"
                        }
                    }
                }
                Rectangle {
                    id: resizeGrip
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    width: 8
                    color: resizeArea.containsMouse ? Theme.accent : Theme.timeline.trimHandle
                    radius: 3
                    MouseArea {
                        id: resizeArea
                        objectName: "clipRightTrim" + clipItem.clipId
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton
                        cursorShape: Qt.SizeHorCursor
                        property real pointerStartX: 0
                        property real lengthStart: 0

                        function updatePreview(mouseX) {
                            const pointerX = mapToItem(scroll.contentItem, mouseX, height / 2).x
                            const delta = (pointerX - pointerStartX) / root.beatWidth
                            editor.updateDrag(lengthStart, delta)
                        }

                        onPressed: mouse => {
                            editor.selectClip(clipItem.trackId, clipItem.clipId, false, true)
                            pointerStartX = mapToItem(scroll.contentItem, mouse.x, mouse.y).x
                            lengthStart = editor.clipEndBeat(clipItem.trackId, clipItem.clipId)
                            editor.beginEdit("resize")
                        }
                        onPositionChanged: mouse => {
                            if (pressed)
                                updatePreview(mouse.x)
                        }
                        onReleased: mouse => {
                            updatePreview(mouse.x)
                            editor.commitGroupEdit()
                        }
                        onCanceled: { editor.cancelEdit() }
                        ToolTip.visible: containsMouse
                        ToolTip.text: root.snapStepBeats > 0 ? "Drag to trim or extend on the snap grid" : "Drag to trim or extend freely"
                        ToolTip.delay: 450
                    }
                }
            }
        }

        MouseArea {
            x: 0; y: 0; width: root.contentWidth; height: root.rulerHeight; z: 3
            acceptedButtons: Qt.LeftButton
            onPressed: mouse => editor.seek(mouse.x / root.beatWidth)
            onPositionChanged: mouse => { if (pressed) editor.seek(mouse.x / root.beatWidth) }
            cursorShape: Qt.PointingHandCursor
        }

        // Dragging selects a range, and the playhead follows the pointer (also while playing)
        // and stays where the mouse is let go, like Reaper's edit cursor; a click puts it there.
        // Shift-drag box-selects clips and leaves the playhead alone. Navigation is through the
        // scrollbars, not by dragging the arrangement background.
        MouseArea {
            objectName: "timelineBackground"
            x: 0; y: root.rulerHeight; width: root.contentWidth; height: scroll.contentHeight - root.rulerHeight; z: 1
            acceptedButtons: Qt.LeftButton
            onPressed: mouse => {
                const lane = root.rowAt(mouse.y)
                if (root.trackModel && lane >= 0 && lane < root.trackModel.count) {
                    root.trackModel.selectTrack(root.trackModel.trackIdAt(lane))
                }
                const clips = !!(mouse.modifiers & Qt.ShiftModifier)
                root.beginSelection(mouse.x, mouse.y, clips)
                if (!clips) editor.seek(mouse.x / root.beatWidth)
            }
            onPositionChanged: mouse => {
                if (!pressed) return
                root.updateSelection(mouse.x, mouse.y)
                if (!root.clipSelectionDrag) editor.seek(Math.max(0, mouse.x) / root.beatWidth)
            }
            onReleased: root.finishSelection()
            onCanceled: { editor.cancelSelection() }
            onDoubleClicked: mouse => editor.createClipAt(mouse.x / root.beatWidth, root.rowAt(mouse.y))
        }
        Rectangle {
            objectName: "timelineSelection"
            x: Math.min(root.selectionStartX, root.selectionEndX)
            y: root.rulerHeight + Math.min(root.selectionStartY, root.selectionEndY)
            width: Math.abs(root.selectionEndX-root.selectionStartX)
            height: Math.max(1,Math.abs(root.selectionEndY-root.selectionStartY))
            visible: root.clipSelectionDrag && root.selecting && width>2
            color: Theme.timeline.selectionBox; border.color: Theme.accent; z: 2
        }
        Rectangle {
            objectName: "timelineTimeRange"
            x: root.rangeStartBeat*root.beatWidth; y: root.rulerHeight
            width: Math.max(0,root.rangeEndBeat-root.rangeStartBeat)*root.beatWidth
            height: scroll.contentHeight-root.rulerHeight
            visible: width>0
            color: Theme.timeline.timeRange; border.color: Theme.accent; z: 2
        }

        // Volume envelopes (restMap G6): in the track's own lane under it, or over its clips.
        Repeater {
            model: root.trackModel
            delegate: VolumeEnvelopeLane {
                id: envelopeLane
                required property int index
                required property int trackId
                required property bool envelopeShown
                required property bool envelopeOverClips
                required property bool envelopeBypassed
                required property var envelopePoints
                objectName: "volumeEnvelope" + trackId
                visible: envelopeShown
                trackModel: root.trackModel
                envelopeTrackId: trackId
                points: envelopePoints
                bypassed: envelopeBypassed
                overClips: envelopeOverClips
                beatWidth: root.beatWidth
                viewX: scroll.contentX
                viewWidth: scroll.width
                x: 0
                y: root.rulerHeight + root.rowTop(index) + (envelopeOverClips ? 8 : root.laneHeight)
                width: root.contentWidth
                height: envelopeOverClips ? root.laneHeight - 18 : root.envelopeLaneHeight
                z: envelopeOverClips ? 3.5 : 2.5
                onMenuRequested: (id, x, y) => {
                    envelopeMenu.lane = envelopeLane
                    const at = root.mapFromItem(null, x, y)
                    envelopeMenu.popup(at.x, at.y)
                }
                onPointMenuRequested: (point, x, y) => {
                    envelopePointMenu.lane = envelopeLane
                    envelopePointMenu.point = point
                    const at = root.mapFromItem(null, x, y)
                    envelopePointMenu.popup(at.x, at.y)
                }
            }
        }

        Repeater {
          model: root.recording ? root.transport.recordingTracks : []
          delegate: Rectangle {
            id: recordingBox
            required property int modelData
            objectName: "recordingPreview" + modelData
            visible: !!root.trackModel
            x: root.transport.recordingStartBeats * root.beatWidth
            y: root.rulerHeight + root.rowTop(root.trackModel ? root.trackModel.indexOfTrack(modelData) : 0) + 8
            width: Math.max(2, root.playhead - x)
            height: root.laneHeight - 18
            color: Theme.recordActiveBg; border.color: Theme.record; radius: 4; z: 3
            Text { anchors.left: parent.left; anchors.leftMargin: 8; anchors.top: parent.top; anchors.topMargin: 8; text: "Recording…"; color: Theme.text }
            // The take's waveform as it is recorded (live, like Reaper's), laid out like a clip's.
            // Only the visible part is fetched and painted; it refreshes as the box grows.
            Canvas {
                id: liveWaveform
                objectName: "recordingWaveform" + recordingBox.modelData
                x: Math.max(0, scroll.contentX - recordingBox.x)
                y: 30
                width: Math.max(0, Math.min(recordingBox.width, scroll.contentX + scroll.width - recordingBox.x) - x)
                height: Math.max(0, recordingBox.height - 38)
                property var peaks: width >= 1 && recordingBox.width > 0
                    ? root.transport.recordingWaveform(recordingBox.modelData, Math.ceil(width),
                                                       x / recordingBox.width, Math.min(1, (x + width) / recordingBox.width))
                    : []
                onPeaksChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.strokeStyle = Theme.timeline.waveform
                    ctx.lineWidth = 1
                    ctx.beginPath()
                    for (let i = 0; i < peaks.length; ++i) {
                        const px = (i + 0.5) * width / peaks.length
                        ctx.moveTo(px, height * (0.5 - Math.min(1, peaks[i][1]) * 0.48))
                        ctx.lineTo(px, height * (0.5 - Math.max(-1, peaks[i][0]) * 0.48) + 1)
                    }
                    ctx.stroke()
                }
            }
          }
        }
        DropArea {
            objectName: "timelineFileDrop"
            x: 0; y: root.rulerHeight; width: root.contentWidth; height: scroll.contentHeight - root.rulerHeight; z: 5
            keys: ["text/uri-list"]
            enabled: !!root.importJob && !root.recording
            onDropped: drop => {
                const files = drop.urls.filter(url => root.importJob.isImportable(url))
                if (files.length === 0) return
                const row = root.rowAt(drop.y)
                const onTrack = root.trackModel && row >= 0 && row < root.trackModel.count
                root.importJob.importFiles(files, Math.max(0, drop.x / root.beatWidth),
                                           onTrack ? root.trackModel.trackIdAt(row) : -1)
                drop.acceptProposedAction()
            }
        }
        Rectangle { objectName: "timelinePlayhead"; x: root.playhead; y: 0; width: 2; height: scroll.contentHeight; color: Theme.record; z: 4 }

        ScrollBar.horizontal: ScrollBar {
            objectName: "timelineHorizontalScroll"
            policy: ScrollBar.AlwaysOn
            onPressedChanged: { if (pressed) root.followPlayhead = false }
        }
        ScrollBar.vertical: ScrollBar {
            objectName: "timelineVerticalScroll"
            policy: ScrollBar.AlwaysOn
            onPressedChanged: { if (pressed) root.followPlayhead = false }
        }
    }

}
