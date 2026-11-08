import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// A track's FX panel: its insert effects and its instrument's own page. A window of its own,
// like the on-screen piano: it floats over the app, and the user moves it by its title bar
// (onto another screen too) and resizes it. open() shows it (centred on the app the first
// time), close(), the x button or Escape hide it; closed() follows every hide, and the
// app's window gets the keyboard back.
Window {
    id: root
    signal closed()
    property var trackModel
    property int trackId: -1
    property string trackName: ""
    property string trackType: ""
    property var eqBands: []
    property bool hasLiveInput: true
    property bool recordDry: false
    // The track's insert effects in processing order (TrackListWrapper "effects" role).
    property var effects: []
    // The pedalboard model of the track shown, loaded only while the panel is open.
    readonly property var effectsModel: visible && supportsGuitarSuite ? EngineController.trackEffects(trackId) : null
    property alias selectedEffect: panelBridge.selectedEffect
    readonly property bool supportsGuitarSuite: panelBridge.supportsGuitarSuite
    readonly property bool supportsHarmonium: panelBridge.supportsHarmonium
    readonly property bool supportsDrumKit: panelBridge.supportsDrumKit
    readonly property bool supportsElectroDrums: panelBridge.supportsElectroDrums
    readonly property bool supportsPiano: panelBridge.supportsPiano
    readonly property bool supportsChords: panelBridge.supportsChords
    // What "+ Add FX" offers; re-read whenever the chain changes.
    readonly property var addableEffects: (root.effects, root.trackModel && root.trackId >= 0
                                           ? root.trackModel.addableEffects(root.trackId) : [])

    TrackEffectsPanelWrapper {
        id: panelBridge
        trackId: root.trackId
        trackName: root.trackName
        trackType: root.trackType
        hasLiveInput: root.hasLiveInput
        recordDry: root.recordDry
        chain: root.effects
    }

    property bool placed: false                             // centred once; then where the user left it

    function open() {
        if (!placed) {
            const screenW = Screen.desktopAvailableWidth, screenH = Screen.desktopAvailableHeight
            width = Math.min(1120, screenW - 32)
            height = Math.min(680, screenH - 70)
            const host = transientParent
            x = host ? host.x + (host.width - width) / 2 : (screenW - width) / 2
            y = host ? host.y + (host.height - height) / 2 : (screenH - height) / 2
            placed = true
        }
        show()
        requestActivate()
    }
    onVisibleChanged: {
        if (visible) return
        // The window it floats over takes the keyboard back (shortcuts, KEYS, the transport).
        if (transientParent) transientParent.requestActivate()
        closed()
    }

    title: root.trackName + " · Effects"
    flags: Qt.Tool | Qt.WindowStaysOnTopHint
    width: 1120
    height: 680
    minimumWidth: 640
    minimumHeight: 420
    color: Theme.popupBg
    // A window of its own does not inherit the main window's palette: without these the
    // tooltips fall back to the system's black on yellow.
    palette.toolTipBase: Theme.tooltipBg
    palette.toolTipText: Theme.tooltipText

    Shortcut {
        sequences: [StandardKey.Cancel]
        onActivated: root.close()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 66
            color: Theme.effects.header
            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Text { text: root.trackName + " · Effects"; color: Theme.text; font.pixelSize: 17; font.bold: true }
                    Text { text: panelBridge.recordModeDescription; color: Theme.textMuted; font.pixelSize: 11 }
                }
                Item { Layout.fillWidth: true }                        // keeps the controls together on the right
                PanelButton {
                    objectName: "exportChainButton"
                    text: "Export chain…"
                    tooltip: "Save this track's effects, in order and with every setting, to a .chaosfx file"
                    onClicked: exportChainDialog.open()
                }
                PanelButton {
                    objectName: "importChainButton"
                    text: "Import chain…"
                    enabled: !EngineController.takeBusy
                    tooltip: "Replace this track's effects with the ones in a .chaosfx file"
                    onClicked: importChainDialog.open()
                }
                ThemedComboBox {
                    objectName: "trackRecordingMode"
                    visible: root.hasLiveInput
                    model: ["Print FX", "Dry · editable FX"]
                    currentIndex: root.recordDry ? 1 : 0
                    enabled: !EngineController.takeBusy
                    onActivated: index => root.trackModel.setRecordDry(root.trackId, index === 1)
                    ToolTip.visible: hovered
                    ToolTip.text: "Applies to new takes. Existing recordings keep their original mode."
                }
                CloseButton {
                    objectName: "closeTrackEffects"
                    implicitWidth: 32; implicitHeight: 32; glyphSize: 22
                    Accessible.name: "Close effects"
                    tooltip: "Close effects"
                    onClicked: root.close()
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            // The chain, top to bottom: select to edit, drag the grip to reorder, the remove button removes.
            Rectangle {
                Layout.preferredWidth: 220
                Layout.fillHeight: true
                color: Theme.effects.sidebar
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "CHAIN"; color: Theme.textMuted; font.pixelSize: 10; font.letterSpacing: 1 }
                        Text { text: "top to bottom"; color: Theme.textMuted; font.pixelSize: Theme.microSize; Layout.fillWidth: true }
                    }
                    Text {
                        objectName: "emptyChainHint"
                        visible: root.effects.length === 0
                        text: "No effects yet"
                        color: Theme.textMuted
                        font.pixelSize: 11
                    }
                    Item {
                        id: chainArea
                        objectName: "effectChain"
                        readonly property real rowHeight: 58
                        readonly property real rowStep: rowHeight + 4
                        // Drop position while a row is dragged (-1: not dragging).
                        property int dropIndex: -1
                        Layout.fillWidth: true
                        implicitHeight: Math.max(0, root.effects.length * rowStep - 4)
                        Repeater {
                            model: root.effects
                            delegate: Item {
                                id: slot
                                required property var modelData
                                required property int index
                                width: chainArea.width
                                height: chainArea.rowHeight
                                y: index * chainArea.rowStep
                                Rectangle {
                                    id: row
                                    objectName: "selectEffect_" + slot.modelData.type
                                    width: parent.width
                                    height: parent.height
                                    radius: 6
                                    z: grip.drag.active ? 2 : 0
                                    readonly property bool selected: root.selectedEffect === slot.modelData.type
                                    color: selected ? Theme.effects.itemSelected : rowArea.containsMouse ? Theme.effects.itemHover : "transparent"
                                    border.color: selected || grip.drag.active ? Theme.accent : "transparent"
                                    MouseArea {
                                        id: rowArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: panelBridge.selectEffect(slot.modelData.type)
                                    }
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 4
                                        anchors.rightMargin: 6
                                        spacing: 4
                                        Text {
                                            objectName: "effectGrip_" + slot.modelData.type
                                            text: Icons.dragHandle
                                            color: grip.containsMouse || grip.drag.active ? Theme.text : Theme.textMuted
                                            font.pixelSize: 14
                                            Layout.preferredWidth: 18
                                            horizontalAlignment: Text.AlignHCenter
                                            Accessible.name: "Drag to reorder " + slot.modelData.title
                                            MouseArea {
                                                id: grip
                                                anchors.fill: parent
                                                anchors.margins: -4
                                                hoverEnabled: true
                                                cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                                drag.target: row
                                                drag.axis: Drag.YAxis
                                                drag.minimumY: -slot.index * chainArea.rowStep
                                                drag.maximumY: (root.effects.length - 1 - slot.index) * chainArea.rowStep
                                                onPositionChanged: if (drag.active)
                                                    chainArea.dropIndex = Math.max(0, Math.min(root.effects.length - 1,
                                                        slot.index + Math.round(row.y / chainArea.rowStep)))
                                                onReleased: {
                                                    const target = chainArea.dropIndex
                                                    chainArea.dropIndex = -1
                                                    row.y = 0
                                                    if (target >= 0 && target !== slot.index)
                                                        root.trackModel.moveTrackEffect(root.trackId, slot.modelData.id, target)
                                                }
                                                ToolTip.visible: containsMouse && !drag.active
                                                ToolTip.text: "Drag to reorder"
                                                ToolTip.delay: 450
                                            }
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 4
                                            Text { text: slot.modelData.title; color: Theme.text; font.pixelSize: 12; font.bold: true }
                                            Text { text: slot.modelData.subtitle; color: Theme.textMuted; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
                                        }
                                        CloseButton {
                                            id: removeEffect
                                            objectName: "removeEffect_" + slot.modelData.type
                                            visible: slot.modelData.removable
                                            destructive: true
                                            implicitWidth: 22; implicitHeight: 22
                                            Accessible.name: "Remove " + slot.modelData.title
                                            tooltip: "Remove " + slot.modelData.title
                                            onClicked: root.trackModel.removeTrackEffect(root.trackId, slot.modelData.id)
                                        }
                                    }
                                }
                            }
                        }
                        // Where a dragged effect will land.
                        Rectangle {
                            visible: chainArea.dropIndex >= 0
                            width: parent.width
                            height: 2
                            color: Theme.accent
                            y: chainArea.dropIndex * chainArea.rowStep - 3
                        }
                    }
                    PanelButton {
                        id: addEffectButton
                        objectName: "addEffectButton"
                        text: Icons.plus + " Add FX"
                        enabled: root.addableEffects.length > 0
                        tooltip: enabled ? "Add an effect to this track" : "Every available effect is on this track"
                        onClicked: addEffectPopup.open()
                        Popup {
                            id: addEffectPopup
                            objectName: "addEffectPopup"
                            y: addEffectButton.height + 4
                            width: 196
                            padding: 6
                            background: Rectangle { color: Theme.popupBg; border.color: Theme.popupBorder; radius: 6 }
                            contentItem: Column {
                                spacing: 2
                                Repeater {
                                    model: root.addableEffects
                                    delegate: ItemDelegate {
                                        required property var modelData
                                        objectName: "addEffect_" + modelData.type
                                        width: 184
                                        implicitHeight: 44
                                        onClicked: {
                                            // Adding rebuilds this list (and this row), so take what we need first.
                                            const type = modelData.type, bridge = panelBridge, model = root.trackModel
                                            addEffectPopup.close()
                                            if (model.addTrackEffect(root.trackId, type) > 0) bridge.selectEffect(type)
                                        }
                                        background: Rectangle { radius: 5; color: parent.hovered ? Theme.effects.itemHover : "transparent" }
                                        contentItem: Column {
                                            spacing: 2
                                            Text { text: modelData.title; color: Theme.text; font.pixelSize: 12; font.bold: true }
                                            Text { text: modelData.subtitle; color: Theme.textMuted; font.pixelSize: 10 }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                    Text {
                        text: "Effects run top to bottom.\nYour settings stay with this track."
                        color: Theme.textMuted
                        font.pixelSize: 11
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                }
            }
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: root.selectedEffect === "eq" ? 0
                            : root.selectedEffect === "guitarsuite" ? 1
                            : root.selectedEffect === "chaoseq" ? 2
                            : root.selectedEffect === "harmonium" ? 3
                            : root.selectedEffect === "drumkit" ? 4
                            : root.selectedEffect === "electrodrums" ? 5
                            : root.selectedEffect === "piano" ? 6
                            : root.selectedEffect === "chords" ? 7 : 8
                Tenbandeq {
                    trackModel: root.trackModel
                    trackId: root.trackId
                    trackName: root.trackName
                    eqBands: root.eqBands
                }
                Loader {
                    active: root.supportsGuitarSuite && root.effectsModel !== null
                    sourceComponent: GuitarSuiteEditor {
                        objectName: "trackGuitarSuiteEditor" + root.trackId
                        trackName: root.trackName
                        // Keep the last model until the Loader unloads; it turns null first on close.
                        Binding on effectsModel {
                            value: root.effectsModel
                            when: root.effectsModel !== null
                            restoreMode: Binding.RestoreNone
                        }
                        monitoring: EngineController.monitoredTracks.indexOf(root.trackId) >= 0
                        levels: EngineController.trackLevels[String(root.trackId)] ?? ({})
                        onClearClipsRequested: EngineController.clearTrackClips(root.trackId)
                    }
                }
                ChaosEqEditor {
                    objectName: "trackChaosEq"
                    trackModel: root.trackModel
                    trackId: root.trackId
                    trackName: root.trackName
                    active: root.visible && root.selectedEffect === "chaoseq"
                }
                Loader {
                    active: root.supportsHarmonium
                    sourceComponent: HarmoniumEditor {
                        objectName: "trackHarmonium" + root.trackId
                        trackModel: root.trackModel
                        trackId: root.trackId
                        trackName: root.trackName
                    }
                }
                Loader {
                    active: root.supportsDrumKit
                    sourceComponent: DrumKitEditor {
                        objectName: "trackDrumKit" + root.trackId
                        trackModel: root.trackModel
                        trackId: root.trackId
                        trackName: root.trackName
                    }
                }
                Loader {
                    active: root.supportsElectroDrums
                    sourceComponent: ElectroDrumsEditor {
                        objectName: "trackElectroDrums" + root.trackId
                        trackModel: root.trackModel
                        trackId: root.trackId
                        trackName: root.trackName
                    }
                }
                Loader {
                    active: root.supportsPiano
                    sourceComponent: PianoEditor {
                        objectName: "trackPiano" + root.trackId
                        trackModel: root.trackModel
                        trackId: root.trackId
                        trackName: root.trackName
                    }
                }
                Loader {
                    active: root.supportsChords
                    sourceComponent: ChordsEditor {
                        objectName: "trackChords" + root.trackId
                        trackModel: root.trackModel
                        trackId: root.trackId
                        trackName: root.trackName
                    }
                }
                Item {
                    Text {
                        objectName: "addEffectHint"
                        anchors.centerIn: parent
                        text: "Add an effect to start"
                        color: Theme.textMuted
                        font.pixelSize: 13
                    }
                }
            }
        }
    }

    // ---- FX chain files (EngineController export / preview / import) ----
    FileDialog {
        id: exportChainDialog
        objectName: "exportChainDialog"
        popupType: Popup.Item                                   // stays inside this window (plan 1.11)
        title: "Export FX chain"
        fileMode: FileDialog.SaveFile
        nameFilters: ["FX chains (*.chaosfx)"]
        defaultSuffix: "chaosfx"
        onAccepted: root.exportChain(selectedFile)
    }
    FileDialog {
        id: importChainDialog
        objectName: "importChainDialog"
        popupType: Popup.Item
        title: "Import FX chain"
        fileMode: FileDialog.OpenFile
        nameFilters: ["FX chains (*.chaosfx)"]
        onAccepted: root.previewChain(selectedFile)
    }
    function exportChain(file) {
        const result = EngineController.exportEffectChain(root.trackId, file)
        chainMessage.show(result.ok ? "FX chain exported" : "Could not export",
                          result.ok ? "Saved " + result.kept.join(", ") + "." : result.error)
    }
    function previewChain(file) {
        const result = EngineController.previewEffectChain(root.trackId, file)
        if (!result.ok) { chainMessage.show("Could not import", result.error); return }
        chainConfirm.file = file
        chainConfirm.kept = result.kept
        chainConfirm.skipped = result.skipped
        chainConfirm.sourceType = result.trackType
        chainConfirm.open()
    }

    // Asks before the track's effects are replaced (effect changes are not on the undo list).
    Popup {
        id: chainConfirm
        objectName: "chainImportConfirm"
        property url file
        property var kept: []
        property var skipped: []
        property string sourceType: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 380
        modal: true
        padding: 14
        background: Rectangle { color: Theme.popupBg; border.color: Theme.popupBorder; radius: 6 }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                text: "Replace " + root.trackName + "'s effects?"
                color: Theme.text; font.pixelSize: Theme.bodySize; font.bold: true
                wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            Text {
                objectName: "chainImportKept"
                text: chainConfirm.kept.length > 0
                      ? "From the " + (chainConfirm.sourceType.length > 0 ? chainConfirm.sourceType + " chain" : "file")
                        + ": " + chainConfirm.kept.join(", ") + "."
                      : "Nothing in the file fits this track; its addable effects will be removed."
                color: Theme.text; font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            Text {
                objectName: "chainImportSkipped"
                visible: chainConfirm.skipped.length > 0
                text: "Skipped, this track type can't take: " + chainConfirm.skipped.join(", ") + "."
                color: Theme.meterWarning; font.pixelSize: Theme.captionSize
                wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            Text {
                text: "This can't be undone."
                color: Theme.textMuted; font.pixelSize: Theme.captionSize
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PanelButton { text: "Cancel"; onClicked: chainConfirm.close() }
                PanelButton {
                    objectName: "chainImportOk"
                    text: "Replace"
                    onClicked: {
                        const result = EngineController.importEffectChain(root.trackId, chainConfirm.file)
                        chainConfirm.close()
                        if (!result.ok) chainMessage.show("Could not import", result.error)
                    }
                }
            }
        }
    }

    // A short result: what was exported, or why something failed.
    Popup {
        id: chainMessage
        objectName: "chainMessage"
        property string heading: ""
        property string detail: ""
        function show(heading, detail) { chainMessage.heading = heading; chainMessage.detail = detail; open() }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 360
        padding: 14
        background: Rectangle { color: Theme.popupBg; border.color: Theme.popupBorder; radius: 6 }
        contentItem: ColumnLayout {
            spacing: 10
            Text { text: chainMessage.heading; color: Theme.text; font.pixelSize: Theme.bodySize; font.bold: true }
            Text {
                objectName: "chainMessageDetail"
                text: chainMessage.detail
                color: Theme.textMuted; font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PanelButton { objectName: "chainMessageOk"; text: "OK"; onClicked: chainMessage.close() }
            }
        }
    }
}
