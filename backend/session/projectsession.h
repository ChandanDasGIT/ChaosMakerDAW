#pragma once

#include "persistence/projectdocument.h"
#include "session/tracklist.h"
#include <string>
#include <vector>

class ProjectSession {
public:
    struct LoadCandidate {
        persistence::ProjectFileContent content;
        std::string filePath;
        std::string warning;
        bool valid = false;
    };

    ProjectSession();
    ~ProjectSession();

    // Current open project file path (empty if untitled / unsaved)
    const std::string& currentFilePath() const noexcept { return m_currentFilePath; }
    void setCurrentFilePath(const std::string& path) { m_currentFilePath = path; }

    // Dirty state
    bool isDirty() const noexcept { return m_dirty; }
    void setDirty(bool dirty) noexcept { m_dirty = dirty; }
    void onDocumentModified(bool hasContent) noexcept {
        if (!m_restoring) setDirty(hasContent);
    }

    // Startup restore configuration
    bool restoreOnStartup() const noexcept { return m_restoreOnStartup; }
    void setRestoreOnStartup(bool enable) noexcept { m_restoreOnStartup = enable; }
    const std::string& sessionFilePath() const noexcept { return m_sessionFilePath; }
    void setSessionFilePath(const std::string& path) { m_sessionFilePath = path; }

    // Save policy checks
    bool canSaveSession(bool busy, bool loading, bool restoring, bool saveBlocked) const noexcept;
    bool canPerformProjectActions(bool busy, bool loading, bool effectsBusy) const noexcept;

    // Save operations
    bool saveSession(
        const Project& project,
        const TrackList::MediaPaths& mediaPaths,
        const std::vector<std::string>& trackEffects,
        std::string* error = nullptr);

    // Copies the clips' files from outside the project's folder beside it first (restMap E4);
    // `warning` (if given) names files that could not be copied (saved as they were).
    bool saveProjectFile(
        const std::string& targetPath,
        const Project& project,
        const TrackList::MediaPaths& mediaPaths,
        const std::vector<std::string>& trackEffects,
        std::string* error = nullptr,
        std::string* warning = nullptr);

    // Load candidate preparation
    bool prepareLoadCandidate(
        const std::string& filePath,
        double sampleRate,
        LoadCandidate& candidate,
        std::string& error) const;

    LoadCandidate prepareNewProjectCandidate(double sampleRate) const;

    // Load commit
    void commitLoadCandidate(const LoadCandidate& candidate);

    // Restoring state flag
    bool isRestoring() const noexcept { return m_restoring; }
    void setRestoring(bool restoring) noexcept { m_restoring = restoring; }

    bool isSaveBlocked() const noexcept { return m_saveBlocked; }
    void setSaveBlocked(bool blocked) noexcept { m_saveBlocked = blocked; }

private:
    std::string m_currentFilePath;
    std::string m_sessionFilePath;
    bool m_dirty = false;
    bool m_restoreOnStartup = true;
    bool m_restoring = false;
    bool m_saveBlocked = false;
};
