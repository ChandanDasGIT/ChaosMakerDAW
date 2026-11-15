#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

// Playing notes from an on-screen keyboard, for QML (backend keyboardplay).
class KeyboardPlayWrap : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit KeyboardPlayWrap(QObject* parent = nullptr);

    Q_INVOKABLE int clickVelocity(double fraction, int lowest) const;
    Q_INVOKABLE int shiftedNote(int note, int shift) const;
};
