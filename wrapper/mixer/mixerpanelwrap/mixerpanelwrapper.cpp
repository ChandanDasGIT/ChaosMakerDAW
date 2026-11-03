#include "mixerpanelwrapper.h"
#include "tracklistwrapper.h"

MixerPanelWrapper::MixerPanelWrapper(QObject* parent)
    : QObject(parent)
{
}

void MixerPanelWrapper::setTrackList(TrackListWrapper* trackList)
{
    if (m_trackList == trackList) return;
    m_trackList = trackList;
    emit trackListChanged();
}

double MixerPanelWrapper::clampVolume(double volume) const noexcept
{
    return mixer::clampVolume(volume);
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

QString MixerPanelWrapper::formatMuteTooltip(bool muted) const
{
    return QString::fromStdString(mixer::formatMuteTooltip(muted));
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

void MixerPanelWrapper::setMuted(int trackId, bool muted)
{
    if (!m_trackList) return;
    m_trackList->setMuted(trackId, muted);
}

void MixerPanelWrapper::setSolo(int trackId, bool solo)
{
    if (!m_trackList) return;
    m_trackList->setSolo(trackId, solo);
}
