#include "amp.h"
Amp::Amp() : controls{{
    &addParameter("drive","Drive",0,40,14,"dB"),
    &addParameter("bass","Bass",-12,12,0,"dB"),
    &addParameter("mid","Mid",-12,12,0,"dB"),
    &addParameter("treble","Treble",-12,12,0,"dB"),
    &addParameter("presence","Presence",0,1,0.3f),
    &addParameter("master","Master",-36,6,-12,"dB")
}} {}
void Amp::prepare(double sampleRate,int) {
    rate=std::max(sampleRate,1000.0);
    for(auto& smoother:smoothers) smoother.prepare(rate,20);
    reset();
}
void Amp::reset() noexcept {
    for(std::size_t i=0;i<controls.size();++i) smoothers[i].reset(controls[i]->get());
    state={};
    for(auto& s:state) s.dcCoefficient=std::exp(-2*pedal::pi*20/float(rate*4));
    inputPrevious.fill(0);inputHighpass.fill(0);low.fill(0);high.fill(0);outputLowpass.fill(0);
    envelope=0;oversampler.reset();
}
float Amp::shape(float x,void* user) noexcept {
    auto& s=*static_cast<State*>(user);
    x*=s.drive;
    for(int stage=0;stage<2;++stage) {
        const float bias=stage==0?0.12f:0.06f;
        const float shaped=std::tanh(x+bias)-std::tanh(bias);
        const float dc=shaped-s.previous[stage]+s.dcCoefficient*s.dc[stage];
        s.previous[stage]=shaped;s.dc[stage]=dc;
        x=dc*(stage==0?1.8f:1.0f);
    }
    return x;
}
void Amp::process(ProcessContext& ctx) noexcept {
    const float hp=std::exp(-2*pedal::pi*75/float(rate));
    const float lo=1-std::exp(-2*pedal::pi*250/float(rate));
    const float hi=1-std::exp(-2*pedal::pi*2500/float(rate));
    const float cut=1-std::exp(-2*pedal::pi*float(std::min(8000.0,rate*0.4))/float(rate));
    for(int i=0;i<ctx.audio.frames;++i) {
        std::array<float,6> v;
        for(std::size_t j=0;j<v.size();++j)v[j]=smoothers[j].next(controls[j]->get());
        const float peak=pedal::peak(ctx,i);
        envelope+=pedal::coefficient(rate,peak>envelope?10:120)*(peak-envelope);
        const float drive=pedal::gain(v[0])/(1+0.25f*envelope);
        const float bass=pedal::gain(v[1]),mid=pedal::gain(v[2]);
        const float treble=pedal::gain(v[3]+v[4]*6),master=pedal::gain(v[5]);
        for(int c=0;c<std::min(ctx.audio.channelCount,2);++c) {
            float& x=ctx.audio.channel(c)[i];
            const float filtered=hp*(inputHighpass[c]+x-inputPrevious[c]);
            inputPrevious[c]=x;inputHighpass[c]=filtered;state[c].drive=drive;
            const float saturated=oversampler.process(filtered,c,&Amp::shape,&state[c]);
            low[c]+=lo*(saturated-low[c]);high[c]+=hi*(saturated-high[c]);
            const float tone=low[c]*bass+(high[c]-low[c])*mid+(saturated-high[c])*treble;
            outputLowpass[c]+=cut*(tone-outputLowpass[c]);
            x=outputLowpass[c]*master;
        }
    }
}
