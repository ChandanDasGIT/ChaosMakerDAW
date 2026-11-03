import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../settings"
import "../engine"

Rectangle {
    id: root
    property var transport: EngineController
    property int timeSignature: 4
    property int timeSignatureDenominator: 4
    property real bpm: 120
    property int snapMode: 1
    property bool playing: false
    property bool recording: false
    property bool repeating: false
    readonly property var focusedItem: Window.window ? Window.window.activeFocusItem : null
    readonly property bool editingText: focusedItem instanceof TextInput || focusedItem instanceof TextEdit
    signal numeratorEdited(int value)
    signal denominatorEdited(int value)
    signal bpmEdited(real value)
    signal snapModeEdited(int value)
    signal transportAction(string action)
    height: 72
    color: "#242424"
    border.color: Theme.divider

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 8

        PlaybackControls {
            id: playbackControls
            transport: root.transport
            playing: root.playing
            recording: root.recording
            repeating: root.repeating
            onTransportAction: action => root.transportAction(action)
        }

        Item { Layout.fillWidth: true }

        QuickSettings {
            id: quickSettings
            Layout.preferredWidth: 132
            Layout.preferredHeight: 36
            transport: root.transport
            timeSignature: root.timeSignature
            timeSignatureDenominator: root.timeSignatureDenominator
            bpm: root.bpm
            snapMode: root.snapMode
            onNumeratorEdited: value => root.numeratorEdited(value)
            onDenominatorEdited: value => root.denominatorEdited(value)
            onBpmEdited: value => root.bpmEdited(value)
            onSnapModeEdited: value => root.snapModeEdited(value)
            onTransportAction: action => root.transportAction(action)
        }
    }
}
