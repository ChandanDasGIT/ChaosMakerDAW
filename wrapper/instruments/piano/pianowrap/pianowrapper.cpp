#include "pianowrapper.h"
#include "instruments/chords/chordsinstrument.h"
#include "instruments/piano/piano.h"
#include "instruments/piano/pianolibrary.h"
#include "midi/notenames.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"

PianoWrapper::PianoWrapper(QObject* parent)
    : QObject(parent)
{
    // The piano loads in the background: look again until it is ready or known to be missing.
    m_statusTimer.setInterval(250);
    connect(&m_statusTimer, &QTimer::timeout, this, &PianoWrapper::pollStatus);
    m_statusTimer.start();
    pollStatus();
    m_keyTimer.setInterval(33);
    connect(&m_keyTimer, &QTimer::timeout, this, &PianoWrapper::pollKeys);
}

QObject* PianoWrapper::trackModel() const
{
    return m_trackModel;
}

void PianoWrapper::setTrackModel(QObject* model)
{
    auto* tracks = qobject_cast<TrackListWrapper*>(model);
    if (tracks == m_trackModel) return;
    if (m_trackModel) disconnect(m_trackModel, nullptr, this, nullptr);
    m_trackModel = tracks;
    if (m_trackModel) {
        connect(m_trackModel, &TrackListWrapper::pianoChanged, this, [this](int id) {
            if (id == m_trackId) reload();
        });
        // Loading a project or bringing a track back replaces the settings without a signal.
        connect(m_trackModel, &TrackListWrapper::projectChanged, this, &PianoWrapper::reload);
        connect(m_trackModel, &QObject::destroyed, this, &PianoWrapper::reload);
    }
    emit trackModelChanged();
    reload();
}

void PianoWrapper::setTrackId(int id)
{
    if (id == m_trackId) return;
    m_trackId = id;
    m_watched.reset();                                      // another track's piano: new counts
    emit trackIdChanged();
    reload();
}

bool PianoWrapper::valid() const
{
    return m_trackModel && m_trackModel->piano(m_trackId) != nullptr;
}

void PianoWrapper::reload()
{
    const auto* stored = m_trackModel ? m_trackModel->piano(m_trackId) : nullptr;
    m_s = stored ? *stored : piano::Settings{};
    emit settingsChanged();
}

bool PianoWrapper::commit(const piano::Settings& settings)
{
    // setPiano sanitises and emits pianoChanged (reloading every panel on the track). Reload
    // here too: a value clamped back to what was stored emits nothing, but the control that
    // sent it must still snap back.
    const bool ok = m_trackModel->setPiano(m_trackId, settings);
    reload();
    return ok;
}

bool PianoWrapper::resetToDefaults()
{
    return valid() && commit(piano::Settings{});
}

QObject* PianoWrapper::engine() const
{
    return m_engine;
}

void PianoWrapper::setEngine(QObject* engine)
{
    auto* controller = qobject_cast<EngineController*>(engine);
    if (controller == m_engine) return;
    m_engine = controller;
    m_watched.reset();
    emit engineChanged();
}

void PianoWrapper::setWatchKeys(bool watch)
{
    if (watch == m_watchKeys) return;
    m_watchKeys = watch;
    m_watched.reset();                                      // start from the counts as they are
    if (watch) { pollKeys(); m_keyTimer.start(); } else { m_keyTimer.stop(); }
    emit watchKeysChanged();
}

void PianoWrapper::play(int key, int velocity)
{
    if (!m_engine || !valid()) return;
    m_engine->sendLiveNoteOn(m_trackId, key, velocity);
    emit played(key, velocity);
}

void PianoWrapper::stop(int key)
{
    if (m_engine && valid()) m_engine->sendLiveNoteOff(m_trackId, key);
}

void PianoWrapper::setPedalDown(bool down)
{
    if (!m_engine || !valid()) return;
    m_engine->sendLiveControl(m_trackId, 64, down ? 127 : 0);
    emit pedalSent(down);
}

QString PianoWrapper::noteName(int key) const
{
    return QString::fromStdString(notenames::name(key));
}

void PianoWrapper::pollKeys()
{
    if (!m_engine || !valid()) return;
    const auto instrument = m_engine->trackInstrument(m_trackId);
    if (!instrument) return;
    // A Piano track's piano, or the one a Chords track plays through.
    const Piano* found = instrument->typeId() == "piano" ? &static_cast<const Piano&>(*instrument)
                       : instrument->typeId() == "chords" ? &static_cast<const ChordsInstrument&>(*instrument).piano()
                       : nullptr;
    if (!found) return;
    const auto& piano = *found;
    // A new piano (a new track, a buffer change): take its counts without striking anything.
    const bool fresh = m_watched.lock() != instrument;
    m_watched = instrument;
    QVariantMap down;
    for (int key = 0; key < 128; ++key) {
        const auto presses = piano.presses(key);
        auto& seen = m_seen[std::size_t(key)];
        if (!fresh && presses != seen) emit struck(key, std::max(1, piano.keyVelocity(key)));
        seen = presses;
        if (const int v = piano.keyVelocity(key); v > 0) down.insert(QString::number(key), v);
    }
    const int pedal = piano.pedal();
    if (down == m_keysDown && pedal == m_pedal) return;
    m_keysDown = down;
    m_pedal = pedal;
    emit keysChanged();
}

void PianoWrapper::pollStatus()
{
    const auto library = piano::Library::shared();
    const auto state = library->state();
    const QString status = state == piano::Library::State::Loading || state == piano::Library::State::Missing
                         ? QString::fromStdString(library->message()) : QString();
    const bool missing = state == piano::Library::State::Missing;
    if (state == piano::Library::State::Ready || missing) m_statusTimer.stop();
    if (status == m_status && missing == m_missing) return;
    m_status = status;
    m_missing = missing;
    emit statusChanged();
}
