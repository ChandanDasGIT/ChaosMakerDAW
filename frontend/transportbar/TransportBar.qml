import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var transport: EngineController
    property int timeSignature: 4
    property int timeSignatureDenominator: 4
    property real bpm: 120
    property bool playing: false
    property bool recording: false
    property bool repeating: false
    readonly property var focusedItem: Window.window ? Window.window.activeFocusItem : null
    readonly property bool editingText: focusedItem instanceof TextInput || focusedItem instanceof TextEdit
    signal numeratorEdited(int value)
    signal denominatorEdited(int value)
    signal bpmEdited(real value)
    signal transportAction(string action)
    height: 72
    color: "#242424"
    border.color: Theme.divider
    Shortcut {
        sequence: "Space"
        context: Qt.WindowShortcut
        enabled: root.visible && root.enabled && !root.editingText && !EngineController.projectLoading && !quickSettings.visible
        autoRepeat: false
        onActivated: root.transportAction(root.playing ? "pause" : "play")
    }
    Shortcut {
        sequence: "R"
        context: Qt.WindowShortcut
        enabled: root.visible && root.enabled && !root.editingText && !EngineController.projectLoading && !quickSettings.visible
        autoRepeat: false
        onActivated: root.transportAction("record")
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 8
        Repeater {
            model: [
                {glyph: "|◀", hintText: "Go to start", command: "rewind"},
                {glyph: "▶|", hintText: "Skip forward one bar", command: "forward"},
                {glyph: "●", hintText: "Start/stop recording (R)", command: "record"},
                {glyph: "▶", hintText: "Play (Space)", command: "play"},
                {glyph: "Ⅱ", hintText: "Pause (Space)", command: "pause"},
                {glyph: "■", hintText: "Stop", command: "stop"},
                {glyph: "⟳", hintText: "Toggle repeat", command: "repeat"}
            ]
            delegate: ToolButton {
                id: transportButton
                objectName: "transport_" + command
                required property int index
                required property string glyph
                required property string hintText
                required property string command
                readonly property bool active: command === "record" ? root.recording || root.transport.countingIn
                                                   : command === "play" ? root.playing && !root.recording
                                                   : command === "repeat" ? root.repeating : false
                text: glyph
                Layout.preferredWidth: index < 2 ? 42 : 36
                Layout.preferredHeight: 36
                contentItem: Text {
                    text: transportButton.glyph
                    color: index === 2 ? Theme.record : (transportButton.active ? Theme.accent : (transportButton.hovered ? Theme.text : Theme.textMuted))
                    font.pixelSize: index === 2 ? 18 : 12
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 6
                    color: index === 2 && transportButton.active ? "#633238"
                         : transportButton.down ? "#454851"
                         : transportButton.active ? "#203a45"
                         : transportButton.hovered ? "#363941" : "#2b2d32"
                    border.color: index === 2 && transportButton.active ? Theme.record
                                : transportButton.active ? Theme.accent
                                : transportButton.hovered ? "#5a5e68" : "#41444c"
                }
                onClicked: root.transportAction(command)
                ToolTip.visible: hovered
                ToolTip.text: hintText
                ToolTip.delay: 450
            }
        }
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 34; color: Theme.divider; Layout.leftMargin: 8; Layout.rightMargin: 8 }
        Text {
            text: {
                const ms = Math.floor(EngineController.positionSeconds * 1000)
                const hours = Math.floor(ms / 3600000)
                const minutes = Math.floor(ms / 60000) % 60
                const seconds = Math.floor(ms / 1000) % 60
                return String(hours).padStart(2, "0") + ":" + String(minutes).padStart(2, "0") + ":" + String(seconds).padStart(2, "0") + "." + String(ms % 1000).padStart(3, "0")
            }
            color: Theme.text; font.pixelSize: 15; font.family: "monospace" }
        Item { Layout.fillWidth: true }
        ToolButton {
            id: quickSettingsButton
            objectName: "quickSettingsButton"
            Layout.preferredWidth: 132; Layout.preferredHeight: 36
            text: "QuickSettings"
            checkable: true
            checked: quickSettings.visible
            onClicked: checked ? quickSettings.open() : quickSettings.close()
            contentItem: Text { text: parent.text; color: quickSettings.visible ? Theme.accent : Theme.text; font.pixelSize: 12; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { radius: 6; color: quickSettings.visible ? "#203a45" : parent.hovered ? "#363941" : "#2b2d32"; border.color: quickSettings.visible ? Theme.accent : "#41444c" }
            ToolTip.visible: hovered
            ToolTip.text: "Metronome, count-in, BPM and time signature"
        }
    }

    Popup {
        id: quickSettings
        objectName: "quickSettingsPopup"
        parent: quickSettingsButton
        anchors.centerIn: Overlay.overlay
        popupType: Popup.Item
        width: 320
        padding: 16
        focus: true
        modal: false
        onOpened: contentItem.forceActiveFocus()
        onAboutToHide: contentItem.forceActiveFocus()
        Shortcut {
            sequence: "Space"
            context: Qt.WindowShortcut
            enabled: quickSettings.visible && !root.editingText && !EngineController.projectLoading
            autoRepeat: false
            onActivated: root.transportAction(root.playing ? "pause" : "play")
        }
        Shortcut {
            sequence: "R"
            context: Qt.WindowShortcut
            enabled: quickSettings.visible && !root.editingText && !EngineController.projectLoading
            autoRepeat: false
            onActivated: root.transportAction("record")
        }
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        background: Rectangle { radius: 8; color: Theme.panelBg; border.color: Theme.divider }
        contentItem: ColumnLayout {
            spacing: 16
            RowLayout {
                Layout.fillWidth: true
                Text { text: "QuickSettings"; color: Theme.text; font.pixelSize: 15; font.bold: true; Layout.fillWidth: true }
                ToolButton {
                    objectName: "closeQuickSettings"
                    text: "×"; implicitWidth: 28; implicitHeight: 24
                    onClicked: quickSettings.close()
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
