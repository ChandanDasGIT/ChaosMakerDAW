#include "plugins/chordplayer/chordsymbol.h"

#include <algorithm>

namespace chords {
namespace {

struct Quality {
    std::string_view canonical;
    std::vector<int> intervals;
    std::vector<std::string_view> spellings;   // every way the suffix may be written
};

const std::vector<Quality>& qualities()
{
    static const std::vector<Quality> table{
        {"", {0, 4, 7}, {"", "maj", "M"}},
        {"m", {0, 3, 7}, {"m", "min", "-"}},
        {"dim", {0, 3, 6}, {"dim", "°", "o"}},
        {"aug", {0, 4, 8}, {"aug", "+"}},
        {"sus2", {0, 2, 7}, {"sus2"}},
        {"sus4", {0, 5, 7}, {"sus4", "sus"}},
        {"5", {0, 7}, {"5"}},
        {"6", {0, 4, 7, 9}, {"6"}},
        {"m6", {0, 3, 7, 9}, {"m6", "min6"}},
        {"7", {0, 4, 7, 10}, {"7"}},
        {"maj7", {0, 4, 7, 11}, {"maj7", "M7", "Δ", "Δ7"}},
        {"m7", {0, 3, 7, 10}, {"m7", "min7", "-7"}},
        {"mMaj7", {0, 3, 7, 11}, {"mMaj7", "mmaj7", "m(maj7)", "minmaj7"}},
        {"dim7", {0, 3, 6, 9}, {"dim7", "°7", "o7"}},
        {"m7b5", {0, 3, 6, 10}, {"m7b5", "ø", "ø7", "min7b5"}},
        {"aug7", {0, 4, 8, 10}, {"aug7", "7#5", "+7"}},
        {"7sus4", {0, 5, 7, 10}, {"7sus4", "7sus"}},
        {"9", {0, 4, 7, 10, 14}, {"9"}},
        {"maj9", {0, 4, 7, 11, 14}, {"maj9", "M9"}},
        {"m9", {0, 3, 7, 10, 14}, {"m9", "min9"}},
        {"add9", {0, 4, 7, 14}, {"add9"}},
        {"madd9", {0, 3, 7, 14}, {"madd9", "m(add9)"}},
    };
    return table;
}

constexpr std::array<int, 7> LetterClass{9, 11, 0, 2, 4, 5, 7};   // A B C D E F G

// "#" / "b" and their Unicode forms. Returns the accidental (+1 / -1 / 0) and how much it used.
std::pair<int, std::size_t> accidental(std::string_view s)
{
    if (s.starts_with("#")) return {1, 1};
    if (s.starts_with("♯")) return {1, 3};          // ♯
    if (s.starts_with("b")) return {-1, 1};
    if (s.starts_with("♭")) return {-1, 3};         // ♭
    return {0, 0};
}

// A note name at the start of `s`: pitch class, its spelling and the bytes it used.
struct Note { int pitchClass; std::string spelling; std::size_t used; };
std::optional<Note> readNote(std::string_view s, bool allowLowerCase)
{
    if (s.empty()) return std::nullopt;
    char letter = s[0];
    if (allowLowerCase && letter >= 'a' && letter <= 'g') letter = static_cast<char>(letter - 'a' + 'A');
    if (letter < 'A' || letter > 'G') return std::nullopt;
    int pc = LetterClass[letter - 'A'];
    std::string spelling(1, letter);
    const auto [shift, used] = accidental(s.substr(1));
    pc = (pc + shift + 12) % 12;
    if (shift > 0) spelling += '#';
    if (shift < 0) spelling += 'b';
    return Note{pc, spelling, 1 + used};
}

int mod12(int v) { return ((v % 12) + 12) % 12; }

Chord make(int root, const Quality& q, int bass, std::string rootName, std::string bassName)
{
    Chord c;
    c.root = root;
    c.intervals = q.intervals;
    c.bass = (bass >= 0 && bass != root) ? bass : -1;
    c.name = rootName + std::string(q.canonical) + (c.bass >= 0 ? "/" + bassName : std::string());
    return c;
}

const Quality* qualityNamed(std::string_view canonical)
{
    for (const auto& q : qualities()) if (q.canonical == canonical) return &q;
    return nullptr;
}

constexpr std::array<int, 7> MajorScale{0, 2, 4, 5, 7, 9, 11};
constexpr std::array<int, 7> MinorScale{0, 2, 3, 5, 7, 8, 10};

} // namespace

std::optional<Chord> parse(std::string_view text)
{
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
    const auto root = readNote(text, true);
    if (!root) return std::nullopt;
    std::string_view rest = text.substr(root->used);
    std::string_view suffix = rest;
    std::optional<Note> bass;
    if (const auto slash = rest.find('/'); slash != std::string_view::npos) {
        suffix = rest.substr(0, slash);
        const std::string_view bassText = rest.substr(slash + 1);
        bass = readNote(bassText, true);
        if (!bass || bass->used != bassText.size()) return std::nullopt;
    }
    for (const auto& q : qualities())
        for (const auto spelling : q.spellings)
            if (suffix == spelling)
                return make(root->pitchClass, q, bass ? bass->pitchClass : -1, root->spelling, bass ? bass->spelling : std::string());
    return std::nullopt;
}

std::string noteName(int pitchClass, bool flats)
{
    static constexpr std::array<std::string_view, 12> Sharps{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static constexpr std::array<std::string_view, 12> Flats{"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    return std::string((flats ? Flats : Sharps)[static_cast<std::size_t>(mod12(pitchClass))]);
}

bool usesFlats(const Key& key) noexcept
{
    // Major keys with flats: F Bb Eb Ab Db Gb; their relative minors: D G C F Bb Eb.
    const int major = key.minor ? mod12(key.tonic + 3) : mod12(key.tonic);
    return major == 5 || major == 10 || major == 3 || major == 8 || major == 1 || major == 6;
}

std::string tonicLabel(int pitchClass)
{
    const int pc = mod12(pitchClass);
    const std::string sharp = noteName(pc, false), flat = noteName(pc, true);
    return sharp == flat ? sharp : sharp + "/" + flat;
}

std::string keyName(const Key& key)
{
    return tonicLabel(key.tonic) + (key.minor ? " minor" : " major");
}

std::array<std::string, 7> degreeLabels(const Key& key)
{
    if (key.minor) return {"i", "ii°", "III", "iv", "v", "VI", "VII"};
    return {"I", "ii", "iii", "IV", "V", "vi", "vii°"};
}

std::optional<Chord> degreeChord(const Key& key, int degree, bool seventh)
{
    if (degree < 1 || degree > 7) return std::nullopt;
    static constexpr std::array<std::string_view, 7> MajorSevenths{"Imaj7", "ii7", "iii7", "IVmaj7", "V7", "vi7", "viiø7"};
    static constexpr std::array<std::string_view, 7> MinorSevenths{"i7", "iiø7", "IIImaj7", "iv7", "v7", "VImaj7", "VII7"};
    if (seventh) return romanChord(key, (key.minor ? MinorSevenths : MajorSevenths)[static_cast<std::size_t>(degree - 1)]);
    return romanChord(key, degreeLabels(key)[static_cast<std::size_t>(degree - 1)]);
}

std::optional<Chord> romanChord(const Key& key, std::string_view token)
{
    const auto [shift, used] = accidental(token);
    token.remove_prefix(used);
    static constexpr std::array<std::string_view, 7> Upper{"VII", "VI", "V", "IV", "III", "II", "I"};
    static constexpr std::array<int, 7> Degree{7, 6, 5, 4, 3, 2, 1};
    int degree = 0;
    bool upper = false;
    for (std::size_t i = 0; i < Upper.size() && degree == 0; ++i) {
        std::string lower(Upper[i]);
        for (auto& ch : lower) ch = static_cast<char>(ch - 'A' + 'a');
        if (token.starts_with(Upper[i])) { degree = Degree[i]; upper = true; token.remove_prefix(Upper[i].size()); }
        else if (token.starts_with(lower)) { degree = Degree[i]; upper = false; token.remove_prefix(lower.size()); }
    }
    if (degree == 0) return std::nullopt;

    std::string_view quality;
    if (token.empty()) quality = upper ? "" : "m";
    else if (token == "°" || token == "o") quality = "dim";
    else if (token == "°7" || token == "o7") quality = "dim7";
    else if (token == "+") quality = "aug";
    else if (token == "ø" || token == "ø7") quality = "m7b5";
    else if (token == "7") quality = upper ? "7" : "m7";
    else if (token == "maj7") quality = upper ? "maj7" : "mMaj7";
    else if (token == "sus4" || token == "sus2") quality = token;
    else if (token == "9") quality = upper ? "9" : "m9";
    else return std::nullopt;
    const Quality* q = qualityNamed(quality);
    if (!q) return std::nullopt;

    const auto& scale = key.minor ? MinorScale : MajorScale;
    const int root = mod12(key.tonic + scale[static_cast<std::size_t>(degree - 1)] + shift);
    const bool flats = shift < 0 || (shift == 0 && usesFlats(key));
    return make(root, *q, -1, noteName(root, flats), {});
}

std::span<const Progression> progressions()
{
    static constexpr std::array<Progression, 7> List{{
        {{"Key chords", "Key chords"}, "The key's own chords, first to seventh, then the dominant seventh",
         {{{"I", "ii", "iii", "IV", "V", "vi", "vii°", "V7"}, {"i", "ii°", "III", "iv", "v", "VI", "VII", "V7"}}}},
        {{"Pop I–V–vi–IV", "Pop i–v–VI–iv"}, "The four-chord pop song, then four more of the key",
         {{{"I", "V", "vi", "IV", "ii", "iii", "V7", "IVmaj7"}, {"i", "v", "VI", "iv", "ii°", "III", "V7", "iv7"}}}},
        {{"Fifties I–vi–IV–V", "Fifties i–VI–iv–v"}, "Doo-wop, then four more of the key",
         {{{"I", "vi", "IV", "V", "ii", "iii", "V7", "I7"}, {"i", "VI", "iv", "v", "ii°", "III", "V7", "i7"}}}},
        {{"Jazz ii–V–I", "Jazz iiø–V–i"}, "The jazz turn in sevenths, then five more",
         {{{"ii7", "V7", "Imaj7", "vi7", "iii7", "IVmaj7", "II7", "VI7"},
           {"iiø7", "V7", "i7", "VImaj7", "IIImaj7", "iv7", "II7", "VI7"}}}},
        {{"Andalusian I–vii°–vi–V", "Andalusian i–VII–VI–V"}, "Stepping down to the fifth (Am G F E in A minor); flamenco and film",
         {{{"I", "vii°", "vi", "V", "IV", "iii", "ii", "V7"}, {"i", "VII", "VI", "V", "iv", "III", "ii°", "V7"}}}},
        {{"Filmi I–IV–vii°–iii", "Filmi i–iv–VII–III"}, "A film song turn",
         {{{"I", "IV", "vii°", "iii", "vi", "ii", "V", "V7"}, {"i", "iv", "VII", "III", "VI", "ii°", "V", "v"}}}},
        {{"Filmi I–IV–V–bVII", "Filmi i–iv–v–VII"}, "With the flat seven",
         {{{"I", "IV", "V", "bVII", "vi", "ii", "iii", "V7"}, {"i", "iv", "v", "VII", "VI", "ii°", "III", "V7"}}}},
    }};
    return List;
}

std::vector<Chord> progressionChords(const Progression& progression, const Key& key)
{
    std::vector<Chord> out;
    for (const auto token : progression.tokens[key.minor ? 1 : 0]) {
        auto chord = romanChord(key, token);
        if (!chord) return {};
        out.push_back(std::move(*chord));
    }
    return out;
}

int matchProgression(std::span<const std::string> strips, const Key& key)
{
    const auto list = progressions();
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto chords = progressionChords(list[i], key);
        if (chords.size() != strips.size()) continue;
        bool same = true;
        for (std::size_t k = 0; k < chords.size() && same; ++k) {
            const auto chord = parse(strips[k]);
            same = chord && *chord == chords[k];
        }
        if (same) return int(i);
    }
    return -1;
}

} // namespace chords
