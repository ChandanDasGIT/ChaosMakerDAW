#include "synthjson.h"

#include <cmath>

namespace persistence::synthjson {
namespace {

using nlohmann::json;
using namespace synth;

// Optional number: absent keeps `out`; present must be finite and in range.
bool number(const json& object, const char* key, double min, double max, double& out)
{
    if (!object.contains(key)) return true;
    if (!object[key].is_number()) return false;
    const double value = object[key].get<double>();
    if (!std::isfinite(value) || value < min || value > max) return false;
    out = value;
    return true;
}

bool integer(const json& object, const char* key, int min, int max, int& out)
{
    if (!object.contains(key)) return true;
    if (!object[key].is_number_integer()) return false;
    const auto value = object[key].get<long long>();
    if (value < min || value > max) return false;
    out = int(value);
    return true;
}

bool boolean(const json& object, const char* key, bool& out)
{
    if (!object.contains(key)) return true;
    if (!object[key].is_boolean()) return false;
    out = object[key].get<bool>();
    return true;
}

template <typename E>
bool name(const json& object, const char* key, bool (*fromName)(std::string_view, E&) noexcept, E& out)
{
    if (!object.contains(key)) return true;
    return object[key].is_string() && fromName(object[key].get<std::string>(), out);
}

// An optional object: absent is fine; present must be an object.
const json* section(const json& object, const char* key, bool& ok)
{
    if (!object.contains(key)) return nullptr;
    if (!object[key].is_object()) { ok = false; return nullptr; }
    return &object[key];
}

json envelopeJson(const Envelope& e)
{
    return {{"attack", e.attack}, {"decay", e.decay}, {"sustain", e.sustain}, {"release", e.release}};
}

bool readEnvelope(const json& j, Envelope& e)
{
    return number(j, "attack", MinEnvSeconds, MaxEnvSeconds, e.attack)
        && number(j, "decay", MinEnvSeconds, MaxEnvSeconds, e.decay)
        && number(j, "sustain", 0.0, 1.0, e.sustain)
        && number(j, "release", MinEnvSeconds, MaxEnvSeconds, e.release);
}

json oscillatorJson(const Oscillator& o)
{
    return {{"on", o.on}, {"wave", std::string(waveName(o.wave))}, {"octave", o.octave},
            {"semitones", o.semitones}, {"fine", o.fineCents}, {"pulseWidth", o.pulseWidth},
            {"level", o.level}};
}

bool readOscillator(const json& j, Oscillator& o)
{
    return boolean(j, "on", o.on) && name(j, "wave", waveFromName, o.wave)
        && integer(j, "octave", -MaxOctave, MaxOctave, o.octave)
        && integer(j, "semitones", -MaxSemitones, MaxSemitones, o.semitones)
        && number(j, "fine", -MaxFineCents, MaxFineCents, o.fineCents)
        && number(j, "pulseWidth", MinPulseWidth, MaxPulseWidth, o.pulseWidth)
        && number(j, "level", 0.0, 1.0, o.level);
}

} // namespace

json toJson(const Settings& s)
{
    const auto& f = s.filter;
    const auto& l = s.lfo;
    return {
        {"osc1", oscillatorJson(s.osc[0])},
        {"osc2", oscillatorJson(s.osc[1])},
        {"sub", {{"wave", std::string(subWaveName(s.subWave))}, {"level", s.subLevel}}},
        {"noise", s.noiseLevel},
        {"filter", {{"mode", std::string(filterModeName(f.mode))}, {"cutoff", f.cutoffHz},
                    {"resonance", f.resonance}, {"drive", f.drive}, {"keyTrack", f.keyTrack},
                    {"envAmount", f.envOctaves}, {"velocity", f.velocity}}},
        {"filterEnv", envelopeJson(s.filterEnv)},
        {"ampEnv", envelopeJson(s.ampEnv)},
        {"ampVelocity", s.ampVelocity},
        {"lfo", {{"shape", std::string(lfoShapeName(l.shape))}, {"target", std::string(lfoTargetName(l.target))},
                 {"rate", l.rateHz}, {"sync", l.sync}, {"division", l.division},
                 {"depth", l.depth}, {"delay", l.delaySeconds}}},
        {"mode", std::string(playModeName(s.mode))},
        {"glide", s.glideSeconds},
        {"bendRange", s.bendSemitones},
        {"volume", s.volumeDb},
    };
}

bool fromJson(const json& j, Settings& settings, std::string& error)
{
    error = "Invalid synth settings.";
    if (!j.is_object()) return false;
    Settings r;
    bool ok = true;

    const char* oscKeys[OscillatorCount] = {"osc1", "osc2"};
    for (int i = 0; i < OscillatorCount; ++i)
        if (const auto* o = section(j, oscKeys[i], ok); o && !readOscillator(*o, r.osc[std::size_t(i)])) return false;
    if (const auto* sub = section(j, "sub", ok)) {
        if (!name(*sub, "wave", subWaveFromName, r.subWave) || !number(*sub, "level", 0.0, 1.0, r.subLevel))
            return false;
    }
    if (!number(j, "noise", 0.0, 1.0, r.noiseLevel)) return false;
    if (const auto* f = section(j, "filter", ok)) {
        auto& t = r.filter;
        if (!name(*f, "mode", filterModeFromName, t.mode)
            || !number(*f, "cutoff", MinCutoffHz, MaxCutoffHz, t.cutoffHz)
            || !number(*f, "resonance", 0.0, 1.0, t.resonance)
            || !number(*f, "drive", 0.0, 1.0, t.drive)
            || !number(*f, "keyTrack", 0.0, 1.0, t.keyTrack)
            || !number(*f, "envAmount", -MaxFilterEnvOctaves, MaxFilterEnvOctaves, t.envOctaves)
            || !number(*f, "velocity", 0.0, 1.0, t.velocity))
            return false;
    }
    if (const auto* e = section(j, "filterEnv", ok); e && !readEnvelope(*e, r.filterEnv)) return false;
    if (const auto* e = section(j, "ampEnv", ok); e && !readEnvelope(*e, r.ampEnv)) return false;
    if (!number(j, "ampVelocity", 0.0, 1.0, r.ampVelocity)) return false;
    if (const auto* l = section(j, "lfo", ok)) {
        auto& t = r.lfo;
        if (!name(*l, "shape", lfoShapeFromName, t.shape)
            || !name(*l, "target", lfoTargetFromName, t.target)
            || !number(*l, "rate", MinLfoHz, MaxLfoHz, t.rateHz)
            || !boolean(*l, "sync", t.sync)
            || !integer(*l, "division", 0, DivisionCount - 1, t.division)
            || !number(*l, "depth", 0.0, 1.0, t.depth)
            || !number(*l, "delay", 0.0, MaxLfoDelaySeconds, t.delaySeconds))
            return false;
    }
    if (!name(j, "mode", playModeFromName, r.mode)
        || !number(j, "glide", 0.0, MaxGlideSeconds, r.glideSeconds)
        || !integer(j, "bendRange", 0, MaxBendSemitones, r.bendSemitones)
        || !number(j, "volume", MinVolumeDb, MaxVolumeDb, r.volumeDb))
        return false;
    if (!ok) return false;
    settings = r;
    error.clear();
    return true;
}

json presetToJson(const Preset& preset)
{
    return {{"format", "ChaosMakerSynthPreset"}, {"version", 1}, {"name", preset.name},
            {"settings", toJson(preset.settings)}};
}

bool presetFromJson(const json& j, Preset& preset, std::string& error)
{
    error = "This is not a synth preset file.";
    if (!j.is_object() || j.value("format", std::string()) != "ChaosMakerSynthPreset") return false;
    if (!j.contains("version") || !j["version"].is_number_integer() || j["version"].get<int>() != 1) {
        error = "This synth preset was made by a newer version.";
        return false;
    }
    if (!j.contains("name") || !j["name"].is_string()) { error = "The synth preset has no name."; return false; }
    const auto name = cleanPresetName(j["name"].get<std::string>());
    if (!name) { error = "The synth preset's name is not usable."; return false; }
    Settings settings;
    if (!j.contains("settings") || !fromJson(j["settings"], settings, error)) {
        error = "The synth preset's settings do not read.";
        return false;
    }
    preset = {*name, settings, false};
    error.clear();
    return true;
}

} // namespace persistence::synthjson
