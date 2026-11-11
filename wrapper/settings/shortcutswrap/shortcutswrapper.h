#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include "settings/shortcutmap.h"

// The keyboard shortcuts for QML (the `Shortcuts` singleton, restMap G16): every Shortcut {}
// reads its key from `keys`, menus show `texts`, and the Preferences editor changes them. The
// rules live in backend/settings/shortcutmap; the user's changes are saved in the platform
// settings under "shortcuts/<action id>" ("" = no key), so the defaults can change later.
class ShortcutsWrapper : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Shortcuts)
    QML_SINGLETON
    // action id -> key in the form Shortcut.sequence takes ("Ctrl+Shift+S"; "" = none).
    Q_PROPERTY(QVariantMap keys READ keys NOTIFY changed)
    // action id -> the key as menus and tooltips show it ("" = none).
    Q_PROPERTY(QVariantMap texts READ texts NOTIFY changed)
    // For the editor: [{ id, name, group, key, text, isDefault }], grouped, in order.
    Q_PROPERTY(QVariantList actions READ actions NOTIFY changed)
    // How many actions there are (fixed): a list view's model, so rows stay put while keys change.
    Q_PROPERTY(int actionCount READ actionCount CONSTANT)

public:
    explicit ShortcutsWrapper(QObject* parent = nullptr);

    QVariantMap keys() const;
    QVariantMap texts() const;
    QVariantList actions() const;
    int actionCount() const { return int(ShortcutMap::actions().size()); }

    // A key press (Keys.onPressed: event.key, event.modifiers) as key text; "" for a modifier
    // pressed alone. Whether it may be used: isAllowed().
    Q_INVOKABLE QString keyFromEvent(int key, int modifiers) const;
    Q_INVOKABLE bool isAllowed(const QString& key) const;
    Q_INVOKABLE QString displayText(const QString& key) const;
    // The names of the actions that would lose `key` if `id` took it.
    Q_INVOKABLE QStringList conflicts(const QString& id, const QString& key) const;
    // Gives `id` the key (taking it from others in the same context); "" clears it. Saved.
    Q_INVOKABLE bool set(const QString& id, const QString& key);
    Q_INVOKABLE void reset(const QString& id);
    Q_INVOKABLE void resetAll();
    // Reads the saved changes again (tests; the app reads them once at start).
    Q_INVOKABLE void reload();

signals:
    void changed();

private:
    void save() const;
    ShortcutMap m_map;
};
