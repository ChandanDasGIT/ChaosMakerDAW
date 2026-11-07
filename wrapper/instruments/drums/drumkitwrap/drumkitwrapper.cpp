#include "drumkitwrapper.h"
#include "instruments/drums/drumkit.h"
#include "instruments/drums/drumkitlibrary.h"
#include "instruments/drums/drummap.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"

#include <QVariantMap>

DrumKitWrapper::DrumKitWrapper(QObject* parent)
    : QObject(parent)
{
    // The kit loads in the background: look again until it is ready or known to be missing.
    m_statusTimer.setInterval(250);
    connect(&m_statusTimer, &QTimer::timeout, this, &DrumKitWrapper::pollStatus);
    m_statusTimer.start();
    pollStatus();
    m_hitTimer.setInterval(33);
    connect(&m_hitTimer, &QTimer::timeout, this, &DrumKitWrapper::pollHits);
}

QObject* DrumKitWrapper::engine() const
{
    return m_engine;
}

void DrumKitWrapper::setEngine(QObject* engine)
{
    auto* controller = qobject_cast<EngineController*>(engine);
    if (controller == m_engine) return;
    m_engine = controller;
    m_watched.reset();
    emit engineChanged();
}

void DrumKitWrapper::setWatchHits(bool watch)
{
    if (watch == m_watchHits) return;
    m_watchHits = watch;
    m_watched.reset();                                      // start from the counts as they are
    if (watch) { pollHits(); m_hitTimer.start(); } else { m_hitTimer.stop(); }
    emit watchHitsChanged();
}

QVariantMap DrumKitWrapper::partKeys() const
{
    QVariantMap keys;
    for (const auto& pad : drummap::pads())
        keys.insert(QString::fromUtf8(pad.id.data(), qsizetype(pad.id.size())), pad.key);
    return keys;
}

QString DrumKitWrapper::partName(int key) const
{
    const auto name = drummap::nameOf(key);
    return QString::fromUtf8(name.data(), qsizetype(name.size()));
}

void DrumKitWrapper::play(int key, int velocity)
{
    if (!m_engine || !valid()) return;
    m_engine->sendLiveNoteOn(m_trackId, key, velocity);
    emit played(key, velocity);
}

void DrumKitWrapper::stop(int key)
{
    if (m_engine && valid()) m_engine->sendLiveNoteOff(m_trackId, key);
}

void DrumKitWrapper::pollHits()
{
    if (!m_engine || !valid()) return;
    const auto instrument = m_engine->trackInstrument(m_trackId);
    if (!instrument || instrument->typeId() != "drumkit") return;
    const auto& kit = static_cast<const DrumKit&>(*instrument);
    // A new kit (a new track, a buffer change): take its counts without lighting anything.
    const bool fresh = m_watched.lock() != instrument;
    m_watched = instrument;
    for (const auto& pad : drummap::pads()) {
        const auto hits = kit.hits(pad.key);
        auto& seen = m_seen[std::size_t(pad.key)];
        if (!fresh && hits.count != seen) emit hit(pad.key, hits.velocity);
        seen = hits.count;
    }
}

QObject* DrumKitWrapper::trackModel() const
{
    return m_trackModel;
}

void DrumKitWrapper::setTrackModel(QObject* model)
{
    auto* tracks = qobject_cast<TrackListWrapper*>(model);
    if (tracks == m_trackModel) return;
    if (m_trackModel) disconnect(m_trackModel, nullptr, this, nullptr);
    m_trackModel = tracks;
    if (m_trackModel) {
        connect(m_trackModel, &TrackListWrapper::drumKitChanged, this, [this](int id) {
            if (id == m_trackId) reload();
        });
        // Loading a project or bringing a track back replaces the settings without a signal.
        connect(m_trackModel, &TrackListWrapper::projectChanged, this, &DrumKitWrapper::reload);
        connect(m_trackModel, &QObject::destroyed, this, &DrumKitWrapper::reload);
    }
    emit trackModelChanged();
    reload();
}

void DrumKitWrapper::setTrackId(int id)
{
    if (id == m_trackId) return;
    m_trackId = id;
    m_watched.reset();                                      // another track's kit: new counts
    emit trackIdChanged();
    reload();
}

bool DrumKitWrapper::valid() const
{
    return m_trackModel && m_trackModel->drumKit(m_trackId) != nullptr;
}

void DrumKitWrapper::reload()
{
    const auto* stored = m_trackModel ? m_trackModel->drumKit(m_trackId) : nullptr;
    m_s = stored ? *stored : drumkit::Settings{};
    emit settingsChanged();
}

bool DrumKitWrapper::commit(const drumkit::Settings& settings)
{
    // setDrumKit sanitises and emits drumKitChanged (reloading every panel on the track). Reload
    // here too: a value clamped back to what was stored emits nothing, but the control that
    // sent it must still snap back.
    const bool ok = m_trackModel->setDrumKit(m_trackId, settings);
    reload();
    return ok;
}

QString DrumKitWrapper::room() const
{
    return QString::fromUtf8(drumkit::roomName(m_s.room));
}

void DrumKitWrapper::setRoom(const QString& name)
{
    drumkit::Room room;
    if (!drumkit::roomFromName(name.toStdString(), room)) return;
    edit([room](auto& s) { s.room = room; });
}

bool DrumKitWrapper::resetToDefaults()
{
    return valid() && commit(drumkit::Settings{});
}

QVariantList DrumKitWrapper::rooms() const
{
    return {QVariantMap{{"id", "close"}, {"title", "Close mics"}},
            QVariantMap{{"id", "front"}, {"title", "Close + front room"}},
            QVariantMap{{"id", "full"}, {"title", "Full room"}}};
}

void DrumKitWrapper::pollStatus()
{
    const auto library = drumkit::Library::shared();
    const auto state = library->state();
    const QString status = state == drumkit::Library::State::Loading || state == drumkit::Library::State::Missing
                         ? QString::fromStdString(library->message()) : QString();
    const bool missing = state == drumkit::Library::State::Missing;
    if (state == drumkit::Library::State::Ready || missing) m_statusTimer.stop();
    if (status == m_status && missing == m_missing) return;
    m_status = status;
    m_missing = missing;
    emit kitStatusChanged();
}
