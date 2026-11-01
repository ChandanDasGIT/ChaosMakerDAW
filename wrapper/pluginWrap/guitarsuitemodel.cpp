#include "guitarsuitemodel.h"

#include "plugins/guitarsuite/guitarsuite.h"
#include "pedalregistry.h"
#include "plugins/tuner/tuner.h"
#include "plugins/tuner/standardtuning.h"
#include "plugins/cabinet/cabinet.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <QSaveFile>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

GuitarSuiteModel::GuitarSuiteModel(QObject* parent)
    : QAbstractListModel(parent)
{
    m_presetFilePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                       + QStringLiteral("/GuitarSuite/Default.gspreset");
}

int GuitarSuiteModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_slots.size());
}

QVariantList GuitarSuiteModel::parameterList(const PedalSlot& slot) const
{
    QVariantList values;
    for (const auto& parameter : slot.processor->parameters()) {
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

QVariant GuitarSuiteModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const auto& slot = *m_slots[static_cast<std::size_t>(index.row())];
    switch (role) {
    case ResourcePathRole:
        return QString::fromStdString(slot.resourcePath);
    case Qt::DisplayRole:
    case PedalNameRole:
        return PedalRegistry::displayName(QString::fromStdString(slot.type));
    case PedalIdRole:
        return slot.id;
    case PedalTypeRole:
        return QString::fromStdString(slot.type);
    case BypassedRole:
        return slot.bypassed.load(std::memory_order_relaxed);
    case ParametersRole:
        return parameterList(slot);
    default:
        return {};
    }
}

QHash<int, QByteArray> GuitarSuiteModel::roleNames() const
{
    return {{PedalIdRole, "pedalId"}, {PedalTypeRole, "pedalType"},
            {PedalNameRole, "pedalName"}, {BypassedRole, "isBypassed"},
            {ParametersRole, "parameters"}, {ResourcePathRole, "resourcePath"}};
}

void GuitarSuiteModel::setPublisher(std::function<void(std::shared_ptr<GuitarSuite>)> publisher)
{
    m_publisher = std::move(publisher);
    publishChain();
}

void GuitarSuiteModel::configure(double sampleRate, int maxFrames)
{
    if (sampleRate > 1000.0 && std::isfinite(sampleRate))
        m_sampleRate = sampleRate;
    m_maxFrames = std::max(1, maxFrames);
    publishChain();
}

QStringList GuitarSuiteModel::availablePedals() const
{
    QStringList result;
    for (const QString& type : PedalRegistry::availablePedals())
        result.push_back(PedalRegistry::displayName(type));
    return result;
}

bool GuitarSuiteModel::addPedal(const QString& type)
{
    if (m_busy) return false;
    auto processor = PedalRegistry::create(type);
    if (!processor)
        return false;
    auto slot = std::make_shared<PedalSlot>();
    slot->id = m_nextPedalId++;
    slot->type = processor->typeId();
    slot->processor = std::move(processor);
    const int row = rowCount();
    beginInsertRows({}, row, row);
    m_slots.push_back(std::move(slot));
    endInsertRows();
    emit countChanged();
    publishChain();
    setError({});
    return true;
}

std::shared_ptr<PedalSlot> GuitarSuiteModel::findSlot(int pedalId) const
{
    const auto it = std::find_if(m_slots.begin(), m_slots.end(), [pedalId](const auto& slot) {
        return slot->id == pedalId;
    });
    return it == m_slots.end() ? std::shared_ptr<PedalSlot>{} : *it;
}

bool GuitarSuiteModel::removePedal(int pedalId)
{
    if (m_busy) return false;
    const auto it = std::find_if(m_slots.begin(), m_slots.end(), [pedalId](const auto& slot) {
        return slot->id == pedalId;
    });
    if (it == m_slots.end()) return false;
    const int row = static_cast<int>(std::distance(m_slots.begin(), it));
    beginRemoveRows({}, row, row);
    m_slots.erase(it);
    endRemoveRows();
    emit countChanged();
    publishChain();
    return true;
}

bool GuitarSuiteModel::movePedal(int from, int to)
{
    if (m_busy) return false;
    if (from < 0 || to < 0 || from >= rowCount() || to >= rowCount() || from == to)
        return false;
    const int destinationChild = to > from ? to + 1 : to;
    if (!beginMoveRows({}, from, from, {}, destinationChild))
        return false;
    auto slot = std::move(m_slots[static_cast<std::size_t>(from)]);
    m_slots.erase(m_slots.begin() + from);
    m_slots.insert(m_slots.begin() + to, std::move(slot));
    endMoveRows();
    publishChain();
    return true;
}

bool GuitarSuiteModel::setBypassed(int pedalId, bool bypassed)
{
    const auto slot = findSlot(pedalId);
    if (!slot) return false;
    slot->bypassed.store(bypassed, std::memory_order_relaxed);
    const auto it = std::find(m_slots.begin(), m_slots.end(), slot);
    const int row = static_cast<int>(std::distance(m_slots.begin(), it));
    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed, {BypassedRole});
    return true;
}

bool GuitarSuiteModel::setParameter(int pedalId, const QString& parameterId, double value)
{
    if (!std::isfinite(value)) return false;
    const auto slot = findSlot(pedalId);
    if (!slot) return false;
    for (const auto& parameter : slot->processor->parameters()) {
        if (QString::fromStdString(parameter->id) == parameterId) {
            parameter->set(static_cast<float>(value));
            const auto it = std::find(m_slots.begin(), m_slots.end(), slot);
            const int row = static_cast<int>(std::distance(m_slots.begin(), it));
            const QModelIndex changed = index(row, 0);
            emit dataChanged(changed, changed, {ParametersRole});
            return true;
        }
    }
    return false;
}

void GuitarSuiteModel::publishChain()
{
    if (!m_publisher) return;
    auto chain = std::make_shared<GuitarSuite>(m_slots);
    chain->prepare(m_sampleRate, m_maxFrames);
    m_publisher(std::move(chain));
}

bool GuitarSuiteModel::savePreset()
{
    if (m_busy) return false;
    const QFileInfo info(m_presetFilePath);
    if (!QDir().mkpath(info.absolutePath())) {
        setError(QStringLiteral("Could not create GuitarSuite preset folder."));
        return false;
    }
    QJsonArray pedals;
    for (const auto& slot : m_slots) {
        QJsonObject params;
        for (const auto& parameter : slot->processor->parameters())
            params.insert(QString::fromStdString(parameter->id), parameter->get());
        pedals.push_back(QJsonObject{
            {QStringLiteral("type"), QString::fromStdString(slot->type)},
            {QStringLiteral("bypassed"), slot->bypassed.load(std::memory_order_relaxed)},
            {QStringLiteral("parameters"), params},
            {QStringLiteral("irPath"), slot->resourcePath.empty() ? QString() : info.absoluteDir().relativeFilePath(QString::fromStdString(slot->resourcePath))}
        });
    }
    QSaveFile file(m_presetFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(QStringLiteral("Could not write GuitarSuite preset."));
        return false;
    }
    const auto bytes = QJsonDocument(QJsonObject{
        {QStringLiteral("format"), QStringLiteral("GuitarSuitePreset")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("inputTrimDb"), m_inputTrimDb},
        {QStringLiteral("pedals"), pedals}
    }).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        setError(QStringLiteral("Could not finish writing GuitarSuite preset.")); return false;
    }
    setError({});
    return true;
}

bool GuitarSuiteModel::savePresetTo(const QUrl& url)
{
    if(m_busy)return false;
    if (!url.isLocalFile()) {
        setError(QStringLiteral("GuitarSuite presets must be saved to a local file."));
        return false;
    }
    if (m_presetFilePath != url.toLocalFile()) {
        m_presetFilePath = url.toLocalFile();
        emit presetFilePathChanged();
    }
    return savePreset();
}

bool GuitarSuiteModel::loadPresetFrom(const QUrl& url)
{
    if(m_busy)return false;
    if (!url.isLocalFile()) {
        setError(QStringLiteral("GuitarSuite presets must be loaded from a local file."));
        return false;
    }
    if (m_presetFilePath != url.toLocalFile()) {
        m_presetFilePath = url.toLocalFile();
        emit presetFilePathChanged();
    }
    return loadPreset();
}

bool GuitarSuiteModel::loadPreset()
{
    if (m_busy) return false;
    QFile file(m_presetFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("GuitarSuite preset not found: %1").arg(m_presetFilePath));
        return false;
    }
    if(file.size()>1024*1024) { setError(QStringLiteral("GuitarSuite preset is too large."));return false; }
    return loadPresetData(file.readAll(), QFileInfo(file).absolutePath());
}

bool GuitarSuiteModel::applyFactoryPreset(const QString& name)
{
    if (m_busy) return false;
    if (!factoryPresetNames().contains(name)) {
        setError(QStringLiteral("Unknown GuitarSuite factory preset.")); return false;
    }
    QFile file(QStringLiteral(":/GuitarSuitePresets/%1.gspreset").arg(name));
    if (!file.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("Could not open GuitarSuite factory preset.")); return false;
    }
    return loadPresetData(file.readAll(), {}, true);
}

bool GuitarSuiteModel::loadPresetData(const QByteArray& bytes, const QString& directory, bool keepCalibrationAndCabinet)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    const QJsonObject root = document.object();
    if (parseError.error != QJsonParseError::NoError
        || root.value(QStringLiteral("format")).toString() != QStringLiteral("GuitarSuitePreset")
        || root.value(QStringLiteral("version")).toInt() != 1
        || !root.value(QStringLiteral("pedals")).isArray()) {
        setError(QStringLiteral("Invalid or unsupported GuitarSuite preset."));
        return false;
    }

    const auto trimValue = root.value(QStringLiteral("inputTrimDb"));
    const double trim = keepCalibrationAndCabinet ? m_inputTrimDb : trimValue.isUndefined() ? 0.0 : trimValue.toDouble(1000.0);
    if (!std::isfinite(trim) || trim < -24 || trim > 24) {
        setError(QStringLiteral("Invalid input trim in GuitarSuite preset."));
        return false;
    }
    std::vector<std::shared_ptr<PedalSlot>> candidate;
    int nextId = m_nextPedalId;
    const QJsonArray pedals = root.value(QStringLiteral("pedals")).toArray();
    for (const QJsonValue& value : pedals) {
        if (!value.isObject()) {
            setError(QStringLiteral("Invalid pedal entry in GuitarSuite preset."));
            return false;
        }
        const QJsonObject item = value.toObject();
        const QString type = item.value(QStringLiteral("type")).toString();
        auto processor = PedalRegistry::create(type);
        if (!processor) {
            setError(QStringLiteral("Unknown pedal type in GuitarSuite preset: %1").arg(type));
            return false;
        }
        auto slot = std::make_shared<PedalSlot>();
        slot->id = nextId++;
        slot->type = type.toStdString();
        const auto resource = item.value(QStringLiteral("irPath"));
        if (!resource.isUndefined() && !resource.isString()) { setError(QStringLiteral("Invalid cabinet IR reference.")); return false; }
        if (type == "pedal.cabinet" && !resource.toString().isEmpty())
            slot->resourcePath = QFileInfo(QDir(directory),resource.toString()).absoluteFilePath().toStdString();
        slot->processor = std::move(processor);
        slot->bypassed.store(item.value(QStringLiteral("bypassed")).toBool());
        if (keepCalibrationAndCabinet && type == "pedal.cabinet") {
            const auto existing = std::find_if(m_slots.begin(), m_slots.end(), [](const auto& entry) {
                return entry->type == "pedal.cabinet" && !entry->resourcePath.empty();
            });
            if (existing != m_slots.end()) {
                const auto& current = *existing;
                slot->resourcePath = current->resourcePath;
                if (current->preparedSampleRate == m_sampleRate) {
                    // Share the already prepared cabinet without resetting its audio state.
                    // The new slot has its own bypass control; no live state is mutated here.
                    slot->processor = current->processor;
                    slot->preparedSampleRate = current->preparedSampleRate;
                    slot->preparedMaxFrames = current->preparedMaxFrames;
                } else {
                    for (const auto& value : current->processor->parameters())
                        for (const auto& parameter : slot->processor->parameters())
                            if (value->id == parameter->id) parameter->set(value->get());
                }
                candidate.push_back(std::move(slot));
                continue;
            }
        }
        const QJsonObject params = item.value(QStringLiteral("parameters")).toObject();
        for (const auto& parameter : slot->processor->parameters()) {
            const auto setting = params.value(QString::fromStdString(parameter->id));
            if (setting.isDouble() && std::isfinite(setting.toDouble()))
                parameter->set(static_cast<float>(setting.toDouble()));
        }
        candidate.push_back(std::move(slot));
    }

    const bool resources = std::any_of(candidate.begin(),candidate.end(),[](const auto& slot){ return !slot->resourcePath.empty() && slot->preparedSampleRate == 0.0; });
    if (!resources) { commitPreset(std::move(candidate),nextId,trim); return true; }
    struct Result { std::vector<std::shared_ptr<PedalSlot>> candidateSlots; QString error; };
    auto* watcher = new QFutureWatcher<Result>(this);
    setBusy(true); setError({});
    const double rate=m_sampleRate; const int frames=m_maxFrames;
    connect(watcher,&QFutureWatcher<Result>::finished,this,[this,watcher,nextId,trim] {
        auto result=watcher->result(); watcher->deleteLater(); setBusy(false);
        if (!result.error.isEmpty()) { setError(result.error); return; }
        commitPreset(std::move(result.candidateSlots),nextId,trim);
    });
    watcher->setFuture(QtConcurrent::run([candidate=std::move(candidate),rate,frames]() mutable {
        Result result;
        try {
            for(auto& slot:candidate) {
                if (slot->preparedSampleRate != 0.0) continue;
                if(!slot->resourcePath.empty()) {
                    auto processor=std::make_shared<Cabinet>(loadImpulseWav(slot->resourcePath,rate));
                    for(const auto& value:slot->processor->parameters())
                        for(const auto& param:processor->parameters())if(param->id==value->id)param->set(value->get());
                    slot->processor=std::move(processor);
                }
                slot->processor->prepare(rate,frames);slot->preparedSampleRate=rate;slot->preparedMaxFrames=frames;
            }
            result.candidateSlots=std::move(candidate);
        } catch(const std::exception& error) { result.error=QString::fromUtf8(error.what()); }
        return result;
    }));
    return true; // Accepted; busy/lastError report asynchronous completion.
}

void GuitarSuiteModel::commitPreset(std::vector<std::shared_ptr<PedalSlot>> candidateSlots,int nextId,double trim)
{
    beginResetModel();m_slots=std::move(candidateSlots);m_nextPedalId=nextId;endResetModel();
    emit countChanged();setInputTrimDb(trim);publishChain();setError({});
}
void GuitarSuiteModel::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void GuitarSuiteModel::setError(const QString& message)
{
    if (message == m_lastError) return;
    m_lastError = message;
    emit lastErrorChanged();
}

QVariantMap GuitarSuiteModel::tunerReading(int pedalId) const
{
    const auto slot = findSlot(pedalId);
    if (!slot) return {};
    const auto tuner = std::dynamic_pointer_cast<Tuner>(slot->processor);
    if (!tuner) return {};
    const float hz = slot->bypassed.load(std::memory_order_relaxed) ? 0.0f : tuner->frequency();
    QVariantMap result{{"frequency", hz}, {"revision", tuner->revision()},
                       {"note", "—"}, {"cents", 0.0}, {"targetHz", 0.0},
                       {"stringNumber", 0}, {"targetSelection", tuner->selectedString()},
                       {"direction", "waiting"}};
    const auto reading = standardtuning::measure(hz, tuner->selectedString());
    if (reading.index >= 0) {
        const auto& target = standardtuning::targets[reading.index];
        result["note"] = QString::fromUtf8(target.note);
        result["cents"] = reading.cents;
        result["targetHz"] = reading.targetHz;
        result["stringNumber"] = target.stringNumber;
        result["direction"] = std::abs(reading.cents) <= 3.0 ? "inTune"
                            : reading.cents < 0 ? "low" : "high";
    }
    return result;
}

void GuitarSuiteModel::setInputTrimDb(double db)
{
    if (!std::isfinite(db)) return;
    db = std::clamp(db, -24.0, 24.0);
    if (m_inputTrimDb == db) return;
    m_inputTrimDb = db;
    emit inputTrimDbChanged();
}

bool GuitarSuiteModel::loadCabinetIr(int pedalId,const QUrl& url)
{
    const auto slot=findSlot(pedalId);
    if(m_busy || !slot || slot->type!="pedal.cabinet" || !url.isLocalFile())return false;
    struct Result { std::shared_ptr<Cabinet> processor; QString error; };
    const QString path=QFileInfo(url.toLocalFile()).absoluteFilePath();
    const double rate=m_sampleRate;const int frames=m_maxFrames;
    auto* watcher=new QFutureWatcher<Result>(this);setBusy(true);setError({});
    connect(watcher,&QFutureWatcher<Result>::finished,this,[this,watcher,pedalId,path,rate,frames] {
        auto result=watcher->result();watcher->deleteLater();setBusy(false);
        if(!result.error.isEmpty()){setError(result.error);return;}
        const auto old=findSlot(pedalId);if(!old)return;
        auto next=std::make_shared<PedalSlot>();next->id=old->id;next->type=old->type;
        next->resourcePath=path.toStdString();next->processor=std::move(result.processor);
        for(const auto& value:old->processor->parameters())
            for(const auto& param:next->processor->parameters())if(param->id==value->id)param->set(value->get());
        next->bypassed.store(old->bypassed.load());next->preparedSampleRate=rate;next->preparedMaxFrames=frames;
        const auto it=std::find(m_slots.begin(),m_slots.end(),old);const int row=int(it-m_slots.begin());*it=std::move(next);
        const auto changed=index(row);emit dataChanged(changed,changed,{ParametersRole,ResourcePathRole});publishChain();
    });
    watcher->setFuture(QtConcurrent::run([path,rate,frames] {
        Result result;
        try { result.processor=std::make_shared<Cabinet>(loadImpulseWav(path.toStdString(),rate));result.processor->prepare(rate,frames); }
        catch(const std::exception& error) { result.error=QString::fromUtf8(error.what()); }
        return result;
    }));return true;
}
bool GuitarSuiteModel::clearCabinetIr(int pedalId)
{
    if(m_busy)return false;
    const auto old=findSlot(pedalId);if(!old || old->type!="pedal.cabinet")return false;
    auto slot=std::make_shared<PedalSlot>();slot->id=old->id;slot->type=old->type;slot->processor=std::make_shared<Cabinet>();
    for(const auto& value:old->processor->parameters())
        for(const auto& param:slot->processor->parameters())if(param->id==value->id)param->set(value->get());
    slot->bypassed.store(old->bypassed.load());
    const auto it=std::find(m_slots.begin(),m_slots.end(),old);const int row=int(it-m_slots.begin());*it=std::move(slot);
    const auto changed=index(row);emit dataChanged(changed,changed,{ParametersRole,ResourcePathRole});publishChain();setError({});return true;
}

QByteArray GuitarSuiteModel::sessionData() const
{
    QJsonArray pedals;
    for (const auto& slot : m_slots) {
        QJsonObject params;
        for (const auto& parameter : slot->processor->parameters())
            params.insert(QString::fromStdString(parameter->id), parameter->get());
        pedals.append(QJsonObject{{"type", QString::fromStdString(slot->type)},
            {"bypassed", slot->bypassed.load(std::memory_order_relaxed)}, {"parameters", params},
            {"irPath", QString::fromStdString(slot->resourcePath)}});
    }
    return QJsonDocument(QJsonObject{{"format", "GuitarSuitePreset"}, {"version", 1},
        {"inputTrimDb", m_inputTrimDb}, {"pedals", pedals}}).toJson();
}

bool GuitarSuiteModel::restoreSessionData(const QByteArray& bytes)
{
    return !m_busy && loadPresetData(bytes, {});
}
