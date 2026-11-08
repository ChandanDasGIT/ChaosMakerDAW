#include "instruments/sampler/sampler.h"

#include <algorithm>

Sampler::Sampler()
{
    setPolyphony(64);
    prepare(48000.0, 1024);
    setZones({});
}

Sampler::~Sampler() = default;

void Sampler::prepare(double sampleRate, int maxFrames)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const auto frames = std::size_t(std::max(1, maxFrames));
    m_left.assign(frames, 0.0f);
    m_right.assign(frames, 0.0f);
    reset();                                               // envelopes were timed for the old rate
}

void Sampler::setPolyphony(int polyphony)
{
    m_polyphony = std::max(1, polyphony);
    m_voices.assign(std::size_t(m_polyphony) * 2, SamplerVoice{});
    m_started.assign(m_voices.size(), 0);
    reset();
}

int Sampler::setZones(std::vector<SampleZone> zones)
{
    reset();                                               // voices point into the old zones
    auto map = std::make_unique<ZoneMap>();
    const int dropped = map->setZones(std::move(zones));
    std::lock_guard lock(m_ownedMutex);
    m_pending.store(nullptr, std::memory_order_relaxed);
    m_retired.store(nullptr, std::memory_order_relaxed);
    m_retiring = nullptr;
    m_owned.clear();
    m_active = map.get();
    m_owned.push_back(std::move(map));
    return dropped;
}

int Sampler::stageZones(std::vector<SampleZone> zones)
{
    auto map = std::make_unique<ZoneMap>();
    const int dropped = map->setZones(std::move(zones));
    std::lock_guard lock(m_ownedMutex);
    collectRetiredLocked();
    ZoneMap* staged = map.get();
    m_owned.push_back(std::move(map));
    // A set still waiting was never taken by the audio thread: nothing plays from it.
    if (ZoneMap* skipped = m_pending.exchange(staged, std::memory_order_acq_rel))
        std::erase_if(m_owned, [skipped](const auto& owned) { return owned.get() == skipped; });
    return dropped;
}

int Sampler::collectRetired()
{
    std::lock_guard lock(m_ownedMutex);
    return collectRetiredLocked();
}

int Sampler::collectRetiredLocked()
{
    ZoneMap* done = m_retired.exchange(nullptr, std::memory_order_acq_rel);
    if (!done) return 0;
    std::erase_if(m_owned, [done](const auto& owned) { return owned.get() == done; });
    return 1;
}

void Sampler::takeStagedZones() noexcept
{
    if (m_retiring && m_retired.load(std::memory_order_acquire) == nullptr) {
        const bool used = std::any_of(m_voices.begin(), m_voices.end(), [this](const auto& voice) {
            return voice.active() && m_retiring->holds(voice.zone());
        });
        if (!used) {
            m_retired.store(const_cast<ZoneMap*>(m_retiring), std::memory_order_release);
            m_retiring = nullptr;
        }
    }
    if (m_retiring) return;                                // one change at a time
    ZoneMap* next = m_pending.exchange(nullptr, std::memory_order_acq_rel);
    if (!next) return;
    for (auto& voice : m_voices) voice.fadeOut();
    m_retiring = m_active;
    m_active = next;
}

void Sampler::reset() noexcept
{
    for (auto& voice : m_voices) voice.stop();
    std::fill(m_started.begin(), m_started.end(), 0);
    m_clock = 0;
    m_presses.fill(0);
    m_releases.fill(0);
    m_velocity.fill(0);
    m_held.fill(false);
    m_cc.fill(0);
}

void Sampler::allSoundOff() noexcept
{
    for (auto& voice : m_voices) voice.stop();
    m_held.fill(false);
}

int Sampler::activeVoices() const noexcept
{
    return int(std::count_if(m_voices.begin(), m_voices.end(), [](const auto& v) { return v.active(); }));
}

// Fades the oldest unfaded voice while too many are sounding. Voices already in their release
// go first, so a held chord is kept over notes that are dying away anyway.
void Sampler::limitPolyphony() noexcept
{
    for (;;) {
        int sounding = 0;
        SamplerVoice* oldest = nullptr;
        std::uint64_t oldestStart = 0;
        bool oldestReleasing = false;
        for (std::size_t i = 0; i < m_voices.size(); ++i) {
            auto& voice = m_voices[i];
            if (!voice.active() || voice.fading()) continue;
            ++sounding;
            const bool releasing = voice.releasing();
            const bool better = !oldest || (releasing && !oldestReleasing)
                             || (releasing == oldestReleasing && m_started[i] < oldestStart);
            if (better) { oldest = &voice; oldestStart = m_started[i]; oldestReleasing = releasing; }
        }
        if (sounding < m_polyphony || !oldest) return;
        oldest->fadeOut();
    }
}

// A free voice, or else the quietest fading one (cut: the pool is full of fades).
SamplerVoice* Sampler::freeVoice() noexcept
{
    SamplerVoice* quietest = nullptr;
    for (auto& voice : m_voices) {
        if (!voice.active()) return &voice;
        if (!quietest || voice.level() < quietest->level()) quietest = &voice;
    }
    if (quietest) quietest->stop();
    return quietest;
}

void Sampler::startZones(int key, int velocity, std::span<const int> zones) noexcept
{
    // A zone that needs a live controller in its range (the pedal down) starts only then.
    const auto playable = [this](const SampleZone& z) {
        if (z.liveCc < 0) return true;
        const int v = m_cc[std::size_t(z.liveCc & 127)];
        return v >= z.liveLo && v <= z.liveHi;
    };
    // Choke first, for every starting zone, so layers of one note never choke each other.
    for (const int index : zones) {
        const auto& z = m_active->zone(index);
        if (!playable(z)) continue;
        if (z.group != 0)
            for (auto& voice : m_voices)
                if (voice.active() && voice.zone() && voice.zone()->offBy == z.group) voice.fadeOut(voice.zone()->offSeconds);
        if (z.notePolyphony > 0) {
            // Keep the newest notePolyphony - 1 voices of this key and group; fade the rest.
            for (;;) {
                int count = 0;
                SamplerVoice* oldest = nullptr;
                std::uint64_t oldestStart = 0;
                for (std::size_t i = 0; i < m_voices.size(); ++i) {
                    auto& voice = m_voices[i];
                    if (!voice.active() || voice.fading() || voice.key() != key || !voice.zone()
                        || voice.zone()->group != z.group)
                        continue;
                    ++count;
                    if (!oldest || m_started[i] < oldestStart) { oldest = &voice; oldestStart = m_started[i]; }
                }
                if (count < z.notePolyphony || !oldest) break;
                oldest->fadeOut(oldest->zone()->offSeconds);
            }
        }
    }
    for (const int index : zones) {
        if (m_active->zone(index).silent || !playable(m_active->zone(index))) continue;
        limitPolyphony();
        auto* voice = freeVoice();
        if (!voice) return;
        voice->start(m_active->zone(index), key, velocity, m_sampleRate);
        m_started[std::size_t(voice - m_voices.data())] = ++m_clock;
    }
}

void Sampler::noteOn(int key, int velocity) noexcept
{
    if (key < 0 || key > 127) return;
    if (velocity <= 0) { noteOff(key); return; }
    velocity = std::min(velocity, 127);

    std::array<int, maxZonesPerNote> found{};
    const int count = m_active->find(key, velocity, ZoneMap::Trigger::Attack, m_presses[std::size_t(key)], found);
    ++m_presses[std::size_t(key)];
    m_velocity[std::size_t(key)] = velocity;
    m_held[std::size_t(key)] = true;
    startZones(key, velocity, std::span<const int>(found.data(), std::size_t(count)));
}

void Sampler::noteOff(int key) noexcept
{
    if (key < 0 || key > 127 || !m_held[std::size_t(key)]) return;
    m_held[std::size_t(key)] = false;

    for (auto& voice : m_voices) {
        if (!voice.active() || voice.key() != key || voice.releasing() || voice.pedalHeld()) continue;
        const auto* zone = voice.zone();
        if (zone->releaseTrigger || zone->oneShot) continue;
        if (m_cc[std::size_t(zone->sustainCc & 127)] >= 64) voice.setPedalHeld(true);   // the pedal keeps it
        else releaseVoice(voice);
    }

    std::array<int, maxZonesPerNote> found{};
    const int velocity = m_velocity[std::size_t(key)];
    const int count = m_active->find(key, velocity, ZoneMap::Trigger::Release, m_releases[std::size_t(key)], found);
    if (count > 0) ++m_releases[std::size_t(key)];
    startZones(key, velocity, std::span<const int>(found.data(), std::size_t(count)));
}

void Sampler::releaseVoice(SamplerVoice& voice) noexcept
{
    const auto* zone = voice.zone();
    voice.setPedalHeld(false);
    if (zone && zone->releaseCc >= 0) voice.release(zone->releaseFor(m_cc[std::size_t(zone->releaseCc & 127)]));
    else voice.release();
}

void Sampler::allNotesOff() noexcept
{
    for (int key = 0; key < 128; ++key) noteOff(key);
    for (auto& voice : m_voices)
        if (voice.active() && voice.pedalHeld()) releaseVoice(voice);
}

void Sampler::controlChange(int controller, int value) noexcept
{
    if (controller < 0 || controller > 127) return;
    value = std::clamp(value, 0, 127);
    m_cc[std::size_t(controller)] = value;
    for (auto& voice : m_voices) {
        if (!voice.active() || !voice.zone()) continue;
        const auto& zone = *voice.zone();
        // The pedal came up: what it held is let go now.
        // (A held voice is a let-go note: its key pressed again has a voice of its own.)
        if (voice.pedalHeld() && zone.sustainCc == controller && value < 64)
            releaseVoice(voice);
        // A release that follows the controller (half pedalling).
        else if (voice.releasing() && !voice.fading() && zone.dynamicRelease && zone.releaseCc == controller)
            voice.setReleaseSeconds(zone.releaseFor(value));
    }
}

void Sampler::render(float* left, float* right, int frames) noexcept
{
    for (auto& voice : m_voices) voice.render(left, right, frames);
}

void Sampler::handle(const MidiEvent& event) noexcept
{
    const int type = event.status & 0xF0;
    if (type == 0x90) noteOn(event.data1, event.data2);   // velocity 0 is a note-off there
    else if (type == 0x80) noteOff(event.data1);
    else if (type == 0xB0 && event.data1 == 123) allNotesOff();
    else if (type == 0xB0 && event.data1 == 120) allSoundOff();
    else if (type == 0xB0) controlChange(event.data1, event.data2);
}

// Renders frames [start, end) through the mix buffers, a buffer's length at a time.
void Sampler::renderInto(const AudioView& audio, int start, int end) noexcept
{
    const int chunk = int(m_left.size());
    while (start < end) {
        const int frames = std::min(chunk, end - start);
        std::fill_n(m_left.data(), frames, 0.0f);
        std::fill_n(m_right.data(), frames, 0.0f);
        render(m_left.data(), m_right.data(), frames);
        if (audio.channelCount == 1) {
            float* out = audio.channel(0) + start;
            for (int i = 0; i < frames; ++i) out[i] += 0.5f * (m_left[std::size_t(i)] + m_right[std::size_t(i)]);
        } else {
            float* outLeft = audio.channel(0) + start;
            float* outRight = audio.channel(1) + start;
            for (int i = 0; i < frames; ++i) {
                outLeft[i] += m_left[std::size_t(i)];
                outRight[i] += m_right[std::size_t(i)];
            }
        }
        start += frames;
    }
}

void Sampler::process(ProcessContext& context) noexcept
{
    const auto& audio = context.audio;
    if (audio.frames <= 0 || audio.channelCount <= 0 || !audio.channels) return;
    takeStagedZones();

    int frame = 0;
    if (context.midi) {
        for (const auto& event : context.midi->events()) {
            const int at = std::clamp(event.offset, 0, audio.frames);
            if (at > frame) { renderInto(audio, frame, at); frame = at; }
            handle(event);
        }
    }
    renderInto(audio, frame, audio.frames);
}
