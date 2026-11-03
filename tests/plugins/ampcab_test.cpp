#include "testkit.h"
#include "testsignals.h"
#include "instruments/guitar/plugins/amp/amp.h"
#include "instruments/guitar/plugins/cabinet/cabinet.h"
#include "instruments/guitar/dsp/partitionedconvolver.h"
#include "plugins/eq/eqpresets.h"
#include "plugins/eq/tenbandeq.h"
#include <numeric>

namespace {
void set(Processor& p,const char* id,float value) { for(const auto& param:p.parameters())if(param->id==id)param->set(value); }
void convolve(PartitionedConvolver& convolution,const std::vector<float>& input,std::vector<float>& output) noexcept RT_SAFE {
    for(std::size_t i=0;i<input.size();++i)output[i]=convolution.process(input[i]);
}
double tailRms(const std::vector<float>& audio) { return testsignals::rmsDb({audio.begin()+audio.size()/2,audio.end()}); }
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
    Cabinet unloaded;CHECK(testsignals::render(unloaded,input,128)==input);
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
