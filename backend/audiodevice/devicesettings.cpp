#include "audiodevice/devicesettings.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <map>
#include <sstream>

namespace {

std::string toLower(std::string_view s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

bool startsWithCi(std::string_view str, std::string_view prefix) {
    if (str.size() < prefix.size())
        return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(str[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    }
    return true;
}

bool containsCi(std::string_view haystack, std::string_view needle) {
    return toLower(haystack).find(toLower(needle)) != std::string::npos;
}

bool endsWithCi(std::string_view str, std::string_view suffix) {
    if (str.size() < suffix.size())
        return false;
    size_t offset = str.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(str[offset + i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i])))
            return false;
    }
    return true;
}

} // namespace

DeviceSettings::DeviceSettings() = default;
DeviceSettings::~DeviceSettings() = default;

const std::vector<int>& DeviceSettings::supportedBufferSizes() {
    static const std::vector<int> sizes = {32, 64, 128, 256, 512, 1024, 2048};
    return sizes;
}

const std::vector<std::string>& DeviceSettings::supportedBufferSizeStrings() {
    static const std::vector<std::string> strings = {"32", "64", "128", "256", "512", "1024", "2048"};
    return strings;
}

bool DeviceSettings::isValidBufferSize(int frames) {
    const auto& sizes = supportedBufferSizes();
    return std::find(sizes.begin(), sizes.end(), frames) != sizes.end();
}

bool DeviceSettings::setBufferSize(int frames) {
    if (!isValidBufferSize(frames))
        return false;
    m_bufferSize = frames;
    return true;
}

void DeviceSettings::setOutputDevices(std::vector<AudioDeviceDescriptor> devices, int defaultIndex) {
    m_outputDevices = std::move(devices);
    if (m_outputDevices.empty()) {
        m_selectedOutputIndex = -1;
    } else if (defaultIndex >= 0 && defaultIndex < static_cast<int>(m_outputDevices.size())) {
        m_selectedOutputIndex = defaultIndex;
    } else {
        m_selectedOutputIndex = 0;
    }
}

std::vector<std::string> DeviceSettings::outputDeviceDescriptions() const {
    std::vector<std::string> result;
    result.reserve(m_outputDevices.size());
    for (const auto& dev : m_outputDevices) {
        result.push_back(dev.description.empty() ? dev.name : dev.description);
    }
    return result;
}

bool DeviceSettings::selectOutputIndex(int index) {
    if (index < 0 || index >= static_cast<int>(m_outputDevices.size()))
        return false;
    m_selectedOutputIndex = index;
    return true;
}

const AudioDeviceDescriptor* DeviceSettings::selectedOutputDevice() const {
    if (m_selectedOutputIndex >= 0 && m_selectedOutputIndex < static_cast<int>(m_outputDevices.size()))
        return &m_outputDevices[m_selectedOutputIndex];
    return nullptr;
}

void DeviceSettings::setInputDevices(std::vector<AudioDeviceDescriptor> devices, int defaultIndex) {
    m_inputDevices = std::move(devices);
    if (m_inputDevices.empty()) {
        m_selectedInputIndex = -1;
    } else if (defaultIndex >= 0 && defaultIndex < static_cast<int>(m_inputDevices.size())) {
        m_selectedInputIndex = defaultIndex;
    } else {
        m_selectedInputIndex = 0;
    }
}

std::vector<std::string> DeviceSettings::inputDeviceDescriptions() const {
    std::vector<std::string> result;
    result.reserve(m_inputDevices.size());
    for (const auto& dev : m_inputDevices) {
        result.push_back(dev.description.empty() ? dev.name : dev.description);
    }
    return result;
}

bool DeviceSettings::selectInputIndex(int index) {
    if (index < 0 || index >= static_cast<int>(m_inputDevices.size()))
        return false;
    m_selectedInputIndex = index;
    return true;
}

const AudioDeviceDescriptor* DeviceSettings::selectedInputDevice() const {
    if (m_selectedInputIndex >= 0 && m_selectedInputIndex < static_cast<int>(m_inputDevices.size()))
        return &m_inputDevices[m_selectedInputIndex];
    return nullptr;
}

void DeviceSettings::setAvailableOutputPorts(std::vector<std::string> ports) {
    m_availableOutputPorts = std::move(ports);
}

void DeviceSettings::setAvailableInputPorts(std::vector<std::string> ports) {
    m_availableInputPorts = std::move(ports);
}

PortRoutingPair DeviceSettings::resolveOutputRouting(const std::string& deviceDescription) const {
    return matchOutputPorts(deviceDescription, m_availableOutputPorts);
}

PortRoutingPair DeviceSettings::matchOutputPorts(const std::string& prefix, const std::vector<std::string>& ports) {
    PortRoutingPair pair;
    const std::string colonPrefix = prefix + ":";

    std::vector<std::string> matching;
    for (const auto& port : ports) {
        if (startsWithCi(port, colonPrefix) || startsWithCi(port, prefix)) {
            matching.push_back(port);
            if (pair.leftPort.empty() && (containsCi(port, "playback_FL") || endsWithCi(port, "playback_1"))) {
                pair.leftPort = port;
            } else if (pair.rightPort.empty() && (containsCi(port, "playback_FR") || endsWithCi(port, "playback_2"))) {
                pair.rightPort = port;
            }
        }
    }

    if (pair.leftPort.empty() && !matching.empty()) {
        pair.leftPort = matching[0];
    }
    if (pair.rightPort.empty() && matching.size() > 1) {
        pair.rightPort = matching[1];
    } else if (pair.rightPort.empty() && !pair.leftPort.empty()) {
        pair.rightPort = pair.leftPort;
    }

    pair.isValid = !pair.leftPort.empty();
    return pair;
}

int DeviceSettings::findPreferredInputPortIndex() const {
    return findPreferredInputIndex(m_availableInputPorts);
}

int DeviceSettings::findPreferredInputIndex(const std::vector<std::string>& ports) {
    if (ports.empty())
        return -1;

    for (size_t i = 0; i < ports.size(); ++i) {
        if (containsCi(ports[i], "inst") || containsCi(ports[i], "guitar")) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

std::vector<std::pair<std::string, std::vector<std::string>>> DeviceSettings::groupPortsByDevice(
    const std::vector<std::string>& ports) {
    std::map<std::string, std::vector<std::string>> groups;
    for (const auto& port : ports) {
        auto colonPos = port.rfind(':');
        std::string client = (colonPos != std::string::npos) ? port.substr(0, colonPos) : "system";
        groups[client].push_back(port);
    }

    std::vector<std::pair<std::string, std::vector<std::string>>> result;
    result.reserve(groups.size());
    for (auto& entry : groups) {
        result.emplace_back(std::move(entry.first), std::move(entry.second));
    }
    return result;
}

std::string DeviceSettings::statusSummary(double sampleRate) const {
    if (!m_audioReady) {
        if (!m_connectionError.empty())
            return "Audio unavailable · " + m_connectionError;
        return "Audio unavailable";
    }

    if (!m_outputConnected) {
        if (!m_connectionError.empty())
            return m_connectionError;
        return "System output changed, but no matching JACK playback ports were found.";
    }

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << (sampleRate / 1000.0) << " kHz · " << m_bufferSize << " frames";
    return ss.str();
}
