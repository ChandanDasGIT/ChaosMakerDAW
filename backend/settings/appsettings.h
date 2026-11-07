#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

class AppSettings {
public:
    static constexpr const char* KeyRestoreSessionOnStartup = "session/restoreOnStartup";
    // Owned by EngineController like the key above: stop playback at the arrangement end (default on).
    static constexpr const char* KeyStopPlaybackAtEnd = "playback/stopAtEnd";
    static constexpr const char* KeyDefaultProjectDir = "paths/defaultProjectDirectory";
    static constexpr const char* KeyAutoSaveEnabled = "autosave/enabled";
    static constexpr const char* KeyAutoSaveInterval = "autosave/intervalSeconds";
    static constexpr const char* KeyPianoRollPresentation = "ui/pianoRollPresentation";
    static constexpr const char* KeyTrackListWidth = "ui/trackListWidth";
    static constexpr const char* KeyMixerHeight = "ui/mixerHeight";
    static constexpr const char* KeyPianoRollDockHeight = "ui/pianoRollDockHeight";

    // Main window layout (pixels). Setters clamp into these ranges; non-finite values are ignored.
    struct SizeLimits { double min, max, fallback; };
    static constexpr SizeLimits TrackListWidthLimits{180.0, 460.0, 230.0};
    static constexpr SizeLimits MixerHeightLimits{120.0, 480.0, 180.0};
    // The view also keeps the docked piano roll above its own minimum and below the window.
    static constexpr SizeLimits PianoRollDockHeightLimits{200.0, 2000.0, 380.0};

    AppSettings();
    ~AppSettings();

    // Startup session restore preference
    bool restoreSessionOnStartup() const noexcept { return m_restoreSessionOnStartup; }
    void setRestoreSessionOnStartup(bool enabled) noexcept;

    // Piano roll presentation ("dock" or "window")
    const std::string& pianoRollPresentation() const noexcept { return m_pianoRollPresentation; }
    void setPianoRollPresentation(std::string presentation) noexcept;

    // Layout sizes, kept between sessions
    double trackListWidth() const noexcept { return m_trackListWidth; }
    void setTrackListWidth(double width) noexcept;
    double mixerHeight() const noexcept { return m_mixerHeight; }
    void setMixerHeight(double height) noexcept;
    double pianoRollDockHeight() const noexcept { return m_pianoRollDockHeight; }
    void setPianoRollDockHeight(double height) noexcept;
    // Clamp into limits; non-finite values give the fallback.
    static double clampSize(double value, SizeLimits limits) noexcept;

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
    double m_trackListWidth = TrackListWidthLimits.fallback;
    double m_mixerHeight = MixerHeightLimits.fallback;
    double m_pianoRollDockHeight = PianoRollDockHeightLimits.fallback;
    std::string m_defaultProjectDirectory;
    bool m_autoSaveEnabled = true;
    int m_autoSaveIntervalSeconds = 60;
    mutable std::string m_lastError;
};
