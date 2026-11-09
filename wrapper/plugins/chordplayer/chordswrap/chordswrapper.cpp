#include "chordswrapper.h"
#include "instruments/chords/chordsinstrument.h"
#include "plugins/chordplayer/chordrender.h"
#include "wrapper/settings/appsettingswrap/appsettingswrapper.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"

#include <QVariantMap>

#include <algorithm>

ChordsWrapper::ChordsWrapper(QObject* parent)
    : QObject(parent)
{
    m_timer.setInterval(33);
    connect(&m_timer, &QTimer::timeout, this, &ChordsWrapper::pollActive);
}

QObject* ChordsWrapper::trackModel() const { return m_trackModel; }

void ChordsWrapper::setTrackModel(QObject* model)
{
    auto* tracks = qobject_cast<TrackListWrapper*>(model);
    if (tracks == m_trackModel) return;
    if (m_trackModel) disconnect(m_trackModel, nullptr, this, nullptr);
    m_trackModel = tracks;
    if (m_trackModel) {
        connect(m_trackModel, &TrackListWrapper::chordsChanged, this, [this](int id) { if (id == m_trackId) reload(); });
        // Loading a project or bringing a track back replaces the settings without a signal.
        connect(m_trackModel, &TrackListWrapper::modelReset, this, &ChordsWrapper::reload);
    }
    emit trackModelChanged();
    reload();
}

void ChordsWrapper::setTrackId(int id)
{
    if (id == m_trackId) return;
    for (int i = 0; i < 16; ++i) if (m_down[std::size_t(i)] >= 0) release(i % 8, i >= 8);   // let go on the old track
    m_trackId = id;
    emit trackIdChanged();
    reload();
}

QObject* ChordsWrapper::engine() const { return m_engine; }

void ChordsWrapper::setEngine(QObject* engine)
{
    auto* controller = qobject_cast<EngineController*>(engine);
    if (controller == m_engine) return;
    m_engine = controller;
    emit engineChanged();
}

void ChordsWrapper::setWatchActive(bool watch)
{
    if (watch == m_watch) return;
    m_watch = watch;
    if (m_watch) { m_timer.start(); pollActive(); }
    else m_timer.stop();
    emit watchActiveChanged();
}

bool ChordsWrapper::valid() const
{
    return m_trackModel && m_trackModel->chords(m_trackId) != nullptr;
}

void ChordsWrapper::reload()
{
    const auto* stored = m_trackModel ? m_trackModel->chords(m_trackId) : nullptr;
    m_s = stored ? *stored : chords::Settings{};
    emit settingsChanged();
}

bool ChordsWrapper::commit(const chords::Settings& settings)
{
    if (!valid()) return false;
    m_trackModel->setChords(m_trackId, settings);
    reload();
    return true;
}

void ChordsWrapper::fail(const QString& message)
{
    m_lastError = message;
    emit lastErrorChanged();
}

QStringList ChordsWrapper::chords() const
{
    QStringList out;
    for (const auto& c : m_s.chords) out << QString::fromStdString(c);
    return out;
}

void ChordsWrapper::setAutoplay(int mode)
{
    if (mode < 0 || mode >= chords::AutoplayModes || mode == m_s.autoplay) return;
    auto s = m_s;
    s.autoplay = mode;
    s.variation = 0;
    commit(s);
}

// Entry 0 is Off, then Variations entries per style.
int ChordsWrapper::autoplayChoice() const
{
    return m_s.autoplay == chords::AutoplayOff ? 0 : 1 + (m_s.autoplay - 1) * chords::Variations + m_s.variation;
}

void ChordsWrapper::setAutoplayChoice(int index)
{
    if (!valid() || index < 0 || index >= 1 + (chords::AutoplayModes - 1) * chords::Variations || index == autoplayChoice()) return;
    auto s = m_s;
    s.autoplay = index == 0 ? chords::AutoplayOff : 1 + (index - 1) / chords::Variations;
    s.variation = index == 0 ? 0 : (index - 1) % chords::Variations;
    commit(s);
}

void ChordsWrapper::setDynamics(int dynamics)
{
    if (dynamics < 0 || dynamics >= chords::DynamicsModes || dynamics == m_s.dynamics) return;
    auto s = m_s;
    s.dynamics = dynamics;
    commit(s);
}

void ChordsWrapper::setBassWithChords(bool on)
{
    if (on == m_s.bassWithChords) return;
    auto s = m_s;
    s.bassWithChords = on;
    commit(s);
}

QString ChordsWrapper::keyName() const { return QString::fromStdString(chords::keyName(m_s.key)); }

QStringList ChordsWrapper::tonicNames() const
{
    QStringList out;
    for (int pc = 0; pc < 12; ++pc) out << QString::fromStdString(chords::tonicLabel(pc));
    return out;
}

QStringList ChordsWrapper::degreeLabels() const
{
    QStringList out;
    for (const auto& label : chords::degreeLabels(m_s.key)) out << QString::fromStdString(label);
    return out;
}

QVariantList ChordsWrapper::autoplayModes() const
{
    const auto text = [](std::string_view v) { return QString::fromUtf8(v.data(), qsizetype(v.size())); };
    QVariantList out;
    const auto off = text(chords::autoplayName(chords::AutoplayOff));
    out.append(QVariantMap{{QStringLiteral("index"), 0}, {QStringLiteral("mode"), chords::AutoplayOff},
                           {QStringLiteral("variation"), 0}, {QStringLiteral("style"), off}, {QStringLiteral("name"), off},
                           {QStringLiteral("title"), off},
                           {QStringLiteral("description"), text(chords::autoplayDescription(chords::AutoplayOff))}});
    for (int mode = 1; mode < chords::AutoplayModes; ++mode)
        for (int v = 0; v < chords::Variations; ++v) {
            const auto style = text(chords::autoplayName(mode));
            const auto name = text(chords::variationName(mode, v));
            out.append(QVariantMap{{QStringLiteral("index"), int(out.size())}, {QStringLiteral("mode"), mode},
                                   {QStringLiteral("variation"), v}, {QStringLiteral("style"), style},
                                   {QStringLiteral("name"), name},
                                   {QStringLiteral("title"), style + QStringLiteral(" \u00b7 ") + name},
                                   {QStringLiteral("description"), text(chords::variationDescription(mode, v))}});
        }
    return out;
}

QVariantList ChordsWrapper::autoplayStyles() const
{
    const auto text = [](std::string_view v) { return QString::fromUtf8(v.data(), qsizetype(v.size())); };
    QVariantList out;
    for (int mode = 1; mode < chords::AutoplayModes; ++mode) {
        QVariantList variations;
        for (int v = 0; v < chords::Variations; ++v)
            variations.append(QVariantMap{{QStringLiteral("index"), 1 + (mode - 1) * chords::Variations + v},
                                          {QStringLiteral("name"), text(chords::variationName(mode, v))},
                                          {QStringLiteral("description"), text(chords::variationDescription(mode, v))}});
        out.append(QVariantMap{{QStringLiteral("mode"), mode}, {QStringLiteral("title"), text(chords::autoplayName(mode))},
                               {QStringLiteral("variations"), variations}});
    }
    return out;
}

QVariantList ChordsWrapper::zoneRanges() const
{
    const auto name = [](int note) {
        return QString::fromStdString(chords::noteName(note % 12, false)) + QString::number(note / 12 - 1);
    };
    QVariantList out;
    for (const auto& text : m_s.chords) {
        QStringList zones;
        if (const auto chord = chords::parse(text)) {
            const auto tones = chords::chordTones(*chord);
            for (int zone = 0; zone < 3 && !tones.empty(); ++zone) {
                const auto range = chords::zoneRange(zone, tones.front());
                zones << name(range.low) + QStringLiteral("\u2013") + name(range.high);
            }
        }
        out.append(zones);
    }
    return out;
}

QVariantList ChordsWrapper::progressions() const
{
    QVariantList out;
    int index = 0;
    for (const auto& p : chords::progressions()) {
        const auto name = p.name(m_s.key.minor);
        out.append(QVariantMap{{QStringLiteral("index"), index++},
                               {QStringLiteral("name"), QString::fromUtf8(name.data(), qsizetype(name.size()))},
                               {QStringLiteral("description"), QString::fromUtf8(p.description.data(), qsizetype(p.description.size()))}});
    }
    return out;
}

int ChordsWrapper::currentProgression() const
{
    return valid() ? chords::matchProgression(m_s.chords, m_s.key) : -1;
}

// The eight strips of progression `index` in `key`; false if there is none.
static bool fillWith(chords::Settings& s, int index, const chords::Key& key)
{
    const auto list = chords::progressions();
    if (index < 0 || index >= int(list.size())) return false;
    const auto chordsIn = chords::progressionChords(list[std::size_t(index)], key);
    if (chordsIn.size() != 8) return false;
    s.key = key;
    for (std::size_t i = 0; i < 8; ++i) s.chords[i] = chordsIn[i].name;
    return true;
}

bool ChordsWrapper::setChord(int strip, const QString& name)
{
    if (!valid() || strip < 0 || strip >= 8) return false;
    const QString text = name.trimmed();
    auto s = m_s;
    if (text.isEmpty()) {
        s.chords[std::size_t(strip)].clear();
    } else {
        const auto chord = chords::parse(text.toStdString());
        if (!chord) {
            fail(QStringLiteral("\"%1\" is not a chord this page knows (try C, Am7, G/B, F#m7b5, Dsus4).").arg(text));
            return false;
        }
        s.chords[std::size_t(strip)] = chord->name;
    }
    if (!m_lastError.isEmpty()) { m_lastError.clear(); emit lastErrorChanged(); }
    return commit(s);
}

QString ChordsWrapper::degreeChord(int degree, bool seventh) const
{
    const auto chord = chords::degreeChord(m_s.key, degree, seventh);
    return chord ? QString::fromStdString(chord->name) : QString();
}

bool ChordsWrapper::setDegree(int strip, int degree, bool seventh)
{
    const QString name = degreeChord(degree, seventh);
    return !name.isEmpty() && setChord(strip, name);
}

void ChordsWrapper::setKey(int tonic, bool minor)
{
    if (!valid()) return;
    const chords::Key key{((tonic % 12) + 12) % 12, minor};
    if (key == m_s.key) return;
    const int progression = std::max(currentProgression(), 0);    // your own chords: the key chords
    auto s = m_s;
    if (!fillWith(s, progression, key)) s.key = key;
    commit(s);
}

bool ChordsWrapper::applyProgression(int index)
{
    if (!valid()) return false;
    auto s = m_s;
    return fillWith(s, index, m_s.key) && commit(s);
}

bool ChordsWrapper::resetToDefaults()
{
    return valid() && commit(chords::Settings{});
}

int ChordsWrapper::keyOf(int strip, bool bass) const
{
    if (strip < 0 || strip >= 8) return -1;
    return (bass ? ChordPlayer::BassKeys : ChordPlayer::ChordKeys)[std::size_t(strip)];
}

void ChordsWrapper::press(int strip, bool bass, int velocity, int zone)
{
    if (strip < 0 || strip >= 8 || !m_engine || !valid()) return;
    if (m_whole >= 0) { m_whole = -1; emit activeChanged(); }
    const auto mapping = AppSettingsWrapper::storedChordZoneMapping() == QStringLiteral("zonekeys")
                             ? ChordPlayer::KeyMapping::ZoneKeys : ChordPlayer::KeyMapping::Octaves;
    const auto trigger = ChordPlayer::triggerNotes(strip, bass, zone, mapping);
    const std::size_t slot = std::size_t(strip + (bass ? 8 : 0));
    if (m_down[slot] >= 0) m_engine->sendLiveNoteOff(m_trackId, m_down[slot]);
    m_down[slot] = trigger.stripKey;
    velocity = std::clamp(velocity, 1, 127);
    for (int i = 0; i < trigger.count; ++i) {
        const int key = trigger.keys[std::size_t(i)];
        m_engine->sendLiveNoteOn(m_trackId, key, velocity);
        if (key != trigger.stripKey) m_engine->sendLiveNoteOff(m_trackId, key);   // a zone key: a short note
    }
}

void ChordsWrapper::release(int strip, bool bass)
{
    if (strip < 0 || strip >= 8) return;
    const std::size_t slot = std::size_t(strip + (bass ? 8 : 0));
    const int key = m_down[slot];
    if (key < 0) return;
    m_down[slot] = -1;
    if (m_engine) m_engine->sendLiveNoteOff(m_trackId, key);
}

void ChordsWrapper::pressWhole(int strip, int velocity)
{
    press(strip, false, velocity, 1);
    if (strip < 0 || strip >= 8 || !valid()) return;
    m_whole = strip;
    emit activeChanged();
}

void ChordsWrapper::releaseWhole(int strip)
{
    release(strip, false);
}

void ChordsWrapper::pollActive()
{
    int strip = -1;
    bool bass = false;
    int zone = 1;
    if (m_engine && valid()) {
        const auto instrument = m_engine->trackInstrument(m_trackId);
        if (instrument && instrument->typeId() == "chords") {
            const auto& player = static_cast<const ChordsInstrument&>(*instrument).player();
            strip = player.activeStrip();
            bass = strip >= 0 && player.activeBass();
            zone = player.activeZone();
        }
    }
    if (strip == m_activeStrip && bass == m_activeBass && zone == m_activeZone) return;
    // The name's press lights both areas while that strip plays; any other press ends it.
    if (m_whole >= 0 && m_activeStrip == m_whole && (strip != m_whole || bass || zone != 1)) m_whole = -1;
    m_activeStrip = strip;
    m_activeBass = bass;
    m_activeZone = zone;
    emit activeChanged();
}

int ChordsWrapper::convertClipToPianoTrack(int clipId)
{
    if (!valid()) { fail(QStringLiteral("Pick a Chords track first.")); return -1; }
    const auto& tracks = m_trackModel->domain();
    const auto* clip = tracks.findClip(m_trackId, clipId);
    if (!clip || !tracks.isPatternClip(*clip)) { fail(QStringLiteral("Select a Chords clip on this track first.")); return -1; }
    const auto& tempo = m_trackModel->project().tempo();
    const double start = tracks.clipStartBeat(m_trackId, clipId), length = tracks.clipLengthBeats(m_trackId, clipId);
    const auto notes = chords::renderClip(m_s, tracks.clipNotes(m_trackId, clipId), start, length,
                                          tempo.bpm(), tempo.numerator(), tempo.denominator());
    if (notes.empty()) { fail(QStringLiteral("The clip plays no chords (no strip presses in it).")); return -1; }
    const auto pianoSettings = *m_trackModel->piano(m_trackId);
    const int piano = m_trackModel->addInstrumentTrackOfType(QStringLiteral("Piano"));
    if (piano < 0) { fail(QStringLiteral("Could not add a Piano track.")); return -1; }
    m_trackModel->setPiano(piano, pianoSettings);
    if (m_trackModel->addPatternClipAtBeat(piano, start, length, notes) < 0) {
        fail(QStringLiteral("Could not put the notes on the new track."));
        return -1;
    }
    if (!m_lastError.isEmpty()) { m_lastError.clear(); emit lastErrorChanged(); }
    return piano;
}
