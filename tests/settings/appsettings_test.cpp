#include "testkit.h"
#include "settings/appsettings.h"

#include <cstdio>
#include <limits>

TEST(appsettings, defaults_and_mutators)
{
    AppSettings settings;
    CHECK(!settings.restoreSessionOnStartup());
    CHECK(settings.pianoRollPresentation() == "dock");
    CHECK(settings.defaultProjectDirectory().empty());
    CHECK(settings.autoSaveEnabled());
    CHECK(settings.autoSaveIntervalSeconds() == 60);

    settings.setRestoreSessionOnStartup(true);
    CHECK(settings.restoreSessionOnStartup());

    settings.setPianoRollPresentation("window");
    CHECK(settings.pianoRollPresentation() == "window");

    settings.setDefaultProjectDirectory("/home/user/Music/Projects");
    CHECK(settings.defaultProjectDirectory() == "/home/user/Music/Projects");

    settings.setAutoSaveEnabled(false);
    CHECK(!settings.autoSaveEnabled());

    CHECK(settings.setAutoSaveIntervalSeconds(120));
    CHECK(settings.autoSaveIntervalSeconds() == 120);

    // Bounded interval validation
    CHECK(!settings.setAutoSaveIntervalSeconds(2));
    CHECK(!settings.setAutoSaveIntervalSeconds(10000));
    CHECK(settings.autoSaveIntervalSeconds() == 120);

    settings.resetToDefaults();
    CHECK(!settings.restoreSessionOnStartup());
    CHECK(settings.pianoRollPresentation() == "dock");
    CHECK(settings.defaultProjectDirectory().empty());
    CHECK(settings.autoSaveEnabled());
    CHECK(settings.autoSaveIntervalSeconds() == 60);
}

TEST(appsettings, import_export_and_migration)
{
    AppSettings settings;

    settings.importValue(AppSettings::KeyRestoreSessionOnStartup, "1");
    CHECK(settings.restoreSessionOnStartup());
    CHECK(settings.exportValue(AppSettings::KeyRestoreSessionOnStartup) == "true");

    settings.importValue(AppSettings::KeyRestoreSessionOnStartup, "false");
    CHECK(!settings.restoreSessionOnStartup());
    CHECK(settings.exportValue(AppSettings::KeyRestoreSessionOnStartup) == "false");

    settings.importValue(AppSettings::KeyPianoRollPresentation, "window");
    CHECK(settings.pianoRollPresentation() == "window");
    CHECK(settings.exportValue(AppSettings::KeyPianoRollPresentation) == "window");

    settings.importValue(AppSettings::KeyDefaultProjectDir, "  /my/custom/path  ");
    CHECK(settings.defaultProjectDirectory() == "/my/custom/path");
    CHECK(settings.exportValue(AppSettings::KeyDefaultProjectDir) == "/my/custom/path");

    settings.importValue(AppSettings::KeyAutoSaveInterval, "300");
    CHECK(settings.autoSaveIntervalSeconds() == 300);
    CHECK(settings.exportValue(AppSettings::KeyAutoSaveInterval) == "300");

    // Invalid interval ignored
    settings.importValue(AppSettings::KeyAutoSaveInterval, "invalid");
    CHECK(settings.autoSaveIntervalSeconds() == 300);
}

TEST(appsettings, serialize_deserialize_roundtrip)
{
    AppSettings settings;
    settings.setRestoreSessionOnStartup(true);
    settings.setDefaultProjectDirectory("/data/audio");
    settings.setAutoSaveEnabled(false);
    settings.setAutoSaveIntervalSeconds(45);

    std::string text = settings.serialize();
    CHECK(!text.empty());

    AppSettings restored;
    CHECK(restored.deserialize(text));
    CHECK(restored.restoreSessionOnStartup());
    CHECK(restored.defaultProjectDirectory() == "/data/audio");
    CHECK(!restored.autoSaveEnabled());
    CHECK(restored.autoSaveIntervalSeconds() == 45);
}

TEST(appsettings, file_save_and_load)
{
    AppSettings settings;
    settings.setRestoreSessionOnStartup(true);
    settings.setDefaultProjectDirectory("/projects/test");
    settings.setAutoSaveIntervalSeconds(90);

    const std::string tmpPath = "/tmp/projectdaw_appsettings_test.ini";
    CHECK(settings.saveToFile(tmpPath));

    AppSettings loaded;
    CHECK(loaded.loadFromFile(tmpPath));
    CHECK(loaded.restoreSessionOnStartup());
    CHECK(loaded.defaultProjectDirectory() == "/projects/test");
    CHECK(loaded.autoSaveIntervalSeconds() == 90);

    std::remove(tmpPath.c_str());

    // Error on nonexistent file
    AppSettings nonExistent;
    CHECK(!nonExistent.loadFromFile("/path/does/not/exist/at/all.ini"));
    CHECK(!nonExistent.lastError().empty());
}

TEST(appsettings, layout_sizes_default_clamp_and_round_trip)
{
    AppSettings settings;
    CHECK(settings.trackListWidth() == 230.0);
    CHECK(settings.mixerHeight() == 180.0);
    CHECK(settings.pianoRollDockHeight() == 380.0);

    settings.setTrackListWidth(300.4);
    settings.setMixerHeight(5000.0);                              // clamped to the maximum
    settings.setPianoRollDockHeight(10.0);                        // clamped to the minimum
    CHECK(settings.trackListWidth() == 300.4);
    CHECK(settings.mixerHeight() == AppSettings::MixerHeightLimits.max);
    CHECK(settings.pianoRollDockHeight() == AppSettings::PianoRollDockHeightLimits.min);
    settings.setTrackListWidth(std::numeric_limits<double>::quiet_NaN());
    CHECK(settings.trackListWidth() == 300.4);                    // non-finite ignored

    // Stored as whole pixels and read back; bad text keeps the current value.
    CHECK(settings.exportValue(AppSettings::KeyTrackListWidth) == "300");
    AppSettings loaded;
    CHECK(loaded.deserialize(settings.serialize()));
    CHECK(loaded.trackListWidth() == 300.0);
    CHECK(loaded.mixerHeight() == 480.0);
    CHECK(loaded.pianoRollDockHeight() == 200.0);
    loaded.importValue(AppSettings::KeyMixerHeight, "tall");
    loaded.importValue(AppSettings::KeyTrackListWidth, "250px");
    CHECK(loaded.mixerHeight() == 480.0);
    CHECK(loaded.trackListWidth() == 300.0);
    loaded.importValue(AppSettings::KeyTrackListWidth, " 999 ");
    CHECK(loaded.trackListWidth() == AppSettings::TrackListWidthLimits.max);

    loaded.resetToDefaults();
    CHECK(loaded.trackListWidth() == 230.0 && loaded.mixerHeight() == 180.0 && loaded.pianoRollDockHeight() == 380.0);
}

TEST(appsettings, default_record_mode_is_dry_validated_and_round_trips)
{
    AppSettings settings;
    CHECK(settings.defaultRecordMode() == "dry");                 // user choice: Dry by default
    CHECK(settings.recordDryByDefault());
    settings.setDefaultRecordMode("print");
    CHECK(settings.defaultRecordMode() == "print" && !settings.recordDryByDefault());
    settings.setDefaultRecordMode("wet");                         // unknown: ignored
    CHECK(settings.defaultRecordMode() == "print");
    CHECK(settings.exportValue(AppSettings::KeyDefaultRecordMode) == "print");

    AppSettings loaded;
    CHECK(loaded.deserialize(settings.serialize()));
    CHECK(loaded.defaultRecordMode() == "print");
    loaded.importValue(AppSettings::KeyDefaultRecordMode, " dry ");
    CHECK(loaded.defaultRecordMode() == "dry");
    loaded.importValue(AppSettings::KeyDefaultRecordMode, "nonsense");
    CHECK(loaded.defaultRecordMode() == "dry");
    settings.resetToDefaults();
    CHECK(settings.defaultRecordMode() == "dry");
}

TEST(appsettings, chord_zone_mapping_defaults_to_octaves_is_validated_and_round_trips)
{
    AppSettings settings;
    CHECK(settings.chordZoneMapping() == "octaves");
    settings.setChordZoneMapping("zonekeys");
    CHECK(settings.chordZoneMapping() == "zonekeys");
    settings.setChordZoneMapping("black keys");                   // unknown: ignored
    CHECK(settings.chordZoneMapping() == "zonekeys");
    CHECK(settings.exportValue(AppSettings::KeyChordZoneMapping) == "zonekeys");

    AppSettings loaded;
    CHECK(loaded.deserialize(settings.serialize()));
    CHECK(loaded.chordZoneMapping() == "zonekeys");
    loaded.importValue(AppSettings::KeyChordZoneMapping, " octaves ");
    CHECK(loaded.chordZoneMapping() == "octaves");
    loaded.importValue(AppSettings::KeyChordZoneMapping, "nonsense");
    CHECK(loaded.chordZoneMapping() == "octaves");
    settings.resetToDefaults();
    CHECK(settings.chordZoneMapping() == "octaves");
}

TEST(appsettings, the_timeline_magnets_are_off_by_default_and_round_trip)
{
    AppSettings settings;
    CHECK(!settings.snapPlayhead() && !settings.snapSelection());
    settings.setSnapPlayhead(true);
    CHECK(settings.exportValue(AppSettings::KeySnapPlayhead) == "true");
    CHECK(settings.exportValue(AppSettings::KeySnapSelection) == "false");
    AppSettings copy;
    CHECK(copy.deserialize(settings.serialize()));
    CHECK(copy.snapPlayhead() && !copy.snapSelection());
    copy.importValue(AppSettings::KeySnapSelection, "true");
    CHECK(copy.snapSelection());
    copy.importValue(AppSettings::KeySnapSelection, "nonsense");
    CHECK(!copy.snapSelection());
    copy.resetToDefaults();
    CHECK(!copy.snapPlayhead() && !copy.snapSelection());
}

TEST(appsettings, ripple_editing_is_off_by_default_and_round_trips)
{
    // restMap G15.
    AppSettings settings;
    CHECK(!settings.rippleEditing());
    CHECK(settings.exportValue(AppSettings::KeyRippleEditing) == "false");
    settings.setRippleEditing(true);
    AppSettings copy;
    CHECK(copy.deserialize(settings.serialize()));
    CHECK(copy.rippleEditing());
    copy.importValue(AppSettings::KeyRippleEditing, "nonsense");
    CHECK(!copy.rippleEditing());
    copy.setRippleEditing(true);
    copy.resetToDefaults();
    CHECK(!copy.rippleEditing());
}
