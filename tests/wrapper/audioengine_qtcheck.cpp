#include "testkit.h"
#include "wrapper/engine/audioenginewrap/audioenginewrapper.h"
#include "audiodevice/audiodevice.h"

namespace {

class DummyOfflineDevice final : public AudioDevice {
public:
    AudioCallback* callback = nullptr;
    bool open(AudioCallback* c, int frames) override {
        callback = c;
        c->deviceChanged(48000.0, frames);
        return true;
    }
    void close() override { callback = nullptr; }
    double sampleRate() const override { return 48000.0; }
    DeviceLatency latency() const override { return {128, 128, 128}; }
    bool setBufferSize(int) override { return true; }
    std::vector<std::string> inputNames() const override { return {"dummy:in_1", "dummy:in_2"}; }
    std::vector<std::string> outputNames() const override { return {"dummy:out_1", "dummy:out_2"}; }
    bool connectInput(const std::string& source) override {
        connectedInput = source;
        return true;
    }
    int connectInputChannel(const std::string& source) override {
        return connectInput(source) ? 0 : -1;
    }
    bool connectOutput(const std::string& left, const std::string& right) override {
        connectedLeft = left;
        connectedRight = right;
        return true;
    }
    std::string lastError() const override { return {}; }

    std::string connectedInput;
    std::string connectedLeft;
    std::string connectedRight;
};

} // namespace

TEST(qtaudioengine, lifecycle_and_properties)
{
    auto device = std::make_unique<DummyOfflineDevice>();
    auto* devPtr = device.get();
    AudioEngineWrapper wrapper(std::move(device));

    CHECK(!wrapper.isAudioReady());
    CHECK(wrapper.engine() != nullptr);

    CHECK(wrapper.start(128));
    CHECK(wrapper.isAudioReady());
    CHECK(wrapper.sampleRate() == 48000.0);
    CHECK(wrapper.latencyFrames() == 128);
    CHECK(wrapper.bufferSize() == 128);

    CHECK(!wrapper.isPlaying());
    CHECK(!wrapper.isRecording());
    CHECK(!wrapper.isCountingIn());
    CHECK(wrapper.positionFrames() == 0);

    CHECK(wrapper.connectInput(QStringLiteral("dummy:in_1")));
    CHECK(devPtr->connectedInput == "dummy:in_1");
    CHECK(wrapper.connectInputChannel(QStringLiteral("dummy:in_2")) == 0);

    CHECK(wrapper.connectOutput(QStringLiteral("dummy:out_1"), QStringLiteral("dummy:out_2")));
    CHECK(devPtr->connectedLeft == "dummy:out_1");
    CHECK(devPtr->connectedRight == "dummy:out_2");

    // Metering
    auto levels = wrapper.takeMonitorLevels();
    CHECK(levels.contains(QStringLiteral("raw")));
    CHECK(levels.contains(QStringLiteral("left")));
    CHECK(levels.contains(QStringLiteral("right")));

    wrapper.clearMonitorClips();

    // Master volume and mute
    CHECK_NEAR(wrapper.engine()->masterVolume(), 1.0f, 1e-6);
    CHECK(!wrapper.engine()->masterMuted());
    wrapper.engine()->setMasterVolume(1.4f);
    CHECK_NEAR(wrapper.engine()->masterVolume(), 1.4f, 1e-6);
    wrapper.engine()->setMasterMuted(true);
    CHECK(wrapper.engine()->masterMuted());

    wrapper.stop();
    CHECK(!wrapper.isAudioReady());
}
