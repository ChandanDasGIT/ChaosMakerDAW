import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    height: 48
    color: Theme.panelBg
    border.color: Theme.divider
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 6
        Repeater {
            model: ["New", "Open", "Save", "↶", "↷", "♩"]
            delegate: ToolButton {
                text: modelData
                implicitHeight: 32
                contentItem: Text {
                    text: parent.text
                    color: Theme.text
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 4
                    color: parent.down ? "#383838" : "transparent"
                }
            }
        }
        ToolButton {
            objectName: "settingsButton"
            text: "Settings"
            onClicked: settingsMenu.open()
            palette.buttonText: Theme.text
            Menu {
                id: settingsMenu
                y: parent.height
                MenuItem {
                    objectName: "restoreSessionSetting"
                    text: "Restore session on startup"
                    checkable: true
                    checked: EngineController.restoreSessionOnStartup
                    onTriggered: EngineController.restoreSessionOnStartup = checked
                }
            }
        }
        Item { Layout.fillWidth: true }
        Text {
            text: "BUFFER"
            color: Theme.textMuted
            font.pixelSize: 9
            font.bold: true
        }
        ComboBox {
            id: bufferSelector
            Layout.preferredWidth: 82
            Layout.preferredHeight: 30
            model: EngineController.bufferSizes
            enabled: EngineController.selectedBufferSize > 0
            currentIndex: model.indexOf(String(EngineController.selectedBufferSize))
            onActivated: index => EngineController.setBufferSize(Number(currentText))
            contentItem: Text {
                leftPadding: 8
                rightPadding: 20
                text: bufferSelector.currentIndex < 0 ? "—" : bufferSelector.displayText
                color: bufferSelector.enabled ? Theme.text : "#777b83"
                font.pixelSize: 10
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: 5
                color: bufferSelector.enabled ? "#292b30" : "#222328"
                border.color: bufferSelector.activeFocus ? Theme.accent : "#444750"
            }
        }
        Text {
            text: "OUTPUT DEVICE"
            color: Theme.textMuted
            font.pixelSize: 9
            font.bold: true
        }
        ComboBox {
            id: outputSelector
            Layout.preferredWidth: 300
            Layout.preferredHeight: 30
            model: EngineController.outputDevices
            enabled: count > 0
            currentIndex: EngineController.selectedOutputIndex
            onActivated: index => EngineController.setOutputDevice(index)
            contentItem: Text {
                leftPadding: 9
                rightPadding: 23
                text: outputSelector.count === 0 ? "No output devices"
                      : outputSelector.currentIndex < 0 ? "Select output device" : outputSelector.displayText
                color: outputSelector.enabled ? Theme.text : "#777b83"
                font.pixelSize: 10
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideMiddle
            }
            background: Rectangle {
                radius: 5
                color: outputSelector.enabled ? "#292b30" : "#222328"
                border.color: outputSelector.activeFocus ? Theme.accent : "#444750"
            }
        }
    }
}
