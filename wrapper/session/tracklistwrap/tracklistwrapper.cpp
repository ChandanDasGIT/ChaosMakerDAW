#include "tracklistwrapper.h"
#include "timeline/clipwaveform.h"
#include "engine/snapshotbuilder.h"
#include <optional>
#include "plugins/eq/eqpresets.h"
#include "plugins/eq/eqpresetstore.h"
#include "recording/takerecorder.h"
#include <QFileInfo>
#include "plugins/eq/tenbandeq.h"
#include <QFile>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

#include <QVariant>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <limits>


namespace {
QVariantList noteList(const std::vector<MidiNote>& notes)
{
    QVariantList list;
    list.reserve(qsizetype(notes.size()));
    for (const auto& n : notes)
        list.append(QVariantMap{{"id", n.id}, {"note", n.note}, {"startBeat", n.startBeat},
                                {"lengthBeats", n.lengthBeats}, {"velocity", n.velocity}});
    return list;
}

QVariantMap clipMap(const TrackList::ClipInfo& clip)
{
    return {{"clipId", clip.clipId}, {"trackId", clip.trackId}, {"laneIndex", clip.laneIndex},
        {"startBeat", clip.startBeat}, {"lengthBeats", clip.lengthBeats},
        {"label", QString::fromStdString(clip.label)}, {"trackColour", QString::fromStdString(clip.colour)},
        {"effectsPrinted", clip.effectsPrinted}, {"gainDb", clip.gainDb},
        {"isRecorded", clip.isRecorded}, {"notes", noteList(clip.notes)}};
}
QVariantList choices(const std::vector<TrackList::TrackChoice>& entries)
{
    QVariantList result;
    for (const auto& entry : entries) result.append(QVariantMap{{"id", QString::fromStdString(entry.id)},
        {"name", QString::fromStdString(entry.name)}, {"hasChildren", entry.hasChildren}});
    return result;
}
std::optional<std::vector<int>> clipIds(const QVariantList& values)
{
    std::vector<int> result;
    for (const auto& value : values) {
        bool ok = false; const int id = value.toInt(&ok);
        if (!ok) return std::nullopt;
        result.push_back(id);
    }
    return result;
}
}

TrackListWrapper::TrackListWrapper(QObject* parent)
    : QAbstractListModel(parent)
    , m_notifiedTrackId(m_state.selectedTrackId())
    , m_notifiedClipId(m_state.selectedClipId())
{
    connect(this, &QAbstractItemModel::dataChanged, this, &TrackListWrapper::projectChanged);
    connect(this, &TrackListWrapper::countChanged, this, &TrackListWrapper::projectChanged);
    connect(this, &TrackListWrapper::clipsChanged, this, &TrackListWrapper::projectChanged);
    connect(this, &TrackListWrapper::clipsChanged, this, &TrackListWrapper::historyChanged);
}

int TrackListWrapper::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_project.tracks().size());
}

QVariant TrackListWrapper::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};

    const auto& track = *m_project.tracks()[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
    case NameRole:
        return QString::fromStdString(track.name);
    case TrackIdRole:
        return track.id;
    case ColourRole:
        return QString::fromStdString(track.colour);
    case KindRole:
        return track.kind == TrackKind::Instrument ? QStringLiteral("instrument") : QStringLiteral("audio");
    case LiveInputRole:
        return track.hasLiveInput();
    case RecordDryRole:
        return track.recordDry;
    case RecordsWithFxRole:
        return track.recordsWithFx();
    case TypeRole:
        return QString::fromStdString(track.type);
    case SoloRole:
        return track.solo;
    case MutedRole:
        return track.muted;
    case RecordArmedRole:
        return track.recordArmed;
    case MonitorEnabledRole:
        return track.monitorEnabled;
    case InputPortRole:
        return QString::fromStdString(track.inputPort);
    case VolumeRole:
        return track.volume;
    case EqBandsRole: {
        QVariantList bands;
        for (float gain : track.eqGainDb)
            bands.push_back(gain);
        return bands;
    }
    case ClipsRole: {
        QVariantList result;
        for (const auto& clip : m_state.clips()) if (clip.trackId == track.id) result.append(clipMap(clip));
        return result;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> TrackListWrapper::roleNames() const
{
    return {
        {TrackIdRole, "trackId"},
        {ColourRole, "trackColour"},
        {NameRole, "name"},
        {KindRole, "kind"},
        {TypeRole, "trackType"},
        {SoloRole, "isSolo"},
        {MutedRole, "isMuted"},
        {RecordArmedRole, "isRecordArmed"},
        {MonitorEnabledRole, "monitorEnabled"},
        {InputPortRole, "inputPort"},
        {VolumeRole, "volume"},
        {ClipsRole, "clips"},
        {EqBandsRole, "eqBands"},
        {LiveInputRole, "hasLiveInput"},
        {RecordsWithFxRole, "recordsWithFx"},
        {RecordDryRole, "recordDry"}
    };
}

QVariantList TrackListWrapper::clips() const
{
    QVariantList result;
    for (const auto& clip : m_state.clips()) result.append(clipMap(clip));
    return result;
}

QVariantList TrackListWrapper::clipWaveform(int trackId, int clipId, int bins, double from, double to) const
{
    QVariantList result;
    const auto* clip = m_state.findClip(trackId, clipId);
    const auto audio = clip ? m_state.audioFor(clip->poolId) : nullptr;
    if (clip && audio) for (const auto& peak : ::clipWaveform(*clip, *audio, m_project.tempo().sampleRate(), bins, from, to))
        result.append(QVariant(QVariantList{peak[0], peak[1]}));
    return result;
}

bool TrackListWrapper::setData(const QModelIndex& idx, const QVariant& value, int role)
{
    if (!idx.isValid() || idx.row() < 0 || idx.row() >= rowCount()) return false;
    const int id = m_state.trackIdAt(idx.row());
    switch (role) {
    case ColourRole: return setTrackColour(id, value.toString());
    case Qt::DisplayRole: case Qt::EditRole: case NameRole: return renameTrack(id, value.toString());
    case SoloRole: return setSolo(id, value.toBool());
    case MutedRole: return setMuted(id, value.toBool());
    case MonitorEnabledRole: return setMonitorEnabled(id, value.toBool());
    case RecordArmedRole: return setRecordArmed(id, value.toBool());
    case InputPortRole: return setTrackInputPort(id, value.toString());
    case VolumeRole: return setVolume(id, value.toDouble());
    default: return false;
    }
}

Qt::ItemFlags TrackListWrapper::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

void TrackListWrapper::syncSelectionState()
{
    const int currentClip = m_state.selectedClipId();
    if (m_notifiedClipId != currentClip) {
        m_notifiedClipId = currentClip;
        emit selectedClipChanged(m_notifiedClipId);
    }
    const int currentTrack = m_state.selectedTrackId();
    if (m_notifiedTrackId != currentTrack) {
        m_notifiedTrackId = currentTrack;
        emit selectedTrackChanged(m_notifiedTrackId);
    }
}

int TrackListWrapper::appendTrack(const std::function<int()>& insert)
{
    const int row = rowCount();
    beginInsertRows({}, row, row);
    const int id = insert();
    endInsertRows(); emit countChanged();
    syncSelectionState();
    return id;
}

bool TrackListWrapper::selectTrack(int id)
{
    if (id >= 0 && !m_project.findTrack(id)) return false;
    m_state.selectTrack(id);
    syncSelectionState();
    return true;
}

void TrackListWrapper::setSelectedClipId(int id)
{
    if (id < 0) {
        m_state.clearSelectedClip();
    } else {
        for (const auto& track : m_project.tracks()) {
            if (m_state.findClip(track->id, id)) {
                m_state.selectClip(track->id, id);
                break;
            }
        }
    }
    syncSelectionState();
}

void TrackListWrapper::selectClip(int trackId, int clipId)
{
    if (!m_state.selectClip(trackId, clipId)) return;
    syncSelectionState();
}

bool TrackListWrapper::setClipGain(int trackId, int clipId, double gainDb)
{
    const auto change = m_state.setClipGain(trackId, clipId, gainDb);
    if (change.changed) {
        notifyClipEdit(trackId);
    }
    return change.accepted;
}

QVariantList TrackListWrapper::clipNotes(int trackId, int clipId) const
{
    return noteList(m_state.clipNotes(trackId, clipId));
}

bool TrackListWrapper::setClipNotes(int trackId, int clipId, std::vector<MidiNote> notes)
{
    const auto change = m_state.setClipNotes(trackId, clipId, std::move(notes));
    if (change.changed) notifyClipEdit(trackId);
    return change.accepted;
}

double TrackListWrapper::clipGain(int trackId, int clipId) const
{
    const auto* clip = m_state.findClip(trackId, clipId);
    return clip ? clip->gainDb : 0.0;
}

int TrackListWrapper::addAudioTrack()
{
    return appendTrack([this] { return m_state.addAudioTrack(); });
}

int TrackListWrapper::addVocalTrack()
{
    return appendTrack([this] { return m_state.addVocalTrack(); });
}

int TrackListWrapper::addInstrumentTrack()
{
    return appendTrack([this] { return m_state.addInstrumentTrack(); });
}

int TrackListWrapper::addInstrumentTrackOfType(const QString& type)
{
    const auto id = type.toStdString();
    if (!TrackList::isInstrumentType(id)) return -1;
    return appendTrack([this, &id] { return m_state.addInstrumentTrackOfType(id); });
}

int TrackListWrapper::rowForTrackId(int id) const { return m_state.indexOfTrack(id); }

bool TrackListWrapper::renameTrack(int id, const QString& name)
{
    const auto change = m_state.renameTrack(id, name.toStdString());
    if (change.changed) { const auto idx = index(rowForTrackId(id)); emit dataChanged(idx, idx, {NameRole, Qt::DisplayRole, Qt::EditRole}); }
    return change.accepted;
}

bool TrackListWrapper::setTrackColour(int id, const QString& colour)
{
    const auto change = m_state.setTrackColour(id, colour.toStdString());
    if (change.changed) { const auto idx = index(rowForTrackId(id)); emit dataChanged(idx, idx, {ColourRole}); emit clipsChanged(); }
    return change.accepted;
}

bool TrackListWrapper::moveTrack(int id, int destination)
{
    if (!m_state.canMoveTrack(id, destination)) return false;
    const int source = rowForTrackId(id);
    if (source == destination) return true;
    if (!beginMoveRows({}, source, source, {}, destination > source ? destination + 1 : destination)) return false;
    m_state.moveTrack(id, destination);
    endMoveRows(); emit clipsChanged(); return true;
}

bool TrackListWrapper::removeTrack(int id)
{
    if (!m_state.canRemoveTrack(id)) return false;
    const int row = rowForTrackId(id);
    beginRemoveRows({}, row, row); m_state.removeTrack(id); endRemoveRows();
    syncSelectionState();
    emit countChanged(); emit contentEndBeatsChanged(); emit clipsChanged();
    emit cleanMonitorChanged(cleanMonitorArmed()); emit cleanMonitorInputChanged();
    emit cleanMonitorGainChanged(cleanMonitorGain()); emit cleanMonitorEqChanged();
    return true;
}

int TrackListWrapper::trackIdAt(int row) const { return m_state.trackIdAt(row); }

bool TrackListWrapper::notifyTrackChanged(int id, int role, TrackList::Change change)
{
    if (change.changed) {
        const auto idx = index(rowForTrackId(id)); emit dataChanged(idx, idx, {role});
        if (role == SoloRole || role == MutedRole) emit cleanMonitorGainChanged(cleanMonitorGain());
    }
    return change.accepted;
}

bool TrackListWrapper::setSolo(int id, bool value)
{
    return notifyTrackChanged(id, SoloRole, m_state.setSolo(id, value));
}

bool TrackListWrapper::setMuted(int id, bool value)
{
    return notifyTrackChanged(id, MutedRole, m_state.setMuted(id, value));
}

bool TrackListWrapper::setRecordDry(int id, bool value)
{
    const auto change = m_state.setRecordDry(id, value);
    if (change.changed) { const auto idx = index(rowForTrackId(id)); emit dataChanged(idx, idx, {RecordDryRole, RecordsWithFxRole}); }
    return change.accepted;
}

bool TrackListWrapper::setRecordArmed(int id, bool value)
{
    return notifyTrackChanged(id, RecordArmedRole, m_state.setRecordArmed(id, value));
}

bool TrackListWrapper::setMonitorEnabled(int id, bool value)
{
    const auto change = m_state.setMonitorEnabled(id, value);
    notifyTrackChanged(id, MonitorEnabledRole, change);
    if (change.changed) {
        emit cleanMonitorChanged(cleanMonitorArmed()); emit cleanMonitorInputChanged();
        emit cleanMonitorGainChanged(cleanMonitorGain()); emit cleanMonitorEqChanged();
    }
    return change.accepted;
}

bool TrackListWrapper::cleanMonitorArmed() const { return m_state.cleanMonitor().enabled; }

QString TrackListWrapper::cleanMonitorInput() const { return QString::fromStdString(m_state.cleanMonitor().input); }

double TrackListWrapper::cleanMonitorGain() const { return m_state.cleanMonitor().gain; }

QVariantList TrackListWrapper::cleanMonitorEqBands() const
{
    QVariantList bands;
    for (float gain : m_state.cleanMonitor().eq) bands.append(gain);
    return bands;
}

bool TrackListWrapper::setEqBand(int id, int band, double gain)
{
    const auto change = m_state.setEqBand(id, band, gain);
    notifyTrackChanged(id, EqBandsRole, change);
    if (change.changed && m_state.isMonitored(id)) emit cleanMonitorEqChanged();
    return change.accepted;
}

bool TrackListWrapper::resetTrackEq(int trackId)
{
    return applyEqPreset(trackId, 0);
}

bool TrackListWrapper::setTrackInputPort(int id, const QString& port)
{
    const auto change = m_state.setTrackInputPort(id, port.toStdString());
    notifyTrackChanged(id, InputPortRole, change);
    if (change.changed && cleanMonitorArmed()) emit cleanMonitorInputChanged();
    return change.accepted;
}

bool TrackListWrapper::setVolume(int id, double value)
{
    const auto change = m_state.setVolume(id, value);
    notifyTrackChanged(id, VolumeRole, change);
    if (change.changed && cleanMonitorArmed()) emit cleanMonitorGainChanged(cleanMonitorGain());
    return change.accepted;
}

bool TrackListWrapper::setMasterVolume(double value)
{
    const auto change = m_state.setMasterVolume(value);
    if (change.changed) {
        emit masterVolumeChanged();
        emit projectChanged();
    }
    return change.accepted;
}

void TrackListWrapper::resetMasterVolume()
{
    setMasterVolume(1.0);
}

bool TrackListWrapper::setMasterMuted(bool value)
{
    const auto change = m_state.setMasterMuted(value);
    if (change.changed) {
        emit masterMutedChanged();
        emit projectChanged();
    }
    return change.accepted;
}

int TrackListWrapper::addAudioClip(int id, const QString& pool, qint64 start, qint64 length, qint64 offset, bool printed)
{
    const int clip = m_state.addAudioClip(id, pool.toStdString(), start, length, offset, printed);
    if (clip >= 0) notifyClipEdit(id);
    return clip;
}

int TrackListWrapper::addClipAtBeat(int id, double start, double length)
{
    const int clip = m_state.addClipAtBeat(id, start, length);
    if (clip >= 0) notifyClipEdit(id);
    return clip;
}

bool TrackListWrapper::trimClipStartAtBeat(int id, int clip, double start)
{
    if (!m_state.trimClipStartAtBeat(id, clip, start)) return false;
    notifyClipEdit(id); return true;
}
int TrackListWrapper::splitClipAtBeat(int id, int clip, double beat)
{
    const int result = m_state.splitClipAtBeat(id, clip, beat);
    if (result >= 0) notifyClipEdit(id);
    return result;
}

bool TrackListWrapper::editClips(const QVariantList& values, const QString& operation, double delta, int lanes)
{
    const auto ids = clipIds(values);
    if (!ids) return false;
    const auto change = m_state.editClips(*ids, operation.toStdString(), delta, lanes);
    if (change.changed) notifyClipEdit(-1);
    return change.accepted;
}

bool TrackListWrapper::canMergeClips(int id, int first, int second) const { return m_state.canMergeClips(id, first, second); }

bool TrackListWrapper::canMergeSelectedClips(const QVariantList& values) const
{
    const auto ids = clipIds(values);
    return ids && m_state.canMergeSelectedClips(*ids);
}

bool TrackListWrapper::mergeSelectedClips(const QVariantList& values)
{
    const auto ids = clipIds(values);
    if (!ids || !m_state.mergeSelectedClips(*ids)) return false;
    notifyClipEdit(-1); return true;
}

bool TrackListWrapper::mergeClips(int id, int first, int second)
{
    if (!m_state.mergeClips(id, first, second)) return false;
    notifyClipEdit(id); return true;
}

bool TrackListWrapper::moveClipAtBeat(int id, int clip, double start)
{
    if (!m_state.moveClipAtBeat(id, clip, start)) return false;
    notifyClipEdit(id); return true;
}

bool TrackListWrapper::resizeClipAtBeat(int id, int clip, double length)
{
    if (!m_state.resizeClipAtBeat(id, clip, length)) return false;
    notifyClipEdit(id); return true;
}

bool TrackListWrapper::removeAudioClip(int id, int clip)
{
    if (!m_state.removeAudioClip(id, clip)) return false;
    notifyClipEdit(id); return true;
}

bool TrackListWrapper::copyClips(const QVariantList& ids)
{
    std::vector<int> intIds;
    intIds.reserve(ids.size());
    for (const auto& id : ids) intIds.push_back(id.toInt());
    const bool ok = m_state.copyClips(intIds);
    if (ok) emit clipboardChanged();
    return ok;
}

QVariantList TrackListWrapper::cutClips(const QVariantList& ids)
{
    std::vector<int> intIds;
    intIds.reserve(ids.size());
    for (const auto& id : ids) intIds.push_back(id.toInt());
    const auto cut = m_state.cutClips(intIds);
    if (!cut.empty()) {
        emit clipboardChanged();
        notifyClipEdit(-1);
    }
    QVariantList result;
    result.reserve(cut.size());
    for (int id : cut) result.append(id);
    return result;
}

QVariantList TrackListWrapper::pasteClips(int targetTrackId, double targetBeat)
{
    const auto pasted = m_state.pasteClips(targetTrackId, targetBeat);
    if (!pasted.empty()) {
        notifyClipEdit(targetTrackId);
    }
    QVariantList result;
    result.reserve(pasted.size());
    for (int id : pasted) result.append(id);
    return result;
}

QVariantList TrackListWrapper::duplicateClips(const QVariantList& ids, double beatOffset)
{
    std::vector<int> intIds;
    intIds.reserve(ids.size());
    for (const auto& id : ids) intIds.push_back(id.toInt());
    const auto duplicated = m_state.duplicateClips(intIds, beatOffset);
    if (!duplicated.empty()) {
        notifyClipEdit(-1);
    }
    QVariantList result;
    result.reserve(duplicated.size());
    for (int id : duplicated) result.append(id);
    return result;
}

void TrackListWrapper::notifyClipEdit(int trackId) {
    const int row = rowForTrackId(trackId);
    if (count()>0) emit dataChanged(index(row<0 ? 0 : row), index(row<0 ? count()-1 : row), {ClipsRole});
    emit contentEndBeatsChanged();
    emit clipsChanged();
}
bool TrackListWrapper::undo()
{
    if (!m_state.undo()) return false;
    notifyClipEdit(-1); return true;
}
bool TrackListWrapper::redo()
{
    if (!m_state.redo()) return false;
    notifyClipEdit(-1); return true;
}

void TrackListWrapper::setTempo(double bpm, int numerator, int denominator)
{
    if (m_state.setTempo(bpm, numerator, denominator)) { emit contentEndBeatsChanged(); emit clipsChanged(); }
}

void TrackListWrapper::setEqPresetError(const QString& error) {
    m_eqPresetError = error; emit eqPresetErrorChanged();
}
QStringList TrackListWrapper::eqPresetNames() const {
    QStringList names;
    for (const auto& name : EqPresetStore::factoryPresetNames()) names.push_back(QString::fromStdString(name));
    return names;
}
bool TrackListWrapper::applyEqBands(int trackId, const std::array<float,10>& bands) {
    auto* track = m_project.findTrack(trackId);
    if (!track) { setEqPresetError(QStringLiteral("Track is unavailable.")); return false; }
    for (int i=0;i<10;++i) m_project.setTrackEqBand(trackId,i,bands[i]);
    const auto changed = index(rowForTrackId(trackId));
    emit dataChanged(changed,changed,{EqBandsRole});
    if (track->hasLiveInput() && track->monitorEnabled) emit cleanMonitorEqChanged();
    setEqPresetError({}); return true;
}
bool TrackListWrapper::applyEqPreset(int trackId, int preset) {
    auto gains = EqPresetStore::factoryPresetGains(preset);
    if (!gains) return false;
    return applyEqBands(trackId, *gains);
}
bool TrackListWrapper::saveEqPreset(int trackId, const QUrl& url) {
    const auto* track = m_project.findTrack(trackId);
    if (!track || !url.isLocalFile()) { setEqPresetError(QStringLiteral("Choose a local file for an existing track.")); return false; }
    std::string error;
    if (!EqPresetStore::saveToFile(url.toLocalFile().toStdString(), track->eqGainDb, &error)) {
        setEqPresetError(QString::fromStdString(error)); return false;
    }
    setEqPresetError({}); return true;
}
bool TrackListWrapper::loadEqPreset(int trackId, const QUrl& url) {
    if (!url.isLocalFile()) { setEqPresetError(QStringLiteral("Choose a local EQ preset.")); return false; }
    std::array<float,10> candidate;
    std::string error;
    if (!EqPresetStore::loadFromFile(url.toLocalFile().toStdString(), candidate, &error)) {
        setEqPresetError(QString::fromStdString(error)); return false;
    }
    return applyEqBands(trackId,candidate);
}

void TrackListWrapper::setSampleRate(double rate) { m_state.setSampleRate(rate); }

int TrackListWrapper::recordingTrackId() const { return m_state.recordingTrackId(); }

bool TrackListWrapper::attachTake(int id, std::int64_t start, std::shared_ptr<const AudioData> audio, bool printed)
{
    if (!m_state.attachTake(id, start, std::move(audio), printed)) return false;
    notifyClipEdit(id); return true;
}

std::unique_ptr<EngineSnapshot> TrackListWrapper::playbackSnapshot() const
{
    return SnapshotBuilder::buildPlaybackSnapshot(
        m_project,
        [this](const std::string& poolId) { return m_state.audioFor(poolId); },
        /*effectResolver=*/nullptr,
        /*multiMonitor=*/false);
}

QJsonObject TrackListWrapper::sessionState() const
{
    QJsonArray tracks;
    for (const auto& track : m_project.tracks()) {
        QJsonArray eq, clips;
        for (float value : track->eqGainDb) eq.append(value);
        for (const auto& clip : track->audioClips) {
            clips.append(QJsonObject{{"path", QString::fromStdString(clip.poolId)},
                {"hasAudio", m_mediaPaths.contains(clip.poolId)},
                {"start", QString::number(clip.start)}, {"length", QString::number(clip.length)},
                {"offset", QString::number(clip.sourceOffset)}, {"gainDb", clip.gainDb}, {"effectsPrinted", clip.effectsPrinted}});
        }
        tracks.append(QJsonObject{{"name", QString::fromStdString(track->name)},
            {"colour", QString::fromStdString(track->colour)},
            {"type", QString::fromStdString(track->type)}, {"instrument", track->kind == TrackKind::Instrument},
            {"solo", track->solo}, {"muted", track->muted}, {"volume", track->volume},
            {"input", QString::fromStdString(track->inputPort)}, {"recordDry", track->recordDry}, {"eq", eq}, {"clips", clips}});
    }
    const auto& tempo = m_project.tempo();
    return {{"version", 1}, {"tracks", tracks}, {"bpm", tempo.bpm()},
        {"numerator", tempo.numerator()}, {"denominator", tempo.denominator()}, {"sampleRate", tempo.sampleRate()}};
}

bool TrackListWrapper::restoreSessionState(const QJsonObject& state, QString& error)
{
    // Parse into a candidate first. Missing media is retained as an offline clip.
    Project candidate;
    TrackList::AudioMap audio;
    QStringList missing;
    TrackList::MediaPaths mediaPaths;
    const double bpm = state["bpm"].toDouble(-1), oldRate = state["sampleRate"].toDouble(-1);
    const int numerator = state["numerator"].toInt(), denominator = state["denominator"].toInt();
    if (state["version"].toInt() != 1 || !state["tracks"].isArray() || bpm < 0.01 || bpm > 10000
        || oldRate < 1000 || oldRate > 768000 || numerator <= 0 || denominator <= 0) {
        error = QStringLiteral("Invalid session document."); return false;
    }
    const double rate = m_project.tempo().sampleRate();
    candidate.tempo().set(bpm, numerator, denominator, rate);
    const auto tracks = state["tracks"].toArray();
    if (tracks.size() > 1024) { error = "Too many session tracks."; return false; }
    for (const auto& value : tracks) {
        if (!value.isObject()) { error = "Invalid session track."; return false; }
        const auto item = value.toObject();
        auto& track = candidate.addTrack(item["instrument"].toBool() ? TrackKind::Instrument : TrackKind::Audio,
            item["name"].toString().toStdString(), item["type"].toString().toStdString());
        track.recordDry = item["recordDry"].toBool(track.type=="Audio");
        if (item.contains("colour") && (!item["colour"].isString()
            || !candidate.setTrackColour(track.id, item["colour"].toString().toStdString()))) {
            error = "Invalid track colour."; return false;
        }
        track.solo = item["solo"].toBool(); track.muted = item["muted"].toBool();
        track.volume = float(item["volume"].toDouble(-1));
        track.inputPort = item["input"].toString().toStdString();
        const auto eq = item["eq"].toArray();
        if (track.volume < 0 || track.volume > 2 || eq.size() != 10 || !item["clips"].isArray()) {
            error = "Invalid track controls."; return false;
        }
        for (int i = 0; i < 10; ++i) {
            const double gain = eq[i].toDouble(1000);
            if (gain < -12 || gain > 12) { error = "Invalid track EQ."; return false; }
            track.eqGainDb[i] = float(gain);
        }
        for (const auto& value : item["clips"].toArray()) {
            const auto c = value.toObject(); AudioClip clip;
            bool okStart, okLength, okOffset;
            const qint64 start = c["start"].toString().toLongLong(&okStart);
            const qint64 length = c["length"].toString().toLongLong(&okLength);
            clip.sourceOffset = c["offset"].toString().toLongLong(&okOffset);
            if (!okStart || !okLength || !okOffset || start < 0 || length <= 0 || clip.sourceOffset < 0
                || double(start) + double(length) > double(INT64_MAX / 4) * std::min(1.0, oldRate/rate)) {
                error = "Invalid clip timing."; return false;
            }
            clip.start = std::llround(start * rate / oldRate); clip.length = std::max<std::int64_t>(1, std::llround(length * rate / oldRate));
            const QString path = c["path"].toString(); clip.poolId = path.toStdString();
            clip.effectsPrinted = c["effectsPrinted"].toBool(true);
            clip.gainDb = float(c["gainDb"].toDouble(0));
            if (!std::isfinite(clip.gainDb) || clip.gainDb < -120 || clip.gainDb > 24) { error = "Invalid clip gain."; return false; }
            if (c["hasAudio"].toBool()) mediaPaths.insert(path.toStdString());
            if (c["hasAudio"].toBool() && !audio.contains(path.toStdString())) {
                try { audio[path.toStdString()] = loadTakeWav(path.toStdString()); }
                catch (const std::exception&) { missing.append(QFileInfo(path).fileName()); }
            }
            if (candidate.addAudioClip(track.id, std::move(clip)) < 0) { error = "Invalid clip."; return false; }
        }
    }
    candidate.clearHistory();
    beginResetModel();
    m_state.replaceDocument(std::move(candidate), std::move(audio), std::move(mediaPaths));
    endResetModel();
    syncSelectionState();
    emit countChanged(); emit clipsChanged(); emit contentEndBeatsChanged();
    emit cleanMonitorChanged(cleanMonitorArmed()); emit cleanMonitorInputChanged();
    emit cleanMonitorGainChanged(cleanMonitorGain()); emit cleanMonitorEqChanged();
    emit masterVolumeChanged(); emit masterMutedChanged();
    error = missing.isEmpty() ? QString() : QStringLiteral("Missing audio (clips kept offline): %1").arg(missing.join(", "));
    return true;
}

void TrackListWrapper::replaceDocument(Project project, TrackList::AudioMap audio, TrackList::MediaPaths paths)
{
    beginResetModel();
    m_state.replaceDocument(std::move(project), std::move(audio), std::move(paths));
    endResetModel();
    syncSelectionState();
    emit countChanged(); emit clipsChanged(); emit contentEndBeatsChanged();
    emit cleanMonitorChanged(cleanMonitorArmed()); emit cleanMonitorInputChanged();
    emit cleanMonitorGainChanged(cleanMonitorGain()); emit cleanMonitorEqChanged();
    emit masterVolumeChanged(); emit masterMutedChanged();
}

void TrackListWrapper::adoptDocument(TrackListWrapper& candidate) {
    beginResetModel();
    m_state.adoptDocument(std::move(candidate.m_state));
    endResetModel();
    syncSelectionState();
    emit countChanged(); emit clipsChanged(); emit contentEndBeatsChanged();
    emit cleanMonitorChanged(cleanMonitorArmed()); emit cleanMonitorInputChanged(); emit cleanMonitorGainChanged(cleanMonitorGain()); emit cleanMonitorEqChanged();
    emit masterVolumeChanged(); emit masterMutedChanged();
}

QVariantList TrackListWrapper::trackChoices() const { return choices(TrackList::trackChoices()); }
QVariantList TrackListWrapper::instrumentChoices() const { return choices(TrackList::instrumentChoices()); }
int TrackListWrapper::addTrackChoice(const QString& choice)
{
    const auto id = choice.toStdString();
    if (!m_state.canAddChoice(id)) return -1;
    return appendTrack([this, &id] { return m_state.addTrackChoice(id); });
}
void TrackListWrapper::lockRecordingTracks(const QSet<int>& ids)
{
    m_state.lockRecordingTracks(std::unordered_set<int>(ids.begin(), ids.end()));
    emit historyChanged();
}
