#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

// The name dialog's rules for QML (backend namedialog).
class NameDialogWrap : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    enum Step { Nothing, Rename, AskReplace, Save };
    Q_ENUM(Step)

    explicit NameDialogWrap(QObject* parent = nullptr);

    Q_INVOKABLE QString trimmed(const QString& name) const;
    Q_INVOKABLE int nextStep(bool renaming, const QString& name, bool taken, bool confirmedReplace) const;
};
