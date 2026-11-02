#include "testkit.h"
#include "core/audiodata.h"
TEST(waveform, cache_preserves_stereo_peaks_offsets_and_silent_extensions) {
    AudioData audio; audio.frames=1024; audio.channels={std::vector<float>(1024),std::vector<float>(1024)};
    audio.channels[0][255]=-0.7f; audio.channels[1][256]=0.9f; audio.buildWaveform();
    auto peaks=audio.waveform(0,1024,4); CHECK(peaks[0][0]==-0.7f); CHECK(peaks[1][1]==0.9f);
    peaks=audio.waveform(256,512,1); CHECK(peaks[0][0]==0); CHECK(peaks[0][1]==0.9f);
    peaks=audio.waveform(1024,2048,4); for(auto p:peaks) CHECK(p[0]==0 && p[1]==0);
    auto cached=audio.waveform(213,891,17); audio.peaks.clear(); CHECK(audio.waveform(213,891,17)==cached);
}
