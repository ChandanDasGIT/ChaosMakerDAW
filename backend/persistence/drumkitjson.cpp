#include "drumkitjson.h"

#include <cmath>

namespace persistence::drumkitjson {
namespace {

using nlohmann::json;

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

} // namespace

json toJson(const drumkit::Settings& s)
{
    return {{"room", std::string(drumkit::roomName(s.room))},
            {"kick", s.kickDb}, {"snare", s.snareDb}, {"hihat", s.hihatDb}, {"toms", s.tomsDb},
            {"volume", s.volumeDb}, {"tune", s.tuneSemitones}, {"micTiming", s.micTiming},
            {"kickBeater", s.kickBeater}, {"snareBody", s.snareBody}, {"hihatTrash", s.hihatTrash},
            {"tomThud", s.tomThud}};
}

bool fromJson(const json& j, drumkit::Settings& settings, std::string& error)
{
    error = "Invalid drum kit settings.";
    if (!j.is_object()) return false;
    drumkit::Settings r;
    if (j.contains("room")) {
        if (!j["room"].is_string() || !drumkit::roomFromName(j["room"].get<std::string>(), r.room)) return false;
    }
    using drumkit::MinLevelDb, drumkit::MaxLevelDb, drumkit::MaxTuneSemitones;
    if (!number(j, "kick", MinLevelDb, MaxLevelDb, r.kickDb) || !number(j, "snare", MinLevelDb, MaxLevelDb, r.snareDb)
        || !number(j, "hihat", MinLevelDb, MaxLevelDb, r.hihatDb) || !number(j, "toms", MinLevelDb, MaxLevelDb, r.tomsDb)
        || !number(j, "volume", MinLevelDb, MaxLevelDb, r.volumeDb)
        || !number(j, "tune", -MaxTuneSemitones, MaxTuneSemitones, r.tuneSemitones)
        || !number(j, "micTiming", 0.0, 1.0, r.micTiming) || !number(j, "kickBeater", 0.0, 1.0, r.kickBeater)
        || !number(j, "snareBody", 0.0, 1.0, r.snareBody) || !number(j, "hihatTrash", 0.0, 1.0, r.hihatTrash)
        || !number(j, "tomThud", 0.0, 1.0, r.tomThud))
        return false;
    settings = r;
    error.clear();
    return true;
}

} // namespace persistence::drumkitjson
