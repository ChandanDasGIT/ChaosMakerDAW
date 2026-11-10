#include "testkit.h"
#include "plugins/effectchain.h"
#include "session/project.h"

#include <string>
#include <vector>

namespace {
std::vector<std::string> types(const Track& track)
{
    std::vector<std::string> result;
    for (const auto& slot : track.effects) result.push_back(slot.type);
    return result;
}
}

TEST(effectchain, only_instrument_tracks_start_with_their_instrument_fx)
{
    using List = std::vector<std::string>;
    CHECK(effectchain::initialEffects("Electric Guitar") == List{"guitarsuite"});
    CHECK(effectchain::initialEffects("Acoustic Guitar") == List{"guitarsuite"});
    CHECK(effectchain::initialEffects("Harmonium") == List{"harmonium"});   // its page, like GuitarSuite
    CHECK(effectchain::initialEffects("Drums") == List{"drumkit"});
    CHECK(effectchain::initialEffects("Electronic Drums") == List{"electrodrums"});
    CHECK(effectchain::initialEffects("Piano") == List{"piano"});
    for (const char* type : {"Vocal", "Audio", "Instrument", "Strings", "Unknown"})
        CHECK(effectchain::initialEffects(type).empty());
}

TEST(effectchain, eq_is_added_once_and_guitarsuite_is_fixed)
{
    std::vector<TrackEffectSlot> chain;
    CHECK(effectchain::canAdd(chain, "eq"));
    CHECK(!effectchain::canAdd(chain, "guitarsuite"));   // only placed on guitar tracks at creation
    CHECK(!effectchain::canAdd(chain, "flanger"));       // unknown type
    using Types = std::vector<std::string_view>;
    CHECK(effectchain::addableEffects(chain) == (Types{"eq", "chaoseq", "compressor", "deesser", "reverb", "chaosverb", "delay", "limiter", "saturation", "gate"}));   // picker order
    chain.push_back({1, "eq"});
    CHECK(!effectchain::canAdd(chain, "eq"));            // one set of EQ bands per track
    CHECK(effectchain::addableEffects(chain) == (Types{"chaoseq", "compressor", "deesser", "reverb", "chaosverb", "delay", "limiter", "saturation", "gate"}));
    chain.push_back({2, "chaoseq"});                     // Chaos EQ sits beside the 10-band EQ
    CHECK(!effectchain::canAdd(chain, "chaoseq"));
    chain.push_back({3, "compressor"});                  // mixing plugins (restMap G7), once each
    chain.push_back({4, "deesser"});
    chain.push_back({5, "reverb"});
    chain.push_back({6, "chaosverb"});
    for (const char* type : {"delay", "limiter", "saturation", "gate"}) chain.push_back({int(chain.size()) + 1, type});
    CHECK(!effectchain::canAdd(chain, "compressor"));
    CHECK(effectchain::canRemove("compressor"));
    CHECK(effectchain::addableEffects(chain).empty());
    CHECK(effectchain::canRemove("chaoseq"));
    const auto* chaos = effectchain::find("chaoseq");
    CHECK(chaos && chaos->title == "Chaos EQ");
    CHECK(effectchain::canRemove("eq"));
    CHECK(!effectchain::canRemove("guitarsuite"));
    CHECK(!effectchain::canRemove("flanger"));
    const auto* eq = effectchain::find("eq");
    CHECK(eq && eq->title == "Equalizer");
}

TEST(effectchain, project_adds_removes_and_reorders_track_effects_without_undo)
{
    Project project;
    auto& guitar = project.addTrack(TrackKind::Instrument, "Gtr", "Electric Guitar");
    auto& vocal = project.addTrack(TrackKind::Audio, "Vox", "Vocal");
    const int guitarId = guitar.id, vocalId = vocal.id;
    CHECK(types(guitar) == std::vector<std::string>{"guitarsuite"});
    CHECK(types(vocal).empty());

    const int eq = project.addTrackEffect(guitarId, "eq");
    CHECK(eq > 0 && eq != guitar.effects.front().id);
    CHECK(project.addTrackEffect(guitarId, "eq") == -1);          // already on the chain
    CHECK(project.addTrackEffect(vocalId, "guitarsuite") == -1);  // never added by the user
    CHECK(project.addTrackEffect(999, "eq") == -1);               // no such track
    const int vocalEq = project.addTrackEffect(vocalId, "eq");
    CHECK(vocalEq > 0 && vocalEq != eq);                          // ids unique across tracks
    CHECK(types(*project.findTrack(guitarId)) == (std::vector<std::string>{"guitarsuite", "eq"}));

    // Free reordering, GuitarSuite included; bad indices and ids are refused.
    CHECK(project.moveTrackEffect(guitarId, eq, 0));
    CHECK(types(*project.findTrack(guitarId)) == (std::vector<std::string>{"eq", "guitarsuite"}));
    CHECK(!project.moveTrackEffect(guitarId, eq, 0));             // already there
    CHECK(!project.moveTrackEffect(guitarId, eq, 2));
    CHECK(!project.moveTrackEffect(guitarId, eq, -1));
    CHECK(!project.moveTrackEffect(guitarId, 12345, 1));

    const int suite = project.findTrack(guitarId)->effects.back().id;
    CHECK(!project.removeTrackEffect(guitarId, suite));           // GuitarSuite stays
    CHECK(project.removeTrackEffect(guitarId, eq));
    CHECK(!project.removeTrackEffect(guitarId, eq));
    CHECK(types(*project.findTrack(guitarId)) == std::vector<std::string>{"guitarsuite"});
    CHECK(!project.canUndo());                                    // effect edits are not undo steps

    // Undoing a track removal brings its effects back.
    project.removeTrack(vocalId);
    project.undo();
    CHECK(project.findTrack(vocalId));
    CHECK(types(*project.findTrack(vocalId)) == std::vector<std::string>{"eq"});
}

TEST(effectchain, harmonium_page_is_fixed_and_comes_back_on_older_files)
{
    std::vector<TrackEffectSlot> chain;
    CHECK(!effectchain::canAdd(chain, "harmonium"));      // only placed on Harmonium tracks
    CHECK(!effectchain::canRemove("harmonium"));
    const auto* page = effectchain::find("harmonium");
    CHECK(page && page->title == "Harmonium");

    Project project;
    auto& track = project.addTrack(TrackKind::Instrument, "Harmonium", "Harmonium");
    CHECK(types(track) == (std::vector<std::string>{"harmonium"}));
    CHECK(project.addTrackEffect(track.id, "eq") > 0);
    // A file saved before the page existed lists only its effects: the page goes back in front.
    CHECK(project.setTrackEffects(track.id, {"eq"}));
    CHECK(types(track) == (std::vector<std::string>{"harmonium", "eq"}));
    CHECK(!project.setTrackEffects(track.id, {"guitarsuite"}));   // not this instrument's page
}

TEST(effectchain, drum_kit_page_is_fixed_and_comes_back_on_older_files)
{
    std::vector<TrackEffectSlot> chain;
    CHECK(!effectchain::canAdd(chain, "drumkit"));        // only placed on Drums tracks
    CHECK(!effectchain::canRemove("drumkit"));
    const auto* page = effectchain::find("drumkit");
    CHECK(page && page->title == "Drum kit");

    Project project;
    auto& track = project.addTrack(TrackKind::Instrument, "Drums", "Drums");
    CHECK(types(track) == (std::vector<std::string>{"drumkit"}));
    // Drums tracks saved before the kit existed had no page: it goes back in front.
    CHECK(project.setTrackEffects(track.id, {"eq"}));
    CHECK(types(track) == (std::vector<std::string>{"drumkit", "eq"}));
    CHECK(project.setTrackEffects(track.id, {}));
    CHECK(types(track) == (std::vector<std::string>{"drumkit"}));
    CHECK(!project.setTrackEffects(track.id, {"harmonium"}));     // not this instrument's page
}

TEST(effectchain, electronic_drum_page_is_fixed_like_the_drum_kit_page)
{
    std::vector<TrackEffectSlot> chain;
    CHECK(!effectchain::canAdd(chain, "electrodrums"));    // only placed on Electronic Drums tracks
    CHECK(!effectchain::canRemove("electrodrums"));
    const auto* page = effectchain::find("electrodrums");
    CHECK(page && page->title == "Electronic drums");

    Project project;
    auto& track = project.addTrack(TrackKind::Instrument, "Beats", "Electronic Drums");
    CHECK(types(track) == (std::vector<std::string>{"electrodrums"}));
    CHECK(project.setTrackEffects(track.id, {"eq"}));
    CHECK(types(track) == (std::vector<std::string>{"electrodrums", "eq"}));
    CHECK(!project.setTrackEffects(track.id, {"drumkit"}));       // the acoustic kit's page
}

TEST(effectchain, piano_page_is_fixed_and_comes_back_on_older_files)
{
    std::vector<TrackEffectSlot> chain;
    CHECK(!effectchain::canAdd(chain, "piano") && !effectchain::canRemove("piano"));
    const auto* page = effectchain::find("piano");
    CHECK(page && page->title == "Grand piano");

    Project project;
    auto& track = project.addTrack(TrackKind::Instrument, "Keys", "Piano");
    CHECK(types(track) == (std::vector<std::string>{"piano"}));
    // Piano tracks saved before the page existed had no page: it goes back in front.
    CHECK(project.setTrackEffects(track.id, {"eq"}));
    CHECK(types(track) == (std::vector<std::string>{"piano", "eq"}));
    CHECK(!project.setTrackEffects(track.id, {"harmonium"}));
}
