#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include "session/projectactions.h"

class ProjectActionsWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString pendingAction READ pendingAction NOTIFY pendingActionChanged)
    Q_PROPERTY(bool resumeAfterSave READ resumeAfterSave NOTIFY pendingActionChanged)

public:
    explicit ProjectActionsWrapper(QObject* parent = nullptr);
    explicit ProjectActionsWrapper(ProjectActions* actions, QObject* parent = nullptr);

    ProjectActions& actions() noexcept { return m_actions; }
    const ProjectActions& actions() const noexcept { return m_actions; }

    QString pendingAction() const;
    bool resumeAfterSave() const noexcept { return m_actions.resumeAfterSave(); }

    Q_INVOKABLE void requestAction(const QString& action, bool isDirty);
    // Open a known project file (Open Recent); asks about unsaved changes first.
    Q_INVOKABLE void requestOpenFile(const QString& path, bool isDirty);
    Q_INVOKABLE void requestSave(bool hasExistingFile);
    Q_INVOKABLE void requestSaveAs();
    Q_INVOKABLE void onUnsavedSave(bool hasExistingFile);
    Q_INVOKABLE void onUnsavedDiscard();
    Q_INVOKABLE void onUnsavedCancel();
    Q_INVOKABLE void onSaveCompleted();
    Q_INVOKABLE void onSaveCancelled();

signals:
    void pendingActionChanged();
    void showUnsavedConfirmation();
    void showOpenDialog();
    void showSaveDialog();
    void executeNew();
    void executeClose();
    void executeOpenFile(const QUrl& file);
    void directSaveRequested();

private:
    void dispatch(ProjectActions::Request request);

    ProjectActions m_ownedActions;
    ProjectActions& m_actions;
};
