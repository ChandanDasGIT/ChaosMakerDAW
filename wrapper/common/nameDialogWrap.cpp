#include "nameDialogWrap.h"

#include "common/namedialog.h"

NameDialogWrap::NameDialogWrap(QObject* parent) : QObject(parent) {}

QString NameDialogWrap::trimmed(const QString& name) const
{
    return QString::fromStdString(namedialog::trimmed(name.toStdString()));
}

int NameDialogWrap::nextStep(bool renaming, const QString& name, bool taken, bool confirmedReplace) const
{
    switch (namedialog::nextStep(renaming, name.toStdString(), taken, confirmedReplace)) {
    case namedialog::Step::Rename: return Rename;
    case namedialog::Step::AskReplace: return AskReplace;
    case namedialog::Step::Save: return Save;
    case namedialog::Step::Nothing: break;
    }
    return Nothing;
}
