#include "shortcutswrapper.h"

#include <QKeySequence>
#include <QSettings>

namespace {
const QString Group = QStringLiteral("shortcuts");

QString text(std::string_view view) { return QString::fromUtf8(view.data(), qsizetype(view.size())); }
}

ShortcutsWrapper::ShortcutsWrapper(QObject* parent)
    : QObject(parent)
{
    reload();
}

void ShortcutsWrapper::reload()
{
    QSettings settings;
    settings.beginGroup(Group);
    std::map<std::string, std::string> changes;
    for (const auto& id : settings.childKeys())
        changes[id.toStdString()] = settings.value(id).toString().toStdString();
    settings.endGroup();
    m_map.loadChanges(changes);
    emit changed();
}

void ShortcutsWrapper::save() const
{
    QSettings settings;
    settings.remove(Group);
    settings.beginGroup(Group);
    for (const auto& [id, key] : m_map.changes())
        settings.setValue(QString::fromStdString(id), QString::fromStdString(key));
    settings.endGroup();
}

QVariantMap ShortcutsWrapper::keys() const
{
    QVariantMap out;
    for (const auto& action : ShortcutMap::actions())
        out.insert(text(action.id), QString::fromStdString(m_map.key(action.id)));
    return out;
}

QString ShortcutsWrapper::displayText(const QString& key) const
{
    return QKeySequence::fromString(key, QKeySequence::PortableText).toString(QKeySequence::NativeText);
}

QVariantMap ShortcutsWrapper::texts() const
{
    QVariantMap out;
    for (const auto& action : ShortcutMap::actions())
        out.insert(text(action.id), displayText(QString::fromStdString(m_map.key(action.id))));
    return out;
}

QVariantList ShortcutsWrapper::actions() const
{
    QVariantList out;
    for (const auto& action : ShortcutMap::actions()) {
        const auto key = QString::fromStdString(m_map.key(action.id));
        out.append(QVariantMap{{"id", text(action.id)}, {"name", text(action.name)}, {"group", text(action.group)},
                               {"key", key}, {"text", displayText(key)}, {"isDefault", m_map.isDefault(action.id)}});
    }
    return out;
}

QString ShortcutsWrapper::keyFromEvent(int key, int modifiers) const
{
    switch (key) {
    case Qt::Key_Shift: case Qt::Key_Control: case Qt::Key_Alt: case Qt::Key_Meta:
    case Qt::Key_AltGr: case Qt::Key_Super_L: case Qt::Key_Super_R: case Qt::Key_Hyper_L:
    case Qt::Key_Hyper_R: case Qt::Key_CapsLock: case Qt::Key_NumLock: case Qt::Key_unknown:
        return {};
    default: break;
    }
    const auto mods = Qt::KeyboardModifiers(modifiers)
                      & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
    const QKeySequence sequence(QKeyCombination(mods, Qt::Key(key)));
    const auto normal = ShortcutMap::normalise(sequence.toString(QKeySequence::PortableText).toStdString());
    // A refused key (Escape, Return, Tab ...) still comes back as text, so the editor can say no.
    return normal ? QString::fromStdString(*normal) : sequence.toString(QKeySequence::PortableText);
}

bool ShortcutsWrapper::isAllowed(const QString& key) const
{
    const auto normal = ShortcutMap::normalise(key.toStdString());
    return normal && !normal->empty();
}

QStringList ShortcutsWrapper::conflicts(const QString& id, const QString& key) const
{
    QStringList names;
    for (const auto& other : m_map.conflicts(id.toStdString(), key.toStdString()))
        if (const auto* action = ShortcutMap::find(other)) names.append(text(action->name));
    return names;
}

bool ShortcutsWrapper::set(const QString& id, const QString& key)
{
    if (!m_map.set(id.toStdString(), key.toStdString())) return false;
    save();
    emit changed();
    return true;
}

void ShortcutsWrapper::reset(const QString& id)
{
    if (!ShortcutMap::find(id.toStdString())) return;
    m_map.reset(id.toStdString());
    save();
    emit changed();
}

void ShortcutsWrapper::resetAll()
{
    m_map.resetAll();
    save();
    emit changed();
}
