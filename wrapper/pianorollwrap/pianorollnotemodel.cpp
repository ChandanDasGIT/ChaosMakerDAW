#include "pianorollnotemodel.h"

#include <algorithm>
#include <unordered_set>

PianoRollNoteModel::PianoRollNoteModel(QObject* parent)
    : QAbstractListModel(parent) {
}

int PianoRollNoteModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant PianoRollNoteModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const MidiNote& n = m_rows[static_cast<std::size_t>(index.row())];
    switch (role) {
    case IdRole: return n.id;
    case NoteRole: return n.note;
    case StartBeatRole: return n.startBeat;
    case LengthBeatsRole: return n.lengthBeats;
    case VelocityRole: return n.velocity;
    case SelectedRole: return n.selected;
    default: return {};
    }
}

QHash<int, QByteArray> PianoRollNoteModel::roleNames() const {
    return {{IdRole, "id"}, {NoteRole, "note"}, {StartBeatRole, "startBeat"},
            {LengthBeatsRole, "lengthBeats"}, {VelocityRole, "velocity"}, {SelectedRole, "selected"}};
}

void PianoRollNoteModel::sync(const std::vector<MidiNote>& notes) {
    // 1. Remove rows whose note is gone (from the back, so indexes stay valid).
    std::unordered_set<int> keep;
    for (const auto& n : notes) keep.insert(n.id);
    for (int row = rowCount() - 1; row >= 0; --row) {
        if (keep.contains(m_rows[static_cast<std::size_t>(row)].id)) continue;
        beginRemoveRows({}, row, row);
        m_rows.erase(m_rows.begin() + row);
        endRemoveRows();
    }

    // 2. Walk the new order: update in place, move a row up from further down, or insert.
    for (std::size_t i = 0; i < notes.size(); ++i) {
        const MidiNote& want = notes[i];
        const int row = static_cast<int>(i);
        if (i < m_rows.size() && m_rows[i].id == want.id) {
            if (!(m_rows[i] == want)) {
                m_rows[i] = want;
                emit dataChanged(index(row), index(row));
            }
            continue;
        }
        const auto found = std::find_if(m_rows.begin() + static_cast<std::ptrdiff_t>(i), m_rows.end(),
                                         [&](const MidiNote& n) { return n.id == want.id; });
        if (found != m_rows.end()) {
            const int from = static_cast<int>(found - m_rows.begin());
            beginMoveRows({}, from, from, {}, row);
            const MidiNote moved = *found;
            m_rows.erase(found);
            m_rows.insert(m_rows.begin() + static_cast<std::ptrdiff_t>(i), moved);
            endMoveRows();
            if (!(m_rows[i] == want)) {
                m_rows[i] = want;
                emit dataChanged(index(row), index(row));
            }
        } else {
            beginInsertRows({}, row, row);
            m_rows.insert(m_rows.begin() + static_cast<std::ptrdiff_t>(i), want);
            endInsertRows();
        }
    }
}
