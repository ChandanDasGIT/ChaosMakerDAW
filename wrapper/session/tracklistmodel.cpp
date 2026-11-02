#include "tracklistmodel.h"
#include "plugins/eq/eqpresets.h"
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

TrackListModel::TrackListModel(QObject* parent)
    : QAbstractListModel(parent)
{
    connect(this, &QAbstractItemModel::dataChanged, this, &TrackListModel::projectChanged);
    connect(this, &TrackListModel::countChanged, this, &TrackListModel::projectChanged);
    connect(this, &TrackListModel::clipsChanged, this, &TrackListModel::projectChanged);
    connect(this, &TrackListModel::clipsChanged, this, &TrackListModel::historyChanged);
}

int TrackListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_project.tracks().size());
}

QVariant TrackListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};

    const auto& track = *m_project.tracks()[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return QString::fromStdString(track.name);
    case TrackIdRole:
        return track.id;
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
        QVariantList clips;
        clips.reserve(static_cast<qsizetype>(track.audioClips.size()));
        for (const auto& clip : track.audioClips) {
            const auto endSample = clip.start + clip.length;
            const double startBeat = m_project.tempo().beatAt(clip.start);
            const double endBeat = m_project.tempo().beatAt(endSample);
            clips.push_back(QVariantMap{
                {QStringLiteral("clipId"), clip.id},
                {QStringLiteral("effectsPrinted"), clip.effectsPrinted},
                {QStringLiteral("startBeat"), startBeat},
                {QStringLiteral("lengthBeats"), endBeat - startBeat},
                {QStringLiteral("label"), clip.poolId.empty()
                     ? QStringLiteral("Audio clip") : QFileInfo(QString::fromStdString(clip.poolId)).fileName()}
            });
        }
        return clips;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    return {
        {TrackIdRole, "trackId"},
        {NameRole, "name"},
        {KindRole, "kind"},
        {TypeRole, "trackType"},
        {SoloRole, "isSolo"},
        {MutedRole, "isMuted"},
        {RecordArmedRole, "isRecordArmed"},
        {InputPortRole, "inputPort"},
        {VolumeRole, "volume"},
        {ClipsRole, "clips"},
        {EqBandsRole, "eqBands"},
        {LiveInputRole, "hasLiveInput"},
        {RecordsWithFxRole, "recordsWithFx"},
        {RecordDryRole, "recordDry"}
    };
}

QVariantList TrackListModel::clips() const
{
    QVariantList result;
    const auto& tracks = m_project.tracks();
    for (std::size_t row = 0; row < tracks.size(); ++row) {
        const auto& track = *tracks[row];
        for (const auto& clip : track.audioClips) {
            const double startBeat = m_project.tempo().beatAt(clip.start);
            const double endBeat = m_project.tempo().beatAt(clip.start + clip.length);
            result.push_back(QVariantMap{
                {QStringLiteral("clipId"), clip.id},
                {QStringLiteral("effectsPrinted"), clip.effectsPrinted},
                {QStringLiteral("trackId"), track.id},
                {QStringLiteral("laneIndex"), static_cast<int>(row)},
                {QStringLiteral("startBeat"), startBeat},
                {QStringLiteral("lengthBeats"), endBeat - startBeat},
                {QStringLiteral("label"), clip.poolId.empty()
                     ? QStringLiteral("Audio clip") : QFileInfo(QString::fromStdString(clip.poolId)).fileName()}
            });
        }
    }
    return result;
}

QVariantList TrackListModel::clipWaveform(int trackId, int clipId, int bins, double from, double to) const {
    QVariantList result;
    if (!std::isfinite(from) || !std::isfinite(to) || from < 0 || to > 1 || to <= from) return result;
    const auto* track = m_project.findTrack(trackId); if (!track) return result;
    for (const auto& clip : track->audioClips) if (clip.id == clipId) {
        const auto audio = m_audio.value(QString::fromStdString(clip.poolId)); if (!audio || clip.sourceOffset >= audio->frames) return result;
        const double length = clip.length * audio->sampleRate / m_project.tempo().sampleRate();
        const auto start = clip.sourceOffset + std::int64_t(length * from);
        const auto end = clip.sourceOffset + std::int64_t(length * to);
        for (const auto& peak : audio->waveform(start, end, bins))
            result.append(QVariant(QVariantList{peak[0], peak[1]}));
        break;
    }
    return result;
}

bool TrackListModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return false;

    const int trackId = m_project.tracks()[static_cast<std::size_t>(index.row())]->id;
    if (m_recordingTracks.contains(trackId) && role == InputPortRole) return false;
    bool changed = false;
    switch (role) {
    case SoloRole:
        changed = m_project.setTrackSolo(trackId, value.toBool());
        break;
    case MutedRole:
        changed = m_project.setTrackMuted(trackId, value.toBool());
        break;
    case RecordArmedRole:
        changed = m_project.setTrackRecordArmed(trackId, value.toBool());
        break;
    case InputPortRole:
        changed = m_project.setTrackInputPort(trackId, value.toString().toStdString());
        break;
    case VolumeRole:
        changed = m_project.setTrackVolume(trackId, value.toFloat());
        break;
    default:
        return false;
    }
    if (changed)
        emit dataChanged(index, index, {role});
    if (changed && role == RecordArmedRole)
        emit cleanMonitorChanged(cleanMonitorArmed());
    if (changed && (role == RecordArmedRole || role == InputPortRole))
        emit cleanMonitorInputChanged();
    if (changed && (role == RecordArmedRole || role == VolumeRole))
        emit cleanMonitorGainChanged(cleanMonitorGain());
    if (changed && role == RecordArmedRole)
        emit cleanMonitorEqChanged();
    return changed;
}

Qt::ItemFlags TrackListModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

int TrackListModel::appendTrack(TrackKind kind, const QString& name, const QString& type)
{
    const int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    Track& track = m_project.addTrack(kind, name.toStdString(), type.toStdString());
    const int trackId = track.id;
    endInsertRows();
    emit countChanged();
    return trackId;
}

int TrackListModel::addAudioTrack()
{
    return appendTrack(TrackKind::Audio, QStringLiteral("Audio %1").arg(m_nextAudioNumber++), QStringLiteral("Audio"));
}

int TrackListModel::addVocalTrack()
{
    return appendTrack(TrackKind::Audio, QStringLiteral("Vocal %1").arg(m_nextVocalNumber++), QStringLiteral("Vocal"));
}

int TrackListModel::addInstrumentTrack()
{
    return appendTrack(TrackKind::Instrument, QStringLiteral("Instrument %1").arg(m_nextInstrumentNumber++), QStringLiteral("Instrument"));
}

int TrackListModel::addInstrumentTrackOfType(const QString& instrumentType)
{
    static const QStringList supportedTypes{
        QStringLiteral("Guitar"), QStringLiteral("Acoustic Guitar"), QStringLiteral("Electric Guitar"),
        QStringLiteral("Strings"), QStringLiteral("Drums")
    };
    if (!supportedTypes.contains(instrumentType))
        return -1;
    const int number = ++m_instrumentNumbers[instrumentType];
    return appendTrack(TrackKind::Instrument,
                       QStringLiteral("%1 %2").arg(instrumentType).arg(number), instrumentType);
}

int TrackListModel::rowForTrackId(int trackId) const
{
    const auto& tracks = m_project.tracks();
    for (std::size_t row = 0; row < tracks.size(); ++row) {
        if (tracks[row]->id == trackId)
            return static_cast<int>(row);
    }
    return -1;
}

bool TrackListModel::removeTrack(int trackId)
{
    if (m_recordingTracks.contains(trackId)) return false;
    const int row = rowForTrackId(trackId);
    if (row < 0)
        return false;
    beginRemoveRows(QModelIndex(), row, row);
    m_project.removeTrack(trackId);
    endRemoveRows();
    emit countChanged();
    emit contentEndBeatsChanged();
    emit clipsChanged();
    emit cleanMonitorChanged(cleanMonitorArmed());
    emit cleanMonitorInputChanged();
    emit cleanMonitorGainChanged(cleanMonitorGain());
    emit cleanMonitorEqChanged();
    return true;
}

int TrackListModel::trackIdAt(int row) const
{
    if (row < 0 || row >= rowCount())
        return -1;
    return m_project.tracks()[static_cast<std::size_t>(row)]->id;
}

bool TrackListModel::notifyTrackChanged(int trackId, int role, bool changed)
{
    if (!changed)
        return false;
    const int row = rowForTrackId(trackId);
    if (row < 0)
        return false;
    const QModelIndex changedIndex = index(row, 0);
    emit dataChanged(changedIndex, changedIndex, {role});
    if (role == SoloRole || role == MutedRole) emit cleanMonitorGainChanged(cleanMonitorGain());
    return true;
}

bool TrackListModel::setSolo(int trackId, bool enabled)
{
    return notifyTrackChanged(trackId, SoloRole, m_project.setTrackSolo(trackId, enabled));
}

bool TrackListModel::setMuted(int trackId, bool enabled)
{
    return notifyTrackChanged(trackId, MutedRole, m_project.setTrackMuted(trackId, enabled));
}

bool TrackListModel::setRecordDry(int trackId, bool dry) {
    auto* track=m_project.findTrack(trackId);
    if(!track || !track->hasLiveInput() || m_recordingTracks.contains(trackId))return false;
    if(track->recordDry==dry)return true;
    track->recordDry=dry; const auto changed=index(rowForTrackId(trackId));
    emit dataChanged(changed,changed,{RecordDryRole,RecordsWithFxRole}); return true;
}

bool TrackListModel::setRecordArmed(int trackId, bool enabled)
{
    const bool changed = notifyTrackChanged(trackId, RecordArmedRole, m_project.setTrackRecordArmed(trackId, enabled));
    if (changed) {
        emit cleanMonitorChanged(cleanMonitorArmed());
        emit cleanMonitorInputChanged();
        emit cleanMonitorGainChanged(cleanMonitorGain());
        emit cleanMonitorEqChanged();
    }
    return changed;
}

bool TrackListModel::cleanMonitorArmed() const
{
    for (const auto& track : m_project.tracks()) {
        if (track->hasLiveInput() && track->recordArmed)
            return true;
    }
    return false;
}

QString TrackListModel::cleanMonitorInput() const
{
    for (const auto& track : m_project.tracks()) {
        if (track->hasLiveInput() && track->recordArmed)
            return QString::fromStdString(track->inputPort);
    }
    return {};
}

double TrackListModel::cleanMonitorGain() const
{
    for (const auto& track : m_project.tracks()) {
        if (track->hasLiveInput() && track->recordArmed)
            return (track->muted || (std::any_of(m_project.tracks().begin(), m_project.tracks().end(),
                [](const auto& t) { return t->solo; }) && !track->solo)) ? 0.0 : track->volume;
    }
    return 1.0;
}

QVariantList TrackListModel::cleanMonitorEqBands() const
{
    for (const auto& track : m_project.tracks()) {
        if (track->hasLiveInput() && track->recordArmed) {
            QVariantList result;
            for (float gain : track->eqGainDb)
                result.push_back(gain);
            return result;
        }
    }
    QVariantList flat;
    for (int i = 0; i < 10; ++i) flat.push_back(0.0);
    return flat;
}

bool TrackListModel::setEqBand(int trackId, int band, double gainDb)
{
    const int row = rowForTrackId(trackId);
    if (row < 0 || !std::isfinite(gainDb)
        || !m_project.setTrackEqBand(trackId, band, static_cast<float>(gainDb)))
        return false;
    const QModelIndex changedIndex = index(row, 0);
    emit dataChanged(changedIndex, changedIndex, {EqBandsRole});
    const Track* track = m_project.findTrack(trackId);
    if (track && track->hasLiveInput() && track->recordArmed)
        emit cleanMonitorEqChanged();
    return true;
}

bool TrackListModel::resetTrackEq(int trackId)
{
    return applyEqPreset(trackId, 0);
}

bool TrackListModel::setTrackInputPort(int trackId, const QString& port)
{
    if (m_recordingTracks.contains(trackId)) return false;
    const int row = rowForTrackId(trackId);
    Track* track = m_project.findTrack(trackId);
    if (row < 0 || !track)
        return false;
    const std::string selectedPort = port.toStdString();
    if (track->inputPort == selectedPort)
        return true;
    if (!m_project.setTrackInputPort(trackId, selectedPort))
        return false;
    const QModelIndex changedIndex = index(row, 0);
    emit dataChanged(changedIndex, changedIndex, {InputPortRole});
    if (cleanMonitorArmed())
        emit cleanMonitorInputChanged();
    return true;
}

bool TrackListModel::setVolume(int trackId, double volume)
{
    const bool changed = notifyTrackChanged(trackId, VolumeRole,
                                            m_project.setTrackVolume(trackId, static_cast<float>(volume)));
    if (changed && cleanMonitorArmed())
        emit cleanMonitorGainChanged(cleanMonitorGain());
    return changed;
}

int TrackListModel::addAudioClip(int trackId, const QString& poolId,
                                 qint64 startSample, qint64 lengthSamples,
                                 qint64 sourceOffset, bool effectsPrinted)
{
    AudioClip clip;
    clip.poolId = poolId.toStdString();
    clip.start = startSample;
    clip.length = lengthSamples;
    clip.sourceOffset = sourceOffset;
    clip.effectsPrinted = effectsPrinted;
    const int clipId = m_project.addAudioClip(trackId, std::move(clip));
    const int row = rowForTrackId(trackId);
    if (clipId >= 0 && row >= 0) {
        const QModelIndex changedIndex = index(row, 0);
        emit dataChanged(changedIndex, changedIndex, {ClipsRole});
        emit contentEndBeatsChanged();
        emit clipsChanged();
    }
    return clipId;
}

int TrackListModel::addClipAtBeat(int trackId, double startBeat, double lengthBeats)
{
    if (!std::isfinite(startBeat) || !std::isfinite(lengthBeats) || startBeat < 0.0 || lengthBeats <= 0.0)
        return -1;

    const double maxBeat = static_cast<double>(std::numeric_limits<qint64>::max())
                           * m_project.tempo().bpm() / (60.0 * m_project.tempo().sampleRate());
    if (startBeat + lengthBeats >= maxBeat)
        return -1;

    const qint64 startSample = m_project.tempo().sampleAt(startBeat);
    const qint64 endSample = m_project.tempo().sampleAt(startBeat + lengthBeats);
    if (endSample <= startSample)
        return -1;

    AudioClip clip;
    clip.poolId = QStringLiteral("Clip %1").arg(m_nextClipNumber++).toStdString();
    clip.start = startSample;
    clip.length = endSample - startSample;
    const int clipId = m_project.addAudioClip(trackId, std::move(clip));
    const int row = rowForTrackId(trackId);
    if (clipId >= 0 && row >= 0) {
        const QModelIndex changedIndex = index(row, 0);
        emit dataChanged(changedIndex, changedIndex, {ClipsRole});
        emit contentEndBeatsChanged();
        emit clipsChanged();
    }
    return clipId;
}

bool TrackListModel::trimClipStartAtBeat(int trackId, int clipId, double startBeat) {
    const double sample = startBeat * 60 * m_project.tempo().sampleRate() / m_project.tempo().bpm();
    if (!m_recordingTracks.isEmpty() || !std::isfinite(sample) || sample < 0 || sample >= double(INT64_MAX/4)) return false;
    const auto* track = m_project.findTrack(trackId); if (!track) return false;
    for (const auto& c : track->audioClips) if (c.id==clipId) {
        const auto audio=m_audio.value(QString::fromStdString(c.poolId));
        const double ratio=audio ? audio->sampleRate/m_project.tempo().sampleRate() : 1.0;
        const qint64 start=std::llround(sample);
        const double offset=c.sourceOffset+(start-c.start)*ratio;
        if (offset<0 || offset>=double(INT64_MAX/4) || start>=c.start+c.length) return false;
        if (!m_project.setClipBounds(trackId,clipId,start,c.start+c.length-start,std::llround(offset))) return false;
        notifyClipEdit(trackId); return true;
    }
    return false;
}
int TrackListModel::splitClipAtBeat(int trackId, int clipId, double beat) {
    const double sample=beat*60*m_project.tempo().sampleRate()/m_project.tempo().bpm();
    if (!m_recordingTracks.isEmpty() || !std::isfinite(sample) || sample<0 || sample>=double(INT64_MAX/4)) return -1;
    const auto* track=m_project.findTrack(trackId); if (!track) return -1;
    for (const auto& c : track->audioClips) if(c.id==clipId) {
        const auto audio=m_audio.value(QString::fromStdString(c.poolId));
        const double ratio=audio ? audio->sampleRate/m_project.tempo().sampleRate() : 1.0;
        const qint64 position=std::llround(sample);
        const double offset=c.sourceOffset+(position-c.start)*ratio;
        if(offset<0 || offset>=double(INT64_MAX/4))return -1;
        const int id=m_project.splitAudioClip(trackId,clipId,position,std::llround(offset));
        if(id>=0)notifyClipEdit(trackId);
        return id;
    }
    return -1;
}

bool TrackListModel::moveClipAtBeat(int trackId, int clipId, double startBeat) {
    const double samples = startBeat * 60.0 * m_project.tempo().sampleRate() / m_project.tempo().bpm();
    if (!m_recordingTracks.isEmpty() || !std::isfinite(samples) || samples < 0 || samples >= double(INT64_MAX / 4)) return false;
    if (!m_project.moveAudioClip(trackId, clipId, std::llround(samples))) return false;
    notifyClipEdit(trackId); return true;
}

bool TrackListModel::resizeClipAtBeat(int trackId, int clipId, double lengthBeats)
{
    if (!m_recordingTracks.isEmpty() || !std::isfinite(lengthBeats) || lengthBeats <= 0.0)
        return false;
    const Track* track = m_project.findTrack(trackId);
    if (!track)
        return false;
    const auto clip = std::find_if(track->audioClips.begin(), track->audioClips.end(), [clipId](const AudioClip& item) {
        return item.id == clipId;
    });
    if (clip == track->audioClips.end())
        return false;

    const double startBeat = m_project.tempo().beatAt(clip->start);
    const double maxBeat = static_cast<double>(std::numeric_limits<qint64>::max())
                           * m_project.tempo().bpm() / (60.0 * m_project.tempo().sampleRate());
    if (startBeat + lengthBeats >= maxBeat)
        return false;
    const qint64 endSample = m_project.tempo().sampleAt(startBeat + lengthBeats);
    const auto audio=m_audio.value(QString::fromStdString(clip->poolId));
    if (audio && endSample-clip->start > std::llround((audio->frames-clip->sourceOffset)*m_project.tempo().sampleRate()/audio->sampleRate)) return false;
    if (endSample <= clip->start || !m_project.resizeAudioClip(trackId, clipId, endSample - clip->start))
        return false;

    const int row = rowForTrackId(trackId);
    if (row >= 0) {
        const QModelIndex changedIndex = index(row, 0);
        emit dataChanged(changedIndex, changedIndex, {ClipsRole});
        emit contentEndBeatsChanged();
        emit clipsChanged();
    }
    return true;
}

bool TrackListModel::removeAudioClip(int trackId, int clipId)
{
    if (!m_project.removeAudioClip(trackId, clipId))
        return false;
    const int row = rowForTrackId(trackId);
    if (row >= 0) {
        const QModelIndex changedIndex = index(row, 0);
        emit dataChanged(changedIndex, changedIndex, {ClipsRole});
        emit contentEndBeatsChanged();
        emit clipsChanged();
    }
    return true;
}

void TrackListModel::notifyClipEdit(int trackId) {
    const auto changed = index(rowForTrackId(trackId));
    emit dataChanged(changed, changed, {ClipsRole});
    emit contentEndBeatsChanged();
    emit clipsChanged();
}
bool TrackListModel::undo() {
    if (!canUndo()) return false;
    notifyClipEdit(m_project.undo()); return true;
}
bool TrackListModel::redo() {
    if (!canRedo()) return false;
    notifyClipEdit(m_project.redo()); return true;
}

void TrackListModel::setTempo(double bpm, int numerator, int denominator)
{
    if (!std::isfinite(bpm) || bpm <= 0.0 || numerator <= 0 || denominator <= 0)
        return;
    m_project.tempo().set(bpm, numerator, denominator, m_project.tempo().sampleRate());
    emit contentEndBeatsChanged();
    emit clipsChanged();
}

void TrackListModel::setEqPresetError(const QString& error) {
    m_eqPresetError = error; emit eqPresetErrorChanged();
}
QStringList TrackListModel::eqPresetNames() const {
    QStringList names;
    for (const auto& preset : eqpresets::factory) names.push_back(QString::fromUtf8(preset.name));
    return names;
}
bool TrackListModel::applyEqBands(int trackId, const std::array<float,10>& bands) {
    auto* track = m_project.findTrack(trackId);
    if (!track) { setEqPresetError(QStringLiteral("Track is unavailable.")); return false; }
    for (int i=0;i<10;++i) m_project.setTrackEqBand(trackId,i,bands[i]);
    const auto changed = index(rowForTrackId(trackId));
    emit dataChanged(changed,changed,{EqBandsRole});
    if (track->hasLiveInput() && track->recordArmed) emit cleanMonitorEqChanged();
    setEqPresetError({}); return true;
}
bool TrackListModel::applyEqPreset(int trackId, int preset) {
    if (preset < 0 || preset >= int(eqpresets::factory.size())) return false;
    return applyEqBands(trackId,eqpresets::factory[preset].gains);
}
bool TrackListModel::saveEqPreset(int trackId, const QUrl& url) {
    const auto* track = m_project.findTrack(trackId);
    if (!track || !url.isLocalFile()) { setEqPresetError(QStringLiteral("Choose a local file for an existing track.")); return false; }
    QJsonArray gains, frequencies;
    for (float gain : track->eqGainDb) gains.push_back(gain);
    for (float hz : TenBandEq::frequencies()) frequencies.push_back(hz);
    QSaveFile file(url.toLocalFile());
    const auto bytes = QJsonDocument(QJsonObject{{"format","ChaosMakerEQ"},{"version",1},
        {"frequenciesHz",frequencies},{"gainsDb",gains}}).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setEqPresetError(QStringLiteral("Could not save EQ preset: ") + file.errorString()); return false;
    }
    setEqPresetError({}); return true;
}
bool TrackListModel::loadEqPreset(int trackId, const QUrl& url) {
    if (!url.isLocalFile()) { setEqPresetError(QStringLiteral("Choose a local EQ preset.")); return false; }
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64*1024) {
        setEqPresetError(QStringLiteral("Could not read EQ preset.")); return false;
    }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(),&error);
    const auto root = doc.object();
    const auto gains = root.value("gainsDb").toArray();
    const auto hz = root.value("frequenciesHz").toArray();
    if (error.error != QJsonParseError::NoError || root.value("format").toString() != "ChaosMakerEQ"
        || root.value("version").toInt() != 1 || gains.size() != 10 || hz.size() != 10) {
        setEqPresetError(QStringLiteral("Invalid or unsupported EQ preset.")); return false;
    }
    std::array<float,10> candidate;
    for (int i=0;i<10;++i) {
        const double value = gains[i].toDouble(1000);
        if (!gains[i].isDouble() || !std::isfinite(value) || value < -12 || value > 12
            || !hz[i].isDouble() || hz[i].toDouble() != TenBandEq::frequencies()[i]) {
            setEqPresetError(QStringLiteral("EQ preset must contain ten matching bands within ±12 dB.")); return false;
        }
        candidate[i]=float(value);
    }
    return applyEqBands(trackId,candidate);
}

void TrackListModel::setSampleRate(double rate)
{
    if (std::isfinite(rate) && rate >= 1000 && rate <= 768000) {
        const auto& t = m_project.tempo();
        m_project.tempo().set(t.bpm(), t.numerator(), t.denominator(), rate);
    }
}

int TrackListModel::recordingTrackId() const
{
    int selected = -1;
    for (const auto& track : m_project.tracks()) {
        if (!track->recordArmed) continue;
        if (!track->hasLiveInput()) return -1;
        if (selected != -1) return -1; // Compatibility query: use the full armed-track list for recording.
        selected = track->id;
    }
    return selected;
}

bool TrackListModel::attachTake(int trackId, std::int64_t start, std::shared_ptr<const AudioData> audio, bool effectsPrinted)
{
    if (!audio || audio->frames <= 0 || !m_project.findTrack(trackId)) return false;
    const QString path = QString::fromStdString(audio->path);
    m_audio[path] = audio; m_mediaPaths.insert(path);
    return addAudioClip(trackId, path, start, std::llround(audio->frames * m_project.tempo().sampleRate() / audio->sampleRate), 0, effectsPrinted) >= 0;
}

std::unique_ptr<EngineSnapshot> TrackListModel::playbackSnapshot() const
{
    auto snapshot = std::make_unique<EngineSnapshot>();
    snapshot->tempo = m_project.tempo();
    for (const auto& track : m_project.tracks()) {
        TrackRender render;
        render.trackId = track->id; render.gain = track->volume;
        render.muted = track->muted; render.soloed = track->solo;
        for (const auto& clip : track->audioClips) {
            auto audio = m_audio.value(QString::fromStdString(clip.poolId));
            if (!audio) continue;
            render.clips.push_back({audio.get(), clip.start, clip.length, clip.sourceOffset,
                                    clip.fadeIn, clip.fadeOut, float(std::pow(10, clip.gainDb / 20)), clip.effectsPrinted});
            snapshot->keepAudio.push_back(std::move(audio));
        }
        snapshot->tracks.push_back(std::move(render));
    }
    return snapshot;
}

QJsonObject TrackListModel::sessionState() const
{
    QJsonArray tracks;
    for (const auto& track : m_project.tracks()) {
        QJsonArray eq, clips;
        for (float value : track->eqGainDb) eq.append(value);
        for (const auto& clip : track->audioClips) {
            clips.append(QJsonObject{{"path", QString::fromStdString(clip.poolId)},
                {"hasAudio", m_mediaPaths.contains(QString::fromStdString(clip.poolId))},
                {"start", QString::number(clip.start)}, {"length", QString::number(clip.length)},
                {"offset", QString::number(clip.sourceOffset)}, {"gainDb", clip.gainDb}, {"effectsPrinted", clip.effectsPrinted}});
        }
        tracks.append(QJsonObject{{"name", QString::fromStdString(track->name)},
            {"type", QString::fromStdString(track->type)}, {"instrument", track->kind == TrackKind::Instrument},
            {"solo", track->solo}, {"muted", track->muted}, {"volume", track->volume},
            {"input", QString::fromStdString(track->inputPort)}, {"recordDry", track->recordDry}, {"eq", eq}, {"clips", clips}});
    }
    const auto& tempo = m_project.tempo();
    return {{"version", 1}, {"tracks", tracks}, {"bpm", tempo.bpm()},
        {"numerator", tempo.numerator()}, {"denominator", tempo.denominator()}, {"sampleRate", tempo.sampleRate()}};
}

bool TrackListModel::restoreSessionState(const QJsonObject& state, QString& error)
{
    // Parse into a candidate first. Missing media is retained as an offline clip.
    Project candidate;
    QHash<QString, std::shared_ptr<const AudioData>> audio;
    QStringList missing;
    QSet<QString> mediaPaths;
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
            if (c["hasAudio"].toBool()) mediaPaths.insert(path);
            if (c["hasAudio"].toBool() && !audio.contains(path)) {
                try { audio[path] = loadTakeWav(path.toStdString()); }
                catch (const std::exception&) { missing.append(QFileInfo(path).fileName()); }
            }
            if (candidate.addAudioClip(track.id, std::move(clip)) < 0) { error = "Invalid clip."; return false; }
        }
    }
    candidate.clearHistory();
    beginResetModel(); m_project = std::move(candidate); m_audio = std::move(audio); m_mediaPaths = std::move(mediaPaths);
    m_nextAudioNumber = m_nextVocalNumber = m_nextInstrumentNumber = count() + 1;
    m_nextClipNumber = clips().size() + 1;
    m_instrumentNumbers.clear();
    for (const auto& track : m_project.tracks()) m_instrumentNumbers[QString::fromStdString(track->type)]++;
    endResetModel();
    emit countChanged(); emit clipsChanged(); emit contentEndBeatsChanged();
    emit cleanMonitorChanged(false); emit cleanMonitorInputChanged();
    emit cleanMonitorGainChanged(cleanMonitorGain()); emit cleanMonitorEqChanged();
    error = missing.isEmpty() ? QString() : QStringLiteral("Missing audio (clips kept offline): %1").arg(missing.join(", "));
    return true;
}

void TrackListModel::adoptDocument(TrackListModel& candidate) {
    beginResetModel();
    m_project=std::move(candidate.m_project); m_audio=std::move(candidate.m_audio); m_mediaPaths=std::move(candidate.m_mediaPaths);
    m_nextAudioNumber=candidate.m_nextAudioNumber; m_nextVocalNumber=candidate.m_nextVocalNumber;
    m_nextInstrumentNumber=candidate.m_nextInstrumentNumber; m_nextClipNumber=candidate.m_nextClipNumber;
    m_instrumentNumbers=std::move(candidate.m_instrumentNumbers); m_recordingTracks.clear();
    m_project.clearHistory(); endResetModel();
    emit countChanged(); emit clipsChanged(); emit contentEndBeatsChanged();
    emit cleanMonitorChanged(false); emit cleanMonitorInputChanged(); emit cleanMonitorGainChanged(cleanMonitorGain()); emit cleanMonitorEqChanged();
}
