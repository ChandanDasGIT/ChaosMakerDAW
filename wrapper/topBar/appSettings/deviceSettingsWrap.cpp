#include "deviceSettingsWrap.h"

DeviceSettingsWrap::DeviceSettingsWrap(QObject* parent)
    : QObject(parent)
    , m_settings(m_ownedSettings)
{
}

DeviceSettingsWrap::DeviceSettingsWrap(DeviceSettings* backend, QObject* parent)
    : QObject(parent)
    , m_settings(backend ? *backend : m_ownedSettings)
{
}

QStringList DeviceSettingsWrap::outputDevices() const
{
    QStringList result;
    for (const auto& desc : m_settings.outputDeviceDescriptions()) {
        result.push_back(QString::fromStdString(desc));
    }
    return result;
}

void DeviceSettingsWrap::setSelectedOutputIndex(int index)
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

QStringList DeviceSettingsWrap::bufferSizes() const
{
    QStringList result;
    for (const auto& s : DeviceSettings::supportedBufferSizeStrings()) {
        result.push_back(QString::fromStdString(s));
    }
    return result;
}

void DeviceSettingsWrap::setSelectedBufferSize(int frames)
{
    if (m_settings.bufferSize() != frames) {
        if (m_settings.setBufferSize(frames)) {
            emit bufferSizeChanged();
            emit requestBufferSizeChange(frames);
        }
    }
}

QString DeviceSettingsWrap::connectionStatus() const
{
    return QString::fromStdString(m_settings.statusSummary(48000.0));
}

QString DeviceSettingsWrap::connectionError() const
{
    return QString::fromStdString(m_settings.connectionError());
}

bool DeviceSettingsWrap::setOutputDevice(int index)
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

bool DeviceSettingsWrap::setBufferSize(int frames)
{
    if (m_settings.setBufferSize(frames)) {
        emit bufferSizeChanged();
        emit requestBufferSizeChange(frames);
        return true;
    }
    return false;
}
