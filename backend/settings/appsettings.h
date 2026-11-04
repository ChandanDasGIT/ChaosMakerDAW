#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

class AppSettings {
public:
    static constexpr const char* KeyRestoreSessionOnStartup = "session/restoreOnStartup";
    static constexpr const char* KeyDefaultProjectDir = "paths/defaultProjectDirectory";
    static constexpr const char* KeyAutoSaveEnabled = "autosave/enabled";
    static constexpr const char* KeyAutoSaveInterval = "autosave/intervalSeconds";
    static constexpr const char* KeyPianoRollPresentation = "ui/pianoRollPresentation";

    AppSettings();
    ~AppSettings();

    // Startup session restore preference
    bool restoreSessionOnStartup() const noexcept { return m_restoreSessionOnStartup; }
    void setRestoreSessionOnStartup(bool enabled) noexcept;

    // Piano roll presentation ("dock" or "window")
    const std::string& pianoRollPresentation() const noexcept { return m_pianoRollPresentation; }
    void setPianoRollPresentation(std::string presentation) noexcept;

    // Default project directory
    const std::string& defaultProjectDirectory() const noexcept { return m_defaultProjectDirectory; }
    void setDefaultProjectDirectory(std::string path);

    // Autosave settings
    bool autoSaveEnabled() const noexcept { return m_autoSaveEnabled; }
    void setAutoSaveEnabled(bool enabled) noexcept;

    int autoSaveIntervalSeconds() const noexcept { return m_autoSaveIntervalSeconds; }
    bool setAutoSaveIntervalSeconds(int seconds) noexcept;

    // Defaults and validation
    void resetToDefaults() noexcept;
    static bool validateAutoSaveInterval(int seconds) noexcept;

    // Key-value adaptation for platform storage (e.g. QSettings migration)
    void importValue(std::string_view key, std::string_view value);
    std::string exportValue(std::string_view key) const;

    // Pure C++ text-format serialization (INI-style key=value)
    std::string serialize() const;
    bool deserialize(std::string_view text);

    bool loadFromFile(const std::string& path);
    bool saveToFile(const std::string& path) const;

    const std::string& lastError() const noexcept { return m_lastError; }

private:
    bool m_restoreSessionOnStartup = false;
    std::string m_pianoRollPresentation = "dock";
    std::string m_defaultProjectDirectory;
    bool m_autoSaveEnabled = true;
    int m_autoSaveIntervalSeconds = 60;
    mutable std::string m_lastError;
};
