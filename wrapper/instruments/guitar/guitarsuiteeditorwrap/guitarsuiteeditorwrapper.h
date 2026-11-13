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
    // The preset that is playing (a factory preset or a preset file); empty when the board was
    // built by hand. presetEdited: changed since.
    Q_PROPERTY(QString presetName READ presetName NOTIFY presetNameChanged)
    Q_PROPERTY(bool presetEdited READ presetEdited NOTIFY presetNameChanged)
    // Editor rules from backend pedaleditorrules (lists, tuner texts, per-pedal features).
    Q_PROPERTY(QStringList beatDivisionNames READ beatDivisionNames CONSTANT)
    Q_PROPERTY(QStringList tunerStringLabels READ tunerStringLabels CONSTANT)
    Q_PROPERTY(QString tunerReferenceText READ tunerReferenceText CONSTANT)

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
    QString presetName() const { return QString::fromStdString(m_editor.presetName()); }
    bool presetEdited() const { return m_editor.presetEdited(); }

    void setPublisher(std::function<void(std::shared_ptr<GuitarSuite>)> publisher);
    void setPlaybackPublisher(std::function<void(std::shared_ptr<GuitarSuite>)> publisher);
    void configure(double sampleRate, int maxFrames);

    QStringList factoryPresetNames() const;
    Q_INVOKABLE bool applyFactoryPreset(const QString& name);
    Q_INVOKABLE QVariantMap tunerReading(int pedalId) const;

    static QStringList beatDivisionNames();
    static QStringList tunerStringLabels();
    static QString tunerReferenceText();
    // {pitchDelayNote, cabinetIr, tuner, sustainToggle, tempoSync} for a pedal type.
    Q_INVOKABLE QVariantMap pedalFeatures(const QString& pedalType) const;
    Q_INVOKABLE bool isDial(const QString& pedalType, const QString& parameterId) const;
    Q_INVOKABLE bool disabledWhenSynced(const QString& pedalType, const QString& parameterId) const;
    Q_INVOKABLE double dialStep(const QString& pedalType, const QString& parameterId) const;
    Q_INVOKABLE QString formatValue(double value, const QString& unit) const;
    Q_INVOKABLE QString formatTrim(double db) const;
    Q_INVOKABLE QString syncText(bool synced, double bpm) const;
    Q_INVOKABLE bool inTune(double cents) const;
    Q_INVOKABLE QString tunerStatus(bool hasPitch, bool bypassed, double cents) const;
    Q_INVOKABLE QString formatCents(double cents) const;
    Q_INVOKABLE QString formatTunerFrequency(double hz, double targetHz) const;
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
    void presetNameChanged();

private:
    QVariantList parameterList(const PedalSlot& slot) const;
    void setBusy(bool busy);
    void setError(const QString& message);
    // name: what the preset is called here (a null string: the name the data carries).
    bool loadPresetData(const QByteArray& bytes, const QString& directory, bool keepCalibrationAndCabinet = false,
                        const QString& name = {});
    void notePreset();                                     // presetNameChanged when either changed

    QString m_notedName;
    bool m_notedEdited = false;

    GuitarSuiteEditor m_editor;
};

using GuitarSuiteModel = GuitarSuiteEditorWrapper;
