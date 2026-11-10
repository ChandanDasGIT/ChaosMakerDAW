#pragma once

#include "tempo/tempoeditor.h"
#include "tempo/taptempo.h"
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

// Converts QML input to the backend tempo editor and forwards proposed values.
class TempoEditorWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double value READ value WRITE setValue NOTIFY stateChanged)
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY stateChanged)
    Q_PROPERTY(bool editing READ editing WRITE setEditing NOTIFY stateChanged)
    Q_PROPERTY(bool acceptableInput READ acceptableInput NOTIFY stateChanged)
    // Taps in the current tap-tempo count (0 after a pause), for the TAP chip (restMap G17).
    Q_PROPERTY(int tapCount READ tapCount NOTIFY stateChanged)

public:
    explicit TempoEditorWrapper(QObject* parent = nullptr) : QObject(parent) { m_clock.start(); }
    double value() const { return m_editor.value(); }
    QString text() const { return QString::fromStdU16String(m_editor.text()); }
    bool editing() const { return m_editor.editing(); }
    bool acceptableInput() const { return m_editor.acceptableInput(); }
    void setValue(double value);
    void setText(const QString& text);
    void setEditing(bool editing);
    Q_INVOKABLE void commit();
    Q_INVOKABLE void adjust(int steps);
    // A tap now (TAP chip, Shift+T); from the second tap of a count the new BPM is proposed
    // through valueEdited, like a typed one. tapAt() takes the time (seconds) for tests.
    Q_INVOKABLE void tap();
    Q_INVOKABLE void tapAt(double seconds);
    int tapCount() const { return m_tap.taps(); }

signals:
    void stateChanged();
    void valueEdited(double value);

private:
    TempoEditor m_editor;
    TapTempo m_tap;
    QElapsedTimer m_clock;
};
