#include "tenbandeqWrap.h"

#include "common/trackEffectsPanel/plugins/tenbandeq/eqpresetstore.h"

TenbandeqWrap::TenbandeqWrap(QObject* parent)
    : QObject(parent)
{
}

QVariantList TenbandeqWrap::bandDescriptors() const
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

QStringList TenbandeqWrap::presetNames() const
{
    QStringList result;
    for (const auto& name : EqPresetStore::factoryPresetNames()) {
        result.push_back(QString::fromStdString(name));
    }
    return result;
}

QString TenbandeqWrap::formatGain(double gainDb) const
{
    return QString::fromStdString(TenBandEq::formatGain(static_cast<float>(gainDb)));
}

bool TenbandeqWrap::isValidGain(double gainDb) const
{
    return TenBandEq::isValidGain(static_cast<float>(gainDb));
}

double TenbandeqWrap::clampGain(double gainDb) const
{
    return TenBandEq::clampGain(static_cast<float>(gainDb));
}
