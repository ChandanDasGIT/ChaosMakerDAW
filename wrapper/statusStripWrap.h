#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

// The status line for QML: its message, sample rate, buffer and xrun readouts, from the engine
// as last polled (backend statusstrip); a click on the xruns starts counting again.
class StatusStripWrap : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString sampleRateText READ sampleRateText NOTIFY changed)
    Q_PROPERTY(QString bufferText READ bufferText NOTIFY changed)
    Q_PROPERTY(QString bufferTooltip READ bufferTooltip NOTIFY changed)
    Q_PROPERTY(int xrunCount READ xrunCount NOTIFY changed)
    Q_PROPERTY(QString xrunText READ xrunText NOTIFY changed)
    Q_PROPERTY(QString xrunTooltip READ xrunTooltip NOTIFY changed)

public:
    explicit StatusStripWrap(QObject* parent = nullptr);

    QString message() const;
    QString sampleRateText() const;
    QString bufferText() const;
    QString bufferTooltip() const;
    int xrunCount() const;
    QString xrunText() const;
    QString xrunTooltip() const;

    Q_INVOKABLE void resetXruns();

signals:
    void changed();
};
