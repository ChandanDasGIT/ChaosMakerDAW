#include "shortcutEditorWrap.h"

#include "topBar/appSettings/shortcutEditor/shortcuteditor.h"
#include "wrapper/common/shortcuts/shortcutsWrap.h"

#include <string>
#include <vector>

ShortcutEditorWrap::ShortcutEditorWrap(QObject* parent) : QObject(parent) {}

QObject* ShortcutEditorWrap::shortcuts() const { return m_shortcuts; }

void ShortcutEditorWrap::setShortcuts(QObject* shortcuts)
{
    auto* wrap = qobject_cast<ShortcutsWrap*>(shortcuts);
    if (wrap == m_shortcuts) return;
    m_shortcuts = wrap;
    emit shortcutsChanged();
}

void ShortcutEditorWrap::startCapture(const QString& id)
{
    m_capturingId = id;
    m_pendingKey.clear();
    m_pendingConflicts.clear();
    m_message = QString::fromStdString(
        shortcuteditor::captureMessage(shortcuteditor::actionName(id.toStdString())));
    emit stateChanged();
}

void ShortcutEditorWrap::stopCapture()
{
    m_capturingId.clear();
    m_pendingKey.clear();
    m_pendingConflicts.clear();
    m_message.clear();
    emit stateChanged();
}

void ShortcutEditorWrap::offer(const QString& key)
{
    if (!m_shortcuts) return;
    const QStringList taken = key.isEmpty() ? QStringList() : m_shortcuts->conflicts(m_capturingId, key);
    const bool allowed = !key.isEmpty() && m_shortcuts->isAllowed(key);
    const auto text = m_shortcuts->displayText(key).toStdString();
    switch (shortcuteditor::judge(key.toStdString(), allowed, std::size_t(taken.size()))) {
    case shortcuteditor::Offer::KeepWaiting:
        return;
    case shortcuteditor::Offer::Reserved:
        m_message = QString::fromStdString(shortcuteditor::reservedMessage(text));
        emit stateChanged();
        return;
    case shortcuteditor::Offer::Assign:
        assign(key);
        return;
    case shortcuteditor::Offer::Conflict: {
        std::vector<std::string> takers;
        for (const auto& name : taken) takers.push_back(name.toStdString());
        m_pendingKey = key;
        m_pendingConflicts = taken;
        m_message = QString::fromStdString(shortcuteditor::conflictMessage(text, takers));
        emit stateChanged();
        return;
    }
    }
}

void ShortcutEditorWrap::assign(const QString& key)
{
    if (m_shortcuts) m_shortcuts->set(m_capturingId, key);
    stopCapture();
}
