#include "common/instruments/instrumentfactory.h"
#include "common/instruments/chordsinstrument.h"
#include "common/trackEffectsPanel/instruments/drums/drumkit.h"
#include "common/trackEffectsPanel/instruments/electrodrums/electrodrums.h"
#include "common/trackEffectsPanel/instruments/harmonium/harmonium.h"
#include "common/trackEffectsPanel/instruments/piano/piano.h"
#include "common/trackEffectsPanel/instruments/synth/synth.h"
#include "common/trackEffectsPanel/instruments/guitar/bass/bassguitar.h"
#include "common/instruments/instrumentcatalog.h"
#include "common/instruments/testsynth.h"

std::shared_ptr<Processor> createInstrument(std::string_view typeId)
{
    if (typeId == "Harmonium") return std::make_shared<Harmonium>();
    if (typeId == "Drums") return std::make_shared<DrumKit>();
    if (typeId == "Electronic Drums") return std::make_shared<ElectroDrums>();
    if (typeId == "Piano") return std::make_shared<Piano>();
    if (typeId == "Chords") return std::make_shared<ChordsInstrument>();
    if (typeId == "Synth") return std::make_shared<Synth>();
    if (typeId == "Bass Guitar") return std::make_shared<BassGuitar>();
    // The other instrument types play through TestSynth until their real instruments exist.
    return std::make_shared<TestSynth>();
}
