#include "plugins/mixfx/mixfxfactory.h"
#include "plugins/compressor/mixcompressor.h"
#include "plugins/deesser/deesser.h"
#include "plugins/reverb/roomreverb.h"
#include "plugins/chaosverb/chaosverb.h"
#include "plugins/delay/mixdelay.h"
#include "plugins/limiter/limiter.h"
#include "plugins/saturation/saturation.h"
#include "plugins/gate/gate.h"
#include "plugins/denoise/denoise.h"

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
    return nullptr;
}

} // namespace mixfx
