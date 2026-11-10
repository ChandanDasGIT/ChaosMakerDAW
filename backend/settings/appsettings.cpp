#include "settings/appsettings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

namespace {

std::string trim(std::string_view s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(static_cast<unsigned char>(*start))) {
        ++start;
    }
    auto end = s.end();
    do {
        if (end == start) break;
        --end;
    } while (std::isspace(static_cast<unsigned char>(*end)));
    return (start < end + 1) ? std::string(start, end + 1) : std::string();
}

bool parseBool(std::string_view val, bool defaultVal = false) {
    std::string s = trim(val);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    if (s == "true" || s == "1" || s == "yes" || s == "on")
        return true;
    if (s == "false" || s == "0" || s == "no" || s == "off")
        return false;
    return defaultVal;
}

// Whole pixels are enough; written without a trailing ".000000".
std::string formatSize(double value) {
    std::ostringstream ss;
    ss << std::lround(value);
    return ss.str();
}

bool parseDouble(std::string_view val, double& out) {
    try {
        std::size_t used = 0;
        const std::string s = trim(val);
        const double parsed = std::stod(s, &used);
        if (used != s.size() || !std::isfinite(parsed)) return false;
        out = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

AppSettings::AppSettings() = default;
AppSettings::~AppSettings() = default;

void AppSettings::setRestoreSessionOnStartup(bool enabled) noexcept {
    m_restoreSessionOnStartup = enabled;
}

void AppSettings::setPianoRollPresentation(std::string presentation) noexcept {
    if (presentation == "window" || presentation == "dock")
        m_pianoRollPresentation = std::move(presentation);
}

void AppSettings::setDefaultRecordMode(std::string mode) noexcept {
    if (mode == "dry" || mode == "print")
        m_defaultRecordMode = std::move(mode);
}

void AppSettings::setChordZoneMapping(std::string mapping) noexcept {
    if (mapping == "octaves" || mapping == "zonekeys")
        m_chordZoneMapping = std::move(mapping);
}

double AppSettings::clampSize(double value, SizeLimits limits) noexcept {
    if (!std::isfinite(value)) return limits.fallback;
    return std::clamp(value, limits.min, limits.max);
}

void AppSettings::setTrackListWidth(double width) noexcept {
    if (std::isfinite(width)) m_trackListWidth = clampSize(width, TrackListWidthLimits);
}

void AppSettings::setMixerHeight(double height) noexcept {
    if (std::isfinite(height)) m_mixerHeight = clampSize(height, MixerHeightLimits);
}

void AppSettings::setPianoRollDockHeight(double height) noexcept {
    if (std::isfinite(height)) m_pianoRollDockHeight = clampSize(height, PianoRollDockHeightLimits);
}

void AppSettings::setDefaultProjectDirectory(std::string path) {
    m_defaultProjectDirectory = trim(path);
}

void AppSettings::setAutoSaveEnabled(bool enabled) noexcept {
    m_autoSaveEnabled = enabled;
}

bool AppSettings::setAutoSaveIntervalSeconds(int seconds) noexcept {
    if (!validateAutoSaveInterval(seconds))
        return false;
    m_autoSaveIntervalSeconds = seconds;
    return true;
}

void AppSettings::resetToDefaults() noexcept {
    m_restoreSessionOnStartup = false;
    m_pianoRollPresentation = "dock";
    m_defaultRecordMode = "dry";
    m_chordZoneMapping = "octaves";
    m_trackListWidth = TrackListWidthLimits.fallback;
    m_mixerHeight = MixerHeightLimits.fallback;
    m_pianoRollDockHeight = PianoRollDockHeightLimits.fallback;
    m_defaultProjectDirectory.clear();
    m_autoSaveEnabled = true;
    m_autoSaveIntervalSeconds = 60;
    m_snapPlayhead = false;
    m_snapSelection = false;
    m_lastError.clear();
}

bool AppSettings::validateAutoSaveInterval(int seconds) noexcept {
    return seconds >= 5 && seconds <= 3600;
}

void AppSettings::importValue(std::string_view key, std::string_view value) {
    if (key == KeyRestoreSessionOnStartup) {
        m_restoreSessionOnStartup = parseBool(value, false);
    } else if (key == KeyPianoRollPresentation) {
        auto p = trim(value);
        if (p == "window" || p == "dock")
            m_pianoRollPresentation = std::move(p);
    } else if (key == KeyDefaultRecordMode) {
        setDefaultRecordMode(trim(value)); // anything but "dry"/"print" keeps the current mode
    } else if (key == KeyChordZoneMapping) {
        setChordZoneMapping(trim(value));  // anything but "octaves"/"zonekeys" keeps the current one
    } else if (key == KeyTrackListWidth || key == KeyMixerHeight || key == KeyPianoRollDockHeight) {
        double size = 0;
        if (!parseDouble(value, size)) return; // invalid text keeps the current size
        if (key == KeyTrackListWidth) setTrackListWidth(size);
        else if (key == KeyMixerHeight) setMixerHeight(size);
        else setPianoRollDockHeight(size);
    } else if (key == KeyDefaultProjectDir) {
        m_defaultProjectDirectory = trim(value);
    } else if (key == KeySnapPlayhead) {
        m_snapPlayhead = parseBool(value, false);
    } else if (key == KeySnapSelection) {
        m_snapSelection = parseBool(value, false);
    } else if (key == KeyAutoSaveEnabled) {
        m_autoSaveEnabled = parseBool(value, true);
    } else if (key == KeyAutoSaveInterval) {
        try {
            int interval = std::stoi(trim(value));
            if (validateAutoSaveInterval(interval)) {
                m_autoSaveIntervalSeconds = interval;
            }
        } catch (...) {
            // invalid integer ignored
        }
    }
}

std::string AppSettings::exportValue(std::string_view key) const {
    if (key == KeyRestoreSessionOnStartup) {
        return m_restoreSessionOnStartup ? "true" : "false";
    }
    if (key == KeyPianoRollPresentation) {
        return m_pianoRollPresentation;
    }
    if (key == KeyDefaultRecordMode) return m_defaultRecordMode;
    if (key == KeyChordZoneMapping) return m_chordZoneMapping;
    if (key == KeyTrackListWidth) return formatSize(m_trackListWidth);
    if (key == KeyMixerHeight) return formatSize(m_mixerHeight);
    if (key == KeyPianoRollDockHeight) return formatSize(m_pianoRollDockHeight);
    if (key == KeyDefaultProjectDir) {
        return m_defaultProjectDirectory;
    }
    if (key == KeyAutoSaveEnabled) {
        return m_autoSaveEnabled ? "true" : "false";
    }
    if (key == KeySnapPlayhead) return m_snapPlayhead ? "true" : "false";
    if (key == KeySnapSelection) return m_snapSelection ? "true" : "false";
    if (key == KeyAutoSaveInterval) {
        return std::to_string(m_autoSaveIntervalSeconds);
    }
    return {};
}

std::string AppSettings::serialize() const {
    std::ostringstream ss;
    ss << KeyRestoreSessionOnStartup << "=" << (m_restoreSessionOnStartup ? "true" : "false") << "\n";
    ss << KeyPianoRollPresentation << "=" << m_pianoRollPresentation << "\n";
    ss << KeyDefaultRecordMode << "=" << m_defaultRecordMode << "\n";
    ss << KeyChordZoneMapping << "=" << m_chordZoneMapping << "\n";
    ss << KeyTrackListWidth << "=" << formatSize(m_trackListWidth) << "\n";
    ss << KeyMixerHeight << "=" << formatSize(m_mixerHeight) << "\n";
    ss << KeyPianoRollDockHeight << "=" << formatSize(m_pianoRollDockHeight) << "\n";
    ss << KeyDefaultProjectDir << "=" << m_defaultProjectDirectory << "\n";
    ss << KeyAutoSaveEnabled << "=" << (m_autoSaveEnabled ? "true" : "false") << "\n";
    ss << KeyAutoSaveInterval << "=" << m_autoSaveIntervalSeconds << "\n";
    ss << KeySnapPlayhead << "=" << (m_snapPlayhead ? "true" : "false") << "\n";
    ss << KeySnapSelection << "=" << (m_snapSelection ? "true" : "false") << "\n";
    return ss.str();
}

bool AppSettings::deserialize(std::string_view text) {
    std::string textStr(text);
    std::istringstream ss(textStr);
    std::string line;
    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        auto pos = line.find('=');
        if (pos == std::string::npos)
            continue;
        std::string key = trim(line.substr(0, pos));
        std::string val = trim(line.substr(pos + 1));
        importValue(key, val);
    }
    return true;
}

bool AppSettings::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        m_lastError = "Could not open settings file: " + path;
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    m_lastError.clear();
    return deserialize(buffer.str());
}

bool AppSettings::saveToFile(const std::string& path) const {
    std::ofstream file(path);
    if (!file.is_open()) {
        m_lastError = "Could not write settings file: " + path;
        return false;
    }
    file << serialize();
    m_lastError.clear();
    return true;
}
