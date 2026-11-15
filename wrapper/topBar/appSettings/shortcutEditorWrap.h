#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class ShortcutsWrap;

// The keyboard shortcuts editor (restMap G16): waiting for a new key for one action, what a
// pressed key leads to, and the message shown. Keys are changed and saved through the shared
// Shortcuts (`shortcuts`), so every menu and Shortcut {} follows.
class ShortcutEditorWrap : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* shortcuts READ shortcuts WRITE setShortcuts NOTIFY shortcutsChanged)
    // The action whose key is being changed ("" = none), the key pressed, and its takers.
    Q_PROPERTY(QString capturingId READ capturingId NOTIFY stateChanged)
    Q_PROPERTY(QString pendingKey READ pendingKey NOTIFY stateChanged)
    Q_PROPERTY(QStringList pendingConflicts READ pendingConflicts NOTIFY stateChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)

public:
    explicit ShortcutEditorWrap(QObject* parent = nullptr);

    QObject* shortcuts() const;
    void setShortcuts(QObject* shortcuts);
    QString capturingId() const { return m_capturingId; }
    QString pendingKey() const { return m_pendingKey; }
    QStringList pendingConflicts() const { return m_pendingConflicts; }
    QString message() const { return m_message; }

    Q_INVOKABLE void startCapture(const QString& id);
    Q_INVOKABLE void stopCapture();
    // A key pressed while capturing (Shortcuts.keyFromEvent).
    Q_INVOKABLE void offer(const QString& key);
    // Gives the capturing action `key` ("" = none) and stops capturing.
    Q_INVOKABLE void assign(const QString& key);

signals:
    void shortcutsChanged();
    void stateChanged();

private:
    QPointer<ShortcutsWrap> m_shortcuts;
    QString m_capturingId;
    QString m_pendingKey;
    QStringList m_pendingConflicts;
    QString m_message;
};
