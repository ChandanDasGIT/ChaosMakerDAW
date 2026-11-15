#include "octaver.h"
Octaver::Octaver() : controls{{
    &addParameter("dry","Dry",0,1,1),
    &addParameter("down","Octave down",0,1,0.5f),
    &addParameter("up","Octave up",0,1,0),
    &addParameter("level","Level",-24,6,0,"dB")
}} {}
void Octaver::prepare(double rate,int) {
    for(auto& voice:down)voice.prepare();
    for(auto& voice:up)voice.prepare();
    for(auto& smoother:smoothers)smoother.prepare(rate,20);
    reset();
}
void Octaver::reset() noexcept {
    for(auto& voice:down)voice.reset();
    for(auto& voice:up)voice.reset();
    for(int i=0;i<4;++i)smoothers[i].reset(controls[i]->get());
}
void Octaver::process(ProcessContext& ctx) noexcept {
    for(int i=0;i<ctx.audio.frames;++i) {
        std::array<float,4> v;for(int j=0;j<4;++j)v[j]=smoothers[j].next(controls[j]->get());
        const float gain=pedal::gain(v[3])/std::max(1.0f,v[0]+v[1]+v[2]);
        for(int c=0;c<std::min(ctx.audio.channelCount,2);++c) {
            float& sample=ctx.audio.channel(c)[i];
            const float lower=down[c].process(sample,0.5f),upper=up[c].process(sample,2);
            sample=(sample*v[0]+lower*v[1]+upper*v[2])*gain;
        }
    }
}
