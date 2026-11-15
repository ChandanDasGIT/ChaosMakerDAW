#include "pitchshift.h"
PitchShift::PitchShift() : controls{{
    &addParameter("semitones","Semitones",-12,12,7,"st"),
    &addParameter("mix","Mix",0,1,0.5f),
    &addParameter("level","Level",-24,6,0,"dB")
}} {}
void PitchShift::prepare(double rate,int) {
    for(auto& voice:voices)voice.prepare();
    for(auto& smoother:smoothers)smoother.prepare(rate,20);
    reset();
}
void PitchShift::reset() noexcept {
    for(auto& voice:voices)voice.reset();
    for(int i=0;i<3;++i)smoothers[i].reset(controls[i]->get());
}
void PitchShift::process(ProcessContext& ctx) noexcept {
    for(int i=0;i<ctx.audio.frames;++i) {
        const float semitones=smoothers[0].next(std::round(controls[0]->get()));
        const float ratio=std::pow(2.0f,semitones/12);
        const float mix=smoothers[1].next(controls[1]->get())*std::min(1.0f,std::abs(semitones));
        const float gain=pedal::gain(smoothers[2].next(controls[2]->get()));
        for(int c=0;c<std::min(ctx.audio.channelCount,2);++c) {
            float& sample=ctx.audio.channel(c)[i];
            const float wet=voices[c].process(sample,ratio);
            sample=(sample*(1-mix)+wet*mix)*gain;
        }
    }
}
