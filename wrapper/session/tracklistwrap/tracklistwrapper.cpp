#include "tracklistwrapper.h"
#include "timeline/clipwaveform.h"
#include "engine/snapshotbuilder.h"
#include "mixer/mixerpanel.h"
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
#include <filesystem>
#include <QPointF>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <limits>


namespace {
QVariantList effectList(const EffectState& track);
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
        {"isRecorded", clip.isRecorded}, {"notes", noteList(clip.notes)},
        {"reversed", clip.reversed}, {"polarityInverted", clip.polarityInverted},
        {"hasEffects", clip.fx.hasEffects()},
        {"fadeInBeats", clip.fadeInBeats}, {"fadeOutBeats", clip.fadeOutBeats},
        {"takeCount", clip.takeCount}, {"activeTake", clip.activeTake}};   // restMap G11
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
    connect(this, &TrackListWrapper::historyChanged, this, &TrackListWrapper::syncUndoHistory);
    connect(this, &TrackListWrapper::projectChanged, this, &TrackListWrapper::syncUndoHistory);
    connect(this, &QAbstractItemModel::modelReset, this, &TrackListWrapper::syncUndoHistory);
    // The selected track's key shift follows a new selection, its harmonium settings and loads.
    connect(this, &TrackListWrapper::selectedTrackChanged, this, &TrackListWrapper::syncKeyShift);
    connect(this, &TrackListWrapper::harmoniumChanged, this, &TrackListWrapper::syncKeyShift);
    connect(this, &TrackListWrapper::pianoChanged, this, &TrackListWrapper::syncKeyShift);
    connect(this, &TrackListWrapper::projectChanged, this, &TrackListWrapper::syncKeyShift);
    // The row layout follows the rows and each track's envelope lane (restMap G6).
    connect(this, &QAbstractItemModel::dataChanged, this, &TrackListWrapper::syncLayout);
    connect(this, &QAbstractItemModel::rowsInserted, this, &TrackListWrapper::syncLayout);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &TrackListWrapper::syncLayout);
    connect(this, &QAbstractItemModel::rowsMoved, this, &TrackListWrapper::syncLayout);
    connect(this, &QAbstractItemModel::modelReset, this, &TrackListWrapper::syncLayout);
    // A new or opened project has its own master effects (G9).
    connect(this, &QAbstractItemModel::modelReset, this, &TrackListWrapper::masterChanged);
}

void TrackListWrapper::syncLayout()
{
    std::vector<bool> lanes;
    lanes.reserve(m_project.tracks().size());
    for (const auto& track : m_project.tracks())
        lanes.push_back(track->volumeEnvelope.shown() && !track->volumeEnvelope.overClips());
    if (lanes.size() == static_cast<std::size_t>(m_layout.count())) {
        bool same = true;
        for (int i = 0; i < m_layout.count() && same; ++i) same = lanes[std::size_t(i)] == m_layout.hasEnvelopeLane(i);
        if (same) return;
    }
    m_layout.setRows(std::move(lanes));
    emit layoutChanged();
}

void TrackListWrapper::setTrackRowHeight(double height)
{
    if (height == m_layout.trackHeight()) return;
    m_layout.setTrackHeight(height);
    emit layoutChanged();
}

void TrackListWrapper::setEnvelopeLaneHeight(double height)
{
    if (height == m_layout.envelopeHeight()) return;
    m_layout.setEnvelopeHeight(height);
    emit layoutChanged();
}

QVariantList TrackListWrapper::rowTops() const
{
    QVariantList result;
    for (double top : m_layout.rowTops()) result.append(top);
    return result;
}

void TrackListWrapper::notifyEnvelope(int trackId, const QList<int>& roles)
{
    const int row = rowForTrackId(trackId);
    if (row >= 0) emit dataChanged(index(row), index(row), roles);
}

int TrackListWrapper::addEnvelopePoint(int trackId, double beat, double level)
{
    const int point = m_state.addEnvelopePointAtBeat(trackId, beat, level);
    if (point >= 0) { notifyEnvelope(trackId, {EnvelopePointsRole}); emit historyChanged(); }
    return point;
}

bool TrackListWrapper::moveEnvelopePoint(int trackId, int point, double beat, double level, bool continueEdit)
{
    const auto change = m_state.moveEnvelopePointAtBeat(trackId, point, beat, level, continueEdit);
    if (change.changed) { notifyEnvelope(trackId, {EnvelopePointsRole}); emit historyChanged(); }
    return change.accepted;
}

bool TrackListWrapper::removeEnvelopePoint(int trackId, int point)
{
    const auto change = m_state.removeEnvelopePoint(trackId, point);
    if (change.changed) { notifyEnvelope(trackId, {EnvelopePointsRole}); emit historyChanged(); }
    return change.accepted;
}

bool TrackListWrapper::setEnvelopePointShape(int trackId, int point, const QString& shape)
{
    const auto known = automation::curveShapeFromId(shape.toStdString());
    if (!known) return false;
    const auto change = m_state.setEnvelopePointShape(trackId, point, *known);
    if (change.changed) { notifyEnvelope(trackId, {EnvelopePointsRole}); emit historyChanged(); }
    return change.accepted;
}

QVariantList TrackListWrapper::envelopeShapes() const
{
    static const char* const names[automation::curveShapeCount] = {"Linear", "Smooth", "Fast start", "Fast end", "Hold"};
    QVariantList result;
    for (int i = 0; i < automation::curveShapeCount; ++i) {
        const auto id = automation::curveShapeId(static_cast<automation::CurveShape>(i));
        result.append(QVariantMap{{QStringLiteral("id"), QString::fromUtf8(id.data(), qsizetype(id.size()))},
                                  {QStringLiteral("name"), QString::fromUtf8(names[i])}});
    }
    return result;
}

double TrackListWrapper::envelopeLevelAt(int trackId, double beat) const
{
    return m_state.envelopeLevelAtBeat(trackId, beat);
}

QVariantList TrackListWrapper::envelopeCurve(int trackId, double fromBeat, double toBeat, int steps) const
{
    QVariantList result;
    for (const auto& sample : m_state.envelopeCurve(trackId, fromBeat, toBeat, steps))
        result.append(QPointF(sample.beat, sample.level));
    return result;
}

bool TrackListWrapper::setEnvelopeBypassed(int trackId, bool bypassed)
{
    const auto change = m_state.setEnvelopeBypassed(trackId, bypassed);
    if (change.changed) { notifyEnvelope(trackId, {EnvelopeBypassedRole}); emit historyChanged(); }
    return change.accepted;
}

bool TrackListWrapper::setEnvelopeShown(int trackId, bool shown)
{
    const auto change = m_state.setEnvelopeShown(trackId, shown);
    // Showing may also add the start point (restMap G6).
    if (change.changed) notifyEnvelope(trackId, {EnvelopeShownRole, EnvelopePointsRole});
    return change.accepted;
}

bool TrackListWrapper::toggleEnvelopeShown(int trackId)
{
    const auto* track = m_project.findTrack(trackId);
    return track && setEnvelopeShown(trackId, !track->volumeEnvelope.shown());
}

bool TrackListWrapper::setEnvelopeOverClips(int trackId, bool overClips)
{
    const auto change = m_state.setEnvelopeOverClips(trackId, overClips);
    if (change.changed) notifyEnvelope(trackId, {EnvelopeOverClipsRole});
    return change.accepted;
}

double TrackListWrapper::envelopeDb(double level) const
{
    return automation::envelopescale::dbForLevel(level);
}

QString TrackListWrapper::envelopeDbText(double level) const
{
    const double db = envelopeDb(level);
    if (!std::isfinite(db) || db <= -150.0) return QStringLiteral("-inf dB");
    const double shown = std::round(db * 10.0) / 10.0;
    return (shown > 0 ? QStringLiteral("+") : QString()) + QString::number(shown == 0 ? 0.0 : shown, 'f', 1)
        + QStringLiteral(" dB");
}

double TrackListWrapper::envelopeUnityLevel() const
{
    return automation::envelopescale::unityLevel();
}

int TrackListWrapper::selectedKeyShift() const
{
    const auto* track = m_project.findTrack(m_state.selectedTrackId());
    return track ? track->keyShift() : 0;
}

void TrackListWrapper::syncKeyShift()
{
    const int current = selectedKeyShift();
    if (m_notifiedKeyShift == current) return;
    m_notifiedKeyShift = current;
    emit selectedKeyShiftChanged(current);
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
    case CanMonitorRole:
        return track.canMonitor();
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
    case PanRole:
        return track.pan;
    case StripWidthRole:
        return track.stripWidth;
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
    case EffectsRole:
        return effectList(track);
    case EnvelopeShownRole:
        return track.volumeEnvelope.shown();
    case EnvelopeOverClipsRole:
        return track.volumeEnvelope.overClips();
    case EnvelopeBypassedRole:
        return track.volumeEnvelope.bypassed();
    case EnvelopePointsRole: {
        QVariantList result;
        for (const auto& point : m_state.envelopePointBeats(track.id))
        {
            const auto shape = automation::curveShapeId(point.shape);
            result.append(QVariantMap{{QStringLiteral("beat"), point.beat}, {QStringLiteral("level"), point.level},
                                      {QStringLiteral("shape"), QString::fromUtf8(shape.data(), qsizetype(shape.size()))}});
        }
        return result;
    }
    default:
        return {};
    }
}

namespace {
// A track's (or the master's, or a clip's) insert effects for QML: [{id, type, title,
// subtitle, removable}] in chain order.
QVariantList effectList(const EffectState& track)
{
    QVariantList result;
    for (const auto& slot : track.effects) {
        const auto* effect = effectchain::find(slot.type);
        result.append(QVariantMap{
            {QStringLiteral("id"), slot.id},
            {QStringLiteral("type"), QString::fromStdString(slot.type)},
            {QStringLiteral("title"), effect ? QString::fromUtf8(effect->title.data(), qsizetype(effect->title.size())) : QString()},
            {QStringLiteral("subtitle"), effect ? QString::fromUtf8(effect->subtitle.data(), qsizetype(effect->subtitle.size())) : QString()},
            {QStringLiteral("removable"), effectchain::canRemove(slot.type)}});
    }
    return result;
}
}

QVariantList TrackListWrapper::masterEffects() const { return effectList(m_project.master()); }

QVariantList TrackListWrapper::masterEqBands() const
{
    QVariantList bands;
    for (float gain : m_project.master().eqGainDb) bands.push_back(gain);
    return bands;
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
        {PanRole, "pan"},
        {StripWidthRole, "stripWidth"},
        {ClipsRole, "clips"},
        {EqBandsRole, "eqBands"},
        {EffectsRole, "effects"},
        {LiveInputRole, "hasLiveInput"},
        {CanMonitorRole, "canMonitor"},
        {RecordsWithFxRole, "recordsWithFx"},
        {RecordDryRole, "recordDry"},
        {EnvelopeShownRole, "envelopeShown"},
        {EnvelopeOverClipsRole, "envelopeOverClips"},
        {EnvelopeBypassedRole, "envelopeBypassed"},
        {EnvelopePointsRole, "envelopePoints"}
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
    const auto audio = clip ? m_state.clipAudio(*clip) : nullptr;   // reversed clips: their reversed copy
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
    case PanRole: return setPan(id, value.toDouble());
    case StripWidthRole: return setStripWidth(id, value.toDouble());
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

bool TrackListWrapper::setClipGain(int trackId, int clipId, double gainDb, bool continueEdit)
{
    const auto change = m_state.setClipGain(trackId, clipId, gainDb, continueEdit);
    if (change.changed) {
        notifyClipGainEdit(trackId);
    }
    return change.accepted;
}

bool TrackListWrapper::setClipFade(int trackId, int clipId, bool fadeOut, double beats, bool continueEdit)
{
    const auto change = m_state.setClipFade(trackId, clipId, fadeOut, beats, continueEdit);
    if (change.changed) notifyClipEdit(trackId);
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
    notifyTrackCountChanged();
    return true;
}

void TrackListWrapper::notifyTrackCountChanged()
{
    syncSelectionState();
    emit countChanged(); emit contentEndBeatsChanged(); emit clipsChanged();
    emit cleanMonitorChanged(cleanMonitorArmed()); emit cleanMonitorInputChanged();
    emit cleanMonitorGainChanged(cleanMonitorGain()); emit cleanMonitorEqChanged();
}

int TrackListWrapper::trackIdAt(int row) const { return m_state.trackIdAt(row); }

bool TrackListWrapper::notifyTrackChanged(int id, int role, TrackList::Change change)
{
    if (change.changed && id == Project::MasterTrackId) {   // the master has no row (G9)
        emit masterChanged();
        emit projectChanged();
        return change.accepted;
    }
    if (change.changed && Project::isClipEffectsId(id)) {    // a clip's effects (R2): its data
        notifyClipEdit(-1);
        return change.accepted;
    }
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

const chaoseq::Settings* TrackListWrapper::chaosEq(int trackId) const
{
    const auto* track = m_project.effects(trackId);   // a track, the master or a clip
    return track ? &track->chaosEq : nullptr;
}

std::optional<mixfx::Settings> TrackListWrapper::mixEffect(int trackId, std::string_view type) const
{
    return m_state.mixEffect(trackId, type);
}

QString TrackListWrapper::learnNoise(int effectsId, double startBeat, double endBeat)
{
    const auto before = m_state.mixEffect(effectsId, mixfx::Denoise);
    const auto error = m_state.learnNoise(effectsId, startBeat, endBeat);
    if (error.empty() && before != m_state.mixEffect(effectsId, mixfx::Denoise))
        emit mixEffectChanged(effectsId, QString::fromUtf8(mixfx::Denoise.data(), qsizetype(mixfx::Denoise.size())));
    return QString::fromStdString(error);
}

bool TrackListWrapper::forgetNoise(int effectsId)
{
    auto settings = m_state.mixEffect(effectsId, mixfx::Denoise);
    if (!settings) return false;
    settings->profile.clear();
    return setMixEffect(effectsId, QString::fromUtf8(mixfx::Denoise.data(), qsizetype(mixfx::Denoise.size())), *settings);
}

bool TrackListWrapper::setMixEffect(int trackId, const QString& type, const mixfx::Settings& settings)
{
    const auto change = m_state.setMixEffect(trackId, type.toStdString(), settings);
    if (change.changed) emit mixEffectChanged(trackId, type);
    return change.accepted;
}

bool TrackListWrapper::isMixEffect(const QString& type) const
{
    return mixfx::find(type.toStdString()) != nullptr;
}

bool TrackListWrapper::setChaosEq(int trackId, const chaoseq::Settings& settings)
{
    const auto change = m_state.setChaosEq(trackId, settings);
    if (change.changed) emit chaosEqChanged(trackId);
    return change.accepted;
}

const drumkit::Settings* TrackListWrapper::drumKit(int trackId) const
{
    const auto* track = m_project.findTrack(trackId);
    return track && track->type == "Drums" ? &track->drumkit : nullptr;
}

bool TrackListWrapper::setDrumKit(int trackId, const drumkit::Settings& settings)
{
    if (!drumKit(trackId)) return false;
    const auto before = *drumKit(trackId);
    const auto change = m_state.setDrumKit(trackId, settings);
    if (change.accepted && !(before == *drumKit(trackId))) emit drumKitChanged(trackId);
    return change.accepted;
}

const piano::Settings* TrackListWrapper::piano(int trackId) const
{
    const auto* track = m_project.findTrack(trackId);
    return track && (track->type == "Piano" || track->type == "Chords") ? &track->piano : nullptr;
}

const chords::Settings* TrackListWrapper::chords(int trackId) const
{
    const auto* track = m_project.findTrack(trackId);
    return track && track->type == "Chords" ? &track->chords : nullptr;
}

bool TrackListWrapper::setChords(int trackId, const chords::Settings& settings)
{
    if (!chords(trackId)) return false;
    const auto before = *chords(trackId);
    const auto change = m_state.setChords(trackId, settings);
    if (change.accepted && !(before == *chords(trackId))) emit chordsChanged(trackId);
    return change.accepted;
}

bool TrackListWrapper::setPiano(int trackId, const piano::Settings& settings)
{
    if (!piano(trackId)) return false;
    const auto before = *piano(trackId);
    const auto change = m_state.setPiano(trackId, settings);
    if (change.accepted && !(before == *piano(trackId))) emit pianoChanged(trackId);
    return change.accepted;
}

const electrodrums::Settings* TrackListWrapper::electroDrums(int trackId) const
{
    const auto* track = m_project.findTrack(trackId);
    return track && track->type == "Electronic Drums" ? &track->electrodrums : nullptr;
}

bool TrackListWrapper::setElectroDrums(int trackId, const electrodrums::Settings& settings)
{
    if (!electroDrums(trackId)) return false;
    const auto before = *electroDrums(trackId);
    const auto change = m_state.setElectroDrums(trackId, settings);
    if (change.accepted && !(before == *electroDrums(trackId))) emit electroDrumsChanged(trackId);
    return change.accepted;
}

const synth::Settings* TrackListWrapper::synth(int trackId) const
{
    const auto* track = m_project.findTrack(trackId);
    return track && track->type == "Synth" ? &track->synth : nullptr;
}

bool TrackListWrapper::setSynth(int trackId, const synth::Settings& settings)
{
    if (!synth(trackId)) return false;
    const auto before = *synth(trackId);
    const auto change = m_state.setSynth(trackId, settings);
    if (change.accepted && !(before == *synth(trackId))) emit synthChanged(trackId);
    return change.accepted;
}

const harmonium::Settings* TrackListWrapper::harmonium(int trackId) const
{
    const auto* track = m_project.findTrack(trackId);
    return track && track->type == "Harmonium" ? &track->harmonium : nullptr;
}

bool TrackListWrapper::setHarmonium(int trackId, const harmonium::Settings& settings)
{
    if (!harmonium(trackId)) return false;
    const auto before = *harmonium(trackId);
    const auto change = m_state.setHarmonium(trackId, settings);
    if (change.accepted && !(before == *harmonium(trackId))) emit harmoniumChanged(trackId);
    return change.accepted;
}

bool TrackListWrapper::setEqBand(int id, int band, double gain)
{
    const auto change = m_state.setEqBand(id, band, gain);
    notifyTrackChanged(id, EqBandsRole, change);
    if (change.changed && m_state.isMonitored(id)) emit cleanMonitorEqChanged();
    return change.accepted;
}

void TrackListWrapper::setNewTrackRecordDry(bool dry)
{
    m_state.setNewTrackRecordDry(dry);
    if (m_newTrackRecordDry == dry) return;
    m_newTrackRecordDry = dry;
    emit newTrackRecordDryChanged();
}

int TrackListWrapper::addTrackEffect(int trackId, const QString& type)
{
    const int id = m_state.addTrackEffect(trackId, type.toStdString());
    notifyTrackChanged(trackId, EffectsRole, {id > 0, id > 0});
    return id;
}

bool TrackListWrapper::removeTrackEffect(int trackId, int effectId)
{
    return notifyTrackChanged(trackId, EffectsRole, m_state.removeTrackEffect(trackId, effectId));
}

bool TrackListWrapper::moveTrackEffect(int trackId, int effectId, int toIndex)
{
    return notifyTrackChanged(trackId, EffectsRole, m_state.moveTrackEffect(trackId, effectId, toIndex));
}

QVariantList TrackListWrapper::addableEffects(int trackId) const
{
    QVariantList result;
    const auto* track = m_project.effects(trackId);   // a track, the master or a clip
    if (!track || m_state.trackLocked(trackId)) return result;
    const auto text = [](std::string_view s) { return QString::fromUtf8(s.data(), qsizetype(s.size())); };
    for (const auto type : effectchain::addableEffects(track->effects)) {
        const auto* effect = effectchain::find(type);
        result.append(QVariantMap{{QStringLiteral("type"), text(type)}, {QStringLiteral("title"), text(effect->title)},
                                  {QStringLiteral("subtitle"), text(effect->subtitle)}});
    }
    return result;
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

bool TrackListWrapper::setPan(int id, double value)
{
    return notifyTrackChanged(id, PanRole, m_state.setPan(id, value));
}

bool TrackListWrapper::setStripWidth(int id, double value)
{
    return notifyTrackChanged(id, StripWidthRole, m_state.setStripWidth(id, value));
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

int TrackListWrapper::addPatternClipAtBeat(int id, double start, double length, std::vector<MidiNote> notes)
{
    const int clip = m_state.addPatternClipAtBeat(id, start, length, std::move(notes));
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

namespace {
std::optional<TrackList::ClipTool> clipTool(const QString& name)
{
    if (name == QLatin1String("reverse")) return TrackList::ClipTool::Reverse;
    if (name == QLatin1String("invertPolarity")) return TrackList::ClipTool::InvertPolarity;
    if (name == QLatin1String("normalise")) return TrackList::ClipTool::Normalise;
    return std::nullopt;
}
}

int TrackListWrapper::clipEffectsId(int clipId) const { return Project::clipEffectsId(clipId); }

bool TrackListWrapper::canHaveClipEffects(int clipId) const { return m_state.canHaveClipEffects(clipId); }

QVariantMap TrackListWrapper::clipEffectsInfo(int clipId) const
{
    int trackId = -1;
    const auto* clip = m_project.clipOfEffects(Project::clipEffectsId(clipId), &trackId);
    if (!clip || !m_state.canHaveClipEffects(clipId)) return {};
    QVariantList bands;
    for (float gain : clip->fx.eqGainDb) bands.push_back(gain);
    return {{QStringLiteral("effectsId"), Project::clipEffectsId(clipId)}, {QStringLiteral("trackId"), trackId},
            {QStringLiteral("name"), QString::fromStdString(clip->poolId.empty() ? std::string("Audio clip")
                                         : std::filesystem::path(clip->poolId).filename().string())},
            {QStringLiteral("effects"), effectList(clip->fx)}, {QStringLiteral("eqBands"), bands}};
}

std::vector<int> TrackListWrapper::importAudio(const std::vector<std::shared_ptr<const AudioData>>& files, double startBeat, int targetTrackId)
{
    if (targetTrackId >= 0) {
        const auto clips = m_state.importAudio(files, startBeat, targetTrackId);
        if (!clips.empty()) notifyClipEdit(targetTrackId);
        return clips;
    }
    // Several new rows at once: views reload the list.
    beginResetModel();
    const auto clips = m_state.importAudio(files, startBeat, -1);
    endResetModel();
    notifyTrackCountChanged();
    notifyClipEdit(-1);
    return clips;
}

int TrackListWrapper::splitForRangeEffects(int trackId, double startBeat, double endBeat)
{
    const int clip = m_state.splitForRangeEffects(trackId, startBeat, endBeat);
    if (clip >= 0) notifyClipEdit(trackId);
    return clip;
}

bool TrackListWrapper::canApplyClipTool(const QVariantList& values) const
{
    const auto ids = clipIds(values);
    return ids && m_state.canApplyClipTool(*ids);
}

bool TrackListWrapper::takeAction(const QVariantList& values, const QString& action, int index)
{
    const auto ids = clipIds(values);
    if (!ids) return false;
    TrackList::Change change;
    if (action == "set") change = m_state.setActiveTake(*ids, index);
    else if (action == "next") change = m_state.nextTake(*ids);
    else if (action == "delete") change = m_state.deleteActiveTake(*ids);
    else if (action == "crop") change = m_state.cropToActiveTake(*ids);
    else return false;
    if (change.changed) notifyClipEdit(-1);
    return change.changed;
}

bool TrackListWrapper::applyClipTool(const QVariantList& values, const QString& tool)
{
    const auto ids = clipIds(values);
    const auto which = clipTool(tool);
    if (!ids || !which) return false;
    const auto change = m_state.applyClipTool(*ids, *which);
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

QVariantList TrackListWrapper::cutClips(const QVariantList& ids, bool ripple)
{
    std::vector<int> intIds;
    intIds.reserve(ids.size());
    for (const auto& id : ids) intIds.push_back(id.toInt());
    const auto cut = m_state.cutClips(intIds, ripple);
    if (!cut.empty()) {
        emit clipboardChanged();
        if (ripple) notifyContentEdit(); else notifyClipEdit(-1);
    }
    QVariantList result;
    result.reserve(cut.size());
    for (int id : cut) result.append(id);
    return result;
}

QVariantList TrackListWrapper::pasteClips(int targetTrackId, double targetBeat, bool ripple)
{
    const bool content = ripple || !m_state.clipboardEnvelopes().empty();
    const auto pasted = m_state.pasteClips(targetTrackId, targetBeat, ripple);
    if (!pasted.empty()) {
        if (content) notifyContentEdit(); else notifyClipEdit(targetTrackId);
    }
    QVariantList result;
    result.reserve(pasted.size());
    for (int id : pasted) result.append(id);
    return result;
}

QVariantList TrackListWrapper::duplicateClips(const QVariantList& ids, double beatOffset, bool ripple)
{
    std::vector<int> intIds;
    intIds.reserve(ids.size());
    for (const auto& id : ids) intIds.push_back(id.toInt());
    const auto duplicated = m_state.duplicateClips(intIds, beatOffset, ripple);
    if (!duplicated.empty()) {
        if (ripple) notifyContentEdit(); else notifyClipEdit(-1);
    }
    QVariantList result;
    result.reserve(duplicated.size());
    for (int id : duplicated) result.append(id);
    return result;
}

bool TrackListWrapper::deleteClips(const QVariantList& values, bool ripple)
{
    const auto ids = clipIds(values);
    if (!ids) return false;
    const auto change = m_state.deleteClips(*ids, ripple);
    if (change.changed) { if (ripple) notifyContentEdit(); else notifyClipEdit(-1); }
    return change.accepted;
}
bool TrackListWrapper::copyRange(double startBeat, double endBeat)
{
    const bool ok = m_state.copyRange(startBeat, endBeat);
    if (ok) emit clipboardChanged();
    return ok;
}
bool TrackListWrapper::cutRange(double startBeat, double endBeat, bool ripple)
{
    const auto change = m_state.cutRange(startBeat, endBeat, ripple);
    if (change.accepted) emit clipboardChanged();
    if (change.changed) notifyContentEdit();
    return change.accepted;
}
bool TrackListWrapper::deleteRange(double startBeat, double endBeat, bool ripple)
{
    const auto change = m_state.deleteRange(startBeat, endBeat, ripple);
    if (change.changed) notifyContentEdit();
    return change.accepted;
}
void TrackListWrapper::notifyContentEdit() {
    notifyClipEdit(-1);
    if (count() > 0) emit dataChanged(index(0), index(count() - 1), {EnvelopePointsRole});
}
void TrackListWrapper::notifyClipEdit(int trackId) {
    const int row = rowForTrackId(trackId);
    if (count()>0) emit dataChanged(index(row<0 ? 0 : row), index(row<0 ? count()-1 : row), {ClipsRole});
    emit contentEndBeatsChanged();
    emit clipsChanged();
}
void TrackListWrapper::notifyClipGainEdit(int trackId) {
    const int row = rowForTrackId(trackId);
    // dataChanged is forwarded to projectChanged (engine refresh, dirty flag) in the constructor.
    if (row >= 0) emit dataChanged(index(row), index(row), {ClipsRole});
    emit historyChanged();
}
QStringList TrackListWrapper::undoHistory() const
{
    QStringList labels;
    for (const auto& label : m_project.historyLabels()) labels.append(QString::fromStdString(label));
    return labels;
}

bool TrackListWrapper::jumpToHistory(int position)
{
    if (position < 0 || position > static_cast<int>(m_project.historySize())) return false;
    while (undoPosition() > position) if (!undo()) return false;
    while (undoPosition() < position) if (!redo()) return false;
    return true;
}

void TrackListWrapper::syncUndoHistory()
{
    const auto steps = m_project.stepCount();
    const auto size = m_project.historySize();
    const auto mark = m_project.historyMark();
    if (steps == m_notifiedSteps && size == m_notifiedHistorySize && mark == m_notifiedHistoryMark) return;
    m_notifiedSteps = steps; m_notifiedHistorySize = size; m_notifiedHistoryMark = mark;
    emit undoHistoryChanged();
}

bool TrackListWrapper::undo()
{
    // Undoing an import takes several rows away at once: views reload the list.
    if (m_project.undoChangesTracks()) {
        beginResetModel(); const bool ok = m_state.undo(); endResetModel();
        notifyTrackCountChanged(); notifyClipEdit(-1);
        return ok;
    }
    // Undoing a track removal puts a row back: announce it so views add just that row.
    if (const auto restore = m_state.undoRestoresTrack()) {
        const int row = static_cast<int>(restore->index);
        beginInsertRows({}, row, row);
        const bool ok = m_state.undo();
        endInsertRows();
        notifyTrackCountChanged();
        return ok;
    }
    if (!m_state.undo()) return false;
    notifyClipEdit(-1);
    if (count() > 0) emit dataChanged(index(0), index(count() - 1), {EnvelopePointsRole, EnvelopeBypassedRole});
    return true;
}
bool TrackListWrapper::redo()
{
    if (m_project.redoChangesTracks()) {
        beginResetModel(); const bool ok = m_state.redo(); endResetModel();
        notifyTrackCountChanged(); notifyClipEdit(-1);
        return ok;
    }
    if (const auto removal = m_state.redoRemovesTrack()) {
        const int row = static_cast<int>(removal->index);
        beginRemoveRows({}, row, row);
        const bool ok = m_state.redo();
        endRemoveRows();
        notifyTrackCountChanged();
        return ok;
    }
    if (!m_state.redo()) return false;
    notifyClipEdit(-1);
    if (count() > 0) emit dataChanged(index(0), index(count() - 1), {EnvelopePointsRole, EnvelopeBypassedRole});
    return true;
}

void TrackListWrapper::setTempo(double bpm, int numerator, int denominator)
{
    if (m_state.setTempo(bpm, numerator, denominator)) {
        emit contentEndBeatsChanged(); emit clipsChanged();
        // Envelope points stay in samples, so their beats follow the tempo (restMap G6).
        if (count() > 0) emit dataChanged(index(0), index(count() - 1), {EnvelopePointsRole});
    }
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
    if (!m_project.effects(trackId)) { setEqPresetError(QStringLiteral("Track is unavailable.")); return false; }
    for (int i=0;i<10;++i) m_project.setTrackEqBand(trackId,i,bands[i]);
    if (trackId == Project::MasterTrackId) { emit masterChanged(); emit projectChanged(); setEqPresetError({}); return true; }
    if (Project::isClipEffectsId(trackId)) { notifyClipEdit(-1); setEqPresetError({}); return true; }
    auto* track = m_project.findTrack(trackId);
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
    const auto* track = m_project.effects(trackId);   // a track, the master or a clip
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

bool TrackListWrapper::attachTake(int id, std::int64_t start, std::shared_ptr<const AudioData> audio, bool printed,
                                  const TrackList::TakeShape& shape)
{
    if (!m_state.attachTake(id, start, std::move(audio), printed, shape)) return false;
    notifyClipEdit(id); return true;
}

bool TrackListWrapper::attachMidiTake(int id, std::int64_t start, std::int64_t length, std::vector<MidiNote> notes,
                                      const TrackList::TakeShape& shape)
{
    if (!m_state.attachMidiTake(id, start, length, std::move(notes), shape)) return false;
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
            {"solo", track->solo}, {"muted", track->muted}, {"volume", track->volume}, {"pan", track->pan}, {"stripWidth", track->stripWidth},
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
        // Sessions saved before pan existed restore centred.
        track.pan = float(item["pan"].toDouble(0));
        track.stripWidth = float(item["stripWidth"].toDouble(Track{}.stripWidth));
        track.inputPort = item["input"].toString().toStdString();
        const auto eq = item["eq"].toArray();
        if (track.volume < 0 || track.volume > 2 || (item.contains("pan") && !item["pan"].isDouble())
            || !std::isfinite(track.pan) || track.pan < -1 || track.pan > 1
            || (item.contains("stripWidth") && !item["stripWidth"].isDouble())
            || track.stripWidth < mixer::minStripWidth || track.stripWidth > mixer::maxStripWidth
            || eq.size() != 10 || !item["clips"].isArray()) {
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
QVariantList TrackListWrapper::variantChoices(const QString& group) const { return choices(TrackList::variantChoices(group.toStdString())); }
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
