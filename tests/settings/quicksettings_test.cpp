#include "testkit.h"
#include "settings/quicksettings.h"

TEST(quicksettings, supported_buffer_sizes)
{
    const auto& sizes = QuickSettings::supportedBufferSizes();
    CHECK(sizes.size() == 5);
    CHECK(sizes[0] == 64);
    CHECK(sizes[1] == 128);
    CHECK(sizes[2] == 256);
    CHECK(sizes[3] == 512);
    CHECK(sizes[4] == 1024);

    CHECK(QuickSettings::isValidBufferSize(64));
    CHECK(QuickSettings::isValidBufferSize(256));
    CHECK(QuickSettings::isValidBufferSize(1024));
    CHECK(!QuickSettings::isValidBufferSize(100));
    CHECK(!QuickSettings::isValidBufferSize(0));

    const auto& strSizes = QuickSettings::supportedBufferSizeStrings();
    CHECK(strSizes.size() == 5);
    CHECK(strSizes[0] == "64");
    CHECK(strSizes[2] == "256");
}

TEST(quicksettings, snap_grid_modes)
{
    const auto& modes = QuickSettings::snapGridModes();
    CHECK(modes.size() == 7);
    CHECK(modes[0] == "Bars");
    CHECK(modes[1] == "Beats");
    CHECK(modes[6] == "Off");

    CHECK(QuickSettings::isValidSnapMode(0));
    CHECK(QuickSettings::isValidSnapMode(6));
    CHECK(!QuickSettings::isValidSnapMode(-1));
    CHECK(!QuickSettings::isValidSnapMode(7));
}

TEST(quicksettings, format_audio_status)
{
    // Disconnected engine
    CHECK(QuickSettings::formatAudioStatus(false, false, false, 48000, 256)
          == "Audio disconnected · JACK/PipeWire not running");

    // Custom error override
    CHECK(QuickSettings::formatAudioStatus(true, true, true, 48000, 256, "Channel error")
          == "Channel error");

    // Partial I/O disconnections
    CHECK(QuickSettings::formatAudioStatus(true, false, false, 48000, 256)
          == "Audio I/O disconnected");
    CHECK(QuickSettings::formatAudioStatus(true, false, true, 48000, 256)
          == "Audio input disconnected");
    CHECK(QuickSettings::formatAudioStatus(true, true, false, 48000, 256)
          == "Audio output disconnected");

    // Connected normal running state
    const auto status = QuickSettings::formatAudioStatus(true, true, true, 48000, 256);
    CHECK(status.find("48 kHz") != std::string::npos);
    CHECK(status.find("256 frames") != std::string::npos);
    CHECK(status.find("5.3 ms") != std::string::npos);
}

TEST(quicksettings, validate_device_index)
{
    CHECK(QuickSettings::validateDeviceIndex(0, 3));
    CHECK(QuickSettings::validateDeviceIndex(2, 3));
    CHECK(!QuickSettings::validateDeviceIndex(-1, 3));
    CHECK(!QuickSettings::validateDeviceIndex(3, 3));
}
