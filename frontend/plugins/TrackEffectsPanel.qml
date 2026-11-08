import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root
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

    parent: Overlay.overlay
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    width: Math.min(1120, parent.width - 32)
    height: Math.min(680, parent.height - 70)
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 0
    background: Rectangle { color: Theme.popupBg; radius: 10; border.color: Theme.popupBorder }
    contentItem: ColumnLayout {
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
                ToolButton {
                    objectName: "closeTrackEffects"
                    text: Icons.close
                    Accessible.name: "Close effects"
                    onClicked: root.close()
                    contentItem: Text { text: Icons.close; color: Theme.text; font.pixelSize: 22; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    ToolTip.visible: hovered
                    ToolTip.text: "Close effects"
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
                                        ToolButton {
                                            id: removeEffect
                                            objectName: "removeEffect_" + slot.modelData.type
                                            visible: slot.modelData.removable
                                            implicitWidth: 22; implicitHeight: 22
                                            Accessible.name: "Remove " + slot.modelData.title
                                            onClicked: root.trackModel.removeTrackEffect(root.trackId, slot.modelData.id)
                                            contentItem: Text { text: Icons.close; color: removeEffect.hovered ? Theme.textStrong : Theme.textMuted; font.pixelSize: 16; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                            background: Rectangle { radius: 4; color: removeEffect.hovered ? Theme.recordBg : "transparent"; border.color: removeEffect.hovered ? Theme.record : "transparent" }
                                            ToolTip.visible: hovered
                                            ToolTip.text: "Remove " + slot.modelData.title
                                            ToolTip.delay: 450
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
                            : root.selectedEffect === "electrodrums" ? 5 : 6
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
}
