#include "instruments/instrumentfactory.h"
#include "instruments/instrumentcatalog.h"
#include "plugins/testsynth/testsynth.h"

std::shared_ptr<Processor> createInstrument(std::string_view typeId)
{
    (void)typeId;
    // Maps every instrument type to TestSynth for now.
    // Later specific types will map to real instruments (e.g. sampler / piano).
    return std::make_shared<TestSynth>();
}
