#pragma once

#include <map>
#include <string_view>

// Everything the user sets on one acoustic drum kit. Plain data: the track keeps one copy and
// the engine's DrumKit receives it through DrumKit::setSettings().
//
// Most values are the kit's own controllers (Gogodze Phu Vol II, read at load time); the part
// levels and the volume are added to the zones, so they go past the kit's own range.
namespace drumkit {

// Which microphones play (the kit's "Mic mix", CC 105).
enum class Room {
    Close,                                // kick, snare, overhead and side mics
    Front,                                // + the front room mic (the kit's default)
    Full,                                 // + the window and retro room mics
};

constexpr double MinLevelDb = -24.0, MaxLevelDb = 12.0;
constexpr double MaxTuneSemitones = 12.0;
constexpr double DefaultMicTiming = 63.0 / 127.0;   // the kit's own "Fi" setting

struct Settings {
    Room room = Room::Front;
    double kickDb = 0.0, snareDb = 0.0, hihatDb = 0.0, tomsDb = 0.0;
    double volumeDb = 0.0;
    double tuneSemitones = 0.0;           // the whole kit, -12..+12
    double micTiming = DefaultMicTiming;  // 0 = the mics' natural delays .. 1 = lined up tight
    double kickBeater = 1.0;              // 0..1: the beater's click in the other mics ("Kick dirt")
    double snareBody = 0.0;               // 0..1: fatter, lower snare ("Snare fat")
    double hihatTrash = 0.0;              // 0..1: trashier, higher hats ("Hi-hat trash")
    double tomThud = 0.0;                 // 0..1: an octave-down thud under the toms ("Tom thud")
};

// Every value in range; non-finite numbers fall back to the defaults.
Settings sanitize(const Settings& settings) noexcept;
bool operator==(const Settings&, const Settings&) noexcept;

// The kit's controllers for these settings: CC number -> 0..127.
std::map<int, double> controllers(const Settings& settings);

// Names used by save files and the UI. Unknown names give false.
std::string_view roomName(Room room) noexcept;
bool roomFromName(std::string_view name, Room& room) noexcept;

} // namespace drumkit
