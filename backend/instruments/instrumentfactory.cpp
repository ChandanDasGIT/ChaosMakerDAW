#include "instruments/instrumentfactory.h"
#include "instruments/chords/chordsinstrument.h"
#include "instruments/drums/drumkit.h"
#include "instruments/electrodrums/electrodrums.h"
#include "instruments/harmonium/harmonium.h"
#include "instruments/piano/piano.h"
#include "instruments/synth/synth.h"
#include "instruments/instrumentcatalog.h"
#include "plugins/testsynth/testsynth.h"

std::shared_ptr<Processor> createInstrument(std::string_view typeId)
{
    if (typeId == "Harmonium") return std::make_shared<Harmonium>();
    if (typeId == "Drums") return std::make_shared<DrumKit>();
    if (typeId == "Electronic Drums") return std::make_shared<ElectroDrums>();
    if (typeId == "Piano") return std::make_shared<Piano>();
    if (typeId == "Chords") return std::make_shared<ChordsInstrument>();
    if (typeId == "Synth") return std::make_shared<Synth>();
    // The other instrument types play through TestSynth until their real instruments exist.
    return std::make_shared<TestSynth>();
}
