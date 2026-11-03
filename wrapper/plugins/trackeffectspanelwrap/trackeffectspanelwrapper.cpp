#include "trackeffectspanelwrapper.h"

TrackEffectsPanelWrapper::TrackEffectsPanelWrapper(QObject* parent)
    : QObject(parent)
{
}

void TrackEffectsPanelWrapper::updateTrackInfo()
{
    const bool prevGuitar = m_panel.supportsGuitarSuite();
    const std::string prevSelected = m_panel.selectedEffect();

    m_panel.setTrackInfo(
        m_stagedTrackId,
        m_stagedTrackName.toStdString(),
        m_stagedTrackType.toStdString(),
        m_stagedHasLiveInput,
        m_stagedRecordDry);

    emit trackChanged();

    if (m_panel.supportsGuitarSuite() != prevGuitar) {
        emit capabilitiesChanged();
    }
    if (m_panel.selectedEffect() != prevSelected) {
        emit selectedEffectChanged();
    }
}

void TrackEffectsPanelWrapper::setTrackId(int id)
{
    if (m_stagedTrackId == id) return;
    m_stagedTrackId = id;
    updateTrackInfo();
}

void TrackEffectsPanelWrapper::setTrackName(const QString& name)
{
    if (m_stagedTrackName == name) return;
    m_stagedTrackName = name;
    updateTrackInfo();
}

void TrackEffectsPanelWrapper::setTrackType(const QString& type)
{
    if (m_stagedTrackType == type) return;
    m_stagedTrackType = type;
    updateTrackInfo();
}

void TrackEffectsPanelWrapper::setHasLiveInput(bool live)
{
    if (m_stagedHasLiveInput == live) return;
    m_stagedHasLiveInput = live;
    updateTrackInfo();
}

void TrackEffectsPanelWrapper::setRecordDry(bool dry)
{
    if (m_stagedRecordDry == dry) return;
    m_stagedRecordDry = dry;
    m_panel.setRecordDry(dry);
    emit recordDryChanged();
}

void TrackEffectsPanelWrapper::setSelectedEffect(const QString& key)
{
    const std::string prev = m_panel.selectedEffect();
    m_panel.setSelectedEffect(key.toStdString());
    if (m_panel.selectedEffect() != prev) {
        emit selectedEffectChanged();
    }
}

void TrackEffectsPanelWrapper::selectEffect(const QString& key)
{
    setSelectedEffect(key);
}

QVariantList TrackEffectsPanelWrapper::effectEntries() const
{
    QVariantList list;
    for (const auto& entry : m_panel.effectEntries()) {
        list.push_back(QVariantMap{
            {QStringLiteral("key"), QString::fromStdString(entry.key)},
            {QStringLiteral("title"), QString::fromStdString(entry.title)},
            {QStringLiteral("subtitle"), QString::fromStdString(entry.subtitle)},
            {QStringLiteral("available"), entry.available}
        });
    }
    return list;
}

QString TrackEffectsPanelWrapper::recordModeDescription() const
{
    return QString::fromStdString(m_panel.currentRecordModeDescription());
}
