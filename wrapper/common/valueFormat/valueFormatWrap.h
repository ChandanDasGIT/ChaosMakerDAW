#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

// Value and note texts every editor shows the same way (backend valueformat and notenames).
class ValueFormatWrap : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit ValueFormatWrap(QObject* parent = nullptr);

    // decimals < 0 gives the shortest form: 2 -> "+2".
    Q_INVOKABLE QString signedValue(double value, int decimals = -1) const;
    Q_INVOKABLE QString signedDb(double value) const;   // "+1.5 dB"
    Q_INVOKABLE QString percent(double fraction) const; // "46 %"
    Q_INVOKABLE QString clock(double seconds) const;    // "1:05"
    Q_INVOKABLE QString noteName(int note) const;       // "C#4"
    Q_INVOKABLE QString pitchName(int note) const;      // "C#"
    Q_INVOKABLE bool isBlackKey(int note) const;
};
