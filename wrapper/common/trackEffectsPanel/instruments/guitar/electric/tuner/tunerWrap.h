#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class ElectricWrap;

// The tuner pedal's display for QML: its reading from the board (`board`) and its texts
// (backend tunertext).
class TunerWrap : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* board READ board WRITE setBoard NOTIFY boardChanged)
    Q_PROPERTY(QStringList stringLabels READ stringLabels CONSTANT)
    Q_PROPERTY(QString referenceText READ referenceText CONSTANT)

public:
    explicit TunerWrap(QObject* parent = nullptr);

    QObject* board() const;
    void setBoard(QObject* board);
    QStringList stringLabels() const;
    QString referenceText() const;

    // {frequency, revision, note, cents, targetHz, stringNumber, targetSelection, direction}.
    Q_INVOKABLE QVariantMap reading(int pedalId) const;
    Q_INVOKABLE bool inTune(double cents) const;
    Q_INVOKABLE QString status(bool hasPitch, bool bypassed, double cents) const;
    Q_INVOKABLE QString formatCents(double cents) const;
    Q_INVOKABLE QString formatFrequency(double hz, double targetHz) const;

signals:
    void boardChanged();

private:
    QPointer<ElectricWrap> m_board;
};
