#pragma once

#include <QObject>
#include <qqmlregistration.h>
#include "dsp/levelmeter.h"

class LevelMeterWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY labelChanged)
    Q_PROPERTY(double levelDb READ levelDb WRITE setLevelDb NOTIFY levelDbChanged)
    Q_PROPERTY(double normalized READ normalized NOTIFY levelDbChanged)
    Q_PROPERTY(QString levelText READ levelText NOTIFY levelDbChanged)
    Q_PROPERTY(int classification READ classification NOTIFY levelDbChanged)
    Q_PROPERTY(bool clipped READ clipped WRITE setClipped NOTIFY clippedChanged)
    Q_PROPERTY(bool showTarget READ showTarget WRITE setShowTarget NOTIFY showTargetChanged)
    Q_PROPERTY(double normalizedTarget READ normalizedTarget CONSTANT)

public:
    explicit LevelMeterWrapper(QObject* parent = nullptr);

    QString label() const;
    void setLabel(const QString& label);

    double levelDb() const noexcept;
    void setLevelDb(double db);

    double normalized() const noexcept;
    QString levelText() const;
    int classification() const noexcept;

    bool clipped() const noexcept;
    void setClipped(bool clipped);

    bool showTarget() const noexcept;
    void setShowTarget(bool show);

    double normalizedTarget() const noexcept;

    Q_INVOKABLE void reset();

signals:
    void labelChanged();
    void levelDbChanged();
    void clippedChanged();
    void showTargetChanged();

private:
    LevelMeter m_backend;
};
