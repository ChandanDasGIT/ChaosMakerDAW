#include "testkit.h"
#include "session/projectactions.h"

using ActionType = ProjectActions::ActionType;
using Request = ProjectActions::Request;

TEST(projectactions, direct_actions_when_not_dirty)
{
    ProjectActions actions;
    CHECK(actions.pendingAction() == ActionType::None);

    CHECK(actions.requestAction("New", false) == Request::ExecuteNew);
    CHECK(actions.pendingAction() == ActionType::None);

    CHECK(actions.requestAction("Open", false) == Request::ShowOpenDialog);
    CHECK(actions.pendingAction() == ActionType::None);

    CHECK(actions.requestAction("Close", false) == Request::ExecuteClose);
    CHECK(actions.pendingAction() == ActionType::None);
}

TEST(projectactions, unsaved_confirmation_when_dirty)
{
    ProjectActions actions;
    CHECK(actions.requestAction("New", true) == Request::ShowUnsavedConfirmation);
    CHECK(actions.pendingAction() == ActionType::New);
    CHECK(actions.pendingActionName() == "New");

    // Discard proceeds to the requested action
    CHECK(actions.onUnsavedDiscard() == Request::ExecuteNew);
    CHECK(actions.pendingAction() == ActionType::None);
}

TEST(projectactions, cancel_unsaved_clears_pending_action)
{
    ProjectActions actions;
    CHECK(actions.requestAction("Open", true) == Request::ShowUnsavedConfirmation);
    CHECK(actions.pendingAction() == ActionType::Open);

    actions.onUnsavedCancel();
    CHECK(actions.pendingAction() == ActionType::None);
}

TEST(projectactions, unsaved_save_with_new_file_and_resume)
{
    ProjectActions actions;
    CHECK(actions.requestAction("New", true) == Request::ShowUnsavedConfirmation);

    // Save with no existing file -> show save dialog
    CHECK(actions.onUnsavedSave(false) == Request::ShowSaveDialog);
    CHECK(actions.resumeAfterSave());

    // Save succeeds -> execute pending New
    CHECK(actions.onSaveCompleted() == Request::ExecuteNew);
    CHECK(actions.pendingAction() == ActionType::None);
    CHECK(!actions.resumeAfterSave());
}

TEST(projectactions, unsaved_save_existing_file_and_resume)
{
    ProjectActions actions;
    CHECK(actions.requestAction("Open", true) == Request::ShowUnsavedConfirmation);

    // Save with existing file -> no dialog, direct save
    CHECK(actions.onUnsavedSave(true) == Request::None);
    CHECK(actions.resumeAfterSave());

    // Direct save completed -> execute pending Open
    CHECK(actions.onSaveCompleted() == Request::ShowOpenDialog);
    CHECK(actions.pendingAction() == ActionType::None);
    CHECK(!actions.resumeAfterSave());
}

TEST(projectactions, direct_save_and_save_as)
{
    ProjectActions actions;

    // Direct save with existing file
    CHECK(actions.requestSave(true) == Request::None);

    // Direct save with no existing file
    CHECK(actions.requestSave(false) == Request::ShowSaveDialog);

    // Save As always shows save dialog and does not resume
    CHECK(actions.requestSaveAs() == Request::ShowSaveDialog);
    CHECK(!actions.resumeAfterSave());
}
