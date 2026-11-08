#include "chordswrapper.h"
#include "instruments/chords/chordsinstrument.h"
#include "plugins/chordplayer/chordrender.h"
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
    for (int i = 0; i < 16; ++i) if (m_down[std::size_t(i)]) release(i % 8, i >= 8);   // let go on the old track
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
    commit(s);
}

QString ChordsWrapper::keyName() const { return QString::fromStdString(chords::keyName(m_s.key)); }

QStringList ChordsWrapper::tonicNames() const
{
    QStringList out;
    const bool flats = chords::usesFlats(m_s.key);
    for (int pc = 0; pc < 12; ++pc) out << QString::fromStdString(chords::noteName(pc, flats));
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
    QVariantList out;
    for (int mode = 0; mode < chords::AutoplayModes; ++mode) {
        const auto name = chords::autoplayName(mode);
        out.append(QVariantMap{{QStringLiteral("mode"), mode},
                               {QStringLiteral("title"), QString::fromUtf8(name.data(), qsizetype(name.size()))}});
    }
    return out;
}

QVariantList ChordsWrapper::progressions() const
{
    QVariantList out;
    int index = 0;
    for (const auto& p : chords::progressions()) {
        out.append(QVariantMap{{QStringLiteral("index"), index++},
                               {QStringLiteral("name"), QString::fromUtf8(p.name.data(), qsizetype(p.name.size()))},
                               {QStringLiteral("description"), QString::fromUtf8(p.description.data(), qsizetype(p.description.size()))},
                               {QStringLiteral("minor"), p.minor}});
    }
    return out;
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
    auto s = m_s;
    s.key = chords::Key{((tonic % 12) + 12) % 12, minor};
    if (s.key == m_s.key) return;
    commit(s);
}

bool ChordsWrapper::applyProgression(int index)
{
    const auto list = chords::progressions();
    if (!valid() || index < 0 || index >= int(list.size())) return false;
    const auto& p = list[std::size_t(index)];
    const auto chordsIn = chords::progressionChords(p, m_s.key.tonic);
    if (chordsIn.size() != 8) return false;
    auto s = m_s;
    s.key.minor = p.minor;
    for (std::size_t i = 0; i < 8; ++i) s.chords[i] = chordsIn[i].name;
    return commit(s);
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

void ChordsWrapper::press(int strip, bool bass, int velocity)
{
    const int key = keyOf(strip, bass);
    if (key < 0 || !m_engine || !valid()) return;
    const std::size_t slot = std::size_t(strip + (bass ? 8 : 0));
    if (m_down[slot]) m_engine->sendLiveNoteOff(m_trackId, key);
    m_down[slot] = true;
    m_engine->sendLiveNoteOn(m_trackId, key, std::clamp(velocity, 1, 127));
}

void ChordsWrapper::release(int strip, bool bass)
{
    const int key = keyOf(strip, bass);
    if (key < 0) return;
    const std::size_t slot = std::size_t(strip + (bass ? 8 : 0));
    if (!m_down[slot]) return;
    m_down[slot] = false;
    if (m_engine) m_engine->sendLiveNoteOff(m_trackId, key);
}

void ChordsWrapper::pollActive()
{
    int strip = -1;
    bool bass = false;
    if (m_engine && valid()) {
        const auto instrument = m_engine->trackInstrument(m_trackId);
        if (instrument && instrument->typeId() == "chords") {
            const auto& player = static_cast<const ChordsInstrument&>(*instrument).player();
            strip = player.activeStrip();
            bass = strip >= 0 && player.activeBass();
        }
    }
    if (strip == m_activeStrip && bass == m_activeBass) return;
    m_activeStrip = strip;
    m_activeBass = bass;
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
