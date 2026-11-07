#include "pitchvoice.h"
#include <algorithm>
#include <cmath>

namespace { constexpr double TwoPi=6.283185307179586; }
void PitchVoice::prepare() noexcept {
    for(int i=0;i<Window;++i)window[i]=float(0.5-0.5*std::cos(TwoPi*i/Window));
    reset();
}
void PitchVoice::reset() noexcept {
    input.fill(0);output.fill(0);lastPhase.fill(0);synthesisPhase.fill(0);
    spectrum.fill({});magnitude.fill(0);frequency.fill(0);
    writePosition=readPosition=hopCount=0;
}
float PitchVoice::process(float sample,float ratio) noexcept {
    input[writePosition]=sample;writePosition=(writePosition+1)%Window;
    const float value=output[readPosition];output[readPosition]=0;
    readPosition=(readPosition+1)%int(output.size());
    if(++hopCount==Hop){render(ratio);hopCount=0;}
    return value;
}
void PitchVoice::render(float ratio) noexcept {
    ratio=std::clamp(ratio,0.5f,2.0f);
    for(int i=0;i<Window;++i)spectrum[i]=input[(writePosition+i)%Window]*window[i];
    fft.forward(spectrum.data());
    magnitude.fill(0);frequency.fill(0);
    for(int bin=0;bin<Bins;++bin) {
        const double phase=std::arg(spectrum[bin]);
        const double expected=TwoPi*bin*Hop/Window;
        const double residual=std::remainder(phase-lastPhase[bin]-expected,TwoPi);
        lastPhase[bin]=phase;
        const double angularFrequency=(TwoPi*bin/Window+residual/Hop)*ratio;
        const int destination=int(std::lround(bin*ratio));
        if(destination>=Bins || angularFrequency<0 || angularFrequency>=TwoPi*0.5)continue;
        const float amplitude=float(std::abs(spectrum[bin]));
        magnitude[destination]+=amplitude;
        frequency[destination]+=float(angularFrequency)*amplitude;
    }
    spectrum.fill({});
    for(int bin=0;bin<Bins;++bin) {
        if(magnitude[bin]>1e-12f) {
            synthesisPhase[bin]=std::remainder(synthesisPhase[bin]+Hop*double(frequency[bin])/magnitude[bin],TwoPi);
            spectrum[bin]=std::polar(double(magnitude[bin]),synthesisPhase[bin]);
        }
        if(bin==0 || bin==Window/2)spectrum[bin]={spectrum[bin].real(),0};
        else spectrum[Window-bin]=std::conj(spectrum[bin]);
    }
    fft.inverse(spectrum.data());
    // Eight Hann windows overlap; their squared weights sum to three.
    for(int i=0;i<Window;++i)
        output[(readPosition+i)%int(output.size())]+=float(spectrum[i].real())*window[i]/3.0f;
}
