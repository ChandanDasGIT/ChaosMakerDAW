#include "testkit.h"
#include "engine/enginestatus.h"
#include "engine/audioengine.h"
#include <limits>

TEST(enginestatus, sample_rate_buffer_and_xrun_readouts)
{
    CHECK(enginestatus::sampleRate(48000.0) == "48 kHz");
    CHECK(enginestatus::sampleRate(44100.0) == "44.1 kHz");
    CHECK(enginestatus::sampleRate(88200.0) == "88.2 kHz");
    CHECK(enginestatus::sampleRate(0.0) == "\xe2\x80\x94");
    CHECK(enginestatus::sampleRate(std::numeric_limits<double>::quiet_NaN()) == "\xe2\x80\x94");

    CHECK(enginestatus::buffer(128, 48000.0) == "128 smp \xc2\xb7 2.7 ms");
    CHECK(enginestatus::buffer(1024, 44100.0) == "1024 smp \xc2\xb7 23.2 ms");
    CHECK(enginestatus::buffer(0, 48000.0) == "\xe2\x80\x94");
    CHECK(enginestatus::buffer(128, 0.0) == "\xe2\x80\x94");

    CHECK(enginestatus::bufferTooltip(128, 128, 48000.0) == "Buffer 128 frames = 2.67 ms at 48 kHz");
    CHECK(enginestatus::bufferTooltip(256, 128, 48000.0) == "Buffer 256 frames = 5.33 ms at 48 kHz (128 requested)");
    CHECK(enginestatus::bufferTooltip(0, 128, 48000.0) == "Audio device not running");

    CHECK(enginestatus::xruns(0) == "0 xruns");
    CHECK(enginestatus::xruns(1) == "1 xrun");
    CHECK(enginestatus::xruns(12) == "12 xruns");
    CHECK(enginestatus::xrunTooltip(0) == "Audio dropouts (xruns): none");
    CHECK(enginestatus::xrunTooltip(3).find("click to reset") != std::string::npos);
}

TEST(enginestatus, device_summary_reads_like_reapers_top_bar_line)
{
    CHECK(enginestatus::deviceSummary(48000.0, 128, 128, 128, "JACK")
          == "48 kHz \xc2\xb7 128 smp \xc2\xb7 ~2.7/2.7 ms \xc2\xb7 JACK");
    CHECK(enginestatus::deviceSummary(44100.0, 512, 512, 1024, "")
          == "44.1 kHz \xc2\xb7 512 smp \xc2\xb7 ~11.6/23.2 ms");
    CHECK(enginestatus::deviceSummary(0.0, 128, 128, 128, "JACK") == "Audio device closed");
    CHECK(enginestatus::deviceSummary(48000.0, 0, 0, 0, "JACK") == "Audio device closed");

    CHECK(enginestatus::deviceSummaryTooltip(48000.0, 128, 128, 128, 0)
          == "Sample rate 48 kHz \xc2\xb7 buffer 128 frames \xc2\xb7 input latency 2.67 ms, output latency 2.67 ms"
             " \xc2\xb7 click for audio settings");
    CHECK(enginestatus::deviceSummaryTooltip(48000.0, 128, 128, 128, 480).find("hardware round trip 10.00 ms")
          != std::string::npos);
    CHECK(enginestatus::deviceSummaryTooltip(0.0, 0, 0, 0, 0) == "Audio device closed \xc2\xb7 click for audio settings");
}

TEST(enginestatus, engine_counts_xruns_from_the_device_and_resets)
{
    AudioEngine engine(nullptr);
    CHECK(engine.xrunCount() == 0);
    engine.xrunOccurred();
    engine.xrunOccurred();
    CHECK(engine.xrunCount() == 2);
    engine.resetXruns();
    CHECK(engine.xrunCount() == 0);
    // Far more than the 512-entry event queue holds: the counter never saturates.
    for (int i = 0; i < 1000; ++i) engine.xrunOccurred();
    CHECK(engine.xrunCount() == 1000);
}
