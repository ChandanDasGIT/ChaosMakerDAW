#include "common/trackEffectsPanel/plugins/mixFx/mixfxfactory.h"
#include "common/trackEffectsPanel/plugins/mixFx/mixcompressor.h"
#include "common/trackEffectsPanel/plugins/mixFx/deesser.h"
#include "common/trackEffectsPanel/plugins/mixFx/roomreverb.h"
#include "common/trackEffectsPanel/plugins/mixFx/chaosverb.h"
#include "common/trackEffectsPanel/plugins/mixFx/mixdelay.h"
#include "common/trackEffectsPanel/plugins/mixFx/limiter.h"
#include "common/trackEffectsPanel/plugins/mixFx/saturation.h"
#include "common/trackEffectsPanel/plugins/mixFx/gate.h"
#include "common/trackEffectsPanel/plugins/denoise.h"
#include "common/trackEffectsPanel/plugins/pitchMonitor/pitchmonitor.h"

namespace mixfx {

bool isMixEffect(std::string_view type) { return find(type) != nullptr; }

std::shared_ptr<MixEffect> create(std::string_view type)
{
    if (type == Compressor) return std::make_shared<MixCompressor>();
    if (type == DeEsser) return std::make_shared<::DeEsser>();
    if (type == Reverb) return std::make_shared<RoomReverb>();
    if (type == ChaosVerb) return std::make_shared<::ChaosVerb>();
    if (type == Delay) return std::make_shared<MixDelay>();
    if (type == Limiter) return std::make_shared<::Limiter>();
    if (type == Saturation) return std::make_shared<::Saturation>();
    if (type == Gate) return std::make_shared<::Gate>();
    if (type == Denoise) return std::make_shared<::Denoise>();
    if (type == PitchMonitor) return std::make_shared<::PitchMonitor>();
    return nullptr;
}

} // namespace mixfx
