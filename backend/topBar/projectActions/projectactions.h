#pragma once

#include <string>

class ProjectActions {
public:
    enum class ActionType {
        None,
        New,
        Open,
        Close,
        OpenFile    // open a known file (e.g. from Open Recent), no file dialog
    };

    enum class Request {
        None,
        ShowUnsavedConfirmation,
        ShowOpenDialog,
        ShowSaveDialog,
        ExecuteNew,
        ExecuteClose,
        ExecuteOpenFile   // open openFilePath()
    };

    ProjectActions();
    ~ProjectActions();

    ActionType pendingAction() const noexcept { return m_pendingAction; }
    std::string pendingActionName() const;
    bool resumeAfterSave() const noexcept { return m_resumeAfterSave; }

    static ActionType parseAction(const std::string& action);
    static std::string actionToString(ActionType action);

    // User triggers New / Open / Close
    Request requestAction(ActionType action, bool isDirty);
    Request requestAction(const std::string& actionName, bool isDirty);

    // User picks a file to open directly (Open Recent). Asks about unsaved changes
    // first, like Open; openFilePath() is the file to open once the request resolves.
    Request requestOpenFile(const std::string& path, bool isDirty);
    const std::string& openFilePath() const noexcept { return m_openFilePath; }

    // User triggers Save
    Request requestSave(bool hasExistingFile, bool resume = false);

    // User triggers Save As
    Request requestSaveAs();

    // User responses from Unsaved Changes dialog
    Request onUnsavedSave(bool hasExistingFile);
    Request onUnsavedDiscard();
    void onUnsavedCancel();

    // File dialog responses
    Request onSaveCompleted();
    void onSaveCancelled();

    void reset() noexcept;

private:
    Request resolveDirect(ActionType action);

    ActionType m_pendingAction = ActionType::None;
    bool m_resumeAfterSave = false;
    // Kept after the action resolves, so the caller can read it; replaced by the next request.
    std::string m_openFilePath;
};
