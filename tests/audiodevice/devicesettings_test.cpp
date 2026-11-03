#include "testkit.h"
#include "audiodevice/devicesettings.h"

TEST(devicesettings, buffer_sizes)
{
    DeviceSettings settings;
    CHECK(DeviceSettings::isValidBufferSize(128));
    CHECK(DeviceSettings::isValidBufferSize(32));
    CHECK(DeviceSettings::isValidBufferSize(2048));
    CHECK(!DeviceSettings::isValidBufferSize(100));

    CHECK(settings.bufferSize() == 128);
    CHECK(settings.setBufferSize(256));
    CHECK(settings.bufferSize() == 256);
    CHECK(!settings.setBufferSize(999));
    CHECK(settings.bufferSize() == 256);
}

TEST(devicesettings, device_enumeration_and_selection)
{
    DeviceSettings settings;

    std::vector<AudioDeviceDescriptor> devices = {
        {"sink1", "Built-in Audio", false, false, {"sink1:playback_1", "sink1:playback_2"}},
        {"sink2", "USB Interface", true, false, {"sink2:playback_FL", "sink2:playback_FR"}}
    };

    settings.setOutputDevices(devices, 1);
    CHECK(settings.outputDevices().size() == 2);
    CHECK(settings.selectedOutputIndex() == 1);
    CHECK(settings.selectedOutputDevice() != nullptr);
    CHECK(settings.selectedOutputDevice()->name == "sink2");

    auto desc = settings.outputDeviceDescriptions();
    CHECK(desc.size() == 2);
    CHECK(desc[0] == "Built-in Audio");
    CHECK(desc[1] == "USB Interface");

    CHECK(settings.selectOutputIndex(0));
    CHECK(settings.selectedOutputIndex() == 0);
    CHECK(settings.selectedOutputDevice()->name == "sink1");
    CHECK(!settings.selectOutputIndex(5));
}

TEST(devicesettings, port_routing_matching)
{
    std::vector<std::string> ports = {
        "Built-in Audio:playback_FL",
        "Built-in Audio:playback_FR",
        "USB Interface:playback_1",
        "USB Interface:playback_2",
        "OtherDevice:out_left"
    };

    auto pair1 = DeviceSettings::matchOutputPorts("Built-in Audio", ports);
    CHECK(pair1.isValid);
    CHECK(pair1.leftPort == "Built-in Audio:playback_FL");
    CHECK(pair1.rightPort == "Built-in Audio:playback_FR");

    auto pair2 = DeviceSettings::matchOutputPorts("USB Interface", ports);
    CHECK(pair2.isValid);
    CHECK(pair2.leftPort == "USB Interface:playback_1");
    CHECK(pair2.rightPort == "USB Interface:playback_2");

    auto pairNone = DeviceSettings::matchOutputPorts("NonExistent", ports);
    CHECK(!pairNone.isValid);
}

TEST(devicesettings, preferred_input_selection)
{
    std::vector<std::string> ports = {
        "system:capture_1",
        "system:capture_2",
        "system:guitar_in",
        "system:line_in"
    };

    int idx = DeviceSettings::findPreferredInputIndex(ports);
    CHECK(idx == 2);

    std::vector<std::string> genericPorts = {
        "system:capture_1",
        "system:capture_2"
    };
    CHECK(DeviceSettings::findPreferredInputIndex(genericPorts) == 0);
    CHECK(DeviceSettings::findPreferredInputIndex({}) == -1);
}

TEST(devicesettings, port_grouping)
{
    std::vector<std::string> ports = {
        "hw:0:playback_1",
        "hw:0:playback_2",
        "hw:1:playback_1"
    };

    auto groups = DeviceSettings::groupPortsByDevice(ports);
    CHECK(groups.size() == 2);
    CHECK(groups[0].first == "hw:0");
    CHECK(groups[0].second.size() == 2);
    CHECK(groups[1].first == "hw:1");
    CHECK(groups[1].second.size() == 1);
}

TEST(devicesettings, status_summary)
{
    DeviceSettings settings;
    settings.setBufferSize(128);

    // Audio not ready
    CHECK(settings.statusSummary(48000.0) == "Audio unavailable");
    settings.setConnectionError("Device busy");
    CHECK(settings.statusSummary(48000.0) == "Audio unavailable · Device busy");

    // Audio ready, output not connected
    settings.setAudioReady(true);
    settings.setConnectionError("No ports");
    CHECK(settings.statusSummary(48000.0) == "No ports");

    // Connected
    settings.setOutputConnected(true);
    CHECK(settings.statusSummary(48000.0) == "48.0 kHz · 128 frames");
}
