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
    property var effectsModel: null
    property string selectedEffect: "eq"
    readonly property bool supportsGuitarSuite: hasLiveInput && trackType !== "Vocal"
    onSupportsGuitarSuiteChanged: {
        if (!supportsGuitarSuite && selectedEffect === "pedals") selectedEffect = "eq"
    }
    onSelectedEffectChanged: {
        if (!supportsGuitarSuite && selectedEffect === "pedals") selectedEffect = "eq"
    }
    // Add future editor entries here; navigation stays in the same workspace.
    readonly property var effectEntries: [
        { key: "eq", title: "Equalizer", subtitle: "10-band tone shaping", available: true },
        { key: "pedals", title: "GuitarSuite", subtitle: "Pedals and signal chain", available: supportsGuitarSuite }
    ].filter(entry => entry.available)
    parent: Overlay.overlay
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    width: Math.min(1120, parent.width - 32)
    height: Math.min(680, parent.height - 70)
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 0
    onAboutToShow: effectsModel = supportsGuitarSuite ? EngineController.trackEffects(trackId) : null
    background: Rectangle { color: "#22252b"; radius: 10; border.color: "#4a4d56" }
    contentItem: ColumnLayout {
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 66
            color: "#2b2f37"
            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Text { text: root.trackName + " · Effects"; color: Theme.text; font.pixelSize: 17; font.bold: true }
                    Text { text: root.recordDry ? "Dry recording · current effects are applied during playback" : "Print FX · effects are baked into new recordings"; color: Theme.textMuted; font.pixelSize: 11 }
                }
                ComboBox {
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
                    text: "×"
                    onClicked: root.close()
                    contentItem: Text { text: "×"; color: Theme.text; font.pixelSize: 22; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    ToolTip.visible: hovered
                    ToolTip.text: "Close effects"
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Rectangle {
                Layout.preferredWidth: 200
                Layout.fillHeight: true
                color: "#1c1f25"
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    TextField {
                        id: search
                        objectName: "effectSearch"
                        Layout.fillWidth: true
                        placeholderText: "Find an effect…"
                        selectByMouse: true
                        color: Theme.text
                        placeholderTextColor: Theme.textMuted
                        background: Rectangle { color: "#292e36"; border.color: search.activeFocus ? Theme.accent : "#414752"; radius: 5 }
                    }
                    Text { text: "EFFECTS"; color: Theme.textMuted; font.pixelSize: 10; font.letterSpacing: 1 }
                    Repeater {
                        model: root.effectEntries
                        delegate: ItemDelegate {
                            required property var modelData
                            objectName: "selectEffect_" + modelData.key
                            Layout.fillWidth: true
                            implicitHeight: 62
                            visible: modelData.available && (modelData.title + " " + modelData.subtitle).toLowerCase().includes(search.text.toLowerCase())
                            highlighted: root.selectedEffect === modelData.key
                            onClicked: root.selectedEffect = modelData.key
                            background: Rectangle {
                                radius: 6
                                color: parent.highlighted ? "#344044" : parent.hovered ? "#2b3038" : "transparent"
                                border.color: parent.highlighted ? Theme.accent : "transparent"
                            }
                            contentItem: ColumnLayout {
                                spacing: 4
                                Text { text: modelData.title; color: Theme.text; font.pixelSize: 12; font.bold: true }
                                Text { text: modelData.subtitle; color: Theme.textMuted; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                    Text {
                        text: "Switch effects here.\nYour settings stay with this track."
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
                currentIndex: root.supportsGuitarSuite && root.selectedEffect === "pedals" ? 1 : 0
                TrackEqualizer {
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
                        effectsModel: root.effectsModel
                        monitoring: EngineController.monitoredTracks.indexOf(root.trackId) >= 0
                        levels: EngineController.trackLevels[String(root.trackId)] ?? ({})
                        onClearClipsRequested: EngineController.clearTrackClips(root.trackId)
                    }
                }
            }
        }
    }
}
