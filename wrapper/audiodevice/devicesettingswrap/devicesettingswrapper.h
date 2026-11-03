#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>
#include "audiodevice/devicesettings.h"

class DeviceSettingsWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QStringList outputDevices READ outputDevices NOTIFY outputDevicesChanged)
    Q_PROPERTY(int selectedOutputIndex READ selectedOutputIndex WRITE setSelectedOutputIndex NOTIFY selectedOutputIndexChanged)
    Q_PROPERTY(QStringList bufferSizes READ bufferSizes CONSTANT)
    Q_PROPERTY(int selectedBufferSize READ selectedBufferSize WRITE setSelectedBufferSize NOTIFY bufferSizeChanged)
    Q_PROPERTY(bool outputConnected READ isOutputConnected NOTIFY connectionStatusChanged)
    Q_PROPERTY(bool inputConnected READ isInputConnected NOTIFY connectionStatusChanged)
    Q_PROPERTY(QString connectionStatus READ connectionStatus NOTIFY connectionStatusChanged)
    Q_PROPERTY(QString connectionError READ connectionError NOTIFY connectionErrorChanged)

public:
    explicit DeviceSettingsWrapper(QObject* parent = nullptr);
    explicit DeviceSettingsWrapper(DeviceSettings* backend, QObject* parent = nullptr);

    DeviceSettings& settings() noexcept { return m_settings; }
    const DeviceSettings& settings() const noexcept { return m_settings; }

    QStringList outputDevices() const;
    int selectedOutputIndex() const noexcept { return m_settings.selectedOutputIndex(); }
    void setSelectedOutputIndex(int index);

    QStringList bufferSizes() const;
    int selectedBufferSize() const noexcept { return m_settings.bufferSize(); }
    void setSelectedBufferSize(int frames);

    bool isOutputConnected() const noexcept { return m_settings.isOutputConnected(); }
    bool isInputConnected() const noexcept { return m_settings.isInputConnected(); }
    QString connectionStatus() const;
    QString connectionError() const;

    Q_INVOKABLE bool setOutputDevice(int index);
    Q_INVOKABLE bool setBufferSize(int frames);

signals:
    void outputDevicesChanged();
    void selectedOutputIndexChanged();
    void bufferSizeChanged();
    void connectionStatusChanged();
    void connectionErrorChanged();
    void requestOutputDeviceChange(int index, const QString& deviceName);
    void requestBufferSizeChange(int frames);

private:
    DeviceSettings m_ownedSettings;
    DeviceSettings& m_settings;
};
