import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."
import "../icons"

Item {
    id: root

    property var transport: EngineController
    property int snapMode: 1
    property bool ripple: false            // restMap G15: ripple editing, owned by the app settings
    readonly property var focusedItem: Window.window ? Window.window.activeFocusItem : null
    readonly property bool editingText: focusedItem instanceof TextInput || focusedItem instanceof TextEdit

    signal snapModeEdited(int value)
    signal rippleEdited(bool on)
    signal transportAction(string action)

    implicitWidth: quickSettingsButton.implicitWidth
    implicitHeight: quickSettingsButton.implicitHeight

    QuickSettingsWrapper {
        id: settingsBridge
    }

    ToolButton {
        id: quickSettingsButton
        objectName: "quickSettingsButton"
        anchors.fill: parent
        checkable: true
        checked: quickSettingsPopup.visible
        onClicked: checked ? quickSettingsPopup.open() : quickSettingsPopup.close()
        Accessible.name: "Metronome and snap settings"
        contentItem: Item {
            MetronomeIcon {
                anchors.centerIn: parent
                width: 18; height: 18
                color: quickSettingsPopup.visible || root.transport.metronomeEnabled ? Theme.accent : Theme.text
            }
        }
        background: Rectangle {
            radius: 6
            color: quickSettingsPopup.visible ? Theme.accentBg : parent.hovered ? Theme.transport.buttonHover : Theme.transport.button
            border.color: quickSettingsPopup.visible ? Theme.accent : Theme.transport.buttonBorder
        }
        ToolTip.visible: hovered
        ToolTip.text: "Metronome, count-in and snap grid"
    }

    Popup {
        id: quickSettingsPopup
        objectName: "quickSettingsPopup"
        parent: quickSettingsButton
        anchors.centerIn: Overlay.overlay
        popupType: Popup.Item
        width: 320
        padding: 16
        focus: true
        modal: false
        onOpened: contentLayout.forceActiveFocus()
        onAboutToHide: contentLayout.forceActiveFocus()

        Shortcut {
            sequence: "Space"
            context: Qt.WindowShortcut
            enabled: quickSettingsPopup.visible && !root.editingText && !EngineController.projectLoading
            autoRepeat: false
            onActivated: root.transportAction(root.transport.playing ? "pause" : "play")
        }
        Shortcut {
            sequence: "R"
            context: Qt.WindowShortcut
            enabled: quickSettingsPopup.visible && !root.editingText && !EngineController.projectLoading
            autoRepeat: false
            onActivated: root.transportAction("record")
        }

        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        background: Rectangle { radius: 8; color: Theme.panelBg; border.color: Theme.divider }

        contentItem: ColumnLayout {
            id: contentLayout
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                Text { text: "Metronome & Snap"; color: Theme.text; font.pixelSize: 15; font.bold: true; Layout.fillWidth: true }
                CloseButton {
                    objectName: "closeQuickSettings"
                    implicitWidth: 28; implicitHeight: 24; glyphSize: 18
                    Accessible.name: "Close metronome and snap settings"
                    tooltip: "Close"
                    onClicked: quickSettingsPopup.close()
                }
            }

            ColumnLayout {
                spacing: 3
                Text { text: "METRONOME"; color: Theme.textMuted; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
                RowLayout {
                    spacing: 5
                    ToggleChip {
                        objectName: "metronomeToggle"
                        active: root.transport.metronomeEnabled
                        idleBg: Theme.transport.button; idleBorder: Theme.transport.buttonBorder; cornerRadius: 5
                        Layout.preferredWidth: 48; Layout.preferredHeight: 29
                        text: "CLICK"
                        onClicked: root.transport.metronomeEnabled = !active
                        tooltip: active ? "Disable metronome" : "Enable metronome; first beat is accented"
                        Accessible.name: "Metronome"
                    }
                    HardwareSlider {
                        id: clickVolume
                        objectName: "metronomeVolume"
                        Layout.preferredWidth: 72; Layout.preferredHeight: 29
                        from: 0; to: 1; stepSize: 0.01
                        value: root.transport.metronomeVolume
                        onMoved: root.transport.metronomeVolume = value
                        ToolTip.visible: hovered
                        ToolTip.text: "Metronome volume " + Math.round(value * 100) + "%"
                        Accessible.name: "Metronome volume"
                    }
                    Text { text: Math.round(root.transport.metronomeVolume * 100) + "%"; color: Theme.textMuted; font.pixelSize: 10; Layout.preferredWidth: 30 }
                    ToggleChip {
                        objectName: "countInToggle"
                        active: root.transport.countInEnabled
                        enabled: !root.transport.takeBusy
                        idleBg: Theme.transport.button; idleBorder: Theme.transport.buttonBorder; cornerRadius: 5
                        Layout.preferredWidth: 48; Layout.preferredHeight: 29
                        text: root.transport.countingIn ? "IN " + root.transport.countInBeatsRemaining : "COUNT"; textSize: 9
                        onClicked: root.transport.countInEnabled = !active
                        tooltip: "One-bar recording count-in/pre-roll; uses the metronome volume"
                        Accessible.name: "Recording count-in"
                    }
                }
            }

            ColumnLayout {
                spacing: 3
                Layout.fillWidth: true
                Text { text: "SNAP GRID"; color: Theme.textMuted; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
                ThemedComboBox {
                    id: snapGrid
                    objectName: "snapGridSelector"
                    Layout.fillWidth: true
                    model: settingsBridge.snapModes
                    currentIndex: root.snapMode
                    onActivated: index => root.snapModeEdited(index)
                    Accessible.name: "Snap grid"
                }
                ToggleChip {
                    objectName: "rippleToggle"
                    active: root.ripple
                    idleBg: Theme.transport.button; idleBorder: Theme.textStrong; cornerRadius: 5   // white outline: easy to spot
                    Layout.preferredWidth: 58; Layout.preferredHeight: 29
                    text: "RIPPLE"; textSize: 9
                    onClicked: root.rippleEdited(!active)
                    tooltip: active ? "Ripple on: deleting closes the gap, pasting and duplicating make room (all tracks)"
                                    : "Ripple off: deleting leaves a gap, pasting goes over what is there"
                    Accessible.name: "Ripple editing"
                }
            }
        }
    }

    Connections {
        target: root.transport
        function onMetronomeChanged() { clickVolume.value = root.transport.metronomeVolume }
    }
}
