#include "chordanalyzerwrapper.h"

#include "plugins/chordplayer/chordplayer.h"
#include "plugins/chordplayer/chordsettings.h"
#include "session/project.h"
#include "session/tracklist.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"

#include <QClipboard>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

namespace {

constexpr double MaxClipSeconds = 600.0;                  // TrackList::ownerAudio reads at most 10 minutes
constexpr double ChartLineSeconds = 15.0;

} // namespace

ChordAnalyzerWrapper::ChordAnalyzerWrapper(QObject* parent) : QObject(parent) {}

ChordAnalyzerWrapper::~ChordAnalyzerWrapper()
{
    if (m_cancel) m_cancel->store(true);
}

QObject* ChordAnalyzerWrapper::trackModel() const { return m_model; }

void ChordAnalyzerWrapper::setTrackModel(QObject* model)
{
    auto* tracks = qobject_cast<TrackListWrapper*>(model);
    if (tracks == m_model) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = tracks;
    if (m_model) connect(m_model, &TrackListWrapper::clipsChanged, this, &ChordAnalyzerWrapper::clipChanged);
    emit trackModelChanged();
    emit clipChanged();
}

void ChordAnalyzerWrapper::setClipId(int id)
{
    if (id == m_clipId) return;
    m_clipId = id;
    emit clipChanged();
}

const AudioClip* ChordAnalyzerWrapper::clip() const
{
    if (!m_model || m_clipId < 0) return nullptr;
    for (const auto& track : m_model->project().tracks())
        for (const auto& c : track->audioClips)
            if (c.id == m_clipId) return &c;
    return nullptr;
}

QString ChordAnalyzerWrapper::clipLabel() const
{
    if (!m_model || !clip()) return {};
    for (const auto& info : m_model->domain().clips())
        if (info.clipId == m_clipId) return QString::fromStdString(info.label);
    return {};
}

QString ChordAnalyzerWrapper::unavailableReason() const
{
    if (!m_model) return QStringLiteral("No project is open.");
    const auto* c = clip();
    if (!c) return QStringLiteral("Select the song's audio clip on the timeline first.");
    if (m_model->domain().isPatternClip(*c)) return QStringLiteral("This is a MIDI clip: select an audio clip.");
    const double rate = m_model->project().tempo().sampleRate();
    if (double(c->length) < analysis::ChordMinSeconds * rate) return QStringLiteral("The clip is too short: it needs at least 4 seconds.");
    if (double(c->length) > MaxClipSeconds * rate) return QStringLiteral("The clip is longer than 10 minutes: split it first.");
    return {};
}

void ChordAnalyzerWrapper::setMessage(const QString& message)
{
    m_message = message;
    emit stateChanged();
}

void ChordAnalyzerWrapper::setSevenths(bool on)
{
    if (on == m_sevenths) return;
    m_sevenths = on;
    emit seventhsChanged();
    if (m_result && !m_busy) {
        analysis::ChordOptions options;
        options.sevenths = m_sevenths;
        analysis::decodeChords(*m_result, options);
        emit resultChanged();
    }
}

bool ChordAnalyzerWrapper::analyse()
{
    if (m_busy) return false;
    if (const auto why = unavailableReason(); !why.isEmpty()) { setMessage(why); return false; }
    const auto* c = clip();
    const double rate = m_model->project().tempo().sampleRate();
    auto audio = m_model->domain().ownerAudio(Project::clipEffectsId(c->id), c->start, c->start + c->length);
    if (audio.empty()) { setMessage(QStringLiteral("This clip has no audio to analyse.")); return false; }
    m_analysedStart = c->start;
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    const int run = ++m_run;
    m_busy = true;
    m_progress = 0.0;
    m_message = QStringLiteral("Analysing…");
    emit stateChanged();
    emit progressChanged();

    QPointer<ChordAnalyzerWrapper> self(this);
    auto cancel = m_cancel;
    analysis::ChordOptions options;
    options.sevenths = m_sevenths;
    auto* watcher = new QFutureWatcher<std::optional<analysis::ChordAnalysis>>(this);
    connect(watcher, &QFutureWatcher<std::optional<analysis::ChordAnalysis>>::finished, this, [this, watcher, run] {
        watcher->deleteLater();
        if (run != m_run) return;                          // a later analysis took over
        auto result = watcher->result();
        m_busy = false;
        const bool cancelled = m_cancel && m_cancel->load();
        if (result) {
            m_result = std::move(result);
            // The option may have changed while it ran.
            analysis::ChordOptions now;
            now.sevenths = m_sevenths;
            analysis::decodeChords(*m_result, now);
            m_message.clear();
            m_progress = 1.0;
            emit resultChanged();
        } else {
            m_message = cancelled ? QStringLiteral("Cancelled.") : QStringLiteral("Nothing could be read from this clip.");
        }
        emit progressChanged();
        emit stateChanged();
        emit finished(m_result.has_value() && !cancelled);
    });
    watcher->setFuture(QtConcurrent::run([audio = std::move(audio), rate, cancel, self, run, options]() {
        analysis::Progress progress;
        progress.cancel = cancel.get();
        auto last = std::make_shared<double>(-1.0);
        progress.report = [self, run, last](double value) {
            if (value - *last < 0.01 && value < 1.0) return;
            *last = value;
            QMetaObject::invokeMethod(self, [self, run, value] {
                if (!self || run != self->m_run || !self->m_busy) return;
                self->m_progress = value;
                emit self->progressChanged();
            }, Qt::QueuedConnection);
        };
        return analysis::analyseChords(audio, rate, options, progress);
    }));
    return true;
}

void ChordAnalyzerWrapper::cancel()
{
    if (m_busy && m_cancel) m_cancel->store(true);
}

QString ChordAnalyzerWrapper::keyName() const { return m_result ? QString::fromStdString(chords::keyName(m_result->key)) : QString(); }
bool ChordAnalyzerWrapper::hasBeat() const { return m_result && m_result->hasBeat; }
double ChordAnalyzerWrapper::bpm() const { return m_result ? m_result->bpm : 0.0; }
int ChordAnalyzerWrapper::numerator() const { return m_result ? m_result->numerator : 4; }
int ChordAnalyzerWrapper::denominator() const { return m_result ? m_result->denominator : 4; }
double ChordAnalyzerWrapper::tuningCents() const { return m_result ? m_result->tuningCents : 0.0; }
double ChordAnalyzerWrapper::seconds() const { return m_result ? m_result->seconds : 0.0; }

QVariantList ChordAnalyzerWrapper::chords() const
{
    QVariantList list;
    if (!m_result) return list;
    for (const auto& c : m_result->chords)
        list.append(QVariantMap{{"name", QString::fromStdString(c.name)}, {"from", c.fromSeconds}, {"to", c.toSeconds},
                                {"beats", c.slotCount}, {"sure", double(c.sureness)}});
    return list;
}

QStringList ChordAnalyzerWrapper::commonChords() const
{
    QStringList list;
    if (!m_result) return list;
    for (const auto& name : analysis::commonChords(*m_result)) list.append(QString::fromStdString(name));
    return list;
}

QVariantList ChordAnalyzerWrapper::targets() const
{
    QVariantList list;
    list.append(QVariantMap{{"id", -1}, {"name", QStringLiteral("New Chords track")}});
    if (!m_model) return list;
    for (const auto& track : m_model->project().tracks())
        if (track->type == "Chords") list.append(QVariantMap{{"id", track->id}, {"name", QString::fromStdString(track->name)}});
    return list;
}

bool ChordAnalyzerWrapper::alignSong()
{
    if (!m_result || !m_model || m_busy || !m_result->hasBeat) return false;
    // Where the clip is now: it may have been moved since it was analysed.
    for (const auto& track : m_model->project().tracks())
        for (const auto& c : track->audioClips) {
            if (c.id != m_clipId) continue;
            const double rate = m_model->project().tempo().sampleRate();
            const auto barLine = c.start + std::int64_t(std::llround(m_result->firstBarSeconds * rate));
            const auto moved = m_model->alignClipToBars(track->id, c.id, barLine);
            if (!moved) return false;
            m_analysedStart = c.start;
            m_songMovedSeconds += double(*moved) / rate;
            return true;
        }
    return false;
}

QString ChordAnalyzerWrapper::songMovedText()
{
    const double by = m_songMovedSeconds;
    m_songMovedSeconds = 0.0;
    if (std::abs(by) < 0.0005) return {};
    return QStringLiteral(" The song moved %1 s %2 to sit on the bars.").arg(std::abs(by), 0, 'f', 2).arg(by > 0 ? QStringLiteral("later") : QStringLiteral("earlier"));
}

int ChordAnalyzerWrapper::putOnTrack(int target, bool withClip)
{
    if (!m_result || !m_model || m_busy) { setMessage(QStringLiteral("Analyse a clip first.")); return -1; }
    const auto common = analysis::commonChords(*m_result);
    if (common.empty()) { setMessage(QStringLiteral("No chords were found.")); return -1; }
    if (target >= 0) {
        const auto* track = m_model->project().findTrack(target);
        if (!track || track->type != "Chords") { setMessage(QStringLiteral("Choose a Chords track.")); return -1; }
    }
    int trackId = target;
    if (target < 0) {
        trackId = m_model->addInstrumentTrackOfType(QStringLiteral("Chords"));
        if (trackId < 0) { setMessage(QStringLiteral("The Chords track could not be added.")); return -1; }
    }
    const auto* current = m_model->chords(trackId);
    chords::Settings settings = current ? *current : chords::Settings{};
    for (std::size_t i = 0; i < settings.chords.size(); ++i) settings.chords[i] = i < common.size() ? common[i] : std::string();
    settings.key = m_result->key;
    m_model->setChords(trackId, settings);
    const int left = int(common.size()) - int(settings.chords.size());
    const QString leftOut = left > 0 ? QStringLiteral(" %1 less played chord%2 did not fit.").arg(left).arg(left == 1 ? "" : "s") : QString();
    const QString moved = songMovedText();
    if (!withClip) { setMessage(QStringLiteral("The chords are on the track's strips.") + leftOut + moved); return trackId; }

    // The clip: each chord's strip held for as long as the chord sounds, at its real time under
    // the project's tempo now.
    const auto& tempo = m_model->project().tempo();
    const auto beatAt = [&](double seconds) { return tempo.beatAt(m_analysedStart + std::int64_t(std::llround(seconds * tempo.sampleRate()))); };
    double first = -1.0, last = 0.0;
    std::vector<MidiNote> notes;
    for (const auto& span : m_result->chords) {
        if (span.root < 0) continue;
        const auto at = std::find(settings.chords.begin(), settings.chords.end(), span.name);
        if (at == settings.chords.end()) continue;
        const double from = std::max(0.0, beatAt(span.fromSeconds)), to = beatAt(span.toSeconds);
        if (to - from < 1e-6) continue;
        if (first < 0.0) first = from;
        MidiNote note;
        note.id = int(notes.size()) + 1;
        note.note = ChordPlayer::ChordKeys[std::size_t(at - settings.chords.begin())];
        note.startBeat = from - first;
        note.lengthBeats = to - from;
        note.velocity = 100;
        notes.push_back(note);
        last = to;
    }
    if (notes.empty()) { setMessage(QStringLiteral("The chords are on the strips; no clip could be made.") + leftOut + moved); return trackId; }
    if (m_model->addPatternClipAtBeat(trackId, first, last - first, std::move(notes)) < 0) {
        setMessage(QStringLiteral("The chords are on the strips; the clip could not be made (not while recording)."));
        return trackId;
    }
    setMessage(QStringLiteral("The chords are on the strips, with a clip that plays them.") + leftOut + moved);
    return trackId;
}

QString ChordAnalyzerWrapper::chartText() const
{
    if (!m_result) return {};
    QString text = QStringLiteral("Key: %1").arg(keyName());
    if (m_result->hasBeat) text += QStringLiteral("   %1 BPM   %2/%3").arg(std::round(m_result->bpm * 10.0) / 10.0).arg(m_result->numerator).arg(m_result->denominator);
    long line = -1;
    for (const auto& c : m_result->chords) {
        const long at = long(c.fromSeconds / ChartLineSeconds);
        if (at != line) {
            line = at;
            const int seconds = int(at * ChartLineSeconds);
            text += QStringLiteral("\n%1:%2 ").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
        }
        text += QStringLiteral("  ") + (c.root < 0 ? QStringLiteral("-") : QString::fromStdString(c.name));
    }
    return text + QLatin1Char('\n');
}

void ChordAnalyzerWrapper::copyChart() const
{
    if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance())) return;   // no screen (tests)
    if (auto* clipboard = QGuiApplication::clipboard()) clipboard->setText(chartText());
}
