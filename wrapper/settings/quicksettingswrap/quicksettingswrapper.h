#pragma once

#include <QObject>
#include <QStringList>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include "settings/quicksettings.h"

class QuickSettingsWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QStringList bufferSizes READ bufferSizes CONSTANT)
    Q_PROPERTY(QStringList snapModes READ snapModes CONSTANT)
    Q_PROPERTY(int selectedBufferSize READ selectedBufferSize WRITE setSelectedBufferSize NOTIFY selectedBufferSizeChanged)
    Q_PROPERTY(QString audioStatus READ audioStatus NOTIFY audioStatusChanged)

public:
    explicit QuickSettingsWrapper(QObject* parent = nullptr);

    QStringList bufferSizes() const;
    QStringList snapModes() const;

    int selectedBufferSize() const noexcept { return m_selectedBufferSize; }
    void setSelectedBufferSize(int frames);

    QString audioStatus() const { return m_audioStatus; }
    void updateStatus(bool audioReady, bool inputConnected, bool outputConnected, double sampleRate, int bufferFrames, const QString& customError = QString());

    Q_INVOKABLE bool isBufferSizeSupported(int frames) const {
        return QuickSettings::isValidBufferSize(frames);
    }
    Q_INVOKABLE bool isValidSnapMode(int index) const {
        return QuickSettings::isValidSnapMode(index);
    }

signals:
    void selectedBufferSizeChanged();
    void audioStatusChanged();
    void bufferSizeSelected(int frames);

private:
    int m_selectedBufferSize = 256;
    QString m_audioStatus;
};
