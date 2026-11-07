#include "instruments/harmonium/harmonium.h"
#include "dsp/eqbanddesign.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace harmonium;

namespace {

constexpr double twoPi = 2.0 * std::numbers::pi;

float approach(double seconds, double rate) noexcept
{
    const double frames = seconds * rate;
    return frames >= 1.0 ? float(std::exp(-1.0 / frames)) : 0.0f;
}

bool on(const Parameter* p) noexcept { return p->get() >= 0.5f; }

biquad::Coefficients section(eqband::Shape shape, double hz, double gainDb, double q, double rate)
{
    eqband::Settings s;
    s.shape = shape; s.frequencyHz = hz; s.gainDb = gainDb; s.q = q; s.slope = eqband::Slope::Db12;
    const auto design = eqband::design(eqband::sanitize(s, rate), rate);
    return design.sectionCount > 0 ? design.sections[0] : biquad::Coefficients{};
}

} // namespace

Harmonium::Harmonium()
{
    reedwave::Tables::instance();                           // build the shared tables here, not in process()
    const Settings d;
    m_p.style = &addParameter("style", "Style", 0, 1, 0);
    m_p.bass = &addParameter("bass", "Bass reeds", 0, 1, d.indian.bass);
    m_p.male = &addParameter("male", "Male reeds", 0, 1, d.indian.male);
    m_p.female = &addParameter("female", "Female reeds", 0, 1, d.indian.female);
    m_p.bourdon16 = &addParameter("bourdon16", "Bourdon 16'", 0, 1, d.western.bourdon16);
    m_p.diapason8 = &addParameter("diapason8", "Diapason 8'", 0, 1, d.western.diapason8);
    m_p.dulciana8 = &addParameter("dulciana8", "Dulciana 8'", 0, 1, d.western.dulciana8);
    m_p.principal4 = &addParameter("principal4", "Principal 4'", 0, 1, d.western.principal4);
    m_p.voxHumana = &addParameter("voxHumana", "Vox Humana", 0, 1, d.western.voxHumana);
    m_p.bellows = &addParameter("bellows", "Bellows", 0, 1, float(d.bellows));
    m_p.pumping = &addParameter("pumping", "Pumping", 0, 1, float(d.pumping));
    m_p.beating = &addParameter("beating", "Beating", 0, float(MaxBeatingCents), float(d.beatingCents), "cents");
    m_p.airNoise = &addParameter("airNoise", "Air noise", 0, 1, float(d.airNoise));
    m_p.keyClick = &addParameter("keyClick", "Key click", 0, 1, float(d.keyClick));
    m_p.caseResonance = &addParameter("caseResonance", "Case", 0, 1, float(d.caseResonance));
    m_p.transpose = &addParameter("transpose", "Scale", -float(MaxTranspose), float(MaxTranspose), 0, "semitones");
    m_p.volume = &addParameter("volume", "Volume", float(MinVolumeDb), float(MaxVolumeDb), 0, "dB");
    for (int i = 0; i < DroneCount; ++i)
        m_p.drones[std::size_t(i)] = &addParameter("drone" + std::to_string(i + 1),
                                                   "Drone " + std::to_string(i + 1), -1, 127, -1);
    m_p.sustain = &addParameter("sustain", "Sustain", 0, float(MaxSustainSeconds), float(d.sustainSeconds), "s");
    prepare(48000.0, 1024);
}

void Harmonium::setSettings(const Settings& input) noexcept
{
    const auto s = sanitize(input);
    m_p.style->set(s.style == Style::Western ? 1.0f : 0.0f);
    m_p.bass->set(s.indian.bass); m_p.male->set(s.indian.male); m_p.female->set(s.indian.female);
    m_p.bourdon16->set(s.western.bourdon16); m_p.diapason8->set(s.western.diapason8);
    m_p.dulciana8->set(s.western.dulciana8); m_p.principal4->set(s.western.principal4);
    m_p.voxHumana->set(s.western.voxHumana);
    m_p.bellows->set(float(s.bellows)); m_p.pumping->set(float(s.pumping));
    m_p.beating->set(float(s.beatingCents)); m_p.airNoise->set(float(s.airNoise));
    m_p.keyClick->set(float(s.keyClick)); m_p.caseResonance->set(float(s.caseResonance));
    m_p.transpose->set(float(s.transpose)); m_p.volume->set(float(s.volumeDb));
    m_p.sustain->set(float(s.sustainSeconds));
    for (int i = 0; i < DroneCount; ++i) m_p.drones[std::size_t(i)]->set(float(s.drones[std::size_t(i)]));
}

Settings Harmonium::settings() const noexcept
{
    Settings s;
    s.style = on(m_p.style) ? Style::Western : Style::Indian;
    s.indian = {on(m_p.bass), on(m_p.male), on(m_p.female)};
    s.western = {on(m_p.bourdon16), on(m_p.diapason8), on(m_p.dulciana8), on(m_p.principal4), on(m_p.voxHumana)};
    s.bellows = m_p.bellows->get(); s.pumping = m_p.pumping->get();
    s.beatingCents = m_p.beating->get(); s.airNoise = m_p.airNoise->get();
    s.keyClick = m_p.keyClick->get(); s.caseResonance = m_p.caseResonance->get();
    s.transpose = int(std::lround(m_p.transpose->get())); s.volumeDb = m_p.volume->get();
    s.sustainSeconds = m_p.sustain->get();
    for (int i = 0; i < DroneCount; ++i) s.drones[std::size_t(i)] = int(std::lround(m_p.drones[std::size_t(i)]->get()));
    return s;
}

void Harmonium::prepare(double sampleRate, int maxFrames)
{
    (void)maxFrames;                                        // works in its own short blocks
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_pressureCoef = approach(0.05, m_sampleRate / Block);
    m_clickCoef = approach(0.004, m_sampleRate);
    m_airFilter = section(eqband::Shape::BandPass, 1800.0, 0.0, 0.8, m_sampleRate);
    m_clickFilter = section(eqband::Shape::BandPass, 2600.0, 0.0, 1.2, m_sampleRate);
    // Short reflections inside the case and off the player's room: a few ms, two per side.
    const double ms[4] = {2.9, 5.7, 3.8, 6.9};
    for (int i = 0; i < 4; ++i)
        m_tapFrames[std::size_t(i)] = std::clamp(int(ms[i] * 0.001 * m_sampleRate), 1, MaxDelay - 1);
    reset();
}

void Harmonium::reset() noexcept
{
    for (auto& v : m_voices) v.stop();
    for (auto& v : m_drones) v.stop();
    m_started.fill(0);
    m_startFrame.fill(0);
    m_clock = 0;
    m_now = 0;
    m_expression = m_breath = 1.0f;
    m_pressure = m_pressureTarget = 0.0f;
    m_pumpPhase = m_tremoloPhase = 0.0;
    m_click = 0.0f;
    m_bodyState.fill({});
    m_airState = m_clickState = {};
    m_delay.fill(0.0f);
    m_delayWrite = 0;
    m_caseResonance = -1.0f;                                // design the body again
    updateControls();
    m_pressure = m_pressureTarget;                          // start at the set pressure
}

void Harmonium::designBody() noexcept
{
    const double c = m_caseResonance;
    const double r = m_sampleRate;
    using eqband::Shape;
    if (m_style == Style::Indian) {
        // A small box: some wood around 300 Hz, the nasal peak near 1.4 kHz, and the steep fall
        // above ~3 kHz both references show.
        m_body[0] = section(Shape::Bell, 300.0, 6.0 * c, 1.0, r);
        m_body[1] = section(Shape::Bell, 1400.0, 8.0 * c, 1.0, r);
        m_body[2] = section(Shape::HighCut, 3200.0, 0.0, 0.707, r);
        m_body[3] = section(Shape::HighCut, 8500.0, 0.0, 0.707, r);
    } else {
        // A larger cabinet: warmer, rounder, darker.
        m_body[0] = section(Shape::Bell, 180.0, 5.0 * c, 0.9, r);
        m_body[1] = section(Shape::Bell, 700.0, 3.0 * c, 1.0, r);
        m_body[2] = section(Shape::HighCut, 2400.0, 0.0, 0.707, r);
        m_body[3] = section(Shape::HighCut, 5000.0, 0.0, 0.707, r);
    }
    m_reflection = float(0.35 * c);
}

void Harmonium::updateControls() noexcept
{
    const auto style = on(m_p.style) ? Style::Western : Style::Indian;
    if (style != m_style) {                                 // other reeds: let the old ones go
        m_style = style;
        for (auto& v : m_voices) v.fadeOut();
        for (auto& v : m_drones) v.fadeOut();
        m_caseResonance = -1.0f;
    }
    const float caseResonance = m_p.caseResonance->get();
    if (caseResonance != m_caseResonance) { m_caseResonance = caseResonance; designBody(); }

    const double beat = m_p.beating->get();
    using reedwave::Timbre;
    if (m_style == Style::Indian) {
        m_ranks[0] = {Timbre::Reed, -1, -beat * 0.5, on(m_p.bass) ? 0.8f : 0.0f};
        m_ranks[1] = {Timbre::Reed, 0, 0.0, on(m_p.male) ? 1.0f : 0.0f};
        m_ranks[2] = {Timbre::Reed, 1, beat, on(m_p.female) ? 0.7f : 0.0f};
        m_ranks[3] = {}; m_ranks[4] = {};
    } else {
        m_ranks[0] = {Timbre::Bourdon, -1, -beat * 0.25, on(m_p.bourdon16) ? 0.8f : 0.0f};
        m_ranks[1] = {Timbre::Diapason, 0, 0.0, on(m_p.diapason8) ? 1.0f : 0.0f};
        m_ranks[2] = {Timbre::Dulciana, 0, beat * 0.5, on(m_p.dulciana8) ? 0.6f : 0.0f};
        m_ranks[3] = {Timbre::Diapason, 1, beat * 0.25, on(m_p.principal4) ? 0.55f : 0.0f};
        m_ranks[4] = {};
    }
    for (auto& v : m_voices) if (!v.releasing()) v.setRanks(m_ranks);
    for (auto& v : m_drones) if (!v.releasing()) v.setRanks(m_ranks);

    // Bellows: the set pressure, the player's expression, the reservoir sagging under big chords.
    int ranks = 0;
    for (const auto& v : m_voices) ranks += v.soundingRanks();
    for (const auto& v : m_drones) ranks += v.soundingRanks();
    float target = m_p.bellows->get() * m_expression * m_breath / (1.0f + 0.015f * float(ranks));

    // Each pump stroke lets the pressure dip; the stroke rate wanders a little.
    const double blockSeconds = Block / m_sampleRate;
    m_pumpRate = std::clamp(m_pumpRate + (double(noise()) * 0.02), 0.8, 1.5);
    m_pumpPhase += m_pumpRate * blockSeconds;
    m_pumpPhase -= std::floor(m_pumpPhase);
    const double stroke = 0.5 - 0.5 * std::cos(twoPi * m_pumpPhase);
    const float pumpDepth = m_p.pumping->get() * (m_style == Style::Indian ? 0.18f : 0.07f);
    target *= 1.0f - pumpDepth * float(stroke * stroke);
    m_pressureTarget = std::clamp(target, 0.0f, 1.0f);

    m_gain = 0.35f * std::pow(10.0f, m_p.volume->get() / 20.0f);
    updateDrones();
}

void Harmonium::updateDrones() noexcept
{
    const int transpose = int(std::lround(m_p.transpose->get()));
    for (int i = 0; i < DroneCount; ++i) {
        auto& voice = m_drones[std::size_t(i)];
        const int key = int(std::lround(m_p.drones[std::size_t(i)]->get()));
        const int sounding = key >= 0 ? std::clamp(key + transpose, 0, 127) : -1;
        if (voice.active() && !voice.releasing() && voice.soundingKey() != sounding) voice.release();
        if (sounding >= 0 && (!voice.active() || voice.releasing()))
            voice.start(-2 - i, sounding, m_ranks, m_sampleRate, attackSeconds(sounding));
    }
}

// Low reeds are heavier and take longer to speak, and less air makes every reed slower.
double Harmonium::attackSeconds(int soundingKey) const noexcept
{
    const double weight = std::clamp((72.0 - soundingKey) / 48.0, 0.0, 1.0);
    return (0.012 + 0.05 * weight) / (0.4 + 0.6 * std::max(m_pressure, 0.05f));
}

float Harmonium::noise() noexcept
{
    m_seed ^= m_seed << 13; m_seed ^= m_seed >> 17; m_seed ^= m_seed << 5;
    return float(m_seed) * (2.0f / 4294967296.0f) - 1.0f;
}

harmonium::ReedVoice* Harmonium::voiceFor(int key) noexcept
{
    for (auto& v : m_voices) if (v.active() && v.inputKey() == key) return &v;
    return nullptr;
}

// A free voice; past the polyphony the oldest key fades; with the pool full, the quietest goes.
harmonium::ReedVoice* Harmonium::freeVoice() noexcept
{
    int held = 0;
    ReedVoice* oldest = nullptr;
    std::uint64_t oldestStart = 0;
    for (std::size_t i = 0; i < m_voices.size(); ++i) {
        auto& v = m_voices[i];
        if (!v.active() || v.releasing()) continue;
        ++held;
        if (!oldest || m_started[i] < oldestStart) { oldest = &v; oldestStart = m_started[i]; }
    }
    if (held >= Polyphony && oldest) oldest->fadeOut();

    ReedVoice* quietest = nullptr;
    for (auto& v : m_voices) {
        if (!v.active()) return &v;
        if (!quietest || v.level() < quietest->level()) quietest = &v;
    }
    if (quietest) quietest->stop();
    return quietest;
}

void Harmonium::noteOn(int key) noexcept
{
    const int sounding = std::clamp(key + int(std::lround(m_p.transpose->get())), 0, 127);
    auto* voice = voiceFor(key);
    if (!voice || voice->soundingKey() != sounding) voice = freeVoice();
    if (!voice) return;
    for (auto& v : m_voices)                                // playing on ends the sustain tails
        if (&v != voice) v.cutTail();
    voice->start(key, sounding, m_ranks, m_sampleRate, attackSeconds(sounding));
    const auto index = std::size_t(voice - m_voices.data());
    m_started[index] = ++m_clock;
    m_startFrame[index] = m_now;
    m_click = std::min(m_click + 1.0f, 2.0f);
}

// The player has moved on from a key when a newer one is held. Keys of one chord go down within
// a short window of each other, so letting go of a chord note by note still sustains.
bool Harmonium::movedOn(std::size_t voice) const noexcept
{
    const auto window = std::uint64_t(0.08 * m_sampleRate);
    for (std::size_t j = 0; j < m_voices.size(); ++j) {
        const auto& v = m_voices[j];
        if (j != voice && v.active() && !v.releasing() && m_startFrame[j] > m_startFrame[voice] + window)
            return true;
    }
    return false;
}

void Harmonium::noteOff(int key) noexcept
{
    const double sustain = m_p.sustain->get();
    bool released = false;
    for (std::size_t i = 0; i < m_voices.size(); ++i) {
        auto& v = m_voices[i];
        if (!v.active() || v.releasing() || v.inputKey() != key) continue;
        if (sustain > 0.01 && !movedOn(i)) v.sustain(0.5 * sustain, sustain);
        else v.release();
        released = true;
    }
    if (released) m_click = std::min(m_click + 0.5f, 2.0f);     // the key coming back up
}

void Harmonium::allNotesOff() noexcept
{
    for (auto& v : m_voices) { v.release(); v.cutTail(); }
}

void Harmonium::allSoundOff() noexcept
{
    for (auto& v : m_voices) v.stop();
    for (auto& v : m_drones) v.stop();                      // drones come back next block if set
}

void Harmonium::handle(const MidiEvent& e) noexcept
{
    const int type = e.status & 0xF0;
    const int key = e.data1 & 0x7F;
    if (type == 0x90 && e.data2 > 0) noteOn(key);
    else if (type == 0x80 || type == 0x90) noteOff(key);
    else if (type == 0xB0) {
        if (e.data1 == 11) m_expression = float(e.data2) / 127.0f;
        else if (e.data1 == 2) m_breath = float(e.data2) / 127.0f;
        else if (e.data1 == 123) allNotesOff();
        else if (e.data1 == 120) allSoundOff();
    }
}

int Harmonium::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& v : m_voices) n += v.active() ? 1 : 0;
    for (const auto& v : m_drones) n += v.active() ? 1 : 0;
    return n;
}

void Harmonium::renderBlock(float* left, float* right, int frames) noexcept
{
    updateControls();
    Air air;
    air.pressureFrom = m_pressure;
    m_pressure = m_pressureTarget + (m_pressure - m_pressureTarget) * m_pressureCoef;
    air.pressureTo = m_pressure;
    // A reed goes a little flat with more air; in tune at the default bellows.
    double cents = -5.0 * (double(m_pressure) - 0.7);
    float tremolo = 1.0f;
    if (m_style == Style::Western && on(m_p.voxHumana)) {   // the fan tremolo, ~5.5 Hz
        m_tremoloPhase += 5.5 * frames / m_sampleRate;
        m_tremoloPhase -= std::floor(m_tremoloPhase);
        const double wave = std::sin(twoPi * m_tremoloPhase);
        cents += 3.0 * wave;
        tremolo = float(1.0 - 0.3 * (0.5 + 0.5 * wave));
    }
    air.pitchRatio = std::exp2(cents / 1200.0);

    std::array<float, Block> mono{};
    float breath = 0.0f;
    for (auto& v : m_voices) { v.render(mono.data(), frames, air); breath += v.breath(); }
    for (auto& v : m_drones) { v.render(mono.data(), frames, air); breath += v.breath(); }

    // Air through open valves: a little while keys are down, more while reeds speak up. With
    // every valve shut no air flows, so there is none.
    int sounding = 0;
    for (const auto& v : m_voices) sounding += v.active() ? 1 : 0;
    for (const auto& v : m_drones) sounding += v.active() ? 1 : 0;
    const float flow = sounding > 0 ? 0.004f + 0.003f * std::sqrt(float(sounding)) : 0.0f;
    const float air0 = m_p.airNoise->get() * m_pressure * (flow + 0.03f * breath);
    const float click = m_p.keyClick->get() * 0.12f;

    for (int i = 0; i < frames; ++i) {
        double x = mono[std::size_t(i)] * tremolo;
        x += biquad::process(m_airFilter, m_airState, double(noise()) * air0);
        x += biquad::process(m_clickFilter, m_clickState, double(noise()) * m_click * click);
        m_click *= m_clickCoef;
        for (int s = 0; s < BodySections; ++s) x = biquad::process(m_body[std::size_t(s)], m_bodyState[std::size_t(s)], x);
        const float dry = float(x) * m_gain;

        m_delay[std::size_t(m_delayWrite)] = dry;
        const auto tap = [&](int t) {
            int at = m_delayWrite - m_tapFrames[std::size_t(t)];
            if (at < 0) at += MaxDelay;
            return m_delay[std::size_t(at)];
        };
        left[i] += dry + m_reflection * (tap(0) * 0.6f + tap(1) * 0.4f);
        right[i] += dry + m_reflection * (tap(2) * 0.6f + tap(3) * 0.35f);
        if (++m_delayWrite == MaxDelay) m_delayWrite = 0;
    }
}

void Harmonium::renderRange(const AudioView& audio, int start, int end) noexcept
{
    while (start < end) {
        const int frames = std::min(Block, end - start);
        std::array<float, Block> l{}, r{};
        renderBlock(l.data(), r.data(), frames);
        m_now += std::uint64_t(frames);
        if (audio.channelCount == 1) {
            for (int i = 0; i < frames; ++i) audio.channel(0)[start + i] += 0.5f * (l[std::size_t(i)] + r[std::size_t(i)]);
        } else {
            for (int i = 0; i < frames; ++i) {
                audio.channel(0)[start + i] += l[std::size_t(i)];
                audio.channel(1)[start + i] += r[std::size_t(i)];
            }
        }
        start += frames;
    }
}

void Harmonium::process(ProcessContext& context) noexcept
{
    const auto& audio = context.audio;
    if (audio.frames <= 0 || audio.channelCount <= 0 || !audio.channels) return;
    int frame = 0;
    if (context.midi) {
        for (const auto& event : context.midi->events()) {
            const int at = std::clamp(event.offset, 0, audio.frames);
            if (at > frame) { renderRange(audio, frame, at); frame = at; }
            handle(event);
        }
    }
    renderRange(audio, frame, audio.frames);
}
