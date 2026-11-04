import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."
import "../tempo"

Item {
    id: root

    property var transport: EngineController
    property int timeSignature: 4
    property int timeSignatureDenominator: 4
    property real bpm: 120
    property int snapMode: 1
    readonly property var focusedItem: Window.window ? Window.window.activeFocusItem : null
    readonly property bool editingText: focusedItem instanceof TextInput || focusedItem instanceof TextEdit

    signal numeratorEdited(int value)
    signal denominatorEdited(int value)
    signal bpmEdited(real value)
    signal snapModeEdited(int value)
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
        text: "QuickSettings"
        checkable: true
        checked: quickSettingsPopup.visible
        onClicked: checked ? quickSettingsPopup.open() : quickSettingsPopup.close()
        contentItem: Text {
            text: parent.text
            color: quickSettingsPopup.visible ? Theme.accent : Theme.text
            font.pixelSize: 12
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 6
            color: quickSettingsPopup.visible ? "#203a45" : parent.hovered ? "#363941" : "#2b2d32"
            border.color: quickSettingsPopup.visible ? Theme.accent : "#41444c"
        }
        ToolTip.visible: hovered
        ToolTip.text: "Metronome, count-in, BPM, time signature and snap grid"
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
                Text { text: "QuickSettings"; color: Theme.text; font.pixelSize: 15; font.bold: true; Layout.fillWidth: true }
                ToolButton {
                    objectName: "closeQuickSettings"
                    text: "×"; implicitWidth: 28; implicitHeight: 24
                    onClicked: quickSettingsPopup.close()
                    ToolTip.visible: hovered; ToolTip.text: "Close QuickSettings"
                }
            }

            ColumnLayout {
                spacing: 3
                Text { text: "METRONOME"; color: Theme.textMuted; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
                RowLayout {
                    spacing: 5
                    ToolButton {
                        objectName: "metronomeToggle"
                        property bool active: root.transport.metronomeEnabled
                        Layout.preferredWidth: 48; Layout.preferredHeight: 29
                        text: "CLICK"
                        onClicked: root.transport.metronomeEnabled = !active
                        contentItem: Text { text: parent.text; color: parent.active ? Theme.accent : Theme.textMuted; font.pixelSize: 10; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: 5; color: parent.active ? "#203a45" : "#2b2d32"; border.color: parent.active ? Theme.accent : "#41444c" }
                        ToolTip.visible: hovered
                        ToolTip.text: active ? "Disable metronome" : "Enable metronome; first beat is accented"
                        Accessible.name: "Metronome"
                    }
                    Slider {
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
                    ToolButton {
                        objectName: "countInToggle"
                        property bool active: root.transport.countInEnabled
                        enabled: !root.transport.takeBusy
                        Layout.preferredWidth: 48; Layout.preferredHeight: 29
                        text: root.transport.countingIn ? "IN " + root.transport.countInBeatsRemaining : "COUNT"
                        onClicked: root.transport.countInEnabled = !active
                        contentItem: Text { text: parent.text; color: parent.active ? Theme.accent : Theme.textMuted; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: 5; color: parent.active ? "#203a45" : "#2b2d32"; border.color: parent.active ? Theme.accent : "#41444c" }
                        ToolTip.visible: hovered
                        ToolTip.text: "One-bar recording count-in/pre-roll; uses the metronome volume"
                        Accessible.name: "Recording count-in"
                    }
                }
            }

            ColumnLayout {
                enabled: !root.transport.countingIn
                spacing: 3
                Text { text: "TIME SIGNATURE"; color: Theme.textMuted; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
                Rectangle {
                    Layout.preferredWidth: 87
                    Layout.preferredHeight: 29
                    radius: 6
                    color: "#191a1e"
                    border.color: "#3a3d45"
                    Row {
                        anchors.centerIn: parent
                        spacing: 3
                        TextField {
                            id: numeratorField
                            objectName: "meterNumerator"
                            width: 32; height: 23
                            text: String(root.timeSignature)
                            color: Theme.text
                            font.pixelSize: 12; font.bold: true
                            horizontalAlignment: TextInput.AlignHCenter
                            verticalAlignment: TextInput.AlignVCenter
                            selectByMouse: true
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: IntValidator { bottom: 1; top: 2147483647 }
                            background: Rectangle {
                                radius: 5
                                color: numeratorField.activeFocus ? "#282b32" : "transparent"
                                border.color: numeratorField.activeFocus ? Theme.accent : "transparent"
                            }
                            onEditingFinished: {
                                if (acceptableInput && Number(text) > 0)
                                    root.numeratorEdited(Number(text))
                                else
                                    text = String(root.timeSignature)
                            }
                        }
                        Text { text: "/"; anchors.verticalCenter: parent.verticalCenter; color: "#7e838d"; font.pixelSize: 13 }
                        TextField {
                            id: denominatorField
                            objectName: "meterDenominator"
                            width: 32; height: 23
                            text: String(root.timeSignatureDenominator)
                            color: Theme.text
                            font.pixelSize: 12; font.bold: true
                            horizontalAlignment: TextInput.AlignHCenter
                            verticalAlignment: TextInput.AlignVCenter
                            selectByMouse: true
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: IntValidator { bottom: 1; top: 2147483647 }
                            background: Rectangle {
                                radius: 5
                                color: denominatorField.activeFocus ? "#282b32" : "transparent"
                                border.color: denominatorField.activeFocus ? Theme.accent : "transparent"
                            }
                            onEditingFinished: {
                                if (acceptableInput && Number(text) > 0)
                                    root.denominatorEdited(Number(text))
                                else
                                    text = String(root.timeSignatureDenominator)
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: 3
                enabled: !root.transport.countingIn
                Text { text: "BPM"; color: Theme.textMuted; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
                TempoEditor {
                    Layout.preferredWidth: 124
                    Layout.preferredHeight: 29
                    value: root.bpm
                    onValueEdited: value => root.bpmEdited(value)
                }
            }

            ColumnLayout {
                spacing: 3
                Layout.fillWidth: true
                Text { text: "SNAP GRID"; color: Theme.textMuted; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
                ComboBox {
                    id: snapGrid
                    objectName: "snapGridSelector"
                    Layout.fillWidth: true
                    model: settingsBridge.snapModes
                    currentIndex: root.snapMode
                    onActivated: index => root.snapModeEdited(index)
                    Accessible.name: "Snap grid"
                }
            }
        }
    }

    Connections {
        target: root.transport
        function onMetronomeChanged() { clickVolume.value = root.transport.metronomeVolume }
    }
    Connections {
        target: root
        function onTimeSignatureChanged() { numeratorField.text = String(root.timeSignature) }
        function onTimeSignatureDenominatorChanged() { denominatorField.text = String(root.timeSignatureDenominator) }
    }
}
