#include "settings/quicksettings.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

const std::vector<int>& QuickSettings::supportedBufferSizes()
{
    static const std::vector<int> sizes = {64, 128, 256, 512, 1024};
    return sizes;
}

const std::vector<std::string>& QuickSettings::supportedBufferSizeStrings()
{
    static const std::vector<std::string> sizeStrings = {"64", "128", "256", "512", "1024"};
    return sizeStrings;
}

bool QuickSettings::isValidBufferSize(int frames)
{
    const auto& sizes = supportedBufferSizes();
    return std::find(sizes.begin(), sizes.end(), frames) != sizes.end();
}

const std::vector<std::string>& QuickSettings::snapGridModes()
{
    static const std::vector<std::string> modes = {
        "Bars", "Beats", "1/2 beat", "1/4 beat", "1/8 beat", "1/16 beat", "Off"
    };
    return modes;
}

bool QuickSettings::isValidSnapMode(int index)
{
    return index >= 0 && index < static_cast<int>(snapGridModes().size());
}

std::string QuickSettings::formatAudioStatus(
    bool audioReady,
    bool inputConnected,
    bool outputConnected,
    double sampleRate,
    int bufferFrames,
    const std::string& customError)
{
    if (!audioReady) {
        return "Audio disconnected · JACK/PipeWire not running";
    }

    if (!customError.empty()) {
        return customError;
    }

    if (!inputConnected || !outputConnected) {
        if (!inputConnected && !outputConnected) {
            return "Audio I/O disconnected";
        }
        if (!inputConnected) {
            return "Audio input disconnected";
        }
        return "Audio output disconnected";
    }

    std::ostringstream ss;
    const double khz = sampleRate / 1000.0;
    if (std::floor(khz) == khz) {
        ss << static_cast<int>(khz);
    } else {
        ss << std::fixed << std::setprecision(1) << khz;
    }
    ss << " kHz · " << bufferFrames << " frames";

    if (sampleRate > 0 && bufferFrames > 0) {
        const double ms = (static_cast<double>(bufferFrames) / sampleRate) * 1000.0;
        ss << " (" << std::fixed << std::setprecision(1) << ms << " ms)";
    }

    return ss.str();
}

bool QuickSettings::validateDeviceIndex(int index, int deviceCount)
{
    return index >= 0 && index < deviceCount;
}
