#include "virtualKeyboardWrap.h"

#include "common/midi/notenames.h"
#include "wrapper/common/engine/audioEngineWrap.h"
#include "common/engine/audioengine.h"

#include <algorithm>

VirtualKeyboardWrap::VirtualKeyboardWrap(QObject* parent)
    : QObject(parent)
{
}

VirtualKeyboardWrap::~VirtualKeyboardWrap()
{
    releaseAll();
}

void VirtualKeyboardWrap::setEngine(QObject* engine)
{
    if (engine == m_engine) return;
    releaseAll();                                           // the old engine gets its note-offs
    m_engine = engine;
    emit engineChanged();
}

void VirtualKeyboardWrap::noteOn(int note, int velocity)
{
    if (note < 0 || note > 127) return;
    if (m_held[std::size_t(note)]) send(note, 0, false);    // pressed again: restart it
    m_held[std::size_t(note)] = true;
    send(note, std::clamp(velocity, 1, 127), true);
}

void VirtualKeyboardWrap::noteOff(int note)
{
    if (note < 0 || note > 127 || !m_held[std::size_t(note)]) return;
    m_held[std::size_t(note)] = false;
    send(note, 0, false);
}

void VirtualKeyboardWrap::releaseAll()
{
    for (int note = 0; note < 128; ++note) noteOff(note);
}

QString VirtualKeyboardWrap::noteName(int note) const
{
    return QString::fromStdString(notenames::name(note));
}

void VirtualKeyboardWrap::send(int note, int velocity, bool on)
{
    if (auto* wrapper = qobject_cast<AudioEngineWrap*>(m_engine.data()))
        if (auto* engine = wrapper->engine())
            engine->sendKeyboardMidi(MidiEvent{0, std::uint8_t(on ? 0x90 : 0x80), std::uint8_t(note), std::uint8_t(velocity)});
    emit noteSent(note, velocity, on);
}
