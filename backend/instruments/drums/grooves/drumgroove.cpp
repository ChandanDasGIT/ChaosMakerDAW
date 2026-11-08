#include "instruments/drums/grooves/drumgroove.h"

#include <algorithm>
#include <cmath>

namespace drumgroove {
namespace {

// The drum keys both kits play (drummap.h).
constexpr int Kick = 60, SideStick = 61, Snare = 62, TomLow = 65, HatClosed = 66, TomMid = 67,
              HatPedal = 68, TomHigh = 69, HatOpen = 70;

constexpr double Hit = 0.25;              // a drum hit's note length (kits play the whole sample)

class Builder {
public:
    Builder(std::string name, int numerator, int denominator, int bars)
    {
        m_groove.name = std::move(name);
        m_groove.numerator = numerator;
        m_groove.denominator = denominator;
        m_groove.bars = bars;
        m_groove.factory = true;
    }
    Builder& hit(int key, double beat, int velocity, double length = Hit)
    {
        MidiNote n;
        n.note = key; n.startBeat = beat; n.lengthBeats = length; n.velocity = velocity;
        m_groove.notes.push_back(n);
        return *this;
    }
    // Hits every `step` beats from `from` (inclusive) to `to` (exclusive); `accent(beat)` gives
    // each one's velocity.
    template <typename Accent>
    Builder& run(int key, double from, double to, double step, Accent accent)
    {
        for (double b = from; b < to - 1e-9; b += step) hit(key, b, accent(b));
        return *this;
    }
    Groove done()
    {
        std::stable_sort(m_groove.notes.begin(), m_groove.notes.end(),
                         [](const MidiNote& a, const MidiNote& b) { return a.startBeat < b.startBeat; });
        int id = 1;
        for (auto& n : m_groove.notes) n.id = id++;
        return std::move(m_groove);
    }

private:
    Groove m_groove;
};

bool onBeat(double beat, double every) { return std::abs(std::fmod(beat, every)) < 1e-9; }

std::vector<Groove> build()
{
    std::vector<Groove> grooves;

    // ---- 3/4 ----
    // Waltz A: kick on one, snare on two and three, hi-hat in eighths leaning on the beats.
    {
        Builder g("Waltz A", 3, 4, 2);
        g.run(HatClosed, 0, 6, 0.5, [](double b) { return onBeat(b, 3) ? 92 : onBeat(b, 1) ? 74 : 52; });
        g.hit(Kick, 0, 112).hit(Snare, 1, 84).hit(Snare, 2, 78)
         .hit(Kick, 3, 108).hit(Snare, 4, 84).hit(Snare, 5, 80).hit(Kick, 5.5, 70);
        grooves.push_back(g.done());
    }
    // Waltz B: a rock waltz, backbeat on two, a pushed kick and a tom fill to close.
    {
        Builder g("Waltz B", 3, 4, 2);
        g.run(HatClosed, 0, 5, 0.5, [](double b) { return onBeat(b, 1) ? 90 : 58; });
        g.hit(Kick, 0, 116).hit(Kick, 1.5, 92).hit(Snare, 1, 108).hit(Snare, 2.75, 34)
         .hit(Kick, 3, 114).hit(Snare, 4, 106).hit(Kick, 4.5, 88)
         .hit(TomHigh, 5, 96).hit(TomHigh, 5.25, 82).hit(TomMid, 5.5, 94).hit(TomLow, 5.75, 104)
         .hit(HatPedal, 5, 60);
        grooves.push_back(g.done());
    }

    // ---- 4/4 ----
    // Rock A: the straight eighth-note rock beat, snare on two and four.
    {
        Builder g("Rock A", 4, 4, 2);
        g.run(HatClosed, 0, 7.5, 0.5, [](double b) { return onBeat(b, 1) ? 94 : 62; });
        g.hit(HatOpen, 7.5, 84, 0.5);
        g.hit(Kick, 0, 116).hit(Kick, 2, 108).hit(Kick, 2.5, 96).hit(Snare, 1, 110).hit(Snare, 3, 110)
         .hit(Kick, 4, 116).hit(Kick, 6, 108).hit(Kick, 6.5, 96).hit(Kick, 7.5, 84)
         .hit(Snare, 5, 110).hit(Snare, 7, 112);
        grooves.push_back(g.done());
    }
    // Rock B: busier, with ghost notes on the snare, a syncopated kick and a fill in bar two.
    {
        Builder g("Rock B", 4, 4, 2);
        g.run(HatClosed, 0, 7, 0.5, [](double b) { return onBeat(b, 1) ? 96 : 66; });
        g.hit(Kick, 0, 118).hit(Kick, 0.75, 86).hit(Kick, 2.5, 104).hit(Kick, 3.5, 90)
         .hit(Snare, 1, 112).hit(Snare, 1.75, 32).hit(Snare, 2.25, 30).hit(Snare, 3, 114).hit(Snare, 3.75, 36)
         .hit(Kick, 4, 118).hit(Kick, 4.75, 86).hit(Kick, 6.5, 104)
         .hit(Snare, 5, 112).hit(Snare, 5.75, 32).hit(Snare, 6, 112)
         .hit(Snare, 7, 100).hit(Snare, 7.25, 86).hit(TomHigh, 7.5, 98).hit(TomMid, 7.625, 92)
         .hit(TomLow, 7.75, 96).hit(Kick, 7.75, 84).hit(HatPedal, 7, 62);
        grooves.push_back(g.done());
    }

    // ---- 7/8 (one bar = 3.5 beats = seven eighths) ----
    // Groove A: counted 2+2+3; kick opens the first and third groups, snare the second, and a
    // second snare in the long group.
    {
        Builder g("Groove A (2+2+3)", 7, 8, 2);
        const auto accent = [](double b) {
            const double inBar = std::fmod(b, 3.5);
            return (onBeat(inBar, 1) && inBar < 2.5) ? 94 : 60;   // 0, 1, 2: the groups' starts
        };
        g.run(HatClosed, 0, 7, 0.5, accent);
        for (int bar = 0; bar < 2; ++bar) {
            const double o = bar * 3.5;
            g.hit(Kick, o + 0, 116).hit(Snare, o + 1, 108).hit(Kick, o + 2, 110).hit(Snare, o + 3, 96);
        }
        g.hit(Kick, 6.0, 84).hit(Snare, 6.75, 40);             // bar two pushes into the long group
        grooves.push_back(g.done());
    }
    // Groove B: counted 3+2+2; a long first group, then two short ones, with the side stick
    // answering in bar two.
    {
        Builder g("Groove B (3+2+2)", 7, 8, 2);
        const auto accent = [](double b) {
            const double inBar = std::fmod(b, 3.5);
            return (onBeat(inBar, 3.5) || std::abs(inBar - 1.5) < 1e-9 || std::abs(inBar - 2.5) < 1e-9) ? 94 : 58;
        };
        g.run(HatClosed, 0, 6.5, 0.5, accent);
        g.hit(HatOpen, 6.5, 82, 0.5);
        g.hit(Kick, 0, 118).hit(Kick, 0.5, 80).hit(Snare, 1.5, 110).hit(Kick, 2.5, 100).hit(Snare, 3, 84)
         .hit(Kick, 3.5, 116).hit(SideStick, 5, 96).hit(Kick, 6, 100).hit(SideStick, 6.5, 90).hit(Snare, 6.75, 42);
        grooves.push_back(g.done());
    }
    return grooves;
}

} // namespace

const std::vector<Groove>& factoryGrooves()
{
    static const std::vector<Groove> grooves = build();
    return grooves;
}

bool validMeter(int numerator, int denominator) noexcept
{
    if (numerator < 1 || numerator > 32) return false;
    for (int d : {1, 2, 4, 8, 16, 32}) if (d == denominator) return true;
    return false;
}

std::optional<std::string> cleanName(std::string_view name)
{
    const auto first = name.find_first_not_of(' ');
    if (first == std::string_view::npos) return std::nullopt;
    const auto last = name.find_last_not_of(' ');
    std::string clean(name.substr(first, last - first + 1));
    if (clean.size() > MaxNameLength) return std::nullopt;
    for (unsigned char c : clean)
        if (c < 0x20 || c == 0x7f || c == '/' || c == '\\') return std::nullopt;
    return clean;
}

std::vector<MidiNote> repeated(const Groove& groove, int bars)
{
    std::vector<MidiNote> out;
    if (bars < 1 || bars > MaxRepeatBars || groove.lengthBeats() <= 0) return out;
    const double end = bars * groove.barBeats();
    int id = 1;
    for (double offset = 0; offset < end - 1e-9; offset += groove.lengthBeats()) {
        for (const auto& n : groove.notes) {
            const double start = offset + n.startBeat;
            if (start >= end - 1e-9) continue;
            MidiNote copy = n;
            copy.startBeat = start;
            copy.lengthBeats = std::min(n.lengthBeats, end - start);
            copy.selected = false;
            copy.id = id++;
            out.push_back(copy);
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const MidiNote& a, const MidiNote& b) { return a.startBeat < b.startBeat; });
    return out;
}

std::optional<Groove> fromClip(std::string_view name, const std::vector<MidiNote>& clipNotes,
                               double clipLengthBeats, int numerator, int denominator, std::string& error)
{
    const auto clean = cleanName(name);
    if (!clean) { error = "Give the groove a name (up to 48 characters, no slashes)."; return std::nullopt; }
    if (!validMeter(numerator, denominator)) { error = "The project's time signature is not supported."; return std::nullopt; }
    if (!std::isfinite(clipLengthBeats) || clipLengthBeats <= 0) { error = "The clip is empty."; return std::nullopt; }

    Groove g;
    g.name = *clean;
    g.numerator = numerator;
    g.denominator = denominator;
    g.bars = std::max(1, static_cast<int>(std::lround(clipLengthBeats / g.barBeats())));
    if (g.bars > MaxBars) { error = "A groove can be at most 16 bars long."; return std::nullopt; }
    const double end = g.lengthBeats();
    for (const auto& n : clipNotes) {
        if (!std::isfinite(n.startBeat) || n.startBeat < -1e-9 || n.startBeat >= end - 1e-9) continue;
        if (n.note < 0 || n.note > 127 || n.velocity < 1 || n.velocity > 127) continue;
        MidiNote copy = n;
        copy.startBeat = std::max(0.0, n.startBeat);
        copy.lengthBeats = std::clamp(std::isfinite(n.lengthBeats) ? n.lengthBeats : Hit, 1e-3, end - copy.startBeat);
        copy.selected = false;
        g.notes.push_back(copy);
    }
    if (g.notes.empty()) { error = "The clip has no notes to keep."; return std::nullopt; }
    if (g.notes.size() > MaxNotes) { error = "The clip has too many notes for a groove."; return std::nullopt; }
    std::stable_sort(g.notes.begin(), g.notes.end(), [](const MidiNote& a, const MidiNote& b) { return a.startBeat < b.startBeat; });
    int id = 1;
    for (auto& n : g.notes) n.id = id++;
    return g;
}

} // namespace drumgroove
