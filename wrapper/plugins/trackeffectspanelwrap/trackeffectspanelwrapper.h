#pragma once

#include "plugins/trackeffectspanel.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class TrackEffectsPanelWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int trackId READ trackId WRITE setTrackId NOTIFY trackChanged)
    Q_PROPERTY(QString trackName READ trackName WRITE setTrackName NOTIFY trackChanged)
    Q_PROPERTY(QString trackType READ trackType WRITE setTrackType NOTIFY trackChanged)
    Q_PROPERTY(bool hasLiveInput READ hasLiveInput WRITE setHasLiveInput NOTIFY trackChanged)
    Q_PROPERTY(bool recordDry READ recordDry WRITE setRecordDry NOTIFY recordDryChanged)
    Q_PROPERTY(bool supportsGuitarSuite READ supportsGuitarSuite NOTIFY capabilitiesChanged)
    Q_PROPERTY(QString selectedEffect READ selectedEffect WRITE setSelectedEffect NOTIFY selectedEffectChanged)
    // The track's chain as the TrackListWrapper "effects" role gives it ([{id, type, ...}]).
    Q_PROPERTY(QVariantList chain READ chain WRITE setChain NOTIFY capabilitiesChanged)
    Q_PROPERTY(QString recordModeDescription READ recordModeDescription NOTIFY recordDryChanged)

public:
    explicit TrackEffectsPanelWrapper(QObject* parent = nullptr);
    ~TrackEffectsPanelWrapper() override = default;

    int trackId() const { return m_panel.trackId(); }
    void setTrackId(int id);

    QString trackName() const { return QString::fromStdString(m_panel.trackName()); }
    void setTrackName(const QString& name);

    QString trackType() const { return QString::fromStdString(m_panel.trackType()); }
    void setTrackType(const QString& type);

    bool hasLiveInput() const { return m_panel.hasLiveInput(); }
    void setHasLiveInput(bool live);

    bool recordDry() const { return m_panel.recordDry(); }
    void setRecordDry(bool dry);

    bool supportsGuitarSuite() const { return m_panel.supportsGuitarSuite(); }
    QString selectedEffect() const { return QString::fromStdString(m_panel.selectedEffect()); }
    void setSelectedEffect(const QString& key);

    QVariantList chain() const { return m_stagedChain; }
    void setChain(const QVariantList& chain);
    QString recordModeDescription() const;

    Q_INVOKABLE void selectEffect(const QString& key);

signals:
    void trackChanged();
    void recordDryChanged();
    void capabilitiesChanged();
    void selectedEffectChanged();

private:
    void updateTrackInfo();

    TrackEffectsPanel m_panel;
    int m_stagedTrackId = -1;
    QString m_stagedTrackName;
    QString m_stagedTrackType;
    bool m_stagedHasLiveInput = true;
    bool m_stagedRecordDry = false;
    QVariantList m_stagedChain;
};
