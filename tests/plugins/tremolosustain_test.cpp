#include "testkit.h"
#include "testsignals.h"
#include "plugins/tremolo/tremolo.h"
namespace {
void set(Tremolo& p,const char* id,float value) { for(const auto& v:p.parameters()) if(v->id==id) v->set(value); }
std::vector<float> render(int block, bool sustain) {
    Tremolo t; set(t,"sustain",sustain); set(t,"hold",180); set(t,"fade",200); set(t,"depth",1);
    t.prepare(48000,block); std::vector<float> data(48000*3);
    // A first note, a louder re-pick without silence, then silence and a new note.
    for(int i=0;i<int(data.size());++i) {
        if(i<48000) data[i]=0.2f;
        else if(i<96000) data[i]=0.7f;
        else if(i>=108000) data[i]=0.2f;
    }
    for(int i=0;i<int(data.size());i+=block) {
        float* ch[]{data.data()+i}; ProcessContext ctx;
        ctx.audio={ch,1,std::min(block,int(data.size())-i)};
        testsignals::rtProcess(t,ctx);
    }
    return data;
}
}
TEST(tremolosustain, attack_stays_clear_sustain_modulates_and_new_attacks_retrigger) {
    const auto audio=render(128,true);
    CHECK_NEAR(audio[2400],0.2,1e-5);
    float low=1,high=0;
    for(int i=24000;i<48000;++i) { low=std::min(low,audio[i]); high=std::max(high,audio[i]); }
    CHECK(low<0.01); CHECK(high>0.19);
    CHECK_NEAR(audio[48000+4800],0.7,0.001);
    CHECK_NEAR(audio[108000+2400],0.2,0.001);
    for(int i=96000;i<108000;++i) CHECK(audio[i]==0);
    CHECK(audio==render(257,true));
    CHECK(render(128,false)[2400]<0.15);
}
TEST(tremolosustain, low_guitar_notes_retrigger_after_decay_and_noise_stays_dry) {
    Tremolo t; set(t,"sustain",1); set(t,"depth",1); t.prepare(48000,128);
    std::vector<float> dry(96000), wet;
    for(int i=0;i<96000;++i) {
        const int age=i%48000;
        dry[i]=float(0.6*std::exp(-double(age)/16000)*std::sin(2*3.141592653589793*82.4*i/48000));
    }
    wet=dry;
    for(int i=0;i<96000;i+=128) {
        float* ch[]{wet.data()+i}; ProcessContext ctx; ctx.audio={ch,1,std::min(128,96000-i)};
        testsignals::rtProcess(t,ctx);
    }
    for(int i=2400;i<4800;++i) CHECK_NEAR(wet[i],dry[i],1e-5);
    double difference=0;
    for(int i=24000;i<36000;++i) difference+=std::abs(wet[i]-dry[i]);
    CHECK(difference>100);
    for(int i=48000+4800;i<48000+6000;++i) CHECK_NEAR(wet[i],dry[i],0.001);
    t.reset();
    float noise[128]; std::fill_n(noise,128,0.0001f); float* ch[]{noise};
    ProcessContext ctx; ctx.audio={ch,1,128}; testsignals::rtProcess(t,ctx);
    for(float v:noise) CHECK_NEAR(v,0.0001,1e-8);
}
