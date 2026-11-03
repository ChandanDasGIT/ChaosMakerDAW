#pragma once

#include "tempo/tempoeditor.h"
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

public:
    explicit TempoEditorWrapper(QObject* parent = nullptr) : QObject(parent) {}
    double value() const { return m_editor.value(); }
    QString text() const { return QString::fromStdU16String(m_editor.text()); }
    bool editing() const { return m_editor.editing(); }
    bool acceptableInput() const { return m_editor.acceptableInput(); }
    void setValue(double value);
    void setText(const QString& text);
    void setEditing(bool editing);
    Q_INVOKABLE void commit();
    Q_INVOKABLE void adjust(int steps);

signals:
    void stateChanged();
    void valueEdited(double value);

private:
    TempoEditor m_editor;
};
