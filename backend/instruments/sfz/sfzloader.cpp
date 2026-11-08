#include "instruments/sfz/sfzloader.h"

#include "audiofile/flacreader.h"
#include "audiofile/wavreader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <set>
#include <stdexcept>

namespace sfz {

namespace {

// Opcodes the sampler plays now, by their base name (CC forms are split off first).
const std::set<std::string, std::less<>> Mapped{
    "sample", "key", "lokey", "hikey", "pitch_keycenter", "pitch_keytrack", "lovel", "hivel",
    "volume", "group_volume", "amplitude", "pan", "tune", "pitch", "transpose",
    "seq_length", "seq_position", "group", "off_by", "trigger",
    "loop_mode", "loopmode", "loop_start", "loopstart", "loop_end", "loopend",
    "ampeg_attack", "ampeg_decay", "ampeg_sustain", "ampeg_release", "offset", "delay",
    "ampeg_hold", "ampeg_dynamic", "cutoff", "resonance", "note_polyphony", "off_time",
    "sustain_cc", "amp_veltrack",
};

// Opcodes that only name things: nothing to play, nothing to report.
const std::set<std::string, std::less<>> Labels{
    "region_label", "group_label", "master_label", "global_label", "sw_label",
};

// Understood, but the sampler does not do them yet (the drums and piano add what they need).
const std::set<std::string, std::less<>> NotYet{
    "offset_random", "delay_random", "end", "count",
    "cutoff", "resonance", "fil_type", "fil_veltrack", "fil_keytrack", "fil_random",
    "polyphony", "polyphony_group", "off_mode",
    "amp_keytrack", "amp_keycenter", "amp_random",
    "ampeg_delay", "ampeg_start",
    "ampeg_attack_shape", "ampeg_decay_shape", "ampeg_release_shape",
    "sustain_sw", "sostenuto_cc", "pitch_veltrack", "pitch_random",
    "rt_decay", "rt_dead", "pitch_keycenter_sample", "direction",
    "sw_lokey", "sw_hikey", "sw_last", "sw_down", "sw_up", "sw_default",
    "xfin_lovel", "xfin_hivel", "xfout_lovel", "xfout_hivel", "xf_velcurve",
    "xfin_lokey", "xfin_hikey", "xfout_lokey", "xfout_hikey", "xf_keycurve",
    "on_locc", "on_hicc", "lorand", "hirand", "lobend", "hibend", "lochanaft", "hichanaft",
};

// Bases whose CC forms (X_onccN, X_ccN, XccN, X_curveccN) the loader works out.
const std::set<std::string, std::less<>> Modulated{
    "amplitude", "volume", "pan", "tune", "pitch",
    "ampeg_attack", "ampeg_decay", "ampeg_sustain", "ampeg_release", "ampeg_hold", "ampeg_delay",
    "offset", "delay", "amp_veltrack",
};

const std::set<std::string, std::less<>> ControlKnown{"default_path", "set_cc", "set_hdcc", "label_cc", "label_key"};
const std::set<std::string, std::less<>> ControlNotYet{"note_offset", "octave_offset", "hint_"};

// Splits "amplitude_oncc100" into ("amplitude_oncc", 100); no trailing number gives -1.
std::pair<std::string, int> splitNumber(const std::string& name)
{
    std::size_t i = name.size();
    while (i > 0 && std::isdigit(static_cast<unsigned char>(name[i - 1]))) --i;
    if (i == name.size() || i == 0) return {name, -1};
    return {name.substr(0, i), std::atoi(name.c_str() + i)};
}

std::string reportName(const std::string& name)
{
    const auto [prefix, n] = splitNumber(name);
    return n < 0 ? name : prefix + "N";
}

bool number(const std::string& text, double& out)
{
    const char* begin = text.c_str();
    char* end = nullptr;
    const double v = std::strtod(begin, &end);
    if (end == begin || !std::isfinite(v)) return false;
    while (*end == ' ') ++end;
    if (*end != '\0') return false;
    out = v;
    return true;
}

struct Mod {
    double value = 0.0;
    int curve = 0;
};

class Curves {
public:
    explicit Curves(const Document& doc)
    {
        for (const auto& c : doc.curves) {
            int index = -1;
            std::array<double, 128> points;
            points.fill(std::nan(""));
            for (const auto& op : c.opcodes) {
                double v = 0.0;
                if (!number(op.value, v)) continue;
                if (op.name == "curve_index") index = int(v);
                else if (op.name.size() == 4 && op.name[0] == 'v') {
                    const int at = std::atoi(op.name.c_str() + 1);
                    if (at >= 0 && at < 128) points[std::size_t(at)] = v;
                }
            }
            if (index < 0 || index > 255) continue;
            if (std::isnan(points[0])) points[0] = 0.0;
            if (std::isnan(points[127])) points[127] = 1.0;
            for (int a = 0; a < 127;) {                         // straight lines between given points
                int b = a + 1;
                while (std::isnan(points[std::size_t(b)])) ++b;
                for (int i = a + 1; i < b; ++i)
                    points[std::size_t(i)] = points[std::size_t(a)] + (points[std::size_t(b)] - points[std::size_t(a)]) * (i - a) / double(b - a);
                a = b;
            }
            m_custom[index] = points;
        }
    }

    // The value of curve `index` for a controller at x (0..1).
    double at(int index, double x) const
    {
        x = std::clamp(x, 0.0, 1.0);
        if (const auto it = m_custom.find(index); it != m_custom.end()) {
            const double p = x * 127.0;
            const int i = std::min(int(p), 126);
            return it->second[std::size_t(i)] + (it->second[std::size_t(i + 1)] - it->second[std::size_t(i)]) * (p - i);
        }
        switch (index) {                                        // the predefined SFZ curves
        case 1: return 2.0 * x - 1.0;                           // bipolar
        case 2: return 1.0 - x;
        case 3: return 1.0 - 2.0 * x;
        case 4: return x * x;
        case 5: return std::sqrt(x);
        case 6: return std::sqrt(1.0 - x);
        default: return x;
        }
    }

private:
    std::map<int, std::array<double, 128>> m_custom;
};

bool isFlac(const std::string& path)
{
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext == ".flac";
}

class Builder {
public:
    Builder(const Document& doc, const LoadOptions& options, LoadResult& out)
        : m_doc(doc), m_options(options), m_out(out), m_curves(doc),
          m_cache(options.cache ? options.cache : std::make_shared<SampleCache>())
    {
        m_cc.fill(0.0);
        for (const auto& op : doc.control) {
            const auto [prefix, n] = splitNumber(op.name);
            double v = 0.0;
            if ((prefix == "set_cc" || prefix == "set_hdcc") && n >= 0 && n < 128) {
                if (!number(op.value, v)) { problem(op, "bad value for " + op.name); continue; }
                m_cc[std::size_t(n)] = std::clamp(prefix == "set_cc" ? v / 127.0 : v, 0.0, 1.0);
            } else if (ControlKnown.contains(prefix)) {
            } else if (ControlNotYet.contains(prefix) || op.name.rfind("hint_", 0) == 0) {
                ++m_out.report.unusedOpcodes["<control> " + reportName(op.name)];
            } else {
                ++m_out.report.unknownOpcodes["<control> " + reportName(op.name)];
            }
        }
        for (const auto& [cc, value] : options.controllers)
            if (cc >= 0 && cc < 128) m_cc[std::size_t(cc)] = std::clamp(value / 127.0, 0.0, 1.0);
    }

    void region(const Region& r)
    {
        ++m_out.report.regions;
        std::set<std::string> unused, unknown;

        SampleZone z;
        std::string sample;
        int keyTrack = 100;
        double volume = 0.0, groupVolume = 0.0, amplitude = 100.0, pan = 0.0, tune = 0.0, transpose = 0.0;
        double attack = 0.0, decay = 0.0, sustain = 100.0, release = 0.001, offset = 0.0, delay = 0.0;
        double hold = 0.0, cutoff = 0.0, resonance = 0.0, velTrack = 100.0;
        std::string filterType;
        std::map<int, double> velocityPoints;                   // amp_velcurve_N
        std::string loopMode, trigger = "attack";
        bool loopStartSet = false, loopEndSet = false;
        std::map<std::string, std::map<int, Mod>> mods;         // base -> cc -> modulation
        std::map<int, std::pair<int, int>> conditions;          // cc -> lo, hi

        for (const auto& op : r.opcodes) {
            const std::string& name = op.name;
            if (Labels.contains(name)) continue;
            double v = 0.0;
            const auto num = [&]() {
                if (number(op.value, v)) return true;
                problem(op, "bad value for " + name + ": " + op.value);
                return false;
            };
            const auto key = [&](int& field) {
                const int k = parseKey(op.value);
                if (k < -1) problem(op, "bad key for " + name + ": " + op.value);
                else field = k;
            };

            if (name == "sample") { sample = op.value; continue; }
            if (name == "key") { int k = 0; key(k); z.loKey = z.hiKey = z.rootKey = k; continue; }
            if (name == "lokey") { key(z.loKey); continue; }
            if (name == "hikey") { key(z.hiKey); continue; }
            if (name == "pitch_keycenter") { key(z.rootKey); continue; }
            if (name == "trigger") { trigger = op.value; continue; }
            if (name == "loop_mode" || name == "loopmode") { loopMode = op.value; continue; }
            if (name == "fil_type") { filterType = op.value; continue; }

            if (Mapped.contains(name)) {
                if (!num()) continue;
                if (name == "pitch_keytrack") keyTrack = int(v);
                else if (name == "lovel") z.loVelocity = int(v);
                else if (name == "hivel") z.hiVelocity = int(v);
                else if (name == "volume") volume = v;
                else if (name == "group_volume") groupVolume = v;
                else if (name == "amplitude") amplitude = v;
                else if (name == "pan") pan = v;
                else if (name == "tune" || name == "pitch") tune = v;
                else if (name == "transpose") transpose = v;
                else if (name == "seq_length") z.roundRobinLength = int(v);
                else if (name == "seq_position") z.roundRobinPosition = int(v);
                else if (name == "group") z.group = int(v);
                else if (name == "off_by") z.offBy = int(v);
                else if (name == "loop_start" || name == "loopstart") { z.loopStart = std::int64_t(v); loopStartSet = true; }
                else if (name == "loop_end" || name == "loopend") { z.loopEnd = std::int64_t(v); loopEndSet = true; }
                else if (name == "ampeg_attack") attack = v;
                else if (name == "ampeg_decay") decay = v;
                else if (name == "ampeg_sustain") sustain = v;
                else if (name == "ampeg_release") release = v;
                else if (name == "offset") offset = v;
                else if (name == "delay") delay = v;
                else if (name == "ampeg_hold") hold = v;
                else if (name == "ampeg_dynamic") z.dynamicRelease = v != 0.0;
                else if (name == "cutoff") cutoff = v;
                else if (name == "resonance") resonance = v;
                else if (name == "note_polyphony") z.notePolyphony = std::max(0, int(v));
                else if (name == "off_time") z.offSeconds = float(std::clamp(v, 0.0, 30.0));
                else if (name == "sustain_cc") z.sustainCc = std::clamp(int(v), 0, 127);
                else if (name == "amp_veltrack") velTrack = v;
                continue;
            }

            const auto [prefix, n] = splitNumber(name);
            if (n >= 0) {
                if (prefix == "amp_velcurve_") {
                    if (!num()) continue;
                    if (n <= 127) velocityPoints[n] = v;
                    continue;
                }
                if (prefix == "locc" || prefix == "hicc") {
                    if (!num()) continue;
                    auto& range = conditions.try_emplace(n, 0, 127).first->second;
                    (prefix == "locc" ? range.first : range.second) = int(v);
                    continue;
                }
                if (const auto mod = modulation(prefix)) {
                    if (!num()) continue;
                    auto& m = mods[mod->first][n];
                    if (mod->second) m.curve = int(v); else m.value = v;
                    if (!Mapped.contains(mod->first)) unused.insert(reportName(name));   // hold, delay
                    continue;
                }
            }
            if (isNotYet(name, prefix, n)) unused.insert(reportName(name));
            else unknown.insert(reportName(name));
        }
        for (const auto& u : unused) ++m_out.report.unusedOpcodes[u];
        for (const auto& u : unknown) ++m_out.report.unknownOpcodes[u];

        const auto leftOut = [&](const std::string& why) {
            ++m_out.report.leftOutOther;
            if (!why.empty()) problem(r, why);
        };

        // Controllers at their defaults decide whether the region is there at all; a live one
        // (the pedal) becomes the zone's condition, checked when a note starts.
        for (const auto& [cc, range] : conditions) {
            if (m_options.liveControllers.contains(cc)) {
                if (z.liveCc >= 0) { problem(r, "only one live controller range per region: locc/hicc" + std::to_string(cc) + " left out"); continue; }
                z.liveCc = cc;
                z.liveLo = range.first;
                z.liveHi = range.second;
                continue;
            }
            const int value = int(std::lround(ccValue(cc, z.rootKey) * 127.0));
            if (value < range.first || value > range.second) { ++m_out.report.leftOutByControllers; return; }
        }
        if (trigger == "release") z.releaseTrigger = true;
        else if (trigger != "attack") { ++m_out.report.unusedOpcodes["trigger=" + trigger]; leftOut(""); return; }
        if (sample.empty()) { leftOut("region has no sample"); return; }
        if (z.loKey < 0 || z.hiKey < 0) { leftOut(""); ++m_out.report.unusedOpcodes["keyless region (lokey=-1)"]; return; }
        if (sample == "*silence") {                             // a hit that only chokes its group
            z.silent = true;
            m_out.zones.push_back(std::move(z));
            m_out.sources.push_back({sample, r.file, r.line});
            return;
        }
        if (sample[0] == '*') { ++m_out.report.unusedOpcodes["sample=" + sample]; leftOut(""); return; }

        if (keyTrack == 0) {
            if (z.loKey != z.hiKey) problem(r, "pitch_keytrack=0 over a key range: played with pitch tracking");
            else z.rootKey = z.loKey;
        } else if (keyTrack != 100) {
            problem(r, "pitch_keytrack=" + std::to_string(keyTrack) + " is played as 100");
        }

        // The modulations, at the default controller values.
        bool controlled = false;
        double gain = amplitude / 100.0;
        for (const auto& [cc, m] : mods["amplitude"]) { gain *= m.value / 100.0 * m_curves.at(m.curve, ccValue(cc, z.rootKey)); controlled = true; }
        const auto add = [&](const char* base) {
            double sum = 0.0;
            for (const auto& [cc, m] : mods[base]) sum += m.value * m_curves.at(m.curve, ccValue(cc, z.rootKey));
            return sum;
        };
        // A release lengthened by a live controller (the pedal) is left to the sampler.
        for (auto it = mods["ampeg_release"].begin(); it != mods["ampeg_release"].end();) {
            if (!m_options.liveControllers.contains(it->first)) { ++it; continue; }
            if (z.releaseCc < 0) {
                z.releaseCc = it->first;
                z.releaseCcSeconds = float(it->second.value);
                z.releaseCcCurve = controllerCurve(it->second.curve);
            } else {
                problem(r, "only one live controller lengthens a release: ampeg_release_cc" + std::to_string(it->first) + " left out");
            }
            it = mods["ampeg_release"].erase(it);
        }
        volume += groupVolume + add("volume");
        pan += add("pan");
        tune += add("tune") + add("pitch") + transpose * 100.0;
        attack += add("ampeg_attack");
        decay += add("ampeg_decay");
        sustain += add("ampeg_sustain");
        release += add("ampeg_release");
        offset += add("offset");
        delay += add("delay");
        hold += add("ampeg_hold");
        velTrack += add("amp_veltrack");
        if (!(gain > 0.0)) {
            if (controlled) ++m_out.report.leftOutByControllers; else leftOut("");
            return;
        }
        z.volumeDb = float(std::clamp(volume + 20.0 * std::log10(gain), -144.0, 24.0));
        z.pan = float(std::clamp(pan / 100.0, -1.0, 1.0));
        z.tuneCents = float(tune);
        z.attack = float(std::max(attack, 0.0));
        z.decay = float(std::max(decay, 0.0));
        z.sustain = float(std::clamp(sustain / 100.0, 0.0, 1.0));
        z.release = float(std::max(release, 0.0));
        z.offset = std::max<std::int64_t>(0, std::llround(offset));
        z.delaySeconds = float(std::clamp(delay, 0.0, 10.0));
        z.hold = float(std::clamp(hold, 0.0, 100.0));
        if (velTrack < 0.0) problem(r, "a negative amp_veltrack is played as 0");
        velTrack = std::clamp(velTrack, 0.0, 100.0);
        if (!velocityPoints.empty() || velTrack != 100.0) z.velocityCurve = velocityCurve(velocityPoints, velTrack);
        if (cutoff > 0.0) {
            if (filterType.empty() || filterType == "lpf_2p") {
                z.cutoffHz = float(cutoff);
                z.resonanceDb = float(resonance);
            } else {
                ++m_out.report.unusedOpcodes["fil_type=" + filterType];   // not played: no filter
            }
        }

        std::string relative = r.defaultPath + sample;
        std::replace(relative.begin(), relative.end(), '\\', '/');
        const std::string path = (std::filesystem::path(m_doc.folder) / relative).lexically_normal().string();
        const SampleCache::Entry entry = load(path);
        if (!entry.usable) { leftOut(""); return; }
        z.audio = entry.audio;

        if (loopMode.empty()) loopMode = entry.hasLoop ? "loop_continuous" : "no_loop";
        if (loopMode == "one_shot") z.oneShot = true;
        else if (loopMode == "loop_continuous" || loopMode == "loop_sustain") {
            if (loopMode == "loop_sustain") ++m_out.report.unusedOpcodes["loop_mode=loop_sustain (played as loop_continuous)"];
            z.looping = true;
            if (!loopStartSet) z.loopStart = entry.loopStart;
            if (!loopEndSet) z.loopEnd = entry.loopEnd;
            if (z.loopEnd <= z.loopStart) { z.looping = false; problem(r, "loop asked for, but the sample has no loop points"); }
        } else if (loopMode != "no_loop") {
            problem(r, "unknown loop_mode " + loopMode + ": played once");
        }
        m_out.zones.push_back(std::move(z));
        m_out.sources.push_back({path, r.file, r.line});
    }

private:
    // "amplitude_oncc" -> (amplitude, false); "pan_curvecc" -> (pan, true). Any base not
    // modulated gives nothing.
    static std::optional<std::pair<std::string, bool>> modulation(const std::string& prefix)
    {
        for (const char* form : {"_curvecc", "_oncc", "_cc", "cc"}) {
            const std::string f(form);
            if (prefix.size() <= f.size() || prefix.compare(prefix.size() - f.size(), f.size(), f) != 0) continue;
            const std::string base = prefix.substr(0, prefix.size() - f.size());
            if (Modulated.contains(base)) return std::make_pair(base, f == "_curvecc");
        }
        return std::nullopt;
    }

    static bool isNotYet(const std::string& name, const std::string& prefix, int n)
    {
        if (NotYet.contains(name) || (n >= 0 && NotYet.contains(prefix))) return true;
        if (n < 0) return false;
        // CC forms of opcodes not played yet: offset_cc110, delay_oncc110, cutoff_curvecc1 ...
        for (const char* form : {"_curvecc", "_smoothcc", "_stepcc", "_oncc", "_cc", "cc"}) {
            const std::string f(form);
            if (prefix.size() > f.size() && prefix.compare(prefix.size() - f.size(), f.size(), f) == 0) {
                const std::string base = prefix.substr(0, prefix.size() - f.size());
                if (NotYet.contains(base) || Modulated.contains(base)) return true;
            }
        }
        return false;
    }

    // The gain for each velocity through the given points (0 at 0 and 1 at 127 unless given),
    // straight lines between them; with no points, (velocity / 127)^2. amp_veltrack (0..100 %)
    // then sets how much velocity counts: gain = 1 - track * (1 - curve). Regions with the same
    // points and tracking share one curve.
    std::shared_ptr<const SampleZone::VelocityCurve> velocityCurve(const std::map<int, double>& given, double track)
    {
        const auto key = std::make_pair(given, track);
        if (const auto it = m_velocityCurves.find(key); it != m_velocityCurves.end()) return it->second;
        auto curve = std::make_shared<SampleZone::VelocityCurve>();
        if (given.empty()) {
            for (int v = 0; v < 128; ++v) (*curve)[std::size_t(v)] = float(v / 127.0 * (v / 127.0));
        } else {
            std::map<int, double> points = given;
            points.try_emplace(0, 0.0);
            points.try_emplace(127, 1.0);
            auto left = points.begin();
            for (auto right = std::next(left); right != points.end(); left = right++) {
                for (int v = left->first; v <= right->first; ++v) {
                    const double t = double(v - left->first) / double(right->first - left->first);
                    (*curve)[std::size_t(v)] = float(std::clamp(left->second + (right->second - left->second) * t, 0.0, 1.0));
                }
            }
        }
        const float t = float(track / 100.0);
        for (auto& g : *curve) g = 1.0f - t * (1.0f - g);
        m_velocityCurves.emplace(key, curve);
        return curve;
    }

    // A controller curve (an SFZ curve index) as a table for the 128 controller values.
    std::shared_ptr<const SampleZone::ControllerCurve> controllerCurve(int index)
    {
        if (const auto it = m_controllerCurves.find(index); it != m_controllerCurves.end()) return it->second;
        auto curve = std::make_shared<SampleZone::ControllerCurve>();
        for (int v = 0; v < 128; ++v) (*curve)[std::size_t(v)] = float(m_curves.at(index, v / 127.0));
        m_controllerCurves.emplace(index, curve);
        return curve;
    }

    double ccValue(int cc, int rootKey) const
    {
        if (cc >= 0 && cc < 128) return m_cc[std::size_t(cc)];
        if (cc == 133) return std::clamp(rootKey, 0, 127) / 127.0;   // the key played
        return 0.0;
    }

    SampleCache::Entry load(const std::string& path)
    {
        std::lock_guard lock(m_cache->mutex);
        if (const auto it = m_cache->entries.find(path); it != m_cache->entries.end()) return it->second;
        SampleCache::Entry& e = m_cache->entries[path];
        auto& report = m_out.report;
        if (!std::filesystem::exists(path)) { report.missingSamples.push_back(path); return e; }
        if (m_options.samples == SampleMode::Exists) { e.usable = true; return e; }
        try {
            std::shared_ptr<AudioData> audio;
            if (isFlac(path)) {
                audio = audiofile::readFlac(path).audio;           // FLAC carries no loop points
            } else {
                auto wav = audiofile::readWav(path);
                e.hasLoop = wav.hasLoop;
                e.loopStart = wav.loopStart;
                e.loopEnd = wav.loopEnd;
                audio = std::move(wav.audio);
            }
            e.usable = true;
            ++report.samplesRead;
            report.sampleMegabytes += double(audio->frames) * double(audio->channels.size()) * 4.0 / 1e6;
            if (m_options.samples == SampleMode::Load) e.audio = std::move(audio);
        } catch (const std::exception& error) {
            report.unreadableSamples[path] = error.what();
        }
        return e;
    }

    void problem(const Opcode& op, std::string message) { m_out.report.problems.push_back({op.file, op.line, std::move(message)}); }
    void problem(const Region& r, std::string message) { m_out.report.problems.push_back({r.file, r.line, std::move(message)}); }

    const Document& m_doc;
    const LoadOptions& m_options;
    LoadResult& m_out;
    Curves m_curves;
    std::array<double, 128> m_cc{};
    std::shared_ptr<SampleCache> m_cache;
    std::map<std::pair<std::map<int, double>, double>, std::shared_ptr<const SampleZone::VelocityCurve>> m_velocityCurves;
    std::map<int, std::shared_ptr<const SampleZone::ControllerCurve>> m_controllerCurves;
};

} // namespace

int parseKey(std::string_view text) noexcept
{
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
    if (text.empty()) return -2;
    double v = 0.0;
    if (number(std::string(text), v)) return v >= -1.0 && v <= 127.0 && v == std::floor(v) ? int(v) : -2;
    static constexpr int Steps[] = {9, 11, 0, 2, 4, 5, 7};    // a b c d e f g
    const char letter = char(std::tolower(static_cast<unsigned char>(text[0])));
    if (letter < 'a' || letter > 'g') return -2;
    int note = Steps[letter - 'a'];
    std::size_t i = 1;
    if (i < text.size() && text[i] == '#') { ++note; ++i; }
    else if (i < text.size() && text[i] == 'b') { --note; ++i; }
    const std::string octave(text.substr(i));
    double o = 0.0;
    if (octave.empty() || !number(octave, o) || o != std::floor(o)) return -2;
    const int key = (int(o) + 1) * 12 + note;                  // c4 = 60
    return key >= 0 && key <= 127 ? key : -2;
}

LoadResult buildZones(const Document& document, const LoadOptions& options)
{
    LoadResult out;
    out.report.problems = document.diagnostics;
    Builder builder(document, options, out);
    for (const auto& region : document.regions) builder.region(region);
    std::sort(out.report.missingSamples.begin(), out.report.missingSamples.end());
    return out;
}

LoadResult loadSfz(const std::string& path, const LoadOptions& options)
{
    return buildZones(parseFile(path), options);
}

} // namespace sfz
