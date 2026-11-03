#include "devicesettingswrapper.h"

DeviceSettingsWrapper::DeviceSettingsWrapper(QObject* parent)
    : QObject(parent)
    , m_settings(m_ownedSettings)
{
}

DeviceSettingsWrapper::DeviceSettingsWrapper(DeviceSettings* backend, QObject* parent)
    : QObject(parent)
    , m_settings(backend ? *backend : m_ownedSettings)
{
}

QStringList DeviceSettingsWrapper::outputDevices() const
{
    QStringList result;
    for (const auto& desc : m_settings.outputDeviceDescriptions()) {
        result.push_back(QString::fromStdString(desc));
    }
    return result;
}

void DeviceSettingsWrapper::setSelectedOutputIndex(int index)
{
    if (m_settings.selectedOutputIndex() != index) {
        if (m_settings.selectOutputIndex(index)) {
            emit selectedOutputIndexChanged();
            const auto* dev = m_settings.selectedOutputDevice();
            if (dev) {
                emit requestOutputDeviceChange(index, QString::fromStdString(dev->name));
            }
        }
    }
}

QStringList DeviceSettingsWrapper::bufferSizes() const
{
    QStringList result;
    for (const auto& s : DeviceSettings::supportedBufferSizeStrings()) {
        result.push_back(QString::fromStdString(s));
    }
    return result;
}

void DeviceSettingsWrapper::setSelectedBufferSize(int frames)
{
    if (m_settings.bufferSize() != frames) {
        if (m_settings.setBufferSize(frames)) {
            emit bufferSizeChanged();
            emit requestBufferSizeChange(frames);
        }
    }
}

QString DeviceSettingsWrapper::connectionStatus() const
{
    return QString::fromStdString(m_settings.statusSummary(48000.0));
}

QString DeviceSettingsWrapper::connectionError() const
{
    return QString::fromStdString(m_settings.connectionError());
}

bool DeviceSettingsWrapper::setOutputDevice(int index)
{
    if (m_settings.selectOutputIndex(index)) {
        emit selectedOutputIndexChanged();
        const auto* dev = m_settings.selectedOutputDevice();
        if (dev) {
            emit requestOutputDeviceChange(index, QString::fromStdString(dev->name));
        }
        return true;
    }
    return false;
}

bool DeviceSettingsWrapper::setBufferSize(int frames)
{
    if (m_settings.setBufferSize(frames)) {
        emit bufferSizeChanged();
        emit requestBufferSizeChange(frames);
        return true;
    }
    return false;
}
