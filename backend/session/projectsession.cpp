#include "session/projectsession.h"

ProjectSession::ProjectSession() = default;
ProjectSession::~ProjectSession() = default;

bool ProjectSession::canSaveSession(bool busy, bool loading, bool restoring, bool saveBlocked) const noexcept
{
    return m_restoreOnStartup && !restoring && !saveBlocked && !loading && !busy;
}

bool ProjectSession::canPerformProjectActions(bool busy, bool loading, bool effectsBusy) const noexcept
{
    return !busy && !loading && !effectsBusy;
}

bool ProjectSession::saveSession(
    const Project& project,
    const TrackList::MediaPaths& mediaPaths,
    const std::vector<std::string>& trackEffects,
    std::string* error)
{
    if (m_sessionFilePath.empty()) {
        if (error) *error = "Session path not set";
        return false;
    }
    return persistence::ProjectDocument::saveToFile(
        m_sessionFilePath,
        project,
        mediaPaths,
        trackEffects,
        persistence::ProjectDocument::sessionFormat,
        error);
}

bool ProjectSession::saveProjectFile(
    const std::string& targetPath,
    const Project& project,
    const TrackList::MediaPaths& mediaPaths,
    const std::vector<std::string>& trackEffects,
    std::string* error)
{
    if (targetPath.empty()) {
        if (error) *error = "Target path is empty";
        return false;
    }
    const bool ok = persistence::ProjectDocument::saveToFile(
        targetPath,
        project,
        mediaPaths,
        trackEffects,
        persistence::ProjectDocument::projectFormat,
        error);
    if (ok) {
        m_currentFilePath = targetPath;
        m_dirty = false;
    }
    return ok;
}

bool ProjectSession::prepareLoadCandidate(
    const std::string& filePath,
    double sampleRate,
    LoadCandidate& candidate,
    std::string& error) const
{
    candidate = LoadCandidate{};
    candidate.filePath = filePath;

    if (!persistence::ProjectDocument::loadFromFile(filePath, candidate.content, error, sampleRate)) {
        return false;
    }

    candidate.warning = candidate.content.warning;
    candidate.valid = true;
    return true;
}

ProjectSession::LoadCandidate ProjectSession::prepareNewProjectCandidate(double sampleRate) const
{
    LoadCandidate candidate;
    candidate.filePath.clear();
    candidate.content.project.tempo().set(120.0, 4, 4, sampleRate);
    candidate.valid = true;
    return candidate;
}

void ProjectSession::commitLoadCandidate(const LoadCandidate& candidate)
{
    m_currentFilePath = candidate.filePath;
    m_dirty = false;
    m_saveBlocked = false;
    m_restoring = false;
}
