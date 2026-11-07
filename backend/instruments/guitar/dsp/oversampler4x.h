#pragma once
#include "oversampler2x.h"
// 4x = a steep 2x step at the base rate around a light 2x step inside it (see oversampler2x.h).
// Latency: 23 + 3.5/2 = 24.75 samples at the base rate.
class Oversampler4x {
public:
    float process(float input, int channel, float (*shape)(float,void*), void* state) noexcept {
        Bridge bridge{&inner,channel,shape,state};
        return outer.process(input,channel,&processInner,&bridge);
    }
    void reset() noexcept { inner.reset(); outer.reset(); }
private:
    struct Bridge { Oversampler2xInner* inner; int channel; float (*shape)(float,void*); void* state; };
    static float processInner(float input, void* user) noexcept {
        const auto& b=*static_cast<Bridge*>(user);
        return b.inner->process(input,b.channel,b.shape,b.state);
    }
    Oversampler2xInner inner;
    Oversampler2x outer;
};
