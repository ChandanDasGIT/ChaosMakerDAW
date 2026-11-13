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
        font.pixelSize: Theme.microSize
        font.bold: true
    }

    ThemedComboBox {
        id: bufferSelector
        objectName: "bufferSizeSelector"
        Layout.preferredWidth: 82
        Layout.preferredHeight: 30
        displayText: currentIndex < 0 ? "\u2014" : currentText
        model: EngineController.bufferSizes
        enabled: EngineController.selectedBufferSize > 0
        currentIndex: model.indexOf(String(EngineController.selectedBufferSize))
        onActivated: index => deviceBridge.setBufferSize(Number(currentText))
        Accessible.name: "Buffer size"
    }

    Text {
        text: "OUTPUT DEVICE"
        color: Theme.textMuted
        font.pixelSize: Theme.microSize
        font.bold: true
    }

    ThemedComboBox {
        id: outputSelector
        objectName: "outputDeviceSelector"
        Layout.fillWidth: true
        Layout.minimumWidth: 220
        Layout.preferredHeight: 30
        displayText: currentIndex < 0 ? "\u2014" : currentText
        model: EngineController.outputDevices
        enabled: count > 0
        currentIndex: EngineController.selectedOutputIndex
        onActivated: index => EngineController.setOutputDevice(index)
        Accessible.name: "Output device"
    }

    Text {
        text: "MIDI IN"
        color: Theme.textMuted
        font.pixelSize: Theme.microSize
        font.bold: true
    }

    ThemedComboBox {
        id: midiSelector
        objectName: "midiInputSelector"
        Layout.fillWidth: true
        Layout.minimumWidth: 220
        Layout.preferredHeight: 30
        displayText: currentIndex < 0 ? "\u2014" : currentText
        model: EngineController.midiInputPorts
        enabled: count > 0
        currentIndex: model.indexOf(EngineController.connectedMidiPort)
        onActivated: index => {
            EngineController.connectMidiInput(currentText)
        }
        Accessible.name: "MIDI input"
    }
}
