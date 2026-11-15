#pragma once

#include <string>
#include <vector>

class QuickSettings {
public:
    static const std::vector<int>& supportedBufferSizes();
    static const std::vector<std::string>& supportedBufferSizeStrings();
    static bool isValidBufferSize(int frames);

    static const std::vector<std::string>& snapGridModes();
    static bool isValidSnapMode(int index);

    static std::string formatAudioStatus(
        bool audioReady,
        bool inputConnected,
        bool outputConnected,
        double sampleRate,
        int bufferFrames,
        const std::string& customError = "");

    static bool validateDeviceIndex(int index, int deviceCount);
};
