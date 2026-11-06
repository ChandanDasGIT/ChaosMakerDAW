import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

// Audio and MIDI device pickers, laid out as label | picker rows (shown in Preferences).
GridLayout {
    id: root
    columns: 2
    columnSpacing: 12
    rowSpacing: 8

    // Buffer changes go through the wrapper (it validates the size and forwards it once).
    // Output changes go straight to EngineController, which owns the real device list.
    DeviceSettingsWrapper {
        id: deviceBridge
        onRequestBufferSizeChange: frames => EngineController.setBufferSize(frames)
    }

    Text {
        text: "BUFFER"
        color: Theme.textMuted
        font.pixelSize: 9
        font.bold: true
    }

    ComboBox {
        id: bufferSelector
        objectName: "bufferSizeSelector"
        Layout.preferredWidth: 82
        Layout.preferredHeight: 30
        model: EngineController.bufferSizes
        enabled: EngineController.selectedBufferSize > 0
        currentIndex: model.indexOf(String(EngineController.selectedBufferSize))
        onActivated: index => deviceBridge.setBufferSize(Number(currentText))
        Accessible.name: "Buffer size"
        contentItem: Text {
            leftPadding: 8
            rightPadding: 20
            text: bufferSelector.currentIndex < 0 ? "—" : bufferSelector.displayText
            color: bufferSelector.enabled ? Theme.text : Theme.settings.deviceTextDisabled
            font.pixelSize: 10
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 5
            color: bufferSelector.enabled ? Theme.settings.deviceBox : Theme.settings.deviceBoxDisabled
            border.color: bufferSelector.activeFocus ? Theme.accent : Theme.settings.deviceBoxBorder
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
        objectName: "outputDeviceSelector"
        Layout.fillWidth: true
        Layout.minimumWidth: 220
        Layout.preferredHeight: 30
        model: EngineController.outputDevices
        enabled: count > 0
        currentIndex: EngineController.selectedOutputIndex
        onActivated: index => EngineController.setOutputDevice(index)
        Accessible.name: "Output device"
        contentItem: Text {
            leftPadding: 9
            rightPadding: 23
            text: outputSelector.count === 0 ? "No output devices"
                  : outputSelector.currentIndex < 0 ? "Select output device" : outputSelector.displayText
            color: outputSelector.enabled ? Theme.text : Theme.settings.deviceTextDisabled
            font.pixelSize: 10
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideMiddle
        }
        background: Rectangle {
            radius: 5
            color: outputSelector.enabled ? Theme.settings.deviceBox : Theme.settings.deviceBoxDisabled
            border.color: outputSelector.activeFocus ? Theme.accent : Theme.settings.deviceBoxBorder
        }
    }

    Text {
        text: "MIDI IN"
        color: Theme.textMuted
        font.pixelSize: 9
        font.bold: true
    }

    ComboBox {
        id: midiSelector
        objectName: "midiInputSelector"
        Layout.fillWidth: true
        Layout.minimumWidth: 220
        Layout.preferredHeight: 30
        model: EngineController.midiInputPorts
        enabled: count > 0
        currentIndex: model.indexOf(EngineController.connectedMidiPort)
        onActivated: index => {
            EngineController.connectMidiInput(currentText)
        }
        Accessible.name: "MIDI input"
        contentItem: Text {
            leftPadding: 9
            rightPadding: 23
            text: midiSelector.count === 0 ? "No MIDI inputs"
                  : midiSelector.currentIndex < 0 ? "Auto (MIDI in)" : midiSelector.displayText
            color: midiSelector.enabled ? Theme.text : Theme.settings.deviceTextDisabled
            font.pixelSize: 10
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideMiddle
        }
        background: Rectangle {
            radius: 5
            color: midiSelector.enabled ? Theme.settings.deviceBox : Theme.settings.deviceBoxDisabled
            border.color: midiSelector.activeFocus ? Theme.accent : Theme.settings.deviceBoxBorder
        }
    }
}
