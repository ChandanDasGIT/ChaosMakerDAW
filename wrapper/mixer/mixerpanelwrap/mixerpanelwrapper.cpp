#include "mixerpanelwrapper.h"
#include "tracklistwrapper.h"

MixerPanelWrapper::MixerPanelWrapper(QObject* parent)
    : QObject(parent)
{
}

void MixerPanelWrapper::setTrackList(TrackListWrapper* trackList)
{
    if (m_trackList == trackList) return;
    if (m_trackList) disconnect(m_trackList, nullptr, this, nullptr);
    m_trackList = trackList;
    if (m_trackList) {
        connect(m_trackList, &TrackListWrapper::masterVolumeChanged, this, &MixerPanelWrapper::masterVolumeChanged);
        connect(m_trackList, &TrackListWrapper::masterMutedChanged, this, &MixerPanelWrapper::masterMutedChanged);
    }
    emit trackListChanged();
    emit masterVolumeChanged();
    emit masterMutedChanged();
}

double MixerPanelWrapper::masterVolume() const noexcept
{
    return m_trackList ? m_trackList->masterVolume() : mixer::defaultVolumeGain;
}

bool MixerPanelWrapper::masterMuted() const noexcept
{
    return m_trackList ? m_trackList->masterMuted() : false;
}

double MixerPanelWrapper::clampVolume(double volume) const noexcept
{
    return mixer::clampVolume(volume);
}

double MixerPanelWrapper::clampClipGain(double gainDb) const noexcept
{
    return mixer::clampClipGain(gainDb);
}

double MixerPanelWrapper::clampStripWidth(double width) const noexcept
{
    return mixer::clampStripWidth(width);
}

QString MixerPanelWrapper::formatVolume(double volume) const
{
    return QString::fromStdString(mixer::formatVolumeDb(volume));
}

QString MixerPanelWrapper::formatVolumeTooltip(double volume) const
{
    return QString::fromStdString(mixer::formatVolumeTooltip(volume));
}

QString MixerPanelWrapper::formatMasterVolume(double volume) const
{
    return QString::fromStdString(mixer::formatVolumeDb(volume));
}

QString MixerPanelWrapper::formatMasterVolumeTooltip(double volume) const
{
    return QString::fromStdString(mixer::formatMasterVolumeTooltip(volume));
}

QString MixerPanelWrapper::formatClipGain(double gainDb) const
{
    return QString::fromStdString(mixer::formatClipGainDb(gainDb));
}

QString MixerPanelWrapper::formatClipGainTooltip(double gainDb) const
{
    return QString::fromStdString(mixer::formatClipGainTooltip(gainDb));
}

QString MixerPanelWrapper::formatMuteTooltip(bool muted) const
{
    return QString::fromStdString(mixer::formatMuteTooltip(muted));
}

QString MixerPanelWrapper::formatMasterMuteTooltip(bool muted) const
{
    return QString::fromStdString(mixer::formatMasterMuteTooltip(muted));
}

QString MixerPanelWrapper::formatSoloTooltip(bool solo) const
{
    return QString::fromStdString(mixer::formatSoloTooltip(solo));
}

void MixerPanelWrapper::setVolume(int trackId, double volume)
{
    if (!m_trackList) return;
    m_trackList->setVolume(trackId, mixer::clampVolume(volume));
}

void MixerPanelWrapper::resetVolume(int trackId)
{
    if (!m_trackList) return;
    m_trackList->setVolume(trackId, mixer::defaultVolumeGain);
}

void MixerPanelWrapper::setMasterVolume(double volume)
{
    if (!m_trackList) return;
    m_trackList->setMasterVolume(mixer::clampVolume(volume));
}

void MixerPanelWrapper::resetMasterVolume()
{
    if (!m_trackList) return;
    m_trackList->resetMasterVolume();
}

void MixerPanelWrapper::setClipGain(int trackId, int clipId, double gainDb, bool continueEdit)
{
    if (!m_trackList || clipId < 0) return;
    m_trackList->setClipGain(trackId, clipId, mixer::clampClipGain(gainDb), continueEdit);
}

void MixerPanelWrapper::resetClipGain(int trackId, int clipId)
{
    if (!m_trackList || clipId < 0) return;
    m_trackList->setClipGain(trackId, clipId, mixer::defaultClipGainDb);
}

void MixerPanelWrapper::setMuted(int trackId, bool muted)
{
    if (!m_trackList) return;
    m_trackList->setMuted(trackId, muted);
}

void MixerPanelWrapper::setMasterMuted(bool muted)
{
    if (!m_trackList) return;
    m_trackList->setMasterMuted(muted);
}

void MixerPanelWrapper::setSolo(int trackId, bool solo)
{
    if (!m_trackList) return;
    m_trackList->setSolo(trackId, solo);
}

int MixerPanelWrapper::targetClipId(const QVariantList& trackClips, int selectedClipId) const
{
    std::vector<int> ids;
    ids.reserve(static_cast<std::size_t>(trackClips.size()));
    for (const auto& clip : trackClips) ids.push_back(clip.toMap().value(QStringLiteral("clipId"), -1).toInt());
    return mixer::chooseTargetClip(ids, selectedClipId);
}

double MixerPanelWrapper::clipGainOf(const QVariantList& trackClips, int clipId) const
{
    for (const auto& clip : trackClips) {
        const auto map = clip.toMap();
        if (map.value(QStringLiteral("clipId"), -1).toInt() == clipId)
            return map.value(QStringLiteral("gainDb"), mixer::defaultClipGainDb).toDouble();
    }
    return mixer::defaultClipGainDb;
}
