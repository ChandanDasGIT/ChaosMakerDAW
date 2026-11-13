import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Two plain tabs for the bottom panel: Mixer and Piano Roll. A tab is lit (bright text and an
// accent line under it) only while its panel is on screen. The window owns the state; this only shows it and asks.
Rectangle {
    id: root
    property bool mixerShown: true
    property bool pianoRollShown: false
    property bool pianoRollAvailable: false
    signal toggleMixerRequested()
    signal togglePianoRollRequested()

    implicitWidth: tabs.implicitWidth + 4
    implicitHeight: 30
    color: "transparent"

    component PanelTab: ToolButton {
        id: tab
        property bool shown: false
        property string hint: ""
        implicitHeight: 28
        leftPadding: 10
        rightPadding: 10
        contentItem: Text {
            text: tab.text
            color: !tab.enabled ? Theme.textDisabled : tab.shown || tab.hovered ? Theme.text : Theme.textMuted
            font.pixelSize: Theme.bodySize
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Item {
            Rectangle {
                anchors.fill: parent
                radius: Theme.controlRadius
                color: tab.down ? Theme.controlBgActive : tab.hovered && tab.enabled ? Theme.controlBgHover : "transparent"
            }
            Rectangle {
                objectName: "panelTabLine"
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: 2
                visible: tab.shown
                color: Theme.accent
            }
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
