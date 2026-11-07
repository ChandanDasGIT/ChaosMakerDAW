#include "trackeffectspanelwrapper.h"

TrackEffectsPanelWrapper::TrackEffectsPanelWrapper(QObject* parent)
    : QObject(parent)
{
}

void TrackEffectsPanelWrapper::updateTrackInfo()
{
    m_panel.setTrackInfo(
        m_stagedTrackId,
        m_stagedTrackName.toStdString(),
        m_stagedTrackType.toStdString(),
        m_stagedHasLiveInput,
        m_stagedRecordDry);
    emit trackChanged();
}

void TrackEffectsPanelWrapper::setChain(const QVariantList& chain)
{
    if (m_stagedChain == chain) return;
    m_stagedChain = chain;
    const std::string prevSelected = m_panel.selectedEffect();
    std::vector<std::string> types;
    for (const auto& entry : chain) types.push_back(entry.toMap().value(QStringLiteral("type")).toString().toStdString());
    m_panel.setChain(std::move(types));
    emit capabilitiesChanged();
    if (m_panel.selectedEffect() != prevSelected) emit selectedEffectChanged();
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

QString TrackEffectsPanelWrapper::recordModeDescription() const
{
    return QString::fromStdString(m_panel.currentRecordModeDescription());
}
