import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

RowLayout {
    id: root
    spacing: 12

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
        Layout.preferredWidth: 82
        Layout.preferredHeight: 30
        model: EngineController.bufferSizes
        enabled: EngineController.selectedBufferSize > 0
        currentIndex: model.indexOf(String(EngineController.selectedBufferSize))
        onActivated: index => deviceBridge.setBufferSize(Number(currentText))
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

    Text {
        text: "MIDI IN"
        color: Theme.textMuted
        font.pixelSize: 9
        font.bold: true
    }

    ComboBox {
        id: midiSelector
        Layout.preferredWidth: 200
        Layout.preferredHeight: 30
        model: EngineController.midiInputPorts
        enabled: count > 0
        currentIndex: model.indexOf(EngineController.connectedMidiPort)
        onActivated: index => {
            EngineController.connectMidiInput(currentText)
        }
        contentItem: Text {
            leftPadding: 9
            rightPadding: 23
            text: midiSelector.count === 0 ? "No MIDI inputs"
                  : midiSelector.currentIndex < 0 ? "Auto (MIDI in)" : midiSelector.displayText
            color: midiSelector.enabled ? Theme.text : "#777b83"
            font.pixelSize: 10
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideMiddle
        }
        background: Rectangle {
            radius: 5
            color: midiSelector.enabled ? "#292b30" : "#222328"
            border.color: midiSelector.activeFocus ? Theme.accent : "#444750"
        }
    }
}
