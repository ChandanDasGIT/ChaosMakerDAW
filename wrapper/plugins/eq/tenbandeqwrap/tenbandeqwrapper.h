#pragma once

#include "plugins/eq/tenbandeq.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class TenBandEqWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList bandDescriptors READ bandDescriptors CONSTANT)
    Q_PROPERTY(QStringList presetNames READ presetNames CONSTANT)
    Q_PROPERTY(double minGain READ minGain CONSTANT)
    Q_PROPERTY(double maxGain READ maxGain CONSTANT)
    Q_PROPERTY(double stepSize READ stepSize CONSTANT)
    Q_PROPERTY(double defaultGain READ defaultGain CONSTANT)

public:
    explicit TenBandEqWrapper(QObject* parent = nullptr);
    ~TenBandEqWrapper() override = default;

    QVariantList bandDescriptors() const;
    QStringList presetNames() const;

    double minGain() const { return TenBandEq::MinGainDb; }
    double maxGain() const { return TenBandEq::MaxGainDb; }
    double stepSize() const { return TenBandEq::StepSizeDb; }
    double defaultGain() const { return TenBandEq::DefaultGainDb; }

    Q_INVOKABLE QString formatGain(double gainDb) const;
    Q_INVOKABLE bool isValidGain(double gainDb) const;
    Q_INVOKABLE double clampGain(double gainDb) const;
};
