#include "groovePanelWrap.h"
#include "instruments/drums/groovePanel/drumgroove.h"
#include "instruments/common/instrumentcatalog.h"
#include "wrapper/common/engine/engineController.h"
#include "wrapper/common/trackListModelWrap.h"

#include <QStandardPaths>
#include <QVariantMap>

#include <algorithm>
#include <cmath>


GroovePanelWrap::GroovePanelWrap(QObject* parent)
    : QObject(parent)
{
}

QObject* GroovePanelWrap::engine() const { return m_engine; }

void GroovePanelWrap::setEngine(QObject* engine)
{
    auto* controller = qobject_cast<EngineController*>(engine);
    if (controller == m_engine) return;
    if (m_engine) disconnect(m_engine, nullptr, this, nullptr);
    m_engine = controller;
    if (m_engine) connect(m_engine, &EngineController::meterChanged, this, &GroovePanelWrap::groovesChanged);
    emit engineChanged();
    emit groovesChanged();
}

GroovePanelWrap::Meter GroovePanelWrap::meter() const
{
    Meter m;
    if (m_engine) { m.numerator = m_engine->numerator(); m.denominator = m_engine->denominator(); }
    if (!drumgroove::validMeter(m.numerator, m.denominator)) m = Meter{};
    return m;
}

GroovePanelWrap::~GroovePanelWrap() = default;

QObject* GroovePanelWrap::trackModel() const { return m_trackModel; }

void GroovePanelWrap::setTrackModel(QObject* model)
{
    auto* tracks = qobject_cast<TrackListModelWrap*>(model);
    if (tracks == m_trackModel) return;
    if (m_trackModel) disconnect(m_trackModel, nullptr, this, nullptr);
    m_trackModel = tracks;
    if (m_trackModel) {
        connect(m_trackModel, &TrackListModelWrap::clipsChanged, this, &GroovePanelWrap::trackChanged);
        connect(m_trackModel, &TrackListModelWrap::selectedClipChanged, this, &GroovePanelWrap::trackChanged);
    }
    emit trackModelChanged();
    emit trackChanged();
}

void GroovePanelWrap::setTrackId(int id)
{
    if (id == m_trackId) return;
    m_trackId = id;
    emit trackIdChanged();
    emit trackChanged();
}

bool GroovePanelWrap::valid() const
{
    if (!m_trackModel) return false;
    const auto* track = m_trackModel->project().findTrack(m_trackId);
    return track && instrumentcatalog::isDrumKit(track->type);
}

QString GroovePanelWrap::folder() const
{
    if (!m_folder.isEmpty()) return m_folder;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/grooves");
}

void GroovePanelWrap::setFolder(const QString& folder)
{
    if (folder == m_folder) return;
    m_folder = folder;
    m_store.reset();
    emit groovesChanged();
}

GrooveStore& GroovePanelWrap::store() const
{
    if (!m_store) m_store = std::make_unique<GrooveStore>(folder().toStdString());
    return *m_store;
}

QString GroovePanelWrap::projectMeter() const
{
    const Meter m = meter();
    return QString::number(m.numerator) + QLatin1Char('/') + QString::number(m.denominator);
}

QVariantList GroovePanelWrap::grooves() const
{
    const Meter m = meter();
    auto all = store().all();
    // The project's time signature first; the store's order (factory, then user) otherwise.
    std::stable_partition(all.begin(), all.end(), [&](const drumgroove::Groove& g) {
        return g.numerator == m.numerator && g.denominator == m.denominator;
    });
    QVariantList list;
    for (const auto& g : all) {
        list.append(QVariantMap{
            {QStringLiteral("name"), QString::fromStdString(g.name)},
            {QStringLiteral("meter"), QString::fromStdString(g.meterText())},
            {QStringLiteral("numerator"), g.numerator},
            {QStringLiteral("denominator"), g.denominator},
            {QStringLiteral("bars"), g.bars},
            {QStringLiteral("factory"), g.factory},
            {QStringLiteral("matchesProject"), g.numerator == m.numerator && g.denominator == m.denominator},
        });
    }
    return list;
}

int GroovePanelWrap::selectedClipOnTrack() const
{
    if (!valid()) return -1;
    const auto& tracks = m_trackModel->domain();
    const int clip = tracks.selectedClipId();
    const auto* found = clip >= 0 ? tracks.findClip(m_trackId, clip) : nullptr;
    if (!found || !tracks.isPatternClip(*found) || tracks.clipNotes(m_trackId, clip).empty()) return -1;
    return clip;
}

bool GroovePanelWrap::canSaveClip() const { return selectedClipOnTrack() >= 0; }

bool GroovePanelWrap::fail(const QString& message)
{
    m_lastError = message;
    emit lastErrorChanged();
    return false;
}

void GroovePanelWrap::succeed()
{
    if (m_lastError.isEmpty()) return;
    m_lastError.clear();
    emit lastErrorChanged();
}

int GroovePanelWrap::addToTrack(const QString& name, int bars)
{
    if (!valid()) { fail(QStringLiteral("Pick a drum track first.")); return -1; }
    const auto groove = store().find(name.toStdString());
    if (!groove) { fail(QStringLiteral("There is no groove named \"%1\".").arg(name)); return -1; }
    if (bars < 1 || bars > drumgroove::MaxRepeatBars) { fail(QStringLiteral("Choose 1 to 64 bars.")); return -1; }

    // The start of the bar the playhead is in, under the project's time signature.
    const Meter m = meter();
    const double position = m_engine ? std::max(0.0, m_engine->positionBeats()) : 0.0;
    const double start = std::floor(position / m.barBeats() + 1e-9) * m.barBeats();
    const int clip = m_trackModel->addPatternClipAtBeat(m_trackId, start, bars * groove->barBeats(),
                                                        drumgroove::repeated(*groove, bars));
    if (clip < 0) { fail(QStringLiteral("The groove could not be added (is the track recording?).")); return -1; }
    succeed();
    return clip;
}

bool GroovePanelWrap::saveSelectedClip(const QString& name, bool replace)
{
    const int clip = selectedClipOnTrack();
    if (clip < 0) return fail(QStringLiteral("Select a drum clip with notes on this track first."));
    const auto& tracks = m_trackModel->domain();
    const Meter m = meter();
    std::string error;
    auto groove = drumgroove::fromClip(name.toStdString(), tracks.clipNotes(m_trackId, clip),
                                       tracks.clipLengthBeats(m_trackId, clip), m.numerator, m.denominator, error);
    if (!groove) return fail(QString::fromStdString(error));
    if (!store().save(std::move(*groove), replace, error)) return fail(QString::fromStdString(error));
    succeed();
    emit groovesChanged();
    return true;
}

bool GroovePanelWrap::renameGroove(const QString& name, const QString& newName)
{
    std::string error;
    if (!store().rename(name.toStdString(), newName.toStdString(), error)) return fail(QString::fromStdString(error));
    succeed();
    emit groovesChanged();
    return true;
}

bool GroovePanelWrap::removeGroove(const QString& name)
{
    std::string error;
    if (!store().remove(name.toStdString(), error)) return fail(QString::fromStdString(error));
    succeed();
    emit groovesChanged();
    return true;
}

bool GroovePanelWrap::exists(const QString& name) const
{
    return store().find(name.toStdString()).has_value();
}

void GroovePanelWrap::refresh() { emit groovesChanged(); }
