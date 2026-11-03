#pragma once

#include <string>

class ProjectActions {
public:
    enum class ActionType {
        None,
        New,
        Open,
        Close
    };

    enum class Request {
        None,
        ShowUnsavedConfirmation,
        ShowOpenDialog,
        ShowSaveDialog,
        ExecuteNew,
        ExecuteClose
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
};
