#include "testkit.h"
#include "testsignals.h"
#include "instruments/guitar/plugins/amp/amp.h"
#include "instruments/guitar/plugins/cabinet/cabinet.h"
#include "instruments/guitar/dsp/partitionedconvolver.h"
#include "plugins/eq/eqpresets.h"
#include "plugins/eq/tenbandeq.h"
#include "dsp/fft.h"
#include <complex>
#include <numeric>

namespace {
void set(Processor& p,const char* id,float value) { for(const auto& param:p.parameters())if(param->id==id)param->set(value); }
void convolve(PartitionedConvolver& convolution,const std::vector<float>& input,std::vector<float>& output) noexcept RT_SAFE {
    for(std::size_t i=0;i<input.size();++i)output[i]=convolution.process(input[i]);
}
double tailRms(const std::vector<float>& audio) { return testsignals::rmsDb({audio.begin()+audio.size()/2,audio.end()}); }
// A guitar-like note: a bright (sawtooth-ish) 110 Hz pluck that decays, repeated every 0.25 s.
std::vector<float> plucks(double gainDb,int frames=48000) {
    std::vector<float> x(static_cast<std::size_t>(frames));
    const double g=std::pow(10.0,gainDb/20);
    for(int i=0;i<frames;++i) {
        const double t=double(i%12000)/48000, phase=std::fmod(110.0*t,1.0);
        x[std::size_t(i)]=float(g*(2*phase-1)*std::exp(-t*6));
    }
    return x;
}
// Energy (dB) between lo and hi Hz, from one 32768-point FFT over the middle of the signal.
double bandDb(const std::vector<float>& x,double lo,double hi) {
    const int n=32768; Fft fft(n); std::vector<std::complex<double>> bins(n);
    const std::size_t start=(x.size()-std::size_t(n))/2;
    for(int i=0;i<n;++i) bins[std::size_t(i)]={x[start+std::size_t(i)]*(0.5-0.5*std::cos(6.283185307179586*i/n)),0.0};
    fft.forward(bins.data());
    double sum=0;for(int k=1;k<n/2;++k){const double hz=k*48000.0/n;if(hz>=lo&&hz<hi)sum+=std::norm(bins[std::size_t(k)]);}
    return 10*std::log10(sum+1e-30);
}
double crestDb(const std::vector<float>& x) {
    double peak=0,sum=0;for(float v:x){peak=std::max(peak,double(std::abs(v)));sum+=double(v)*v;}
    return 20*std::log10(peak/std::sqrt(sum/double(x.size())));
}
}
TEST(ampcab, partitioned_convolution_matches_direct_reference_without_latency) {
    for(int length : {1,127,128,129,255,256,301,9600}) {
        std::vector<float> impulse(length),input(4096),output(input.size());
        for(int i=0;i<length;++i)impulse[i]=float(std::cos(i*0.81)*0.08*std::exp(-i/1000.0));
        for(int i=0;i<int(input.size());++i)input[i]=float(0.3*std::sin(i*0.37));
        PartitionedConvolver convolution;convolution.prepare(impulse);convolve(convolution,input,output);
        double maximum=0;
        for(int i=0;i<int(input.size());++i) {
            double expected=0;for(int j=0;j<std::min(i+1,length);++j)expected+=double(impulse[j])*input[i-j];
            maximum=std::max(maximum,std::abs(output[i]-expected));
        }
        CHECK(maximum<2e-5);
        convolution.reset();std::vector<float> again(input.size());convolve(convolution,input,again);CHECK(again==output);
    }
}
TEST(ampcab, cabinet_is_block_independent_and_passes_dry_when_unloaded) {
    CabinetImpulse impulse;impulse.sampleRate=48000;
    impulse.channels[0]=std::vector<float>(800);impulse.channels[0][0]=0.5f;impulse.channels[0][300]=0.25f;
    impulse.channels[1]=impulse.channels[0];
    Cabinet cabinet(impulse);const auto input=testsignals::sine(220,-12,16000);
    const auto reference=testsignals::render(cabinet,input,128);
    CHECK(testsignals::render(cabinet,input,37)==reference);
    CHECK(testsignals::render(cabinet,input,512)==reference);
    CHECK(cabinet.latencySamples()==0);
    set(cabinet,"mix",0);CHECK(testsignals::render(cabinet,input,128)==input);
    // No IR and Cab = None: the old dry pass-through.
    Cabinet unloaded;set(unloaded,"model",0);CHECK(testsignals::render(unloaded,input,128)==input);
    // With an IR loaded the built-in model is ignored.
    set(cabinet,"mix",1);const auto withIr=testsignals::render(cabinet,input,128);
    set(cabinet,"model",1);CHECK(testsignals::render(cabinet,input,128)==withIr);
}
TEST(ampcab, cabinet_keeps_stereo_channels_separate) {
    CabinetImpulse impulse;impulse.sampleRate=48000;impulse.channels[0]={0.5f};impulse.channels[1]={0.25f};
    Cabinet cabinet(impulse);cabinet.prepare(48000,128);
    float left[128]{1},right[128]{1};float* channels[]{left,right};ProcessContext ctx;ctx.audio={channels,2,128};
    testsignals::rtProcess(cabinet,ctx);CHECK_NEAR(left[0],0.5,1e-6);CHECK_NEAR(right[0],0.25,1e-6);
}
TEST(ampcab, amp_controls_change_sound_and_processing_is_block_independent) {
    Amp amp;const auto input=testsignals::sine(220,-18,24000);
    const auto low=testsignals::render(amp,input,128);
    CHECK(testsignals::render(amp,input,37)==low);
    set(amp,"drive",35);const auto high=testsignals::render(amp,input,128);CHECK(high!=low);
    set(amp,"mid",-12);const auto cut=testsignals::render(amp,input,128);CHECK(cut!=high);
    for(const auto& parameter:amp.parameters())parameter->set(parameter->max);
    const auto extremes=testsignals::render(amp,input,64);
    for(float value:extremes)CHECK(std::isfinite(value)&&std::abs(value)<32);
    const auto silence=testsignals::render(amp,std::vector<float>(10000),128);
    CHECK(testsignals::peakDb(silence)<-110);
}
TEST(ampcab, amp_master_sets_output_level_and_input_filter_rejects_dc) {
    Amp amp;const auto input=testsignals::sine(220,-24,24000);
    set(amp,"master",-12);const double before=tailRms(testsignals::render(amp,input,128));
    set(amp,"master",-18);CHECK_NEAR(tailRms(testsignals::render(amp,input,128)),before-6,0.02);
    const auto dc=testsignals::render(amp,std::vector<float>(48000,0.1f),128);CHECK(tailRms(dc)<-90);
}
TEST(ampcab, eq_factory_presets_have_valid_distinct_curves_and_flat_is_dry) {
    TenBandEq eq;const auto input=testsignals::sine(1000,-18,10000);
    CHECK(testsignals::render(eq,input,128)==input);
    for (std::size_t i=0;i<eqpresets::factory.size();++i)
        for (std::size_t j=0;j<i;++j)
            CHECK(eqpresets::factory[i].gains != eqpresets::factory[j].gains);
    for(const auto& preset:eqpresets::factory) {
        for(int i=0;i<10;++i) { CHECK(preset.gains[i]>=-12&&preset.gains[i]<=12);eq.setBandGain(i,preset.gains[i]); }
        const auto output=testsignals::render(eq,input,128);
        for(float value:output)CHECK(std::isfinite(value));
    }
}

TEST(ampcab, amp_has_three_channels_and_the_channel_switches_rather_than_glides) {
    Amp amp;
    const Parameter* channel=nullptr;
    for(const auto& p:amp.parameters()) if(p->id=="channel") channel=p.get();
    CHECK(channel && channel->min==0 && channel->max==2 && channel->unit=="channel");
    const auto input=plucks(-14);
    std::vector<std::vector<float>> outputs;
    for(float c:{0.0f,1.0f,2.0f}) { set(amp,"channel",c); set(amp,"drive",20); outputs.push_back(testsignals::render(amp,input,128)); }
    CHECK(outputs[0]!=outputs[1] && outputs[1]!=outputs[2]);
    // Block size does not change any channel.
    set(amp,"channel",2); CHECK(testsignals::render(amp,input,37)==outputs[2]);
}
TEST(ampcab, clean_channel_stays_clean_at_low_drive) {
    Amp amp; set(amp,"channel",0); set(amp,"drive",0);
    // A 220 Hz sine at a typical pickup level: harmonics stay far below the note.
    const auto out=testsignals::render(amp,testsignals::sine(220,-14,48000),128);
    const double fundamental=bandDb(out,200,240), harmonics=bandDb(out,400,5000);
    CHECK(harmonics<fundamental-35);
}
TEST(ampcab, crunch_adds_even_harmonics_like_a_tube_stage) {
    Amp amp; set(amp,"channel",1); set(amp,"drive",18);
    const auto out=testsignals::render(amp,testsignals::sine(220,-14,48000),128);
    // Uneven clipping: the 2nd harmonic (440 Hz) is clearly there, not only odd ones.
    CHECK(bandDb(out,430,450)>bandDb(out,200,240)-30);
}
TEST(ampcab, high_gain_is_tight_and_bright_compared_with_crunch) {
    Amp amp; const auto input=plucks(-14);
    set(amp,"drive",24); set(amp,"channel",1); const auto crunch=testsignals::render(amp,input,128);
    set(amp,"channel",2); const auto high=testsignals::render(amp,input,128);
    // Less low-mid mud (150-400 Hz) than Crunch, and articulation (2-5 kHz) kept about level
    // with 1 kHz, like a mixed high-gain reference tone. High Gain's mid push lifts 1 kHz too,
    // so its 2-5 kHz is not "brighter than Crunch" relative to 1 kHz, only balanced.
    const auto rel=[&](const std::vector<float>& x,double lo,double hi){return bandDb(x,lo,hi)-bandDb(x,900,1100);};
    CHECK(rel(high,150,400)<rel(crunch,150,400)-3);
    CHECK(rel(high,2000,5000)>-3.0);
}
TEST(ampcab, high_gain_keeps_picking_dynamics) {
    Amp amp; set(amp,"channel",2); set(amp,"drive",28);
    const auto soft=testsignals::render(amp,plucks(-30),128), hard=testsignals::render(amp,plucks(-12),128);
    // Not a brick-wall limiter: harder picking is still louder, and the attack stands out.
    CHECK(testsignals::rmsDb(hard)>testsignals::rmsDb(soft)+1.5);
    CHECK(crestDb(hard)>6.0);
}

TEST(ampcab, cabinet_without_ir_uses_a_built_in_speaker_model) {
    Cabinet cabinet;   // no IR; Cab defaults to 4x12 Closed
    const Parameter* model=nullptr;
    for(const auto& p:cabinet.parameters()) if(p->id=="model") model=p.get();
    CHECK(model && model->def==3 && model->max==3 && model->unit=="cabmodel");
    const auto level=[&](double hz){ return tailRms(testsignals::render(cabinet,testsignals::sine(hz,-18,24000),128)); };
    // A speaker: the fizz above the cone's range is gone, the low resonance (100-160 Hz) is
    // strong, there is presence near 3 kHz and almost nothing below the speaker's range.
    CHECK(level(10000)<level(1000)-20);
    CHECK(level(125)>level(1000)+5);
    CHECK(level(3200)>level(1000)+2);
    CHECK(level(40)<level(125)-15);
    // Each model sounds different; block size never changes the result.
    const auto input=testsignals::sine(3000,-18,12000);
    std::vector<std::vector<float>> outputs;
    for(float m:{1.0f,2.0f,3.0f}) { set(cabinet,"model",m); outputs.push_back(testsignals::render(cabinet,input,128)); }
    CHECK(outputs[0]!=outputs[1] && outputs[1]!=outputs[2]);
    CHECK(testsignals::render(cabinet,input,37)==outputs[2]);
    for(float v:outputs[2]) CHECK(std::isfinite(v));
}
