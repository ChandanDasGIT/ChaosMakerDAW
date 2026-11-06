#pragma once

#include "transport/musicalgrid.h"
#include "timeline/timeline.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"
#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

class TimelineWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(TrackListWrapper* trackModel READ trackModel WRITE setTrackModel NOTIFY trackModelChanged)
    Q_PROPERTY(int snapMode READ snapMode WRITE setSnapMode NOTIFY stateChanged)
    Q_PROPERTY(int numerator READ numerator WRITE setNumerator NOTIFY stateChanged)
    Q_PROPERTY(int denominator READ denominator WRITE setDenominator NOTIFY stateChanged)
    Q_PROPERTY(double bpm READ bpm WRITE setBpm NOTIFY stateChanged)
    Q_PROPERTY(bool takeBusy READ takeBusy WRITE setTakeBusy NOTIFY stateChanged)
    Q_PROPERTY(bool projectLoading READ projectLoading WRITE setProjectLoading NOTIFY stateChanged)
    Q_PROPERTY(bool recording READ recording WRITE setRecording NOTIFY stateChanged)
    Q_PROPERTY(double snapStepBeats READ snapStepBeats NOTIFY stateChanged)
    Q_PROPERTY(double gridStepBeats READ gridStepBeats NOTIFY stateChanged)
    Q_PROPERTY(double beatUnit READ beatUnit NOTIFY stateChanged)
    Q_PROPERTY(double secondsPerBeat READ secondsPerBeat NOTIFY stateChanged)
    Q_PROPERTY(double beatsPerBar READ beatsPerBar NOTIFY stateChanged)
    Q_PROPERTY(double contentEndBars READ contentEndBars NOTIFY stateChanged)
    Q_PROPERTY(QVariantList selectedClipIds READ selectedClipIds NOTIFY stateChanged)
    Q_PROPERTY(int selectedClipId READ selectedClipId NOTIFY stateChanged)
    Q_PROPERTY(int selectedTrackId READ selectedTrackId NOTIFY stateChanged)
    Q_PROPERTY(bool keyboardAllowed READ keyboardAllowed NOTIFY stateChanged)
    Q_PROPERTY(bool canEditSelection READ canEditSelection NOTIFY stateChanged)
    Q_PROPERTY(bool canMergeSelection READ canMergeSelection NOTIFY stateChanged)
    Q_PROPERTY(bool canSplitSelection READ canSplitSelection NOTIFY stateChanged)
    Q_PROPERTY(bool canCopy READ canCopy NOTIFY stateChanged)
    Q_PROPERTY(bool canCut READ canCut NOTIFY stateChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY stateChanged)
    Q_PROPERTY(bool canDuplicate READ canDuplicate NOTIFY stateChanged)
    Q_PROPERTY(QString editOperation READ editOperation NOTIFY stateChanged)
    Q_PROPERTY(double editDelta READ editDelta NOTIFY stateChanged)
    Q_PROPERTY(int editLaneDelta READ editLaneDelta NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap previews READ previews NOTIFY stateChanged)
    Q_PROPERTY(bool selecting READ selecting NOTIFY stateChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY stateChanged)
    Q_PROPERTY(bool clipSelectionDrag READ clipSelectionDrag NOTIFY stateChanged)
    Q_PROPERTY(double rangeStartBeat READ rangeStartBeat NOTIFY stateChanged)
    Q_PROPERTY(double rangeEndBeat READ rangeEndBeat NOTIFY stateChanged)
    Q_PROPERTY(double timeRangeStartBeat READ timeRangeStartBeat WRITE setTimeRangeStartBeat NOTIFY stateChanged)
    Q_PROPERTY(double timeRangeEndBeat READ timeRangeEndBeat WRITE setTimeRangeEndBeat NOTIFY stateChanged)

public:
    explicit TimelineWrapper(QObject* parent = nullptr) : QObject(parent) {}
    TrackListWrapper* trackModel() const { return m_tracks; }
    void setTrackModel(TrackListWrapper* tracks);
    int snapMode() const { return m_timeline.snapMode(); }
    void setSnapMode(int mode) { m_timeline.setSnapMode(mode); emit stateChanged(); }
    int numerator() const { return m_timeline.numerator(); }
    int denominator() const { return m_timeline.denominator(); }
    double bpm() const { return m_timeline.bpm(); }
    void setNumerator(int n) { m_timeline.setMeter(n, denominator()); emit stateChanged(); }
    void setDenominator(int n) { m_timeline.setMeter(numerator(), n); emit stateChanged(); }
    void setBpm(double n) { m_timeline.setBpm(n); emit stateChanged(); }
    bool takeBusy() const { return m_timeline.busy(); }
    bool projectLoading() const { return m_timeline.loading(); }
    bool recording() const { return m_timeline.recording(); }
    void setTakeBusy(bool value) { m_timeline.setBusy(value); emit stateChanged(); }
    void setProjectLoading(bool value) { m_timeline.setProjectLoading(value); emit stateChanged(); }
    void setRecording(bool value) { m_timeline.setRecording(value); emit stateChanged(); }
    double snapStepBeats() const { return m_timeline.snapStep(); }
    double gridStepBeats() const { return m_timeline.gridStep(); }
    double beatUnit() const { return m_timeline.beatUnit(); }
    double secondsPerBeat() const { return m_timeline.secondsPerBeat(); }
    double beatsPerBar() const { return m_timeline.beatsPerBar(); }
    double contentEndBars() const { return m_timeline.contentEndBars(); }
    QVariantList selectedClipIds() const;
    int selectedClipId() const { return m_timeline.selectedClipId(); }
    int selectedTrackId() const { return m_timeline.selectedTrackId(); }
    bool keyboardAllowed() const { return m_timeline.keyboardAllowed(); }
    bool canEditSelection() const { return m_timeline.canEditSelection(); }
    bool canMergeSelection() const { return m_timeline.canMergeSelection(); }
    bool canSplitSelection() const { return m_timeline.canSplitSelection(); }
    bool canCopy() const { return m_timeline.canCopySelection(); }
    bool canCut() const { return m_timeline.canCutSelection(); }
    bool canPaste() const { return m_timeline.canPaste(); }
    bool canDuplicate() const { return m_timeline.canDuplicateSelection(); }
    QString editOperation() const { return QString::fromStdString(m_timeline.editOperation()); }
    double editDelta() const { return m_timeline.editDelta(); }
    int editLaneDelta() const { return m_timeline.editLaneDelta(); }
    QVariantMap previews() const;
    bool selecting() const { return m_timeline.selecting(); }
    bool hasSelection() const { return m_timeline.hasSelection(); }
    bool clipSelectionDrag() const { return m_timeline.selectingClips(); }
    double rangeStartBeat() const { return m_timeline.rangeStart(); }
    double rangeEndBeat() const { return m_timeline.rangeEnd(); }
    double timeRangeStartBeat() const { return m_timeline.savedRangeStart(); }
    double timeRangeEndBeat() const { return m_timeline.savedRangeEnd(); }
    void setTimeRangeStartBeat(double value) { m_timeline.setRangeStart(value); emit stateChanged(); }
    void setTimeRangeEndBeat(double value) { m_timeline.setRangeEnd(value); emit stateChanged(); }

    Q_INVOKABLE void deselectClips() {
        m_timeline.deselect();
        if (m_tracks) m_tracks->setSelectedClipId(-1);
        emit stateChanged();
    }
    Q_INVOKABLE void selectAllClips() {
        m_timeline.selectAll();
        if (m_tracks) m_tracks->setSelectedClipId(m_timeline.selectedClipId());
        emit stateChanged();
    }
    Q_INVOKABLE void selectTrack(int id) { if (m_tracks) m_tracks->selectTrack(id); else m_timeline.selectTrack(id); emit stateChanged(); }
    Q_INVOKABLE void selectClip(int id, int clip, bool toggle, bool preserve) {
        m_timeline.selectClip(id, clip, toggle, preserve);
        if (m_tracks) {
            m_tracks->setSelectedClipId(m_timeline.selectedClipId());
            if (m_timeline.selectedClipId() >= 0) m_tracks->selectTrack(id);
        }
        emit stateChanged();
    }
    Q_INVOKABLE bool copySelectedClips();
    Q_INVOKABLE QVariantList cutSelectedClips();
    Q_INVOKABLE QVariantList pasteClips(double targetBeat = -1, int targetTrackId = -1);
    Q_INVOKABLE QVariantList duplicateSelectedClips(double beatOffset = 0.0);
    Q_INVOKABLE void beginEdit(const QString& op) { m_timeline.beginEdit(op.toStdString()); emit stateChanged(); }
    Q_INVOKABLE void updateGroupEdit(double delta) { m_timeline.updateEdit(delta); emit stateChanged(); }
    Q_INVOKABLE void updateDrag(double original, double delta, bool verticalOnly = false) { m_timeline.updateDrag(original, delta, verticalOnly); emit stateChanged(); }
    Q_INVOKABLE void updateGroupLane(int delta) { m_timeline.updateLane(delta); emit stateChanged(); }
    Q_INVOKABLE void cancelEdit() { m_timeline.cancelEdit(); emit stateChanged(); }
    Q_INVOKABLE void commitGroupEdit();
    Q_INVOKABLE void nudgeClips(int direction);
    Q_INVOKABLE void seek(double beat) { emit seekRequested(m_timeline.snap(beat)); }
    Q_INVOKABLE void createClipAt(double beat, int lane);
    Q_INVOKABLE bool canSplit(int track, int clip, double beat) const { return m_timeline.canSplit(track, clip, beat); }
    Q_INVOKABLE double clipEndBeat(int track, int clip) const { return m_timeline.clipEndBeat(track, clip); }
    Q_INVOKABLE bool isBarLine(double beat, double beatsPerBar) const { return Timeline::isBarLine(beat, beatsPerBar); }
    Q_INVOKABLE QString barLabel(double bar) const { return QString::fromStdString(m_timeline.barLabel(bar)); }
    // Which bar labels / grid lines to draw (shared MusicalGrid rules; see musicalgrid.h).
    Q_INVOKABLE int lineStride(double unitPixels, double minimumPixels) const { return MusicalGrid::lineStride(unitPixels, minimumPixels); }
    Q_INVOKABLE double firstVisibleLine(double scrollPixels, double unitPixels, int stride) const { return MusicalGrid::firstVisibleLine(scrollPixels, unitPixels, stride); }
    Q_INVOKABLE int visibleLineCount(double viewPixels, double unitPixels, int stride) const { return MusicalGrid::visibleLineCount(viewPixels, unitPixels, stride); }
    Q_INVOKABLE void beginSelection(double beat, double lane, bool clips) { m_timeline.beginSelection(beat, lane, clips); emit stateChanged(); }
    Q_INVOKABLE void updateSelection(double beat, double lane, bool dragged) { m_timeline.updateSelection(beat, lane, dragged); emit stateChanged(); }
    Q_INVOKABLE void finishSelection(double top, double bottom, double padding);
    Q_INVOKABLE void acceptRepeatRange(bool accepted) { m_timeline.acceptRepeatRange(accepted); emit stateChanged(); }
    Q_INVOKABLE void cancelSelection() { m_timeline.cancelSelection(); emit stateChanged(); }

signals:
    void stateChanged();
    void trackModelChanged();
    void seekRequested(double beat);
    void repeatRangeRequested(double start, double end);

private:
    QPointer<TrackListWrapper> m_tracks;
    Timeline m_timeline;
    void apply(const std::optional<Timeline::EditRequest>& request);
};
