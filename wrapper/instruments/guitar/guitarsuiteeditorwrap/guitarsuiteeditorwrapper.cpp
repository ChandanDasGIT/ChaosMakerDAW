#include "guitarsuiteeditorwrapper.h"

#include "instruments/guitar/audiofile/impulsewav.h"
#include "instruments/guitar/plugins/cabinet/cabinet.h"
#include "instruments/guitar/plugins/guitarsuite/guitarsuite.h"
#include "instruments/guitar/plugins/guitarsuite/pedalregistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <utility>

GuitarSuiteEditorWrapper::GuitarSuiteEditorWrapper(QObject* parent)
    : QAbstractListModel(parent)
{
    m_editor.setPresetFilePath(
        (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
         + QStringLiteral("/GuitarSuite/Default.gspreset")).toStdString());
}

int GuitarSuiteEditorWrapper::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_editor.count();
}

QVariantList GuitarSuiteEditorWrapper::parameterList(const PedalSlot& slot) const
{
    QVariantList values;
    if (!slot.processor) return values;

    for (const auto& parameter : slot.processor->parameters()) {
        if (!parameter) continue;
        values.push_back(QVariantMap{
            {QStringLiteral("id"), QString::fromStdString(parameter->id)},
            {QStringLiteral("name"), QString::fromStdString(parameter->name)},
            {QStringLiteral("min"), parameter->min},
            {QStringLiteral("max"), parameter->max},
            {QStringLiteral("value"), parameter->get()},
            {QStringLiteral("unit"), QString::fromStdString(parameter->unit)}
        });
    }
    return values;
}

QVariant GuitarSuiteEditorWrapper::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};

    const auto slot = m_editor.slotAt(index.row());
    if (!slot) return {};

    switch (role) {
    case ResourcePathRole:
        return QString::fromStdString(slot->resourcePath);
    case Qt::DisplayRole:
    case PedalNameRole:
        return QString::fromStdString(PedalRegistry::displayName(slot->type));
    case PedalIdRole:
        return slot->id;
    case PedalTypeRole:
        return QString::fromStdString(slot->type);
    case BypassedRole:
        return slot->bypassed.load(std::memory_order_relaxed);
    case ParametersRole:
        return parameterList(*slot);
    default:
        return {};
    }
}

QHash<int, QByteArray> GuitarSuiteEditorWrapper::roleNames() const
{
    return {
        {PedalIdRole, "pedalId"},
        {PedalTypeRole, "pedalType"},
        {PedalNameRole, "pedalName"},
        {BypassedRole, "isBypassed"},
        {ParametersRole, "parameters"},
        {ResourcePathRole, "resourcePath"}
    };
}

void GuitarSuiteEditorWrapper::setInputTrimDb(double db)
{
    if (!std::isfinite(db)) return;
    db = std::clamp(db, -24.0, 24.0);
    if (m_editor.inputTrimDb() == db) return;
    m_editor.setInputTrimDb(db);
    emit inputTrimDbChanged();
}

QString GuitarSuiteEditorWrapper::presetFilePath() const
{
    return QString::fromStdString(m_editor.presetFilePath());
}

QString GuitarSuiteEditorWrapper::lastError() const
{
    return QString::fromStdString(m_editor.lastError());
}

void GuitarSuiteEditorWrapper::setPublisher(std::function<void(std::shared_ptr<GuitarSuite>)> publisher)
{
    m_editor.setPublisher(std::move(publisher));
}

void GuitarSuiteEditorWrapper::setPlaybackPublisher(std::function<void(std::shared_ptr<GuitarSuite>)> publisher)
{
    m_editor.setPlaybackPublisher(std::move(publisher));
}

void GuitarSuiteEditorWrapper::configure(double sampleRate, int maxFrames)
{
    m_editor.configure(sampleRate, maxFrames);
}

QStringList GuitarSuiteEditorWrapper::factoryPresetNames() const
{
    QStringList result;
    for (const auto& name : GuitarSuiteEditor::factoryPresetNames()) {
        result.push_back(QString::fromStdString(name));
    }
    return result;
}

QStringList GuitarSuiteEditorWrapper::availablePedals() const
{
    QStringList result;
    for (const auto& type : PedalRegistry::availablePedals()) {
        result.push_back(QString::fromStdString(PedalRegistry::displayName(type)));
    }
    return result;
}

bool GuitarSuiteEditorWrapper::addPedal(const QString& type)
{
    if (m_editor.busy()) return false;
    const int row = rowCount();
    beginInsertRows({}, row, row);
    bool ok = m_editor.addPedal(type.toStdString());
    if (!ok) {
        endInsertRows();
        return false;
    }
    endInsertRows();
    emit countChanged();
    setError({});
    return true;
}

bool GuitarSuiteEditorWrapper::removePedal(int pedalId)
{
    if (m_editor.busy()) return false;
    const int row = m_editor.indexOfSlot(pedalId);
    if (row < 0) return false;

    beginRemoveRows({}, row, row);
    bool ok = m_editor.removePedal(pedalId);
    endRemoveRows();
    if (ok) {
        emit countChanged();
    }
    return ok;
}

bool GuitarSuiteEditorWrapper::movePedal(int from, int to)
{
    if (m_editor.busy()) return false;
    if (from < 0 || to < 0 || from >= rowCount() || to >= rowCount() || from == to)
        return false;

    const int destinationChild = to > from ? to + 1 : to;
    if (!beginMoveRows({}, from, from, {}, destinationChild))
        return false;

    bool ok = m_editor.movePedal(from, to);
    endMoveRows();
    return ok;
}

bool GuitarSuiteEditorWrapper::setBypassed(int pedalId, bool bypassed)
{
    const int row = m_editor.indexOfSlot(pedalId);
    if (row < 0) return false;

    bool ok = m_editor.setBypassed(pedalId, bypassed);
    if (!ok) return false;

    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed, {BypassedRole});
    return true;
}

bool GuitarSuiteEditorWrapper::setParameter(int pedalId, const QString& parameterId, double value)
{
    const int row = m_editor.indexOfSlot(pedalId);
    if (row < 0) return false;

    bool ok = m_editor.setParameter(pedalId, parameterId.toStdString(), value);
    if (!ok) return false;

    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed, {ParametersRole});
    return true;
}

bool GuitarSuiteEditorWrapper::savePreset()
{
    if (m_editor.busy()) return false;
    const QFileInfo info(presetFilePath());
    if (!QDir().mkpath(info.absolutePath())) {
        setError(QStringLiteral("Could not create GuitarSuite preset folder."));
        return false;
    }

    bool ok = m_editor.savePreset();
    if (!ok) {
        setError(QString::fromStdString(m_editor.lastError()));
        return false;
    }
    setError({});
    return true;
}

bool GuitarSuiteEditorWrapper::savePresetTo(const QUrl& url)
{
    if (m_editor.busy()) return false;
    if (!url.isLocalFile()) {
        setError(QStringLiteral("GuitarSuite presets must be saved to a local file."));
        return false;
    }
    const QString local = url.toLocalFile();
    if (presetFilePath() != local) {
        m_editor.setPresetFilePath(local.toStdString());
        emit presetFilePathChanged();
    }
    return savePreset();
}

bool GuitarSuiteEditorWrapper::loadPresetFrom(const QUrl& url)
{
    if (m_editor.busy()) return false;
    if (!url.isLocalFile()) {
        setError(QStringLiteral("GuitarSuite presets must be loaded from a local file."));
        return false;
    }
    const QString local = url.toLocalFile();
    if (presetFilePath() != local) {
        m_editor.setPresetFilePath(local.toStdString());
        emit presetFilePathChanged();
    }
    return loadPreset();
}

bool GuitarSuiteEditorWrapper::loadPreset()
{
    if (m_editor.busy()) return false;
    QFile file(presetFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("GuitarSuite preset not found: %1").arg(presetFilePath()));
        return false;
    }
    if (file.size() > 1024 * 1024) {
        setError(QStringLiteral("GuitarSuite preset is too large."));
        return false;
    }
    return loadPresetData(file.readAll(), QFileInfo(file).absolutePath());
}

bool GuitarSuiteEditorWrapper::applyFactoryPreset(const QString& name)
{
    if (m_editor.busy()) return false;
    if (!factoryPresetNames().contains(name)) {
        setError(QStringLiteral("Unknown GuitarSuite factory preset."));
        return false;
    }
    QFile file(QStringLiteral(":/GuitarSuitePresets/%1.gspreset").arg(name));
    if (!file.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("Could not open GuitarSuite factory preset."));
        return false;
    }
    return loadPresetData(file.readAll(), {}, true);
}

bool GuitarSuiteEditorWrapper::loadPresetData(
    const QByteArray& bytes,
    const QString& directory,
    bool keepCalibrationAndCabinet)
{
    auto candidate = m_editor.preparePresetData(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())),
        directory.toStdString(),
        keepCalibrationAndCabinet);

    if (!candidate.error.empty()) {
        setError(QString::fromStdString(candidate.error));
        return false;
    }

    if (!candidate.needsAsync) {
        beginResetModel();
        m_editor.commitPreset(std::move(candidate.candidateSlots), candidate.nextId, candidate.trim);
        endResetModel();
        emit countChanged();
        emit inputTrimDbChanged();
        setError({});
        return true;
    }

    struct AsyncResult {
        std::vector<std::shared_ptr<PedalSlot>> candidateSlots;
        QString error;
    };

    auto* watcher = new QFutureWatcher<AsyncResult>(this);
    setBusy(true);
    setError({});

    const double rate = m_editor.sampleRate();
    const int frames = m_editor.maxFrames();
    const int nextId = candidate.nextId;
    const double trim = candidate.trim;

    connect(watcher, &QFutureWatcher<AsyncResult>::finished, this, [this, watcher, nextId, trim] {
        auto result = watcher->result();
        watcher->deleteLater();
        setBusy(false);

        if (!result.error.isEmpty()) {
            setError(result.error);
            return;
        }

        beginResetModel();
        m_editor.commitPreset(std::move(result.candidateSlots), nextId, trim);
        endResetModel();
        emit countChanged();
        emit inputTrimDbChanged();
        setError({});
    });

    auto candidatePedals = std::move(candidate.candidateSlots);
    watcher->setFuture(QtConcurrent::run([candidatePedals = std::move(candidatePedals), rate, frames]() mutable {
        AsyncResult result;
        try {
            GuitarSuiteEditor::prepareSlotResources(candidatePedals, rate, frames);
            result.candidateSlots = std::move(candidatePedals);
        } catch (const std::exception& error) {
            result.error = QString::fromUtf8(error.what());
        }
        return result;
    }));

    return true;
}

void GuitarSuiteEditorWrapper::setBusy(bool busy)
{
    if (m_editor.busy() == busy)
        return;
    m_editor.setBusy(busy);
    emit busyChanged();
}

void GuitarSuiteEditorWrapper::setError(const QString& message)
{
    if (lastError() == message)
        return;
    m_editor.setError(message.toStdString());
    emit lastErrorChanged();
}

QVariantMap GuitarSuiteEditorWrapper::tunerReading(int pedalId) const
{
    const auto reading = m_editor.tunerReading(pedalId);
    return QVariantMap{
        {QStringLiteral("frequency"), reading.frequency},
        {QStringLiteral("revision"), reading.revision},
        {QStringLiteral("note"), QString::fromStdString(reading.note)},
        {QStringLiteral("cents"), reading.cents},
        {QStringLiteral("targetHz"), reading.targetHz},
        {QStringLiteral("stringNumber"), reading.stringNumber},
        {QStringLiteral("targetSelection"), reading.targetSelection},
        {QStringLiteral("direction"), QString::fromStdString(reading.direction)}
    };
}

bool GuitarSuiteEditorWrapper::loadCabinetIr(int pedalId, const QUrl& url)
{
    const int row = m_editor.indexOfSlot(pedalId);
    if (m_editor.busy() || row < 0 || !url.isLocalFile())
        return false;

    const auto slot = m_editor.slotAt(row);
    if (!slot || slot->type != "pedal.cabinet")
        return false;

    struct Result {
        std::shared_ptr<Cabinet> processor;
        QString error;
    };

    const QString path = QFileInfo(url.toLocalFile()).absoluteFilePath();
    const double rate = m_editor.sampleRate();
    const int frames = m_editor.maxFrames();

    auto* watcher = new QFutureWatcher<Result>(this);
    setBusy(true);
    setError({});

    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, pedalId, path, rate, frames] {
        auto result = watcher->result();
        watcher->deleteLater();
        setBusy(false);

        if (!result.error.isEmpty()) {
            setError(result.error);
            return;
        }

        bool ok = m_editor.replaceCabinetSlot(pedalId, path.toStdString(), std::move(result.processor), rate, frames);
        if (ok) {
            const int changedRow = m_editor.indexOfSlot(pedalId);
            if (changedRow >= 0) {
                const auto changed = index(changedRow);
                emit dataChanged(changed, changed, {ParametersRole, ResourcePathRole});
            }
        }
    });

    watcher->setFuture(QtConcurrent::run([path, rate, frames] {
        Result result;
        try {
            result.processor = std::make_shared<Cabinet>(loadImpulseWav(path.toStdString(), rate));
            result.processor->prepare(rate, frames);
        } catch (const std::exception& error) {
            result.error = QString::fromUtf8(error.what());
        }
        return result;
    }));

    return true;
}

bool GuitarSuiteEditorWrapper::clearCabinetIr(int pedalId)
{
    if (m_editor.busy()) return false;
    const int row = m_editor.indexOfSlot(pedalId);
    if (row < 0) return false;

    bool ok = m_editor.clearCabinetIr(pedalId);
    if (!ok) return false;

    const auto changed = index(row);
    emit dataChanged(changed, changed, {ParametersRole, ResourcePathRole});
    setError({});
    return true;
}

QByteArray GuitarSuiteEditorWrapper::sessionData() const
{
    return QByteArray::fromStdString(m_editor.sessionData());
}

std::string GuitarSuiteEditorWrapper::sessionDataStd() const
{
    return m_editor.sessionData();
}

bool GuitarSuiteEditorWrapper::restoreSessionData(const QByteArray& bytes)
{
    return !m_editor.busy() && loadPresetData(bytes, {});
}

bool GuitarSuiteEditorWrapper::restoreSessionData(std::string_view bytes)
{
    return restoreSessionData(QByteArray::fromRawData(bytes.data(), static_cast<qsizetype>(bytes.size())));
}
