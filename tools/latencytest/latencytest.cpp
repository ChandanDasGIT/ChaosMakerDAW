// latencytest: measures the audio interface's round-trip latency through JACK. Not part of
// the app. Connect a cable from an output of the interface to an input (loopback), turn any
// direct monitoring off, then:
//
//   latencytest [--out <port>] [--in <port>] [--runs <n>]
//   latencytest --list
//
// --out / --in name the hardware ports to use (default: the first physical playback and
// capture ports, e.g. system:playback_1 and system:capture_1); --list prints them all.
// Each run plays a short click (backend dsp/latencymeasure) and finds it in the recording.
// Prints each run and the median, in samples and ms, next to the latency JACK reports for
// the two ports (playback + capture). The difference is what the reports leave out, i.e.
// what recording would need to shift takes by to line up with playback.

#include "dsp/latencymeasure.h"

#include <jack/jack.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Run {
    jack_port_t* out = nullptr;
    jack_port_t* in = nullptr;
    std::vector<float> click;
    std::vector<float> recorded;          // preallocated; written by the process callback
    std::atomic<std::int64_t> frame{0};   // frames since this run started
    std::atomic<std::size_t> written{0};  // recorded frames
    std::int64_t clickAt = 0;             // frame the click starts on (after a short settle)
    std::atomic<bool> armed{false};
};

int process(jack_nframes_t frames, void* arg)
{
    auto* run = static_cast<Run*>(arg);
    auto* out = static_cast<float*>(jack_port_get_buffer(run->out, frames));
    const auto* in = static_cast<const float*>(jack_port_get_buffer(run->in, frames));
    std::memset(out, 0, sizeof(float) * frames);
    if (!run->armed.load(std::memory_order_acquire)) return 0;

    const std::int64_t start = run->frame.load(std::memory_order_relaxed);
    for (jack_nframes_t i = 0; i < frames; ++i) {
        const std::int64_t k = start + i - run->clickAt;
        if (k >= 0 && k < std::int64_t(run->click.size())) out[i] = run->click[std::size_t(k)];
    }
    std::size_t w = run->written.load(std::memory_order_relaxed);
    const std::size_t room = run->recorded.size() - w;
    const std::size_t n = std::min<std::size_t>(room, frames);
    std::memcpy(run->recorded.data() + w, in, sizeof(float) * n);
    run->written.store(w + n, std::memory_order_release);
    run->frame.store(start + frames, std::memory_order_relaxed);
    return 0;
}

std::vector<std::string> physicalPorts(jack_client_t* client, unsigned long flags)
{
    std::vector<std::string> names;
    if (const char** ports = jack_get_ports(client, nullptr, JACK_DEFAULT_AUDIO_TYPE, JackPortIsPhysical | flags)) {
        for (int i = 0; ports[i]; ++i) names.emplace_back(ports[i]);
        jack_free(ports);
    }
    return names;
}

jack_nframes_t reportedLatency(jack_client_t* client, const std::string& name, jack_latency_callback_mode_t mode)
{
    jack_latency_range_t range{0, 0};
    if (jack_port_t* port = jack_port_by_name(client, name.c_str())) jack_port_get_latency_range(port, mode, &range);
    return range.max;
}

int usage()
{
    std::fprintf(stderr, "usage: latencytest [--out <port>] [--in <port>] [--runs <n>]\n       latencytest --list\n");
    return 2;
}

}

int main(int argc, char** argv)
{
    std::string outName, inName;
    int runs = 5;
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--list") list = true;
        else if (arg == "--out" && i + 1 < argc) outName = argv[++i];
        else if (arg == "--in" && i + 1 < argc) inName = argv[++i];
        else if (arg == "--runs" && i + 1 < argc) runs = std::atoi(argv[++i]);
        else return usage();
    }
    if (runs < 1 || runs > 100) return usage();

    jack_status_t status{};
    jack_client_t* client = jack_client_open("latencytest", JackNoStartServer, &status);
    if (!client) {
        std::fprintf(stderr, "latencytest: cannot reach the JACK server (status 0x%x)\n", unsigned(status));
        return 1;
    }
    const auto playback = physicalPorts(client, JackPortIsInput);   // hardware outputs take our audio in
    const auto capture = physicalPorts(client, JackPortIsOutput);
    if (list) {
        std::printf("playback ports (--out):\n");
        for (const auto& p : playback) std::printf("  %s\n", p.c_str());
        std::printf("capture ports (--in):\n");
        for (const auto& p : capture) std::printf("  %s\n", p.c_str());
        jack_client_close(client);
        return 0;
    }
    if (outName.empty() && !playback.empty()) outName = playback.front();
    if (inName.empty() && !capture.empty()) inName = capture.front();
    if (outName.empty() || inName.empty()) {
        std::fprintf(stderr, "latencytest: no physical ports found; name them with --out / --in\n");
        jack_client_close(client);
        return 1;
    }

    const double rate = jack_get_sample_rate(client);
    const jack_nframes_t period = jack_get_buffer_size(client);
    Run run;
    run.click = latencymeasure::makeClick(rate);
    run.clickAt = static_cast<std::int64_t>(rate * 0.25);           // let the connections settle
    run.recorded.assign(static_cast<std::size_t>(rate * 1.25), 0.0f); // up to 1 s of round trip
    run.out = jack_port_register(client, "click_out", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    run.in = jack_port_register(client, "loop_in", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    if (!run.out || !run.in || jack_set_process_callback(client, process, &run) != 0 || jack_activate(client) != 0) {
        std::fprintf(stderr, "latencytest: cannot set up the JACK client\n");
        jack_client_close(client);
        return 1;
    }
    if (jack_connect(client, jack_port_name(run.out), outName.c_str()) != 0
        || jack_connect(client, inName.c_str(), jack_port_name(run.in)) != 0) {
        std::fprintf(stderr, "latencytest: cannot connect %s -> loopback -> %s\n", outName.c_str(), inName.c_str());
        jack_client_close(client);
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const jack_nframes_t outLatency = reportedLatency(client, outName, JackPlaybackLatency);
    const jack_nframes_t inLatency = reportedLatency(client, inName, JackCaptureLatency);
    const auto ms = [&](double samples) { return samples * 1000.0 / rate; };
    std::printf("%s -> %s, %.0f Hz, %u frames per period\n", outName.c_str(), inName.c_str(), rate, period);
    std::printf("JACK reports: playback %u + capture %u = %u samples (%.2f ms)\n",
                outLatency, inLatency, outLatency + inLatency, ms(outLatency + inLatency));

    std::vector<std::int64_t> measured;
    for (int r = 0; r < runs; ++r) {
        run.armed.store(false, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));   // a cycle or more: callback idle
        run.frame.store(0);
        run.written.store(0);
        std::fill(run.recorded.begin(), run.recorded.end(), 0.0f);
        run.armed.store(true, std::memory_order_release);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (run.written.load(std::memory_order_acquire) < run.recorded.size()
               && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        run.armed.store(false, std::memory_order_release);

        std::vector<float> after(run.recorded.begin() + run.clickAt, run.recorded.end());
        const auto result = latencymeasure::findDelay(run.click, after);
        if (!result.found) {
            std::printf("run %d: click not found (confidence %.2f) - check the loopback cable and levels\n",
                        r + 1, result.confidence);
            continue;
        }
        measured.push_back(result.delaySamples);
        std::printf("run %d: %lld samples (%.2f ms), confidence %.2f%s\n", r + 1,
                    static_cast<long long>(result.delaySamples), ms(double(result.delaySamples)),
                    result.confidence, result.inverted ? ", polarity inverted" : "");
    }
    jack_client_close(client);

    if (measured.empty()) {
        std::printf("no run found the click\n");
        return 1;
    }
    std::sort(measured.begin(), measured.end());
    const std::int64_t median = measured[measured.size() / 2];
    const std::int64_t extra = median - std::int64_t(outLatency + inLatency);
    std::printf("measured round trip (median of %zu): %lld samples (%.2f ms)\n", measured.size(),
                static_cast<long long>(median), ms(double(median)));
    std::printf("not in JACK's report: %lld samples (%.2f ms)\n", static_cast<long long>(extra), ms(double(extra)));
    return 0;
}
