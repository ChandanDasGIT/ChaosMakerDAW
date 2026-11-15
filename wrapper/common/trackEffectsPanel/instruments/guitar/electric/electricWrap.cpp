#include "electricWrap.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/pedaleditorrules.h"

#include "common/trackEffectsPanel/instruments/guitar/electric/impulsewav.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/pedals/cabinet/cabinet.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/guitarsuite.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/pedalregistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <utility>

ElectricWrap::ElectricWrap(QObject* parent)
    : QAbstractListModel(parent)
{
    m_editor.setPresetFilePath(
        (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
         + QStringLiteral("/GuitarSuite/Default.gspreset")).toStdString());
}

int ElectricWrap::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_editor.count();
}

QVariantList ElectricWrap::parameterList(const PedalSlot& slot) const
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
            {QStringLiteral("default"), parameter->def},
            {QStringLiteral("unit"), QString::fromStdString(parameter->unit)}
        });
    }
    return values;
}

QVariant ElectricWrap::data(const QModelIndex& index, int role) const
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

QHash<int, QByteArray> ElectricWrap::roleNames() const
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

void ElectricWrap::setInputTrimDb(double db)
{
    if (!std::isfinite(db)) return;
    db = std::clamp(db, -24.0, 24.0);
    if (m_editor.inputTrimDb() == db) return;
    m_editor.setInputTrimDb(db);
    emit inputTrimDbChanged();
}

QString ElectricWrap::presetFilePath() const
{
    return QString::fromStdString(m_editor.presetFilePath());
}

QString ElectricWrap::lastError() const
{
    return QString::fromStdString(m_editor.lastError());
}

void ElectricWrap::setPublisher(std::function<void(std::shared_ptr<GuitarSuite>)> publisher)
{
    m_editor.setPublisher(std::move(publisher));
}

void ElectricWrap::setPlaybackPublisher(std::function<void(std::shared_ptr<GuitarSuite>)> publisher)
{
    m_editor.setPlaybackPublisher(std::move(publisher));
}

void ElectricWrap::configure(double sampleRate, int maxFrames)
{
    m_editor.configure(sampleRate, maxFrames);
}

QStringList ElectricWrap::factoryPresetNames() const
{
    QStringList result;
    for (const auto& name : GuitarSuiteEditor::factoryPresetNames()) {
        result.push_back(QString::fromStdString(name));
    }
    return result;
}

QStringList ElectricWrap::availablePedals() const
{
    QStringList result;
    for (const auto& type : PedalRegistry::availablePedals()) {
        result.push_back(QString::fromStdString(PedalRegistry::displayName(type)));
    }
    return result;
}

bool ElectricWrap::addPedal(const QString& type)
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
    notePreset();
    return true;
}

bool ElectricWrap::removePedal(int pedalId)
{
    if (m_editor.busy()) return false;
    const int row = m_editor.indexOfSlot(pedalId);
    if (row < 0) return false;

    beginRemoveRows({}, row, row);
    bool ok = m_editor.removePedal(pedalId);
    endRemoveRows();
    if (ok) {
        emit countChanged();
        notePreset();
    }
    return ok;
}

bool ElectricWrap::movePedal(int from, int to)
{
    if (m_editor.busy()) return false;
    if (from < 0 || to < 0 || from >= rowCount() || to >= rowCount() || from == to)
        return false;

    const int destinationChild = to > from ? to + 1 : to;
    if (!beginMoveRows({}, from, from, {}, destinationChild))
        return false;

    bool ok = m_editor.movePedal(from, to);
    endMoveRows();
    notePreset();
    return ok;
}

bool ElectricWrap::setBypassed(int pedalId, bool bypassed)
{
    const int row = m_editor.indexOfSlot(pedalId);
    if (row < 0) return false;

    bool ok = m_editor.setBypassed(pedalId, bypassed);
    if (!ok) return false;

    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed, {BypassedRole});
    notePreset();
    return true;
}

bool ElectricWrap::setParameter(int pedalId, const QString& parameterId, double value)
{
    const int row = m_editor.indexOfSlot(pedalId);
    if (row < 0) return false;

    bool ok = m_editor.setParameter(pedalId, parameterId.toStdString(), value);
    if (!ok) return false;

    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed, {ParametersRole});
    notePreset();
    return true;
}

bool ElectricWrap::savePreset()
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
    notePreset();
    return true;
}

bool ElectricWrap::savePresetTo(const QUrl& url)
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

bool ElectricWrap::loadPresetFrom(const QUrl& url)
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

bool ElectricWrap::loadPreset()
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
    return loadPresetData(file.readAll(), QFileInfo(file).absolutePath(), false, QFileInfo(file).completeBaseName());
}

bool ElectricWrap::applyFactoryPreset(const QString& name)
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
    return loadPresetData(file.readAll(), {}, true, name);
}

void ElectricWrap::notePreset()
{
    if (presetName() == m_notedName && presetEdited() == m_notedEdited) return;
    m_notedName = presetName();
    m_notedEdited = presetEdited();
    emit presetNameChanged();
}

bool ElectricWrap::loadPresetData(
    const QByteArray& bytes,
    const QString& directory,
    bool keepCalibrationAndCabinet,
    const QString& name)
{
    auto candidate = m_editor.preparePresetData(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())),
        directory.toStdString(),
        keepCalibrationAndCabinet);

    if (!candidate.error.empty()) {
        setError(QString::fromStdString(candidate.error));
        return false;
    }

    // A name given here is a preset just taken: not edited yet. None given: the data's own (a session).
    const std::string presetName = name.isNull() ? candidate.name : name.toStdString();
    const bool presetEdited = name.isNull() && candidate.edited;

    if (!candidate.needsAsync) {
        beginResetModel();
        m_editor.commitPreset(std::move(candidate.candidateSlots), candidate.nextId, candidate.trim, presetName, presetEdited);
        endResetModel();
        emit countChanged();
        emit inputTrimDbChanged();
        setError({});
        notePreset();
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

    connect(watcher, &QFutureWatcher<AsyncResult>::finished, this, [this, watcher, nextId, trim, presetName, presetEdited] {
        auto result = watcher->result();
        watcher->deleteLater();
        setBusy(false);

        if (!result.error.isEmpty()) {
            setError(result.error);
            return;
        }

        beginResetModel();
        m_editor.commitPreset(std::move(result.candidateSlots), nextId, trim, presetName, presetEdited);
        endResetModel();
        emit countChanged();
        emit inputTrimDbChanged();
        setError({});
        notePreset();
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

void ElectricWrap::setBusy(bool busy)
{
    if (m_editor.busy() == busy)
        return;
    m_editor.setBusy(busy);
    emit busyChanged();
}

void ElectricWrap::setError(const QString& message)
{
    if (lastError() == message)
        return;
    m_editor.setError(message.toStdString());
    emit lastErrorChanged();
}

bool ElectricWrap::loadCabinetIr(int pedalId, const QUrl& url)
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
            notePreset();
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

bool ElectricWrap::clearCabinetIr(int pedalId)
{
    if (m_editor.busy()) return false;
    const int row = m_editor.indexOfSlot(pedalId);
    if (row < 0) return false;

    bool ok = m_editor.clearCabinetIr(pedalId);
    if (!ok) return false;

    const auto changed = index(row);
    emit dataChanged(changed, changed, {ParametersRole, ResourcePathRole});
    setError({});
    notePreset();
    return true;
}

QByteArray ElectricWrap::sessionData() const
{
    return QByteArray::fromStdString(m_editor.sessionData());
}

std::string ElectricWrap::sessionDataStd() const
{
    return m_editor.sessionData();
}

bool ElectricWrap::restoreSessionData(const QByteArray& bytes)
{
    return !m_editor.busy() && loadPresetData(bytes, {});
}

bool ElectricWrap::restoreSessionData(std::string_view bytes)
{
    return restoreSessionData(QByteArray::fromRawData(bytes.data(), static_cast<qsizetype>(bytes.size())));
}

namespace {
QStringList toQStringList(const std::vector<std::string>& values)
{
    QStringList list;
    for (const auto& value : values) list.push_back(QString::fromStdString(value));
    return list;
}
}

QStringList ElectricWrap::beatDivisionNames() { return toQStringList(pedaleditor::beatDivisionNames()); }

QVariantMap ElectricWrap::pedalFeatures(const QString& pedalType) const
{
    const auto f = pedaleditor::features(pedalType.toStdString());
    return {{QStringLiteral("pitchDelayNote"), f.pitchDelayNote}, {QStringLiteral("cabinetIr"), f.cabinetIr},
            {QStringLiteral("tuner"), f.tuner}, {QStringLiteral("sustainToggle"), f.sustainToggle},
            {QStringLiteral("tempoSync"), f.tempoSync}};
}
bool ElectricWrap::isDial(const QString& type, const QString& id) const { return pedaleditor::isDial(type.toStdString(), id.toStdString()); }
bool ElectricWrap::disabledWhenSynced(const QString& type, const QString& id) const { return pedaleditor::disabledWhenSynced(type.toStdString(), id.toStdString()); }
double ElectricWrap::dialStep(const QString& type, const QString& id) const { return pedaleditor::dialStep(type.toStdString(), id.toStdString()); }
QString ElectricWrap::formatValue(double value, const QString& unit) const { return QString::fromStdString(pedaleditor::formatValue(value, unit.toStdString())); }
QString ElectricWrap::syncText(bool synced, double bpm) const { return QString::fromStdString(pedaleditor::syncText(synced, bpm)); }
QString ElectricWrap::formatTrim(double db) const { return QString::fromStdString(pedaleditor::formatTrim(db)); }

int ElectricWrap::slotWhileDragging(int index, int from, int to) const
{
    return pedaleditor::slotWhileDragging(index, from, to);
}
