#include "plugins/mixfx/mixfxfactory.h"
#include "plugins/compressor/mixcompressor.h"
#include "plugins/deesser/deesser.h"
#include "plugins/reverb/roomreverb.h"
#include "plugins/chaosverb/chaosverb.h"

namespace mixfx {

bool isMixEffect(std::string_view type) { return find(type) != nullptr; }

std::shared_ptr<MixEffect> create(std::string_view type)
{
    if (type == Compressor) return std::make_shared<MixCompressor>();
    if (type == DeEsser) return std::make_shared<::DeEsser>();
    if (type == Reverb) return std::make_shared<RoomReverb>();
    if (type == ChaosVerb) return std::make_shared<::ChaosVerb>();
    return nullptr;
}

} // namespace mixfx
