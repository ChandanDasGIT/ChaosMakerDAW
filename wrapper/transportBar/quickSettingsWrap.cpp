#include "quickSettingsWrap.h"

QuickSettingsWrap::QuickSettingsWrap(QObject* parent)
    : QObject(parent)
{
}

QStringList QuickSettingsWrap::bufferSizes() const
{
    QStringList list;
    for (const auto& s : QuickSettings::supportedBufferSizeStrings()) {
        list.push_back(QString::fromStdString(s));
    }
    return list;
}

QStringList QuickSettingsWrap::snapModes() const
{
    QStringList list;
    for (const auto& s : QuickSettings::snapGridModes()) {
        list.push_back(QString::fromStdString(s));
    }
    return list;
}

void QuickSettingsWrap::setSelectedBufferSize(int frames)
{
    if (m_selectedBufferSize == frames) return;
    if (!QuickSettings::isValidBufferSize(frames)) return;
    m_selectedBufferSize = frames;
    emit selectedBufferSizeChanged();
    emit bufferSizeSelected(frames);
}

void QuickSettingsWrap::updateStatus(
    bool audioReady,
    bool inputConnected,
    bool outputConnected,
    double sampleRate,
    int bufferFrames,
    const QString& customError)
{
    const std::string formatted = QuickSettings::formatAudioStatus(
        audioReady,
        inputConnected,
        outputConnected,
        sampleRate,
        bufferFrames,
        customError.toStdString());

    const QString newStatus = QString::fromStdString(formatted);
    if (m_audioStatus == newStatus) return;
    m_audioStatus = newStatus;
    emit audioStatusChanged();
}
