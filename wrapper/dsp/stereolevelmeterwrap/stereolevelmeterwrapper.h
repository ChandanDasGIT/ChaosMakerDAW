#pragma once

#include <QObject>
#include <qqmlregistration.h>
#include "dsp/stereolevelmeter.h"

class StereoLevelMeterWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY labelChanged)
    Q_PROPERTY(double leftDb READ leftDb WRITE setLeftDb NOTIFY levelsChanged)
    Q_PROPERTY(double rightDb READ rightDb WRITE setRightDb NOTIFY levelsChanged)
    Q_PROPERTY(double leftNormalized READ leftNormalized NOTIFY levelsChanged)
    Q_PROPERTY(double rightNormalized READ rightNormalized NOTIFY levelsChanged)
    Q_PROPERTY(QString leftLevelText READ leftLevelText NOTIFY levelsChanged)
    Q_PROPERTY(QString rightLevelText READ rightLevelText NOTIFY levelsChanged)
    Q_PROPERTY(int leftClassification READ leftClassification NOTIFY levelsChanged)
    Q_PROPERTY(int rightClassification READ rightClassification NOTIFY levelsChanged)
    Q_PROPERTY(bool leftClipped READ leftClipped WRITE setLeftClipped NOTIFY clipChanged)
    Q_PROPERTY(bool rightClipped READ rightClipped WRITE setRightClipped NOTIFY clipChanged)
    Q_PROPERTY(QString tooltipText READ tooltipText NOTIFY tooltipTextChanged)

public:
    explicit StereoLevelMeterWrapper(QObject* parent = nullptr);

    QString label() const;
    void setLabel(const QString& label);

    double leftDb() const noexcept;
    void setLeftDb(double db);

    double rightDb() const noexcept;
    void setRightDb(double db);

    double leftNormalized() const noexcept;
    double rightNormalized() const noexcept;
    // Bar height (0..1) for any dB value on this meter's scale, e.g. to place scale labels.
    Q_INVOKABLE double normalizedFor(double db) const noexcept;

    QString leftLevelText() const;
    QString rightLevelText() const;

    int leftClassification() const noexcept;
    int rightClassification() const noexcept;

    bool leftClipped() const noexcept;
    void setLeftClipped(bool clipped);

    bool rightClipped() const noexcept;
    void setRightClipped(bool clipped);

    QString tooltipText() const;

    Q_INVOKABLE void reset();
    Q_INVOKABLE void requestClearClips();

signals:
    void labelChanged();
    void levelsChanged();
    void clipChanged();
    void tooltipTextChanged();
    void clearClipsRequested();

private:
    StereoLevelMeter m_backend;
};
