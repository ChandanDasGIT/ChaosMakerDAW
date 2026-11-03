#include "testkit.h"
#include "testsignals.h"
#include "instruments/guitar/plugins/parametriceq/parametriceq.h"
#include "instruments/guitar/plugins/fuzz/fuzz.h"
#include "instruments/guitar/plugins/phaser/phaser.h"
#include "instruments/guitar/plugins/guitarsuite/guitarsuite.h"
#include <memory>

namespace {
void set(Processor& processor, const char* id, float value) {
    for (const auto& parameter : processor.parameters())
        if (parameter->id == id) parameter->set(value);
}
double tailRms(const std::vector<float>& audio) {
    return testsignals::rmsDb({audio.begin()+audio.size()/2,audio.end()});
}
std::vector<float> render(Processor& processor, std::vector<float> audio, double bpm, int block=128) {
    processor.prepare(48000,block); processor.reset();
    for(int position=0;position<int(audio.size());position+=block) {
        float* channels[]{audio.data()+position}; ProcessContext context;
        context.audio={channels,1,std::min(block,int(audio.size())-position)}; context.bpm=bpm;
        testsignals::rtProcess(processor,context);
    }
    return audio;
}
}

TEST(extrafx, parametric_eq_is_flat_by_default_and_boosts_or_cuts_each_center) {
    ParametricEq eq;
    CHECK(testsignals::render(eq,testsignals::sine(220,-24,12000),128)==testsignals::sine(220,-24,12000));
    struct Band { const char* gain; double frequency; };
    for(const auto& band : std::vector<Band>{{"lowgain",150},{"midgain",900},{"highgain",4000}}) {
        const auto input=testsignals::sine(band.frequency,-30,24000);
        const double dry=tailRms(input);
        for(float db:{-12.0f,6.0f,12.0f}) {
            set(eq,band.gain,db);
            CHECK_NEAR(tailRms(testsignals::render(eq,input,128)),dry+db,0.05);
        }
        set(eq,band.gain,0);
    }
}

TEST(extrafx, parametric_eq_q_controls_width_and_level_controls_output) {
    ParametricEq eq; set(eq,"midgain",12); set(eq,"midq",0.3f);
    const auto input=testsignals::sine(1800,-30,24000);
    const double broad=tailRms(testsignals::render(eq,input,128));
    set(eq,"midq",4);
    const double narrow=tailRms(testsignals::render(eq,input,128)); CHECK(broad>narrow+6);
    set(eq,"level",-6);
    CHECK_NEAR(tailRms(testsignals::render(eq,input,128)),narrow-6,0.05);
}

TEST(extrafx, fuzz_sustain_tone_and_output_level_have_distinct_effects) {
    Fuzz fuzz;set(fuzz,"fuzz",0);set(fuzz,"sustain",0);
    const auto input=testsignals::sine(220,-48,24000);
    const double low=tailRms(testsignals::render(fuzz,input,128));
    set(fuzz,"sustain",1);
    CHECK(tailRms(testsignals::render(fuzz,input,128))>low+8);
    const auto high=testsignals::sine(4000,-30,24000);
    set(fuzz,"tone",0);const double dark=tailRms(testsignals::render(fuzz,high,128));
    set(fuzz,"tone",1);const double bright=tailRms(testsignals::render(fuzz,high,128));CHECK(bright>dark+12);
    set(fuzz,"level",-16);CHECK_NEAR(tailRms(testsignals::render(fuzz,high,128)),bright-6,0.03);
}

TEST(extrafx, phaser_mix_zero_is_dry_and_rate_changes_the_sweep) {
    Phaser phaser;const auto input=testsignals::sine(600,-18,24000);
    set(phaser,"mix",0);CHECK(testsignals::render(phaser,input,128)==input);
    set(phaser,"mix",0.5f);set(phaser,"rate",0.4f);
    const auto slow=testsignals::render(phaser,input,128);CHECK(slow!=input);
    set(phaser,"rate",3);CHECK(testsignals::render(phaser,input,128)!=slow);
    set(phaser,"depth",0);
    const auto stationary=testsignals::render(phaser,input,128);
    set(phaser,"rate",1);CHECK(testsignals::render(phaser,input,128)==stationary);
}

TEST(extrafx, phaser_bpm_sync_matches_manual_hz_for_all_note_divisions) {
    Phaser phaser;const auto input=testsignals::sine(600,-24,16000);
    const double beats[]{4,2,1,0.5,0.25,0.75,1.0/3};
    for(int division=0;division<7;++division) {
        for(double bpm:{60.0,120.0,137.5}) {
            set(phaser,"sync",0);set(phaser,"rate",float(bpm/(60*beats[division])));
            const auto manual=render(phaser,input,bpm);
            set(phaser,"sync",1);set(phaser,"division",float(division));
            CHECK(render(phaser,input,bpm)==manual);
        }
    }
}

TEST(extrafx, all_modules_are_block_independent_finite_and_stereo_isolated) {
    ParametricEq eq;Fuzz fuzz;Phaser phaser;
    for(Processor* processor:std::vector<Processor*>{&eq,&fuzz,&phaser}) {
        const auto input=testsignals::sine(220,-24,12000);
        const auto reference=testsignals::render(*processor,input,128);
        CHECK(testsignals::render(*processor,input,37)==reference);
        CHECK(testsignals::render(*processor,input,257)==reference);
        for(bool maximum:{false,true}) {
            for(const auto& parameter:processor->parameters()) parameter->set(maximum?parameter->max:parameter->min);
            for(double rate:{44100.0,48000.0,96000.0}) {
                processor->prepare(rate,128);
                for(int block=0;block<32;++block) {
                    float left[128],right[128]{};
                    for(int i=0;i<128;++i)left[i]=float(0.1*std::sin(testsignals::kTwoPi*220*(block*128+i)/rate));
                    float* channels[]{left,right};ProcessContext context;
                    context.audio={channels,2,128};context.sampleRate=rate;context.bpm=10000;
                    testsignals::rtProcess(*processor,context);
                    for(int i=0;i<128;++i){CHECK(std::isfinite(left[i])&&std::abs(left[i])<128);CHECK(right[i]==0);}
                }
            }
        }
        CHECK(testsignals::peakDb(testsignals::render(*processor,std::vector<float>(6000),128))<-110);
    }
}

TEST(extrafx, post_gain_eq_placement_changes_the_distorted_tone) {
    auto fuzz=std::make_shared<PedalSlot>();fuzz->processor=std::make_shared<Fuzz>();
    auto eq=std::make_shared<PedalSlot>();eq->processor=std::make_shared<ParametricEq>();
    set(*eq->processor,"midfreq",1000);set(*eq->processor,"midgain",-12);
    const auto input=testsignals::sine(1000,-18,24000);
    GuitarSuite before({eq,fuzz}),after({fuzz,eq});
    const auto first=testsignals::render(before,input,128),second=testsignals::render(after,input,128);
    CHECK(first!=second);CHECK(tailRms(first)>tailRms(second)+3);
}

TEST(extrafx, parameter_changes_and_slot_bypass_are_smoothed) {
    ParametricEq eq;Fuzz fuzz;Phaser phaser;
    for(Processor* processor:std::vector<Processor*>{&eq,&fuzz,&phaser}) {
        processor->prepare(48000,128);
        auto output=testsignals::sine(220,-30,8000);
        for(int position=0;position<int(output.size());position+=128) {
            if(position==4096)set(*processor,"level",6);
            float* channels[]{output.data()+position};ProcessContext context;
            context.audio={channels,1,std::min(128,int(output.size())-position)};
            testsignals::rtProcess(*processor,context);
        }
        CHECK(std::abs(output[4096]-output[4095])<0.03);
    }
    auto slot=std::make_shared<PedalSlot>();slot->processor=std::make_shared<Fuzz>();
    GuitarSuite suite({slot});suite.prepare(48000,128);
    auto input=testsignals::sine(220,-18,128);
    float samples[128];float* channels[]{samples};ProcessContext context;context.audio={channels,1,128};
    for(int block=0;block<70;++block) {
        if(block==1)slot->bypassed.store(true);
        std::copy(input.begin(),input.end(),samples);testsignals::rtProcess(suite,context);
    }
    for(int i=0;i<128;++i)CHECK_NEAR(samples[i],input[i],1e-5);
}
