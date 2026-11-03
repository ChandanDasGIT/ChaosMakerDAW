#pragma once

#include <QObject>
#include <qqmlregistration.h>
#include "mixer/mixerpanel.h"

class TrackListWrapper;

class MixerPanelWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(TrackListWrapper* trackList READ trackList WRITE setTrackList NOTIFY trackListChanged)
    Q_PROPERTY(double minVolume READ minVolume CONSTANT)
    Q_PROPERTY(double maxVolume READ maxVolume CONSTANT)
    Q_PROPERTY(double defaultVolume READ defaultVolume CONSTANT)
    Q_PROPERTY(double minStripWidth READ minStripWidth CONSTANT)
    Q_PROPERTY(double maxStripWidth READ maxStripWidth CONSTANT)
    Q_PROPERTY(double defaultStripWidth READ defaultStripWidth CONSTANT)

public:
    explicit MixerPanelWrapper(QObject* parent = nullptr);

    TrackListWrapper* trackList() const noexcept { return m_trackList; }
    void setTrackList(TrackListWrapper* trackList);

    double minVolume() const noexcept { return mixer::minVolumeGain; }
    double maxVolume() const noexcept { return mixer::maxVolumeGain; }
    double defaultVolume() const noexcept { return mixer::defaultVolumeGain; }

    double minStripWidth() const noexcept { return mixer::minStripWidth; }
    double maxStripWidth() const noexcept { return mixer::maxStripWidth; }
    double defaultStripWidth() const noexcept { return mixer::defaultStripWidth; }

    Q_INVOKABLE double clampVolume(double volume) const noexcept;
    Q_INVOKABLE double clampStripWidth(double width) const noexcept;
    Q_INVOKABLE QString formatVolume(double volume) const;
    Q_INVOKABLE QString formatVolumeTooltip(double volume) const;
    Q_INVOKABLE QString formatMuteTooltip(bool muted) const;
    Q_INVOKABLE QString formatSoloTooltip(bool solo) const;

    Q_INVOKABLE void setVolume(int trackId, double volume);
    Q_INVOKABLE void resetVolume(int trackId);
    Q_INVOKABLE void setMuted(int trackId, bool muted);
    Q_INVOKABLE void setSolo(int trackId, bool solo);

signals:
    void trackListChanged();

private:
    TrackListWrapper* m_trackList{nullptr};
};
