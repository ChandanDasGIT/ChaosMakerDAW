#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

class AppSettings {
public:
    static constexpr const char* KeyRestoreSessionOnStartup = "session/restoreOnStartup";
    // Owned by EngineController like the key above: stop playback at the arrangement end (default on).
    static constexpr const char* KeyStopPlaybackAtEnd = "playback/stopAtEnd";
    // Owned by EngineController too (restMap G10): recording latency compensation (default on)
    // and the extra offset in ms added to the device's reported round trip (default 0).
    static constexpr const char* KeyRecordingLatencyCompensation = "recording/latencyCompensation";
    static constexpr const char* KeyRecordingExtraOffsetMs = "recording/extraOffsetMs";
    static constexpr const char* KeyDefaultProjectDir = "paths/defaultProjectDirectory";
    static constexpr const char* KeyAutoSaveEnabled = "autosave/enabled";
    static constexpr const char* KeyAutoSaveInterval = "autosave/intervalSeconds";
    static constexpr const char* KeyPianoRollPresentation = "ui/pianoRollPresentation";
    static constexpr const char* KeyDefaultRecordMode = "recording/defaultMode";
    static constexpr const char* KeyChordZoneMapping = "chords/zoneMapping";
    static constexpr const char* KeyTrackListWidth = "ui/trackListWidth";
    static constexpr const char* KeyMixerHeight = "ui/mixerHeight";
    static constexpr const char* KeyPianoRollDockHeight = "ui/pianoRollDockHeight";
    static constexpr const char* KeySnapPlayhead = "timeline/snapPlayhead";
    static constexpr const char* KeySnapSelection = "timeline/snapSelection";
    static constexpr const char* KeyRippleEditing = "timeline/ripple";   // restMap G15

    // Main window layout (pixels). Setters clamp into these ranges; non-finite values are ignored.
    struct SizeLimits { double min, max, fallback; };
    // At least 210 px: one line of track buttons, the volume-envelope V included (restMap U9).
    static constexpr SizeLimits TrackListWidthLimits{210.0, 460.0, 230.0};
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

    // Recording mode new tracks start with: "dry" (editable FX, the default) or "print"
    const std::string& defaultRecordMode() const noexcept { return m_defaultRecordMode; }
    void setDefaultRecordMode(std::string mode) noexcept;
    bool recordDryByDefault() const noexcept { return m_defaultRecordMode == "dry"; }

    // How the Chords track's strips name a chord zone (low / middle / high) in MIDI: "octaves"
    // (each zone its own octave of trigger keys, the default) or "zonekeys" (F#2 / G#2 / A#2
    // before a middle-octave press). The player understands both; this picks what is written.
    const std::string& chordZoneMapping() const noexcept { return m_chordZoneMapping; }
    void setChordZoneMapping(std::string mapping) noexcept;

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

    // The timeline's magnets (both off by default): the playhead and the time selection land on
    // the snap grid only when these are on. Clips snap to the grid either way.
    bool snapPlayhead() const noexcept { return m_snapPlayhead; }
    void setSnapPlayhead(bool on) noexcept { m_snapPlayhead = on; }
    bool snapSelection() const noexcept { return m_snapSelection; }
    void setSnapSelection(bool on) noexcept { m_snapSelection = on; }
    // Ripple editing (restMap G15, off by default): deletes close their gap, pastes and
    // duplicates make room.
    bool rippleEditing() const noexcept { return m_rippleEditing; }
    void setRippleEditing(bool on) noexcept { m_rippleEditing = on; }

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
    std::string m_defaultRecordMode = "dry";
    std::string m_chordZoneMapping = "octaves";
    double m_trackListWidth = TrackListWidthLimits.fallback;
    double m_mixerHeight = MixerHeightLimits.fallback;
    double m_pianoRollDockHeight = PianoRollDockHeightLimits.fallback;
    std::string m_defaultProjectDirectory;
    bool m_autoSaveEnabled = true;
    int m_autoSaveIntervalSeconds = 60;
    bool m_snapPlayhead = false;
    bool m_snapSelection = false;
    bool m_rippleEditing = false;
    mutable std::string m_lastError;
};
