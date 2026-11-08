#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include <QtQml/qqmlregistration.h>
#include "midi/pianorollpattern.h"
#include "midi/pianorollgrid.h"
#include "midi/notenames.h"
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
    // Colour of the bound clip's track ("#rrggbb"); empty when unbound.
    Q_PROPERTY(QString trackColour READ trackColour NOTIFY trackColourChanged)
    // A Drums track's clip: the keys the kit plays are labelled with its parts (backend drummap).
    Q_PROPERTY(bool drumMode READ drumMode NOTIFY bindingChanged)
    // Which drum kit names the keys (the track type, "" for other tracks): the labels follow it
    // from one kit to the other, where drumMode stays true.
    Q_PROPERTY(QString drumKit READ drumKit NOTIFY bindingChanged)
    // Grid and snapping (backend PianoRollGrid). Settings use the menu names below.
    Q_PROPERTY(QString gridDivision READ gridDivision WRITE setGridDivision NOTIFY gridChanged)
    Q_PROPERTY(QString gridStyle READ gridStyle WRITE setGridStyle NOTIFY gridChanged)
    Q_PROPERTY(QString noteLengthMode READ noteLengthMode WRITE setNoteLengthMode NOTIFY gridChanged)
    Q_PROPERTY(bool snapEnabled READ snapEnabled WRITE setSnapEnabled NOTIFY gridChanged)
    // Zoom of the view, needed for grid line spacing.
    Q_PROPERTY(double pixelsPerBeat READ pixelsPerBeat WRITE setPixelsPerBeat NOTIFY gridChanged)
    Q_PROPERTY(double gridLineBeats READ gridLineBeats NOTIFY gridChanged)
    Q_PROPERTY(int gridLinesPerBar READ gridLinesPerBar NOTIFY gridChanged)
    Q_PROPERTY(double snapStep READ snapStep NOTIFY gridChanged)
    Q_PROPERTY(QStringList gridDivisionOptions READ gridDivisionOptions CONSTANT)
    Q_PROPERTY(QStringList gridStyleOptions READ gridStyleOptions CONSTANT)
    Q_PROPERTY(QStringList noteLengthOptions READ noteLengthOptions CONSTANT)

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
    QString trackColour() const;
    bool drumMode() const;
    QString drumKit() const;
    // Clip-relative beat as "bar.beat.hundredths" in the project meter.
    Q_INVOKABLE QString positionLabel(double beat) const;

    const PianoRollGrid& grid() const noexcept { return m_grid; }
    QString gridDivision() const { return QString::fromStdString(m_grid.divisionName()); }
    void setGridDivision(const QString& name);
    QString gridStyle() const { return QString::fromStdString(m_grid.styleName()); }
    void setGridStyle(const QString& name);
    QString noteLengthMode() const { return QString::fromStdString(m_grid.noteLengthName()); }
    void setNoteLengthMode(const QString& name);
    bool snapEnabled() const noexcept { return m_grid.snapEnabled(); }
    void setSnapEnabled(bool enabled);
    double pixelsPerBeat() const noexcept { return m_pixelsPerBeat; }
    void setPixelsPerBeat(double pixels);
    double gridLineBeats() const noexcept { return m_grid.lineBeats(m_pixelsPerBeat); }
    int gridLinesPerBar() const noexcept { return m_grid.linesPerBar(m_pixelsPerBeat); }
    double snapStep() const noexcept { return m_grid.snapStep(); }
    static QStringList gridDivisionOptions();
    static QStringList gridStyleOptions();
    static QStringList noteLengthOptions();

    // Snapping helpers for the view's drag and draw code (all in clip-relative beats).
    Q_INVOKABLE double snapFloor(double beat) const noexcept { return m_grid.snapFloor(beat); }
    Q_INVOKABLE double snapDelta(double deltaBeats) const noexcept { return m_grid.snapDelta(deltaBeats); }
    Q_INVOKABLE double newNoteLength() const noexcept { return m_grid.newNoteLength(); }
    Q_INVOKABLE double minimumNoteLength() const noexcept { return m_grid.minimumNoteLength(); }
    Q_INVOKABLE bool isOnGrid(double position, double unit) const noexcept { return PianoRollGrid::isOnGrid(position, unit); }
    // Ruler ticks and grid lines (shared MusicalGrid rules). The view passes its notifying
    // properties as arguments so its bindings follow meter and zoom changes.
    Q_INVOKABLE int countedBeatsPerBar(double beatsPerBar, double beatUnit) const noexcept { return MusicalGrid::countedBeatsPerBar(beatsPerBar, beatUnit); }
    Q_INVOKABLE int tickCount(double lengthBeats, double beatUnit, bool includeEnd) const noexcept { return MusicalGrid::tickCount(lengthBeats, beatUnit, includeEnd); }
    Q_INVOKABLE bool isBarTick(int index, int perBar) const noexcept { return MusicalGrid::isBarTick(index, perBar); }
    Q_INVOKABLE QString tickLabel(int index, int perBar) const { return QString::fromStdString(MusicalGrid::tickLabel(index, perBar)); }
    Q_INVOKABLE int lineCount(double lengthBeats, double beatsPerBar, int linesPerBar) const noexcept { return MusicalGrid::lineCount(lengthBeats, beatsPerBar, linesPerBar); }
    Q_INVOKABLE double lineOffsetInBar(int index, int linesPerBar, double spacing) const noexcept { return MusicalGrid::lineOffsetInBar(index, linesPerBar, spacing); }
    Q_INVOKABLE double linePosition(int index, int linesPerBar, double spacing, double beatsPerBar) const noexcept { return MusicalGrid::linePosition(index, linesPerBar, spacing, beatsPerBar); }
    Q_INVOKABLE bool isBarLineIndex(int index, int linesPerBar) const noexcept { return MusicalGrid::isBarLineIndex(index, linesPerBar); }
    // Note naming (backend notenames): "C4", black keys, C rows, "C4 60" readouts.
    Q_INVOKABLE QString noteName(int note) const { return QString::fromStdString(notenames::name(note)); }
    Q_INVOKABLE bool isBlackKey(int note) const noexcept { return notenames::isBlackKey(note); }
    Q_INVOKABLE bool isOctaveStart(int note) const noexcept { return notenames::isOctaveStart(note); }
    Q_INVOKABLE QString noteReadout(int note) const { return QString::fromStdString(notenames::readout(note)); }
    // The drum kit's part on `note` ("Kick", "Hi-hat open"), or "" when the kit has none there.
    Q_INVOKABLE QString drumName(int note) const;
    // Quantize to the Grid (selected notes, or all), as one undoable edit.
    Q_INVOKABLE bool quantizeToGrid() { return quantize(m_grid.quantizeStep()); }

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
    // Rubber-band box (pitches and clip-relative beats, any order); returns notes touched.
    Q_INVOKABLE int selectNotesInRange(int lowNote, int highNote, double startBeat, double endBeat,
                                       bool addToSelection = false);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool deleteSelected();
    // Quantize note starts to stepBeats within each bar of the project meter (selected notes,
    // or all when none selected): one edit.
    Q_INVOKABLE bool quantize(double stepBeats);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void reload();

signals:
    void notesChanged();
    void bindingChanged();
    void clipStartChanged();
    void meterChanged();
    void trackColourChanged();
    void gridChanged();

private:
    bool commit();       // write the pattern to the bound clip; reverts on rejection
    void bindingEdited();

    void syncGridMeter();

    PianoRollPattern m_pattern;
    PianoRollGrid m_grid;
    double m_pixelsPerBeat = 96.0;
    QPointer<TrackListWrapper> m_trackList;
    QMetaObject::Connection m_clipsConnection;
    int m_trackId = -1;
    int m_clipId = -1;
};
