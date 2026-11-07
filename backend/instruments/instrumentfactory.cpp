#include "instruments/instrumentfactory.h"
#include "instruments/drums/drumkit.h"
#include "instruments/harmonium/harmonium.h"
#include "instruments/instrumentcatalog.h"
#include "plugins/testsynth/testsynth.h"

std::shared_ptr<Processor> createInstrument(std::string_view typeId)
{
    if (typeId == "Harmonium") return std::make_shared<Harmonium>();
    if (typeId == "Drums") return std::make_shared<DrumKit>();
    // The other instrument types play through TestSynth until their real instruments exist.
    return std::make_shared<TestSynth>();
}
