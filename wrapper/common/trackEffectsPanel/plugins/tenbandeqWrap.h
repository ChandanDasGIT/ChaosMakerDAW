#pragma once

#include "common/trackEffectsPanel/plugins/tenbandeq/tenbandeq.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class TenbandeqWrap : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList bandDescriptors READ bandDescriptors CONSTANT)
    Q_PROPERTY(QStringList presetNames READ presetNames CONSTANT)
    Q_PROPERTY(double minGain READ minGain CONSTANT)
    Q_PROPERTY(double maxGain READ maxGain CONSTANT)
    Q_PROPERTY(double stepSize READ stepSize CONSTANT)
    Q_PROPERTY(double defaultGain READ defaultGain CONSTANT)

public:
    explicit TenbandeqWrap(QObject* parent = nullptr);
    ~TenbandeqWrap() override = default;

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
