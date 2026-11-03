#pragma once

#include "instruments/guitar/guitarsuiteeditor.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>

class GuitarSuite;

class GuitarSuiteEditorWrapper : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QStringList factoryPresetNames READ factoryPresetNames CONSTANT)
    Q_PROPERTY(double inputTrimDb READ inputTrimDb WRITE setInputTrimDb NOTIFY inputTrimDbChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString presetFilePath READ presetFilePath NOTIFY presetFilePathChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    enum Role {
        PedalIdRole = Qt::UserRole + 1,
        PedalTypeRole,
        PedalNameRole,
        BypassedRole,
        ParametersRole,
        ResourcePathRole
    };
    Q_ENUM(Role)

    explicit GuitarSuiteEditorWrapper(QObject* parent = nullptr);
    ~GuitarSuiteEditorWrapper() override = default;

    GuitarSuiteEditor& domain() { return m_editor; }
    const GuitarSuiteEditor& domain() const { return m_editor; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    bool busy() const { return m_editor.busy(); }
    int count() const { return rowCount(); }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    double inputTrimDb() const { return m_editor.inputTrimDb(); }
    void setInputTrimDb(double db);
    QString presetFilePath() const;
    QString lastError() const;

    void setPublisher(std::function<void(std::shared_ptr<GuitarSuite>)> publisher);
    void setPlaybackPublisher(std::function<void(std::shared_ptr<GuitarSuite>)> publisher);
    void configure(double sampleRate, int maxFrames);

    QStringList factoryPresetNames() const;
    Q_INVOKABLE bool applyFactoryPreset(const QString& name);
    Q_INVOKABLE QVariantMap tunerReading(int pedalId) const;
    Q_INVOKABLE QStringList availablePedals() const;
    Q_INVOKABLE bool loadCabinetIr(int pedalId, const QUrl& url);
    Q_INVOKABLE bool clearCabinetIr(int pedalId);
    Q_INVOKABLE bool addPedal(const QString& type);
    Q_INVOKABLE bool removePedal(int pedalId);
    Q_INVOKABLE bool movePedal(int from, int to);
    Q_INVOKABLE bool setBypassed(int pedalId, bool bypassed);
    Q_INVOKABLE bool setParameter(int pedalId, const QString& parameterId, double value);
    QByteArray sessionData() const;
    std::string sessionDataStd() const;
    bool restoreSessionData(const QByteArray& bytes);
    bool restoreSessionData(std::string_view bytes);
    Q_INVOKABLE bool savePreset();
    Q_INVOKABLE bool loadPreset();
    Q_INVOKABLE bool savePresetTo(const QUrl& url);
    Q_INVOKABLE bool loadPresetFrom(const QUrl& url);

signals:
    void busyChanged();
    void inputTrimDbChanged();
    void countChanged();
    void presetFilePathChanged();
    void lastErrorChanged();

private:
    QVariantList parameterList(const PedalSlot& slot) const;
    void setBusy(bool busy);
    void setError(const QString& message);
    bool loadPresetData(const QByteArray& bytes, const QString& directory, bool keepCalibrationAndCabinet = false);

    GuitarSuiteEditor m_editor;
};

using GuitarSuiteModel = GuitarSuiteEditorWrapper;
