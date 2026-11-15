#include "tunerWrap.h"

#include "instruments/guitar/electric/tuner/tunertext.h"
#include "wrapper/instruments/guitar/electric/electricWrap.h"

TunerWrap::TunerWrap(QObject* parent) : QObject(parent) {}

QObject* TunerWrap::board() const { return m_board; }

void TunerWrap::setBoard(QObject* board)
{
    auto* wrap = qobject_cast<ElectricWrap*>(board);
    if (wrap == m_board) return;
    m_board = wrap;
    emit boardChanged();
}

QStringList TunerWrap::stringLabels() const
{
    QStringList labels;
    for (const auto& label : tunertext::stringLabels()) labels << QString::fromStdString(label);
    return labels;
}

QString TunerWrap::referenceText() const { return QString::fromStdString(tunertext::referenceText()); }

QVariantMap TunerWrap::reading(int pedalId) const
{
    if (!m_board) return {};
    const auto reading = m_board->domain().tunerReading(pedalId);
    return QVariantMap{
        {QStringLiteral("frequency"), reading.frequency},
        {QStringLiteral("revision"), reading.revision},
        {QStringLiteral("note"), QString::fromStdString(reading.note)},
        {QStringLiteral("cents"), reading.cents},
        {QStringLiteral("targetHz"), reading.targetHz},
        {QStringLiteral("stringNumber"), reading.stringNumber},
        {QStringLiteral("targetSelection"), reading.targetSelection},
        {QStringLiteral("direction"), QString::fromStdString(reading.direction)}
    };
}

bool TunerWrap::inTune(double cents) const { return tunertext::inTune(cents); }

QString TunerWrap::status(bool hasPitch, bool bypassed, double cents) const
{
    return QString::fromStdString(tunertext::status(hasPitch, bypassed, cents));
}

QString TunerWrap::formatCents(double cents) const { return QString::fromStdString(tunertext::formatCents(cents)); }

QString TunerWrap::formatFrequency(double hz, double targetHz) const
{
    return QString::fromStdString(tunertext::formatFrequency(hz, targetHz));
}
