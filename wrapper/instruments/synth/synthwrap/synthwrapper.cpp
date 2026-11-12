#include "synthwrapper.h"
#include "instruments/guitar/dsp/tempodivision.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"

#include <QStandardPaths>

#include <cmath>

namespace {

// What the page shows for each choice id.
QString choiceTitle(std::string_view key, std::string_view id)
{
    if (key == "lfo.division") {
        const int i = std::stoi(std::string(id));
        return QString::fromUtf8(tempo::divisionNames[std::size_t(i)]);
    }
    static const QVariantMap titles{
        {"saw", "Saw"}, {"pulse", "Pulse"}, {"triangle", "Triangle"}, {"sine", "Sine"},
        {"square", "Square"}, {"random", "Random"},
        {"lowpass12", "Low-pass 12 dB"}, {"lowpass24", "Low-pass 24 dB"},
        {"highpass", "High-pass"}, {"bandpass", "Band-pass"},
        {"pitch", "Pitch"}, {"filter", "Filter"}, {"volume", "Volume"}, {"pulsewidth", "Pulse width"},
        {"poly", "Poly"}, {"mono", "Mono"}, {"legato", "Legato"},
    };
    const auto name = QString::fromUtf8(id.data(), qsizetype(id.size()));
    return titles.value(name, name).toString();
}

QString kindName(synth::ControlKind kind)
{
    switch (kind) {
    case synth::ControlKind::Number: return QStringLiteral("number");
    case synth::ControlKind::Integer: return QStringLiteral("integer");
    case synth::ControlKind::Toggle: return QStringLiteral("toggle");
    case synth::ControlKind::Choice: return QStringLiteral("choice");
    }
    return {};
}

QString text(std::string_view s) { return QString::fromUtf8(s.data(), qsizetype(s.size())); }

} // namespace

SynthWrapper::SynthWrapper(QObject* parent)
    : QObject(parent)
{
}

SynthWrapper::~SynthWrapper() = default;

QObject* SynthWrapper::trackModel() const { return m_trackModel; }

void SynthWrapper::setTrackModel(QObject* model)
{
    auto* tracks = qobject_cast<TrackListWrapper*>(model);
    if (tracks == m_trackModel) return;
    if (m_trackModel) disconnect(m_trackModel, nullptr, this, nullptr);
    m_trackModel = tracks;
    if (m_trackModel) {
        connect(m_trackModel, &TrackListWrapper::synthChanged, this, [this](int id) {
            if (id == m_trackId) reload();
        });
        // Loading a project or bringing a track back replaces the settings without a signal.
        connect(m_trackModel, &TrackListWrapper::projectChanged, this, &SynthWrapper::reload);
        connect(m_trackModel, &QObject::destroyed, this, &SynthWrapper::reload);
    }
    emit trackModelChanged();
    reload();
}

void SynthWrapper::setTrackId(int id)
{
    if (id == m_trackId) return;
    m_trackId = id;
    emit trackIdChanged();
    reload();
}

bool SynthWrapper::valid() const
{
    return m_trackModel && m_trackModel->synth(m_trackId) != nullptr;
}

void SynthWrapper::reload()
{
    const auto* stored = m_trackModel ? m_trackModel->synth(m_trackId) : nullptr;
    m_s = stored ? *stored : synth::Settings{};
    emit settingsChanged();
    emit currentPresetChanged();
}

bool SynthWrapper::commit(const synth::Settings& settings)
{
    // setSynth sanitises and emits synthChanged (reloading every panel on the track). Reload
    // here too: a value clamped back to what was stored changes nothing and emits nothing, but
    // the control that sent it must still snap back.
    const bool ok = m_trackModel->setSynth(m_trackId, settings);
    reload();
    return ok;
}

QVariantMap SynthWrapper::values() const
{
    QVariantMap map;
    for (const auto& c : synth::controls()) {
        const double v = c.get(m_s);
        QVariant value;
        switch (c.kind) {
        case synth::ControlKind::Number: value = v; break;
        case synth::ControlKind::Integer: value = int(std::lround(v)); break;
        case synth::ControlKind::Toggle: value = v >= 0.5; break;
        case synth::ControlKind::Choice: value = text(c.choices[std::size_t(std::lround(v))]); break;
        }
        map.insert(text(c.key), value);
    }
    return map;
}

QVariantMap SynthWrapper::controls() const
{
    QVariantMap map;
    for (const auto& c : synth::controls()) {
        QVariantMap entry{{"kind", kindName(c.kind)}, {"min", c.min}, {"max", c.max}};
        if (c.kind == synth::ControlKind::Choice) {
            QVariantList choices;
            for (const auto id : c.choices)
                choices.append(QVariantMap{{"id", text(id)}, {"title", choiceTitle(c.key, id)}});
            entry.insert("choices", choices);
        }
        map.insert(text(c.key), entry);
    }
    return map;
}

bool SynthWrapper::set(const QString& key, const QVariant& value)
{
    if (!valid()) return false;
    const auto k = key.toStdString();
    const auto* control = synth::findControl(k);
    if (!control) return false;
    auto next = m_s;
    if (control->kind == synth::ControlKind::Choice) {
        if (!synth::setChoice(next, k, value.toString().toStdString())) return false;
    } else if (control->kind == synth::ControlKind::Toggle) {
        if (!synth::setControl(next, k, value.toBool() ? 1.0 : 0.0)) return false;
    } else {
        bool ok = false;
        const double v = value.toDouble(&ok);
        if (!ok || !synth::setControl(next, k, v)) return false;
    }
    return commit(next);
}

bool SynthWrapper::resetToDefaults()
{
    return valid() && commit(synth::Settings{});
}

// ---------------------------------------------------------------- presets

QString SynthWrapper::folder() const
{
    if (!m_folder.isEmpty()) return m_folder;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/synth-presets");
}

void SynthWrapper::setFolder(const QString& folder)
{
    if (folder == m_folder) return;
    m_folder = folder;
    m_store.reset();
    emit presetsChanged();
    emit currentPresetChanged();
}

SynthPresetStore& SynthWrapper::store() const
{
    if (!m_store) m_store = std::make_unique<SynthPresetStore>(folder().toStdString());
    return *m_store;
}

QVariantList SynthWrapper::presets() const
{
    QVariantList list;
    for (const auto& p : store().all())
        list.append(QVariantMap{{"name", QString::fromStdString(p.name)}, {"factory", p.factory}});
    return list;
}

QString SynthWrapper::currentPreset() const
{
    if (!valid()) return {};
    if (!m_chosen.isEmpty())
        if (const auto chosen = store().find(m_chosen.toStdString()); chosen && chosen->settings == m_s)
            return QString::fromStdString(chosen->name);
    for (const auto& p : store().all())
        if (p.settings == m_s) return QString::fromStdString(p.name);
    return {};
}

bool SynthWrapper::fail(const QString& message)
{
    m_lastError = message;
    emit lastErrorChanged();
    return false;
}

void SynthWrapper::succeed()
{
    if (m_lastError.isEmpty()) return;
    m_lastError.clear();
    emit lastErrorChanged();
}

bool SynthWrapper::loadPreset(const QString& name)
{
    if (!valid()) return fail(QStringLiteral("Pick a synth track first."));
    const auto preset = store().find(name.toStdString());
    if (!preset) return fail(QStringLiteral("There is no preset named \"%1\".").arg(name));
    succeed();
    m_chosen = QString::fromStdString(preset->name);
    const bool ok = commit(preset->settings);
    emit currentPresetChanged();                       // also when the sound was this already
    return ok;
}

bool SynthWrapper::savePreset(const QString& name, bool replace)
{
    if (!valid()) return fail(QStringLiteral("Pick a synth track first."));
    std::string error;
    if (!store().save({name.toStdString(), m_s, false}, replace, error)) return fail(QString::fromStdString(error));
    succeed();
    if (const auto saved = store().find(name.trimmed().toStdString())) m_chosen = QString::fromStdString(saved->name);
    emit presetsChanged();
    emit currentPresetChanged();
    return true;
}

bool SynthWrapper::renamePreset(const QString& name, const QString& newName)
{
    std::string error;
    if (!store().rename(name.toStdString(), newName.toStdString(), error)) return fail(QString::fromStdString(error));
    succeed();
    if (m_chosen.compare(name, Qt::CaseInsensitive) == 0) m_chosen = newName.trimmed();
    emit presetsChanged();
    emit currentPresetChanged();
    return true;
}

bool SynthWrapper::removePreset(const QString& name)
{
    std::string error;
    if (!store().remove(name.toStdString(), error)) return fail(QString::fromStdString(error));
    succeed();
    emit presetsChanged();
    emit currentPresetChanged();
    return true;
}

bool SynthWrapper::presetExists(const QString& name) const
{
    return store().find(name.toStdString()).has_value();
}

bool SynthWrapper::isFactoryPreset(const QString& name) const
{
    const auto p = store().find(name.toStdString());
    return p && p->factory;
}

void SynthWrapper::refreshPresets()
{
    emit presetsChanged();
    emit currentPresetChanged();
}
