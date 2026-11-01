#include "plugins/exampleeffect/exampleeffect.h"

ExampleEffect::ExampleEffect()
    : m_amount(addParameter("amount", "Amount", 0.0f, 1.0f, 1.0f))
{
}

void ExampleEffect::prepare(double sampleRate, int /*maxFrames*/)
{
    m_amountSmoother.prepare(sampleRate, 20.0f);
}

void ExampleEffect::reset() noexcept
{
    m_amountSmoother.reset(m_amount.get());
}

void ExampleEffect::process(ProcessContext& context) noexcept
{
    const float target = m_amount.get();                        // read a parameter once per block

    for (int i = 0; i < context.audio.frames; ++i) {
        const float amount = m_amountSmoother.next(target);     // use the smoothed value on each sample

        for (int c = 0; c < context.audio.channelCount; ++c) {
            float& sample = context.audio.channel(c)[i];
            sample *= amount;                                   // your DSP goes here
        }
    }
}
