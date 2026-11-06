#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlregistration.h>
#include "mixer/mixerpanel.h"

class TrackListWrapper;

class MixerPanelWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(TrackListWrapper* trackList READ trackList WRITE setTrackList NOTIFY trackListChanged)
    Q_PROPERTY(double masterVolume READ masterVolume WRITE setMasterVolume NOTIFY masterVolumeChanged)
    Q_PROPERTY(bool masterMuted READ masterMuted WRITE setMasterMuted NOTIFY masterMutedChanged)
    Q_PROPERTY(double minVolume READ minVolume CONSTANT)
    Q_PROPERTY(double maxVolume READ maxVolume CONSTANT)
    Q_PROPERTY(double defaultVolume READ defaultVolume CONSTANT)
    Q_PROPERTY(double minClipGain READ minClipGain CONSTANT)
    Q_PROPERTY(double maxClipGain READ maxClipGain CONSTANT)
    Q_PROPERTY(double defaultClipGain READ defaultClipGain CONSTANT)
    Q_PROPERTY(double clipGainStep READ clipGainStep CONSTANT)
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

    double masterVolume() const noexcept;
    bool masterMuted() const noexcept;

    double minClipGain() const noexcept { return mixer::minClipGainDb; }
    double maxClipGain() const noexcept { return mixer::maxClipGainDb; }
    double defaultClipGain() const noexcept { return mixer::defaultClipGainDb; }
    double clipGainStep() const noexcept { return mixer::clipGainStep; }

    double minStripWidth() const noexcept { return mixer::minStripWidth; }
    double maxStripWidth() const noexcept { return mixer::maxStripWidth; }
    double defaultStripWidth() const noexcept { return mixer::defaultStripWidth; }

    Q_INVOKABLE double clampVolume(double volume) const noexcept;
    Q_INVOKABLE double clampClipGain(double gainDb) const noexcept;
    Q_INVOKABLE double clampStripWidth(double width) const noexcept;
    Q_INVOKABLE QString formatVolume(double volume) const;
    Q_INVOKABLE QString formatVolumeTooltip(double volume) const;
    Q_INVOKABLE QString formatMasterVolume(double volume) const;
    Q_INVOKABLE QString formatMasterVolumeTooltip(double volume) const;
    // Target clip of a strip's clip gain fader (backend mixer::chooseTargetClip). The view passes
    // the track's "clips" role and the selected clip id so its bindings follow both.
    Q_INVOKABLE int targetClipId(const QVariantList& trackClips, int selectedClipId) const;
    // Gain (dB) of that clip in the same list; 0 dB when it is not there.
    Q_INVOKABLE double clipGainOf(const QVariantList& trackClips, int clipId) const;
    Q_INVOKABLE QString formatClipGain(double gainDb) const;
    Q_INVOKABLE QString formatClipGainTooltip(double gainDb) const;
    Q_INVOKABLE QString formatMuteTooltip(bool muted) const;
    Q_INVOKABLE QString formatMasterMuteTooltip(bool muted) const;
    Q_INVOKABLE QString formatSoloTooltip(bool solo) const;

    Q_INVOKABLE void setVolume(int trackId, double volume);
    Q_INVOKABLE void resetVolume(int trackId);
    Q_INVOKABLE void setMasterVolume(double volume);
    Q_INVOKABLE void resetMasterVolume();
    // continueEdit: later steps of the same fader drag join the drag's single undo step.
    Q_INVOKABLE void setClipGain(int trackId, int clipId, double gainDb, bool continueEdit = false);
    Q_INVOKABLE void resetClipGain(int trackId, int clipId);
    Q_INVOKABLE void setMuted(int trackId, bool muted);
    Q_INVOKABLE void setMasterMuted(bool muted);
    Q_INVOKABLE void setSolo(int trackId, bool solo);

signals:
    void trackListChanged();
    void masterVolumeChanged();
    void masterMutedChanged();

private:
    TrackListWrapper* m_trackList{nullptr};
};
