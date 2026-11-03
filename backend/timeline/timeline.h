#pragma once

#include "session/tracklist.h"

#include <optional>
#include <string_view>

// Musical edit/selection state, independent of Qt and pixel rendering.
class Timeline {
public:
    struct EditRequest { std::vector<int> ids; std::string operation; double delta; int lanes; };
    struct CreationRequest { int trackId; double start, length; };
    struct RepeatRange { double start = 0, end = 0; };
    struct Preview { int clipId, lane; double start, length; bool selected; };

    void setTracks(TrackList* tracks);
    void sync();
    void setMeter(int numerator, int denominator);
    void setBpm(double bpm);
    void setSnapMode(int mode);
    int snapMode() const { return m_snapMode; }
    double snapStep() const;
    double beatUnit() const { return 4.0 / m_denominator; }
    double secondsPerBeat() const { return 60.0 / m_bpm; }
    double clipEndBeat(int trackId, int clipId) const;
    double beatsPerBar() const { return m_numerator * beatUnit(); }
    double gridStep() const { return snapStep() > 0 ? snapStep() : beatUnit(); }
    double contentEndBars() const;
    double snap(double beat, std::string_view direction = {}) const;
    bool isBarLine(double beat) const;
    static bool isBarLine(double beat, double beatsPerBar);
    std::string barLabel(double bar) const;
    bool hasOverlap(int trackId, double start, double length) const;
    std::optional<CreationRequest> createClip(double beat, int lane) const;
    bool canSplit(int trackId, int clipId, double beat) const;

    void setBusy(bool busy) { m_busy = busy; }
    void setProjectLoading(bool loading) { m_loading = loading; }
    void setRecording(bool recording) { m_recording = recording; }
    int numerator() const { return m_numerator; }
    int denominator() const { return m_denominator; }
    double bpm() const { return m_bpm; }
    bool busy() const { return m_busy; }
    bool loading() const { return m_loading; }
    bool recording() const { return m_recording; }
    bool keyboardAllowed() const;
    bool canEditSelection() const;
    bool canMergeSelection() const;
    bool canSplitSelection() const;
    const std::vector<int>& selectedIds() const { return m_selectedIds; }
    int selectedClipId() const { return m_selectedClipId; }
    int selectedTrackId() const { return m_selectedTrackId; }
    void deselect();
    void selectAll();
    void selectClip(int trackId, int clipId, bool toggle, bool preserve);
    void beginEdit(const std::string& operation);
    void updateEdit(double delta);
    void updateDrag(double originalBeat, double deltaBeats, bool verticalOnly = false);
    void updateLane(int delta);
    void cancelEdit();
    const std::string& editOperation() const { return m_operation; }
    double editDelta() const { return m_delta; }
    int editLaneDelta() const { return m_lanes; }
    std::optional<EditRequest> commitEdit();
    std::optional<EditRequest> nudge(int direction) const;
    std::vector<Preview> previews() const;

    void beginSelection(double beat, double lane, bool clips);
    void updateSelection(double beat, double lane, bool dragged);
    // Insets/padding are normalized lane geometry supplied by the presentation layer.
    std::optional<RepeatRange> finishSelection(double topInset, double bottomInset, double padding);
    void acceptRepeatRange(bool accepted);
    void cancelSelection();
    bool selecting() const { return m_selecting; }
    bool hasSelection() const { return m_hasSelection; }
    bool selectingClips() const { return m_clipSelection; }
    double rangeStart() const;
    double rangeEnd() const;
    double savedRangeStart() const { return m_range.start; }
    double savedRangeEnd() const { return m_range.end; }
    void setRangeStart(double value);
    void setRangeEnd(double value);

private:
    TrackList* m_tracks = nullptr;
    int m_snapMode = 1, m_numerator = 4, m_denominator = 4;
    double m_bpm = 120;
    bool m_busy = false, m_loading = false, m_recording = false;
    std::vector<int> m_selectedIds;
    int m_selectedClipId = -1, m_selectedTrackId = -1;
    std::string m_operation;
    double m_delta = 0;
    int m_lanes = 0;
    bool m_selecting = false, m_hasSelection = false, m_clipSelection = false, m_dragged = false;
    double m_startBeat = 0, m_endBeat = 0, m_startLane = 0, m_endLane = 0;
    RepeatRange m_range;
    std::optional<RepeatRange> m_pendingRange;
    bool selected(int id) const;
};
