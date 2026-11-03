#pragma once

#include <string>
#include <vector>
#include <utility>

struct AudioDeviceDescriptor {
    std::string name;          // System sink/source identifier
    std::string description;   // Friendly display name
    bool isDefault = false;
    bool isInput = false;
    std::vector<std::string> ports;
};

struct PortRoutingPair {
    std::string leftPort;
    std::string rightPort;
    bool isValid = false;
};

class DeviceSettings {
public:
    DeviceSettings();
    ~DeviceSettings();

    // Buffer sizes
    static const std::vector<int>& supportedBufferSizes();
    static const std::vector<std::string>& supportedBufferSizeStrings();
    static bool isValidBufferSize(int frames);

    int bufferSize() const noexcept { return m_bufferSize; }
    bool setBufferSize(int frames);

    // Device enumeration & selection
    void setOutputDevices(std::vector<AudioDeviceDescriptor> devices, int defaultIndex = 0);
    const std::vector<AudioDeviceDescriptor>& outputDevices() const noexcept { return m_outputDevices; }
    std::vector<std::string> outputDeviceDescriptions() const;
    int selectedOutputIndex() const noexcept { return m_selectedOutputIndex; }
    bool selectOutputIndex(int index);
    const AudioDeviceDescriptor* selectedOutputDevice() const;

    void setInputDevices(std::vector<AudioDeviceDescriptor> devices, int defaultIndex = 0);
    const std::vector<AudioDeviceDescriptor>& inputDevices() const noexcept { return m_inputDevices; }
    std::vector<std::string> inputDeviceDescriptions() const;
    int selectedInputIndex() const noexcept { return m_selectedInputIndex; }
    bool selectInputIndex(int index);
    const AudioDeviceDescriptor* selectedInputDevice() const;

    // Available JACK ports
    void setAvailableOutputPorts(std::vector<std::string> ports);
    const std::vector<std::string>& availableOutputPorts() const noexcept { return m_availableOutputPorts; }
    void setAvailableInputPorts(std::vector<std::string> ports);
    const std::vector<std::string>& availableInputPorts() const noexcept { return m_availableInputPorts; }

    // Port routing decisions
    PortRoutingPair resolveOutputRouting(const std::string& deviceDescription) const;
    static PortRoutingPair matchOutputPorts(const std::string& prefix, const std::vector<std::string>& ports);
    int findPreferredInputPortIndex() const;
    static int findPreferredInputIndex(const std::vector<std::string>& ports);

    // Port grouping
    static std::vector<std::pair<std::string, std::vector<std::string>>> groupPortsByDevice(const std::vector<std::string>& ports);

    // Connection & status
    bool isAudioReady() const noexcept { return m_audioReady; }
    void setAudioReady(bool ready) noexcept { m_audioReady = ready; }

    bool isOutputConnected() const noexcept { return m_outputConnected; }
    void setOutputConnected(bool connected) noexcept { m_outputConnected = connected; }

    bool isInputConnected() const noexcept { return m_inputConnected; }
    void setInputConnected(bool connected) noexcept { m_inputConnected = connected; }

    const std::string& connectionError() const noexcept { return m_connectionError; }
    void setConnectionError(const std::string& error) { m_connectionError = error; }
    void clearConnectionError() { m_connectionError.clear(); }

    std::string statusSummary(double sampleRate) const;

private:
    int m_bufferSize = 128;
    bool m_audioReady = false;
    bool m_outputConnected = false;
    bool m_inputConnected = false;
    int m_selectedOutputIndex = -1;
    int m_selectedInputIndex = -1;
    std::vector<AudioDeviceDescriptor> m_outputDevices;
    std::vector<AudioDeviceDescriptor> m_inputDevices;
    std::vector<std::string> m_availableOutputPorts;
    std::vector<std::string> m_availableInputPorts;
    std::string m_connectionError;
};
