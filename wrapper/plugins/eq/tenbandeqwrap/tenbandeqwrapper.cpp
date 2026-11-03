#include "tenbandeqwrapper.h"

#include "plugins/eq/eqpresetstore.h"

TenBandEqWrapper::TenBandEqWrapper(QObject* parent)
    : QObject(parent)
{
}

QVariantList TenBandEqWrapper::bandDescriptors() const
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

QStringList TenBandEqWrapper::presetNames() const
{
    QStringList result;
    for (const auto& name : EqPresetStore::factoryPresetNames()) {
        result.push_back(QString::fromStdString(name));
    }
    return result;
}

QString TenBandEqWrapper::formatGain(double gainDb) const
{
    return QString::fromStdString(TenBandEq::formatGain(static_cast<float>(gainDb)));
}

bool TenBandEqWrapper::isValidGain(double gainDb) const
{
    return TenBandEq::isValidGain(static_cast<float>(gainDb));
}

double TenBandEqWrapper::clampGain(double gainDb) const
{
    return TenBandEq::clampGain(static_cast<float>(gainDb));
}
