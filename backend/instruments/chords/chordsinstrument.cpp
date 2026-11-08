#include "instruments/chords/chordsinstrument.h"

ChordsInstrument::ChordsInstrument(std::shared_ptr<piano::Library> library)
    : m_piano(std::make_shared<Piano>(std::move(library)))
{
}

void ChordsInstrument::prepare(double sampleRate, int maxFrames)
{
    m_player.prepare(sampleRate);
    m_piano->prepare(sampleRate, maxFrames);
}

void ChordsInstrument::reset() noexcept
{
    m_player.reset();
    m_piano->reset();
}

void ChordsInstrument::process(ProcessContext& context) noexcept
{
    m_player.process(context);
    m_piano->process(context);
}
