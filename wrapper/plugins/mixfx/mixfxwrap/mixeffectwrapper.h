#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class TrackListWrapper;

// One mixing plugin's FX page (restMap G7: compressor, de-esser, reverb, Chaos Verb) in QML.
// Its cards and controls come from the backend spec (mixfxspec); values go to the track through
// TrackListWrapper::setMixEffect, and the engine follows its mixEffectChanged signal.
class MixEffectWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* trackModel READ trackModel WRITE setTrackModel NOTIFY trackModelChanged)
    Q_PROPERTY(int trackId READ trackId WRITE setTrackId NOTIFY trackIdChanged)
    Q_PROPERTY(QString effectType READ effectType WRITE setEffectType NOTIFY effectTypeChanged)
    // The EngineController, for the gain-reduction meter. Without it the meter stays at 0.
    Q_PROPERTY(QObject* engine READ engine WRITE setEngine NOTIFY engineChanged)
    // Poll the meter about 30 times a second while true (the page is visible).
    Q_PROPERTY(bool metering READ metering WRITE setMetering NOTIFY meteringChanged)

    Q_PROPERTY(QString title READ title NOTIFY effectTypeChanged)
    // [{title, controls: [{id, name, kind ("slider" | "choice" | "switch"), min, max, default,
    //   unit, choices: [names]}]}], in page order. Stable while the type stays the same, so a
    // value change does not rebuild the page.
    Q_PROPERTY(QVariantList cards READ cards NOTIFY effectTypeChanged)
    Q_PROPERTY(bool hasMeter READ hasMeter NOTIFY effectTypeChanged)
    // {id: value} for the current track.
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    Q_PROPERTY(bool bypassed READ bypassed WRITE setBypassed NOTIFY valuesChanged)
    Q_PROPERTY(double reductionDb READ reductionDb NOTIFY reductionChanged)

public:
    explicit MixEffectWrapper(QObject* parent = nullptr);

    QObject* trackModel() const;
    void setTrackModel(QObject* model);
    int trackId() const { return m_trackId; }
    void setTrackId(int id);
    QString effectType() const { return m_type; }
    void setEffectType(const QString& type);
    QObject* engine() const { return m_engine; }
    void setEngine(QObject* engine);
    bool metering() const { return m_metering; }
    void setMetering(bool on);

    QString title() const;
    QVariantList cards() const;
    bool hasMeter() const;
    QVariantMap values() const;
    bool bypassed() const;
    void setBypassed(bool bypassed);
    double reductionDb() const { return m_reductionDb; }

    // False for an unknown control, a bad track or while the type is not a mixing plugin.
    Q_INVOKABLE bool setValue(const QString& id, double value);
    Q_INVOKABLE bool resetValue(const QString& id);
    // "−18.0 dB", "4.0:1", "150 ms", "2.5 kHz", "80 %", a choice's name, "On" / "Off".
    Q_INVOKABLE QString formatValue(const QString& id, double value) const;
    // A sensible slider step for a control (finer for small ranges).
    Q_INVOKABLE double step(const QString& id) const;

signals:
    void trackModelChanged();
    void trackIdChanged();
    void effectTypeChanged();
    void engineChanged();
    void meteringChanged();
    void valuesChanged();
    void reductionChanged();

private:
    void pollMeter();
    QPointer<TrackListWrapper> m_model;
    QPointer<QObject> m_engine;
    int m_trackId = -1;
    QString m_type;
    bool m_metering = false;
    double m_reductionDb = 0.0;
    QTimer m_meterTimer;
    QMetaObject::Connection m_changeConnection;
};
