#include "basswrapper.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"

#include <QStandardPaths>

#include <cmath>

namespace {

// What the page shows for each choice id.
QString choiceTitle(std::string_view id)
{
    static const QVariantMap titles{{"finger", "Finger"}, {"slap", "Slap / pop"}, {"mute", "Palm mute"}};
    const auto name = QString::fromUtf8(id.data(), qsizetype(id.size()));
    return titles.value(name, name).toString();
}

QString kindName(bass::ControlKind kind)
{
    switch (kind) {
    case bass::ControlKind::Number: return QStringLiteral("number");
    case bass::ControlKind::Integer: return QStringLiteral("integer");
    case bass::ControlKind::Toggle: return QStringLiteral("toggle");
    case bass::ControlKind::Choice: return QStringLiteral("choice");
    }
    return {};
}

QString text(std::string_view s) { return QString::fromUtf8(s.data(), qsizetype(s.size())); }

} // namespace

BassWrapper::BassWrapper(QObject* parent)
    : QObject(parent)
{
}

BassWrapper::~BassWrapper() = default;

QObject* BassWrapper::trackModel() const { return m_trackModel; }

void BassWrapper::setTrackModel(QObject* model)
{
    auto* tracks = qobject_cast<TrackListWrapper*>(model);
    if (tracks == m_trackModel) return;
    if (m_trackModel) disconnect(m_trackModel, nullptr, this, nullptr);
    m_trackModel = tracks;
    if (m_trackModel) {
        connect(m_trackModel, &TrackListWrapper::bassChanged, this, [this](int id) {
            if (id == m_trackId) reload();
        });
        // Loading a project or bringing a track back replaces the settings without a signal.
        connect(m_trackModel, &TrackListWrapper::projectChanged, this, &BassWrapper::reload);
        connect(m_trackModel, &QObject::destroyed, this, &BassWrapper::reload);
    }
    emit trackModelChanged();
    reload();
}

void BassWrapper::setTrackId(int id)
{
    if (id == m_trackId) return;
    m_trackId = id;
    emit trackIdChanged();
    reload();
}

bool BassWrapper::valid() const
{
    return m_trackModel && m_trackModel->bass(m_trackId) != nullptr;
}

void BassWrapper::reload()
{
    const auto* stored = m_trackModel ? m_trackModel->bass(m_trackId) : nullptr;
    m_s = stored ? *stored : bass::Settings{};
    emit settingsChanged();
    emit currentPresetChanged();
}

bool BassWrapper::commit(const bass::Settings& settings)
{
    // setBass sanitises and emits bassChanged (reloading every panel on the track). Reload
    // here too: a value clamped back to what was stored changes nothing and emits nothing, but
    // the control that sent it must still snap back.
    const bool ok = m_trackModel->setBass(m_trackId, settings);
    reload();
    return ok;
}

QVariantMap BassWrapper::values() const
{
    QVariantMap map;
    for (const auto& c : bass::controls()) {
        const double v = c.get(m_s);
        QVariant value;
        switch (c.kind) {
        case bass::ControlKind::Number: value = v; break;
        case bass::ControlKind::Integer: value = int(std::lround(v)); break;
        case bass::ControlKind::Toggle: value = v >= 0.5; break;
        case bass::ControlKind::Choice: value = text(c.choices[std::size_t(std::lround(v))]); break;
        }
        map.insert(text(c.key), value);
    }
    return map;
}

QVariantMap BassWrapper::controls() const
{
    QVariantMap map;
    for (const auto& c : bass::controls()) {
        QVariantMap entry{{"kind", kindName(c.kind)}, {"min", c.min}, {"max", c.max}};
        if (c.kind == bass::ControlKind::Choice) {
            QVariantList choices;
            for (const auto id : c.choices)
                choices.append(QVariantMap{{"id", text(id)}, {"title", choiceTitle(id)}});
            entry.insert("choices", choices);
        }
        map.insert(text(c.key), entry);
    }
    return map;
}

bool BassWrapper::set(const QString& key, const QVariant& value)
{
    if (!valid()) return false;
    const auto k = key.toStdString();
    const auto* control = bass::findControl(k);
    if (!control) return false;
    auto next = m_s;
    if (control->kind == bass::ControlKind::Choice) {
        if (!bass::setChoice(next, k, value.toString().toStdString())) return false;
    } else if (control->kind == bass::ControlKind::Toggle) {
        if (!bass::setControl(next, k, value.toBool() ? 1.0 : 0.0)) return false;
    } else {
        bool ok = false;
        const double v = value.toDouble(&ok);
        if (!ok || !bass::setControl(next, k, v)) return false;
    }
    return commit(next);
}

bool BassWrapper::resetToDefaults()
{
    return valid() && commit(bass::Settings{});
}

// ---------------------------------------------------------------- presets

QString BassWrapper::folder() const
{
    if (!m_folder.isEmpty()) return m_folder;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/bass-presets");
}

void BassWrapper::setFolder(const QString& folder)
{
    if (folder == m_folder) return;
    m_folder = folder;
    m_store.reset();
    emit presetsChanged();
    emit currentPresetChanged();
}

BassPresetStore& BassWrapper::store() const
{
    if (!m_store) m_store = std::make_unique<BassPresetStore>(folder().toStdString());
    return *m_store;
}

QVariantList BassWrapper::presets() const
{
    QVariantList list;
    for (const auto& p : store().all())
        list.append(QVariantMap{{"name", QString::fromStdString(p.name)}, {"factory", p.factory}});
    return list;
}

QString BassWrapper::currentPreset() const
{
    if (!valid()) return {};
    if (!m_chosen.isEmpty())
        if (const auto chosen = store().find(m_chosen.toStdString()); chosen && chosen->settings == m_s)
            return QString::fromStdString(chosen->name);
    for (const auto& p : store().all())
        if (p.settings == m_s) return QString::fromStdString(p.name);
    return {};
}

bool BassWrapper::fail(const QString& message)
{
    m_lastError = message;
    emit lastErrorChanged();
    return false;
}

void BassWrapper::succeed()
{
    if (m_lastError.isEmpty()) return;
    m_lastError.clear();
    emit lastErrorChanged();
}

bool BassWrapper::loadPreset(const QString& name)
{
    if (!valid()) return fail(QStringLiteral("Pick a bass track first."));
    const auto preset = store().find(name.toStdString());
    if (!preset) return fail(QStringLiteral("There is no preset named \"%1\".").arg(name));
    succeed();
    m_chosen = QString::fromStdString(preset->name);
    const bool ok = commit(preset->settings);
    emit currentPresetChanged();                       // also when the sound was this already
    return ok;
}

bool BassWrapper::savePreset(const QString& name, bool replace)
{
    if (!valid()) return fail(QStringLiteral("Pick a bass track first."));
    std::string error;
    if (!store().save({name.toStdString(), m_s, false}, replace, error)) return fail(QString::fromStdString(error));
    succeed();
    if (const auto saved = store().find(name.trimmed().toStdString())) m_chosen = QString::fromStdString(saved->name);
    emit presetsChanged();
    emit currentPresetChanged();
    return true;
}

bool BassWrapper::renamePreset(const QString& name, const QString& newName)
{
    std::string error;
    if (!store().rename(name.toStdString(), newName.toStdString(), error)) return fail(QString::fromStdString(error));
    succeed();
    if (m_chosen.compare(name, Qt::CaseInsensitive) == 0) m_chosen = newName.trimmed();
    emit presetsChanged();
    emit currentPresetChanged();
    return true;
}

bool BassWrapper::removePreset(const QString& name)
{
    std::string error;
    if (!store().remove(name.toStdString(), error)) return fail(QString::fromStdString(error));
    succeed();
    emit presetsChanged();
    emit currentPresetChanged();
    return true;
}

bool BassWrapper::presetExists(const QString& name) const
{
    return store().find(name.toStdString()).has_value();
}

bool BassWrapper::isFactoryPreset(const QString& name) const
{
    const auto p = store().find(name.toStdString());
    return p && p->factory;
}

void BassWrapper::refreshPresets()
{
    emit presetsChanged();
    emit currentPresetChanged();
}
