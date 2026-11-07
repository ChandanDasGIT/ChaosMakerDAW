#include "instruments/drums/drumkitsettings.h"

#include <algorithm>
#include <cmath>

namespace drumkit {

namespace {

double clampOr(double value, double low, double high, double fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

} // namespace

Settings sanitize(const Settings& in) noexcept
{
    const Settings d;
    Settings s = in;
    if (s.room != Room::Close && s.room != Room::Front && s.room != Room::Full) s.room = d.room;
    for (auto* level : {&s.kickDb, &s.snareDb, &s.hihatDb, &s.tomsDb, &s.volumeDb})
        *level = clampOr(*level, MinLevelDb, MaxLevelDb, 0.0);
    s.tuneSemitones = clampOr(s.tuneSemitones, -MaxTuneSemitones, MaxTuneSemitones, d.tuneSemitones);
    s.micTiming = clampOr(s.micTiming, 0.0, 1.0, d.micTiming);
    s.kickBeater = clampOr(s.kickBeater, 0.0, 1.0, d.kickBeater);
    s.snareBody = clampOr(s.snareBody, 0.0, 1.0, d.snareBody);
    s.hihatTrash = clampOr(s.hihatTrash, 0.0, 1.0, d.hihatTrash);
    s.tomThud = clampOr(s.tomThud, 0.0, 1.0, d.tomThud);
    return s;
}

bool operator==(const Settings& a, const Settings& b) noexcept
{
    return a.room == b.room && a.kickDb == b.kickDb && a.snareDb == b.snareDb && a.hihatDb == b.hihatDb
        && a.tomsDb == b.tomsDb && a.volumeDb == b.volumeDb && a.tuneSemitones == b.tuneSemitones
        && a.micTiming == b.micTiming && a.kickBeater == b.kickBeater && a.snareBody == b.snareBody
        && a.hihatTrash == b.hihatTrash && a.tomThud == b.tomThud;
}

std::map<int, double> controllers(const Settings& in)
{
    const Settings s = sanitize(in);
    // CC 105 picks mics by range: the front mic from 64, window and retro from 96.
    const double mix = s.room == Room::Close ? 0.0 : s.room == Room::Front ? 80.0 : 127.0;
    // The kit tunes by tune = -1200 + 2400 * CC 108 / 127 cents.
    const double tune = (s.tuneSemitones * 100.0 + 1200.0) * 127.0 / 2400.0;
    return {{105, mix}, {108, tune}, {110, s.micTiming * 127.0}, {111, s.kickBeater * 127.0},
            {112, s.snareBody * 127.0}, {113, s.hihatTrash * 127.0}, {114, s.tomThud * 127.0}};
}

std::string_view roomName(Room room) noexcept
{
    switch (room) {
    case Room::Close: return "close";
    case Room::Full: return "full";
    default: return "front";
    }
}

bool roomFromName(std::string_view name, Room& room) noexcept
{
    if (name == "close") { room = Room::Close; return true; }
    if (name == "front") { room = Room::Front; return true; }
    if (name == "full") { room = Room::Full; return true; }
    return false;
}

} // namespace drumkit
