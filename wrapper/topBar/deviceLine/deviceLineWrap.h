#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

// The top bar's device line for QML: "48 kHz · 128 smp · ~2.7/2.7 ms · JACK" and its tooltip,
// from the audio device as the engine last polled it (backend deviceline).
class DeviceLineWrap : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QString tooltip READ tooltip NOTIFY changed)

public:
    explicit DeviceLineWrap(QObject* parent = nullptr);

    QString text() const;
    QString tooltip() const;

signals:
    void changed();
};
