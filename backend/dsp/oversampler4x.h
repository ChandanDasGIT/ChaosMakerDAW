#pragma once
#include "oversampler2x.h"
class Oversampler4x {
public:
    float process(float input, int channel, float (*shape)(float,void*), void* state) noexcept {
        Bridge bridge{&inner,channel,shape,state};
        return outer.process(input,channel,&processInner,&bridge);
    }
    void reset() noexcept { inner.reset(); outer.reset(); }
private:
    struct Bridge { Oversampler2x* inner; int channel; float (*shape)(float,void*); void* state; };
    static float processInner(float input, void* user) noexcept {
        const auto& b=*static_cast<Bridge*>(user);
        return b.inner->process(input,b.channel,b.shape,b.state);
    }
    Oversampler2x inner,outer;
};
