#pragma once

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>
#include <vector>
#include "midi/midinote.h"

// The piano roll's notes as a list model, one row per note (roles: id, note, startBeat,
// lengthBeats, velocity, selected). sync() compares the new notes with the rows by note id:
// a changed note updates only its own row, new and removed notes insert and remove rows,
// a reordered note moves. The model is never reset, so QML keeps the other notes' delegates.
class PianoRollNoteModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

public:
    enum Role { IdRole = Qt::UserRole + 1, NoteRole, StartBeatRole, LengthBeatsRole, VelocityRole, SelectedRole };

    explicit PianoRollNoteModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void sync(const std::vector<MidiNote>& notes);
    const std::vector<MidiNote>& rows() const noexcept { return m_rows; }

private:
    std::vector<MidiNote> m_rows;
};
