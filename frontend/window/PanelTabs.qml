import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Two-part switch for the bottom panel: [Mixer | Piano Roll]. A part is lit only while
// its panel is on screen. The window owns the state; this only shows it and asks.
Rectangle {
    id: root
    property bool mixerShown: true
    property bool pianoRollShown: false
    property bool pianoRollAvailable: false
    signal toggleMixerRequested()
    signal togglePianoRollRequested()

    implicitWidth: tabs.implicitWidth + 4
    implicitHeight: 32
    radius: 5
    color: Theme.cardBgAlt
    border.color: Theme.divider

    component PanelTab: ToolButton {
        id: tab
        property bool shown: false
        property string hint: ""
        implicitHeight: 28
        leftPadding: 10
        rightPadding: 10
        contentItem: RowLayout {
            spacing: 6
            Rectangle {
                width: 7; height: 7; radius: 3.5
                color: tab.shown ? Theme.accent : Theme.textMuted
                opacity: tab.enabled ? 1 : 0.5
            }
            Text {
                text: tab.text
                color: !tab.enabled ? Theme.textMuted : tab.shown ? Theme.text : Theme.textMuted
                font.pixelSize: Theme.bodySize
                font.bold: tab.shown
                verticalAlignment: Text.AlignVCenter
            }
        }
        background: Rectangle {
            radius: 4
            color: tab.down ? Theme.controlBgActive
                 : tab.shown ? Theme.controlBgActive
                 : tab.hovered && tab.enabled ? Theme.controlBgHover : "transparent"
        }
        ToolTip.visible: hovered
        ToolTip.text: hint
        ToolTip.delay: 450
    }

    RowLayout {
        id: tabs
        anchors.centerIn: parent
        spacing: 2

        PanelTab {
            objectName: "mixerTab"
            text: "Mixer"
            shown: root.mixerShown
            hint: (root.mixerShown ? "Hide the mixer" : "Show the mixer") + (Shortcuts.texts["view.mixer"] ? " (" + Shortcuts.texts["view.mixer"] + ")" : "")
            Accessible.name: "Mixer panel"
            onClicked: root.toggleMixerRequested()
        }
        // Wrapped so the "why is it disabled" tooltip covers only this part (a disabled
        // button gets no hover itself).
        Item {
            implicitWidth: pianoRollTab.implicitWidth
            implicitHeight: pianoRollTab.implicitHeight
            PanelTab {
                id: pianoRollTab
                objectName: "pianoRollTab"
                anchors.fill: parent
                text: "Piano Roll"
                shown: root.pianoRollShown
                enabled: root.pianoRollAvailable || root.pianoRollShown
                hint: root.pianoRollShown ? "Close the piano roll" : "Show the piano roll"
                Accessible.name: "Piano roll panel"
                onClicked: root.togglePianoRollRequested()
            }
            HoverHandler { id: disabledHover; enabled: !pianoRollTab.enabled }
            ToolTip.visible: disabledHover.hovered
            ToolTip.text: "Open a MIDI clip to use the piano roll"
            ToolTip.delay: 450
        }
    }
}
