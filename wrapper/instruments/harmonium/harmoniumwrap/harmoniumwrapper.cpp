#include "harmoniumwrapper.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"

#include <QVariantMap>

HarmoniumWrapper::HarmoniumWrapper(QObject* parent)
    : QObject(parent)
{
}

QObject* HarmoniumWrapper::trackModel() const
{
    return m_trackModel;
}

void HarmoniumWrapper::setTrackModel(QObject* model)
{
    auto* tracks = qobject_cast<TrackListWrapper*>(model);
    if (tracks == m_trackModel) return;
    if (m_trackModel) disconnect(m_trackModel, nullptr, this, nullptr);
    m_trackModel = tracks;
    if (m_trackModel) {
        connect(m_trackModel, &TrackListWrapper::harmoniumChanged, this, [this](int id) {
            if (id == m_trackId) reload();
        });
        // Loading a project or bringing a track back replaces the settings without a signal.
        connect(m_trackModel, &TrackListWrapper::projectChanged, this, &HarmoniumWrapper::reload);
        connect(m_trackModel, &QObject::destroyed, this, &HarmoniumWrapper::reload);
    }
    emit trackModelChanged();
    reload();
}

void HarmoniumWrapper::setTrackId(int id)
{
    if (id == m_trackId) return;
    m_trackId = id;
    emit trackIdChanged();
    reload();
}

bool HarmoniumWrapper::valid() const
{
    return m_trackModel && m_trackModel->harmonium(m_trackId) != nullptr;
}

void HarmoniumWrapper::reload()
{
    const auto* stored = m_trackModel ? m_trackModel->harmonium(m_trackId) : nullptr;
    m_s = stored ? *stored : harmonium::Settings{};
    emit settingsChanged();
}

bool HarmoniumWrapper::commit(const harmonium::Settings& settings)
{
    // setHarmonium sanitises and emits harmoniumChanged (reloading every panel on the track).
    // Reload here too: a value clamped back to what was stored changes nothing and emits
    // nothing, but the control that sent it must still snap back.
    const bool ok = m_trackModel->setHarmonium(m_trackId, settings);
    reload();
    return ok;
}

QString HarmoniumWrapper::style() const
{
    return QString::fromUtf8(harmonium::styleName(m_s.style));
}

void HarmoniumWrapper::setStyle(const QString& name)
{
    harmonium::Style style;
    if (!harmonium::styleFromName(name.toStdString(), style)) return;
    edit([style](auto& s) { s.style = style; });
}

QVariantList HarmoniumWrapper::drones() const
{
    QVariantList keys;
    for (const int key : m_s.drones) keys.append(key);
    return keys;
}

bool HarmoniumWrapper::setDrone(int index, int key)
{
    if (index < 0 || index >= harmonium::DroneCount) return false;
    if (key != harmonium::NoDrone && (key < 0 || key > 127)) return false;
    return edit([index, key](auto& s) { s.drones[std::size_t(index)] = key; });
}

bool HarmoniumWrapper::resetToDefaults()
{
    return valid() && commit(harmonium::Settings{});
}

QVariantList HarmoniumWrapper::styles() const
{
    return {QVariantMap{{"id", "indian"}, {"title", "Indian"}},
            QVariantMap{{"id", "western"}, {"title", "Western reed organ"}}};
}
