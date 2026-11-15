#include "mixerPanelWrap.h"
#include "trackListModelWrap.h"

MixerPanelWrap::MixerPanelWrap(QObject* parent)
    : QObject(parent)
{
}

void MixerPanelWrap::setTrackList(TrackListModelWrap* trackList)
{
    if (m_trackList == trackList) return;
    if (m_trackList) disconnect(m_trackList, nullptr, this, nullptr);
    m_trackList = trackList;
    if (m_trackList) {
        connect(m_trackList, &TrackListModelWrap::masterVolumeChanged, this, &MixerPanelWrap::masterVolumeChanged);
        connect(m_trackList, &TrackListModelWrap::masterMutedChanged, this, &MixerPanelWrap::masterMutedChanged);
    }
    emit trackListChanged();
    emit masterVolumeChanged();
    emit masterMutedChanged();
}

double MixerPanelWrap::masterVolume() const noexcept
{
    return m_trackList ? m_trackList->masterVolume() : mixer::defaultVolumeGain;
}

bool MixerPanelWrap::masterMuted() const noexcept
{
    return m_trackList ? m_trackList->masterMuted() : false;
}

double MixerPanelWrap::clampVolume(double volume) const noexcept
{
    return mixer::clampVolume(volume);
}

double MixerPanelWrap::clampClipGain(double gainDb) const noexcept
{
    return mixer::clampClipGain(gainDb);
}

double MixerPanelWrap::clampStripWidth(double width) const noexcept
{
    return mixer::clampStripWidth(width);
}

QString MixerPanelWrap::formatVolume(double volume) const
{
    return QString::fromStdString(mixer::formatVolumeDb(volume));
}

QString MixerPanelWrap::formatVolumeTooltip(double volume) const
{
    return QString::fromStdString(mixer::formatVolumeTooltip(volume));
}

QString MixerPanelWrap::formatMasterVolume(double volume) const
{
    return QString::fromStdString(mixer::formatVolumeDb(volume));
}

QString MixerPanelWrap::formatMasterVolumeTooltip(double volume) const
{
    return QString::fromStdString(mixer::formatMasterVolumeTooltip(volume));
}

QString MixerPanelWrap::formatClipGain(double gainDb) const
{
    return QString::fromStdString(mixer::formatClipGainDb(gainDb));
}

QString MixerPanelWrap::formatClipGainTooltip(double gainDb) const
{
    return QString::fromStdString(mixer::formatClipGainTooltip(gainDb));
}

QString MixerPanelWrap::formatPan(double pan) const
{
    return QString::fromStdString(mixer::formatPan(pan));
}

QString MixerPanelWrap::formatPanTooltip(double pan) const
{
    return QString::fromStdString(mixer::formatPanTooltip(pan));
}

QString MixerPanelWrap::formatMuteTooltip(bool muted) const
{
    return QString::fromStdString(mixer::formatMuteTooltip(muted));
}

QString MixerPanelWrap::formatMasterMuteTooltip(bool muted) const
{
    return QString::fromStdString(mixer::formatMasterMuteTooltip(muted));
}

QString MixerPanelWrap::formatSoloTooltip(bool solo) const
{
    return QString::fromStdString(mixer::formatSoloTooltip(solo));
}

void MixerPanelWrap::setVolume(int trackId, double volume)
{
    if (!m_trackList) return;
    m_trackList->setVolume(trackId, mixer::clampVolume(volume));
}

void MixerPanelWrap::resetVolume(int trackId)
{
    if (!m_trackList) return;
    m_trackList->setVolume(trackId, mixer::defaultVolumeGain);
}

void MixerPanelWrap::setPan(int trackId, double pan)
{
    if (!m_trackList) return;
    m_trackList->setPan(trackId, panlaw::clampPan(pan));
}

void MixerPanelWrap::setStripWidth(int trackId, double width)
{
    if (!m_trackList) return;
    m_trackList->setStripWidth(trackId, mixer::clampStripWidth(width));
}

void MixerPanelWrap::resetPan(int trackId)
{
    if (!m_trackList) return;
    m_trackList->setPan(trackId, panlaw::centre);
}

void MixerPanelWrap::setMasterVolume(double volume)
{
    if (!m_trackList) return;
    m_trackList->setMasterVolume(mixer::clampVolume(volume));
}

void MixerPanelWrap::resetMasterVolume()
{
    if (!m_trackList) return;
    m_trackList->resetMasterVolume();
}

void MixerPanelWrap::setClipGain(int trackId, int clipId, double gainDb, bool continueEdit)
{
    if (!m_trackList || clipId < 0) return;
    m_trackList->setClipGain(trackId, clipId, mixer::clampClipGain(gainDb), continueEdit);
}

void MixerPanelWrap::resetClipGain(int trackId, int clipId)
{
    if (!m_trackList || clipId < 0) return;
    m_trackList->setClipGain(trackId, clipId, mixer::defaultClipGainDb);
}

void MixerPanelWrap::setMuted(int trackId, bool muted)
{
    if (!m_trackList) return;
    m_trackList->setMuted(trackId, muted);
}

void MixerPanelWrap::setMasterMuted(bool muted)
{
    if (!m_trackList) return;
    m_trackList->setMasterMuted(muted);
}

void MixerPanelWrap::setSolo(int trackId, bool solo)
{
    if (!m_trackList) return;
    m_trackList->setSolo(trackId, solo);
}

int MixerPanelWrap::targetClipId(const QVariantList& trackClips, int selectedClipId) const
{
    std::vector<int> ids;
    ids.reserve(static_cast<std::size_t>(trackClips.size()));
    for (const auto& clip : trackClips) ids.push_back(clip.toMap().value(QStringLiteral("clipId"), -1).toInt());
    return mixer::chooseTargetClip(ids, selectedClipId);
}

double MixerPanelWrap::clipGainOf(const QVariantList& trackClips, int clipId) const
{
    for (const auto& clip : trackClips) {
        const auto map = clip.toMap();
        if (map.value(QStringLiteral("clipId"), -1).toInt() == clipId)
            return map.value(QStringLiteral("gainDb"), mixer::defaultClipGainDb).toDouble();
    }
    return mixer::defaultClipGainDb;
}
