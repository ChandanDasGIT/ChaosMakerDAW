#include "pedalregistry.h"

#include "common/trackEffectsPanel/instruments/guitar/electric/pedals/amp/amp.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/pedals/cabinet/cabinet.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/pedals/chorus/chorus.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/pedals/compressor/compressor.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/pedals/crunch/crunch.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/pedals/delay/delaypedal.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/pedals/distortion/distortion.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/pedals/fuzz/fuzz.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/pedals/noisegate/noisegate.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/pedals/octaver/octaver.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/pedals/overdrive/overdrive.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/pedals/parametriceq/parametriceq.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/pedals/phaser/phaser.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/pedals/pitchshift/pitchshift.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/pedals/reverb/reverbpedal.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/pedals/tremolo/tremolo.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/tuner/tuner.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/pedals/wah/wah.h"

namespace PedalRegistry {

std::vector<std::string> availablePedals()
{
    return {
        "pedal.noisegate",
        "pedal.overdrive",
        "pedal.crunch",
        "pedal.distortion",
        "pedal.delay",
        "pedal.compressor",
        "pedal.wah",
        "pedal.chorus",
        "pedal.tremolo",
        "pedal.tuner",
        "pedal.reverb",
        "pedal.amp",
        "pedal.cabinet",
        "pedal.octaver",
        "pedal.pitchshift",
        "pedal.parametriceq",
        "pedal.fuzz",
        "pedal.phaser"
    };
}

std::string displayName(const std::string& type)
{
    if (type == "pedal.overdrive") return "Overdrive";
    if (type == "pedal.delay") return "Delay";
    if (type == "pedal.reverb") return "Reverb";
    if (type == "pedal.noisegate") return "Noise Gate";
    if (type == "pedal.compressor") return "Compressor";
    if (type == "pedal.wah") return "Wah";
    if (type == "pedal.chorus") return "Chorus";
    if (type == "pedal.tremolo") return "Tremolo";
    if (type == "pedal.tuner") return "Tuner";
    if (type == "pedal.amp") return "Amp";
    if (type == "pedal.cabinet") return "Cabinet IR";
    if (type == "pedal.octaver") return "Octaver";
    if (type == "pedal.pitchshift") return "Pitch Shifter";
    if (type == "pedal.crunch") return "Crunch";
    if (type == "pedal.distortion") return "Distortion";
    if (type == "pedal.parametriceq") return "Parametric EQ";
    if (type == "pedal.fuzz") return "Fuzz";
    if (type == "pedal.phaser") return "Phaser";
    return type;
}

std::shared_ptr<Processor> create(const std::string& type)
{
    if (type == "pedal.overdrive" || type == "Overdrive") return std::make_shared<Overdrive>();
    if (type == "pedal.delay" || type == "Delay") return std::make_shared<DelayPedal>();
    if (type == "pedal.reverb" || type == "Reverb") return std::make_shared<ReverbPedal>();
    if (type == "pedal.noisegate" || type == "Noise Gate") return std::make_shared<NoiseGate>();
    if (type == "pedal.compressor" || type == "Compressor") return std::make_shared<Compressor>();
    if (type == "pedal.wah" || type == "Wah") return std::make_shared<Wah>();
    if (type == "pedal.chorus" || type == "Chorus") return std::make_shared<Chorus>();
    if (type == "pedal.tremolo" || type == "Tremolo") return std::make_shared<Tremolo>();
    if (type == "pedal.tuner" || type == "Tuner") return std::make_shared<Tuner>();
    if (type == "pedal.amp" || type == "Amp") return std::make_shared<Amp>();
    if (type == "pedal.cabinet" || type == "Cabinet IR") return std::make_shared<Cabinet>();
    if (type == "pedal.octaver" || type == "Octaver") return std::make_shared<Octaver>();
    if (type == "pedal.pitchshift" || type == "Pitch Shifter") return std::make_shared<PitchShift>();
    if (type == "pedal.crunch" || type == "Crunch") return std::make_shared<Crunch>();
    if (type == "pedal.distortion" || type == "Distortion") return std::make_shared<Distortion>();
    if (type == "pedal.parametriceq" || type == "Parametric EQ") return std::make_shared<ParametricEq>();
    if (type == "pedal.fuzz" || type == "Fuzz") return std::make_shared<Fuzz>();
    if (type == "pedal.phaser" || type == "Phaser") return std::make_shared<Phaser>();
    return {};
}

} // namespace PedalRegistry
