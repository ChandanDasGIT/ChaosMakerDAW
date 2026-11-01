#include "testkit.h"
#include "testsignals.h"
#include "plugins/octaver/octaver.h"
#include "plugins/pitchshift/pitchshift.h"
#include <memory>

namespace {
void set(Processor& p,const char* id,float value) { for(const auto& parameter:p.parameters())if(parameter->id==id)parameter->set(value); }
double power(const std::vector<float>& audio,double frequency,double rate=48000) {
    double re=0,im=0;
    for(std::size_t i=8192;i<audio.size();++i) {
        const double phase=testsignals::kTwoPi*frequency*i/rate;
        re+=audio[i]*std::cos(phase);im+=audio[i]*std::sin(phase);
    }
    return re*re+im*im;
}
void checkPitch(const std::vector<float>& audio,double expected,double source) {
    double best=-1,frequency=0;
    for(double offset=-3;offset<=3;offset+=0.5) {
        const double energy=power(audio,expected+offset);
        if(energy>best){best=energy;frequency=expected+offset;}
    }
    CHECK_NEAR(frequency,expected,1.0);
    CHECK(best>power(audio,source)*10);
    CHECK(best>1);
}
}
TEST(pitch, semitone_transposition_moves_the_fundamental_in_both_directions) {
    PitchShift shift;set(shift,"mix",1);
    const auto input=testsignals::sine(220,-18,48000);
    for(float semitones:{-12.0f,-5.0f,7.0f,12.0f}) {
        set(shift,"semitones",semitones);
        checkPitch(testsignals::render(shift,input,128),220*std::pow(2.0,semitones/12),220);
    }
}
TEST(pitch, octaver_produces_both_octaves_and_keeps_dry_available) {
    Octaver octave;const auto input=testsignals::sine(220,-18,48000);
    set(octave,"dry",0);set(octave,"down",1);set(octave,"up",0);
    checkPitch(testsignals::render(octave,input,128),110,220);
    set(octave,"down",0);set(octave,"up",1);
    checkPitch(testsignals::render(octave,input,128),440,220);
    set(octave,"up",0);set(octave,"dry",1);
    CHECK(testsignals::render(octave,input,128)==input);
}
TEST(pitch, zero_mix_or_zero_semitones_is_dry_and_reset_clears_tails) {
    PitchShift shift;const auto input=testsignals::sine(220,-18,16000);
    set(shift,"mix",0);CHECK(testsignals::render(shift,input,128)==input);
    set(shift,"mix",1);set(shift,"semitones",0);CHECK(testsignals::render(shift,input,128)==input);
    set(shift,"semitones",7);testsignals::render(shift,input,128);
    const auto silence=testsignals::render(shift,std::vector<float>(12000),128);
    CHECK(testsignals::peakDb(silence)<-110);
}
TEST(pitch, processors_are_block_independent_and_finite_at_extreme_settings) {
    Octaver octave;PitchShift shift;
    for(Processor* processor:std::vector<Processor*>{&octave,&shift}) {
        const auto input=testsignals::sine(220,-12,16000);
        const auto reference=testsignals::render(*processor,input,128);
        CHECK(testsignals::render(*processor,input,37)==reference);
        CHECK(testsignals::render(*processor,input,257)==reference);
        for(const auto& parameter:processor->parameters())parameter->set(parameter->max);
        const auto output=testsignals::render(*processor,input,64);
        for(float sample:output)CHECK(std::isfinite(sample)&&std::abs(sample)<16);
    }
}
TEST(pitch, stereo_pitch_voices_do_not_leak_between_channels) {
    PitchShift shift;set(shift,"mix",1);set(shift,"semitones",-12);shift.prepare(48000,128);
    auto left=testsignals::sine(220,-18,48000);auto right=testsignals::sine(330,-18,48000);
    for(int pos=0;pos<int(left.size());pos+=128) {
        float* channels[]{left.data()+pos,right.data()+pos};ProcessContext ctx;
        ctx.audio={channels,2,std::min(128,int(left.size())-pos)};testsignals::rtProcess(shift,ctx);
    }
    checkPitch(left,110,165);checkPitch(right,165,110);
}
TEST(pitch, upward_shift_discards_frequencies_that_would_cross_nyquist) {
    PitchShift shift;set(shift,"mix",1);set(shift,"semitones",12);
    const auto audio=testsignals::render(shift,testsignals::sine(18000,-18,24000),128);
    CHECK(testsignals::rmsDb({audio.begin()+8192,audio.end()}) < -70);
}
TEST(pitch, octave_voices_track_low_and_high_open_strings) {
    Octaver octave;set(octave,"dry",0);
    for(double source:{82.407,329.628}) {
        const auto input=testsignals::sine(source,-18,48000);
        set(octave,"down",1);set(octave,"up",0);
        checkPitch(testsignals::render(octave,input,128),source*0.5,source);
        set(octave,"down",0);set(octave,"up",1);
        checkPitch(testsignals::render(octave,input,128),source*2,source);
    }
}
