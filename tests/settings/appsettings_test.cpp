#include "testkit.h"
#include "settings/appsettings.h"

#include <cstdio>

TEST(appsettings, defaults_and_mutators)
{
    AppSettings settings;
    CHECK(!settings.restoreSessionOnStartup());
    CHECK(settings.defaultProjectDirectory().empty());
    CHECK(settings.autoSaveEnabled());
    CHECK(settings.autoSaveIntervalSeconds() == 60);

    settings.setRestoreSessionOnStartup(true);
    CHECK(settings.restoreSessionOnStartup());

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
