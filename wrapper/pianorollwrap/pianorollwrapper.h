#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include "midi/pianorollpattern.h"

class PianoRollWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantList notes READ notes NOTIFY notesChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY notesChanged)
    Q_PROPERTY(int count READ count NOTIFY notesChanged)

public:
    explicit PianoRollWrapper(QObject* parent = nullptr);

    PianoRollPattern& backend() noexcept { return m_pattern; }
    const PianoRollPattern& backend() const noexcept { return m_pattern; }

    QVariantList notes() const;
    int selectedCount() const noexcept { return m_pattern.selectedCount(); }
    int count() const noexcept { return static_cast<int>(m_pattern.notes().size()); }

    Q_INVOKABLE int addNote(int note, double startBeat, double lengthBeats, int velocity = 100);
    Q_INVOKABLE bool removeNote(int noteId);
    Q_INVOKABLE bool removeNotesAt(int note, double beat);
    Q_INVOKABLE bool moveNote(int noteId, int newNote, double newStartBeat);
    Q_INVOKABLE bool resizeNote(int noteId, double newLengthBeats);
    Q_INVOKABLE void selectNote(int noteId, bool addToSelection = false);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool deleteSelected();
    Q_INVOKABLE void clear();

signals:
    void notesChanged();

private:
    PianoRollPattern m_pattern;
};
