#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include "midi/pianorollpattern.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h" // full type needed by Q_PROPERTY

// Adapts PianoRollPattern (editing state + selection) to QML. When bound to a clip through
// trackList/trackId/clipId, the clip's notes are loaded from the document, every committed
// edit is written back as one undoable change, and document changes (undo/redo, the other
// piano roll view, clip removal) reload the notes. Unbound, it edits a local pattern only.
class PianoRollWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantList notes READ notes NOTIFY notesChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY notesChanged)
    Q_PROPERTY(int count READ count NOTIFY notesChanged)
    Q_PROPERTY(TrackListWrapper* trackList READ trackList WRITE setTrackList NOTIFY bindingChanged)
    Q_PROPERTY(int trackId READ trackId WRITE setTrackId NOTIFY bindingChanged)
    Q_PROPERTY(int clipId READ clipId WRITE setClipId NOTIFY bindingChanged)
    Q_PROPERTY(bool bound READ bound NOTIFY bindingChanged)
    // Project beat where the bound clip starts; note beats and the playhead are relative to it.
    Q_PROPERTY(double clipStartBeat READ clipStartBeat NOTIFY clipStartChanged)
    // Project meter in quarter-note beats (4/4 -> 4 and 1, 6/8 -> 3 and 0.5); 4/4 when unbound.
    Q_PROPERTY(double beatsPerBar READ beatsPerBar NOTIFY meterChanged)
    Q_PROPERTY(double beatUnit READ beatUnit NOTIFY meterChanged)

public:
    explicit PianoRollWrapper(QObject* parent = nullptr);

    PianoRollPattern& backend() noexcept { return m_pattern; }
    const PianoRollPattern& backend() const noexcept { return m_pattern; }

    QVariantList notes() const;
    int selectedCount() const noexcept { return m_pattern.selectedCount(); }
    int count() const noexcept { return static_cast<int>(m_pattern.notes().size()); }

    TrackListWrapper* trackList() const { return m_trackList; }
    void setTrackList(TrackListWrapper* trackList);
    int trackId() const noexcept { return m_trackId; }
    void setTrackId(int id);
    int clipId() const noexcept { return m_clipId; }
    void setClipId(int id);
    bool bound() const;
    double clipStartBeat() const;
    double beatsPerBar() const;
    double beatUnit() const;
    // Clip-relative beat as "bar.beat.hundredths" in the project meter.
    Q_INVOKABLE QString positionLabel(double beat) const;

    Q_INVOKABLE int addNote(int note, double startBeat, double lengthBeats, int velocity = 100);
    Q_INVOKABLE bool removeNote(int noteId);
    Q_INVOKABLE bool removeNotesAt(int note, double beat);
    Q_INVOKABLE bool moveNote(int noteId, int newNote, double newStartBeat);
    Q_INVOKABLE bool resizeNote(int noteId, double newLengthBeats);
    // Set velocities of several notes (parallel lists, values clamped 0..127) as ONE edit,
    // so a multi-note velocity drag is a single undo step.
    Q_INVOKABLE bool setNoteVelocities(const QVariantList& noteIds, const QVariantList& velocities);
    Q_INVOKABLE void selectNote(int noteId, bool addToSelection = false);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool deleteSelected();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void reload();

signals:
    void notesChanged();
    void bindingChanged();
    void clipStartChanged();
    void meterChanged();

private:
    bool commit();       // write the pattern to the bound clip; reverts on rejection
    void bindingEdited();

    PianoRollPattern m_pattern;
    QPointer<TrackListWrapper> m_trackList;
    QMetaObject::Connection m_clipsConnection;
    int m_trackId = -1;
    int m_clipId = -1;
};
