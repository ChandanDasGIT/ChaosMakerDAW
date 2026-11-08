#pragma once

#include "instruments/piano/piano.h"
#include "plugins/chordplayer/chordplayer.h"
#include "processor/processor.h"

#include <memory>

// The Chords track's instrument: the ChordPlayer (eight chord strips with autoplay, played by
// MIDI on fixed keys) in front of the grand piano. The player rewrites the block's MIDI, the
// piano plays it. The piano's settings are the track's "piano" settings, like a Piano track's.
class ChordsInstrument final : public Processor {
public:
    explicit ChordsInstrument(std::shared_ptr<piano::Library> library = piano::Library::shared());

    std::string typeId() const override { return "chords"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
    bool keepsRunning() const noexcept override { return m_player.running(); }

    ChordPlayer& player() noexcept { return m_player; }
    const ChordPlayer& player() const noexcept { return m_player; }
    Piano& piano() noexcept { return *m_piano; }
    const Piano& piano() const noexcept { return *m_piano; }

private:
    ChordPlayer m_player;
    std::shared_ptr<Piano> m_piano;
};
