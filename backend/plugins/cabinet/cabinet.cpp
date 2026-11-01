#include "cabinet.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
Cabinet::Cabinet(CabinetImpulse value) : impulse(std::move(value)),controls{{
    &addParameter("mix","Mix",0,1,1), &addParameter("level","Level",-24,12,0,"dB")
}} {}
void Cabinet::prepare(double sampleRate,int) {
    if(loaded()) {
        if(std::abs(impulse.sampleRate-sampleRate)>0.5)throw std::runtime_error("Cabinet IR needs resampling for this audio rate.");
        for(int c=0;c<2;++c)convolvers[c].prepare(impulse.channels[c]);
    }
    for(auto& smoother:smoothers)smoother.prepare(sampleRate,20);
    reset();
}
void Cabinet::reset() noexcept {
    for(auto& convolver:convolvers)convolver.reset();
    for(int i=0;i<2;++i)smoothers[i].reset(controls[i]->get());
}
void Cabinet::process(ProcessContext& ctx) noexcept {
    if(!loaded())return; // No IR selected: pass through rather than silence.
    for(int i=0;i<ctx.audio.frames;++i) {
        const float mix=smoothers[0].next(controls[0]->get());
        const float gain=std::pow(10.0f,smoothers[1].next(controls[1]->get())/20);
        for(int c=0;c<std::min(ctx.audio.channelCount,2);++c) {
            float& x=ctx.audio.channel(c)[i];const float wet=convolvers[c].process(x);
            x=(x*(1-mix)+wet*mix)*gain;
        }
    }
}
