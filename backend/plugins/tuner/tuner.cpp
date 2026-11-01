#include "tuner.h"
void Tuner::process(ProcessContext& context) noexcept {
    if (context.audio.channelCount <= 0) return;
    for (int i = 0; i < context.audio.frames; ++i)
        if (detector.push(context.audio.channel(0)[i])) {
            published.store(detector.frequency(), std::memory_order_relaxed);
            updates.fetch_add(1, std::memory_order_relaxed);
        }
}
