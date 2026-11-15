#include "eqWrap.h"

#include "common/eq/eqpresetstore.h"

EqWrap::EqWrap(QObject* parent)
    : QObject(parent)
{
}

QVariantList EqWrap::bandDescriptors() const
{
    QVariantList list;
    for (const auto& d : TenBandEq::bandDescriptors()) {
        list.push_back(QVariantMap{
            {QStringLiteral("index"), d.index},
            {QStringLiteral("frequencyHz"), d.frequencyHz},
            {QStringLiteral("shortLabel"), QString::fromStdString(d.shortLabel)},
            {QStringLiteral("fullLabel"), QString::fromStdString(d.fullLabel)},
            {QStringLiteral("min"), d.minGainDb},
            {QStringLiteral("max"), d.maxGainDb},
            {QStringLiteral("stepSize"), d.stepSizeDb},
            {QStringLiteral("defaultValue"), d.defaultGainDb}
        });
    }
    return list;
}

QStringList EqWrap::presetNames() const
{
    QStringList result;
    for (const auto& name : EqPresetStore::factoryPresetNames()) {
        result.push_back(QString::fromStdString(name));
    }
    return result;
}

QString EqWrap::formatGain(double gainDb) const
{
    return QString::fromStdString(TenBandEq::formatGain(static_cast<float>(gainDb)));
}

bool EqWrap::isValidGain(double gainDb) const
{
    return TenBandEq::isValidGain(static_cast<float>(gainDb));
}

double EqWrap::clampGain(double gainDb) const
{
    return TenBandEq::clampGain(static_cast<float>(gainDb));
}
