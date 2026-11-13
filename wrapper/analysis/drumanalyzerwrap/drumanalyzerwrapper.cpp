#include "drumanalyzerwrapper.h"

#include "instruments/drums/grooves/drumgroove.h"
#include "instruments/instrumentcatalog.h"
#include "persistence/groovestore.h"
#include "session/project.h"
#include "session/tracklist.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"

#include <QFutureWatcher>
#include <QStandardPaths>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

namespace {

constexpr double MaxClipSeconds = 600.0;                  // TrackList::ownerAudio reads at most 10 minutes
constexpr int PreviewBars = 2;

int rowOf(int key)
{
    if (key == analysis::KickKey) return 0;
    if (key == analysis::SnareKey) return 1;
    return 2;
}

} // namespace

DrumAnalyzerWrapper::DrumAnalyzerWrapper(QObject* parent) : QObject(parent) {}

DrumAnalyzerWrapper::~DrumAnalyzerWrapper()
{
    if (m_cancel) m_cancel->store(true);
}

QObject* DrumAnalyzerWrapper::trackModel() const { return m_model; }

void DrumAnalyzerWrapper::setTrackModel(QObject* model)
{
    auto* tracks = qobject_cast<TrackListWrapper*>(model);
    if (tracks == m_model) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = tracks;
    if (m_model) connect(m_model, &TrackListWrapper::clipsChanged, this, &DrumAnalyzerWrapper::clipChanged);
    emit trackModelChanged();
    emit clipChanged();
}

void DrumAnalyzerWrapper::setClipId(int id)
{
    if (id == m_clipId) return;
    m_clipId = id;
    emit clipChanged();
}

const AudioClip* DrumAnalyzerWrapper::clip() const
{
    if (!m_model || m_clipId < 0) return nullptr;
    for (const auto& track : m_model->project().tracks())
        for (const auto& c : track->audioClips)
            if (c.id == m_clipId) return &c;
    return nullptr;
}


QString DrumAnalyzerWrapper::clipLabel() const
{
    if (!m_model || !clip()) return {};
    for (const auto& info : m_model->domain().clips())
        if (info.clipId == m_clipId) return QString::fromStdString(info.label);
    return {};
}

QString DrumAnalyzerWrapper::unavailableReason() const
{
    if (!m_model) return QStringLiteral("No project is open.");
    const auto* c = clip();
    if (!c) return QStringLiteral("Select an audio clip on the timeline first.");
    if (m_model->domain().isPatternClip(*c)) return QStringLiteral("This is a MIDI clip: select an audio clip.");
    const double rate = m_model->project().tempo().sampleRate();
    if (double(c->length) < analysis::MinSeconds * rate) return QStringLiteral("The clip is too short: it needs at least 4 seconds.");
    if (double(c->length) > MaxClipSeconds * rate) return QStringLiteral("The clip is longer than 10 minutes: split it first.");
    return {};
}

void DrumAnalyzerWrapper::setMessage(const QString& message)
{
    m_message = message;
    emit stateChanged();
}

bool DrumAnalyzerWrapper::analyse()
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

    QPointer<DrumAnalyzerWrapper> self(this);
    auto cancel = m_cancel;
    auto* watcher = new QFutureWatcher<std::optional<analysis::DrumAnalysis>>(this);
    connect(watcher, &QFutureWatcher<std::optional<analysis::DrumAnalysis>>::finished, this, [this, watcher, run] {
        watcher->deleteLater();
        if (run != m_run) return;                          // a later analysis took over
        auto result = watcher->result();
        m_busy = false;
        const bool cancelled = m_cancel && m_cancel->load();
        if (result) {
            m_result = std::move(result);
            m_message.clear();
            m_progress = 1.0;
            emit resultChanged();
        } else {
            m_message = cancelled ? QStringLiteral("Cancelled.") : QStringLiteral("No beat could be found in this clip.");
        }
        emit progressChanged();
        emit stateChanged();
        emit finished(m_result.has_value() && !cancelled);
    });
    watcher->setFuture(QtConcurrent::run([audio = std::move(audio), rate, cancel, self, run]() {
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
        return analysis::analyseDrums(audio, rate, progress);
    }));
    return true;
}

void DrumAnalyzerWrapper::cancel()
{
    if (m_cancel) m_cancel->store(true);
}

bool DrumAnalyzerWrapper::retempo(double factor)
{
    if (!m_result || m_busy) return false;
    auto again = analysis::retempo(*m_result, factor);
    if (!again) { setMessage(QStringLiteral("That tempo is out of range.")); return false; }
    m_result = std::move(again);
    m_message.clear();
    emit resultChanged();
    emit stateChanged();
    return true;
}

double DrumAnalyzerWrapper::bpm() const { return m_result ? m_result->pattern.bpm : 0.0; }
int DrumAnalyzerWrapper::numerator() const { return m_result ? m_result->pattern.numerator : 4; }
int DrumAnalyzerWrapper::denominator() const { return m_result ? m_result->pattern.denominator : 4; }
int DrumAnalyzerWrapper::bars() const { return m_result ? m_result->pattern.bars : 0; }
int DrumAnalyzerWrapper::kicks() const { return m_result ? m_result->pattern.kicks : 0; }
int DrumAnalyzerWrapper::snares() const { return m_result ? m_result->pattern.snares : 0; }
int DrumAnalyzerWrapper::hats() const { return m_result ? m_result->pattern.hats : 0; }

int DrumAnalyzerWrapper::columnsPerBeat() const
{
    if (!m_result || m_result->pattern.gridQuarters <= 0.0) return 4;
    return std::max(1, int(std::lround(1.0 / m_result->pattern.gridQuarters)));
}

int DrumAnalyzerWrapper::previewColumns() const
{
    if (!m_result || m_result->pattern.gridQuarters <= 0.0) return 0;
    return int(std::lround(PreviewBars * m_result->pattern.barQuarters() / m_result->pattern.gridQuarters));
}

QVariantList DrumAnalyzerWrapper::preview() const
{
    QVariantList cells;
    if (!m_result) return cells;
    const auto& p = m_result->pattern;
    const int columns = previewColumns();
    for (const auto& n : p.notes) {
        const int column = int(std::lround(n.startBeat / p.gridQuarters));
        if (column >= columns) break;
        cells.append(QVariantMap{{"row", rowOf(n.note)}, {"column", column}, {"velocity", n.velocity}});
    }
    return cells;
}

QVariantList DrumAnalyzerWrapper::targets() const
{
    QVariantList list;
    list.append(QVariantMap{{"id", -1}, {"name", QStringLiteral("New Drums track")}});
    list.append(QVariantMap{{"id", -2}, {"name", QStringLiteral("New Electronic Drums track")}});
    if (!m_model) return list;
    for (const auto& track : m_model->project().tracks())
        if (instrumentcatalog::isDrumKit(track->type))
            list.append(QVariantMap{{"id", track->id}, {"name", QString::fromStdString(track->name)}});
    return list;
}

bool DrumAnalyzerWrapper::alignSong()
{
    if (!m_result || !m_model || m_busy) return false;
    // Where the clip is now: it may have been moved since it was analysed.
    for (const auto& track : m_model->project().tracks())
        for (const auto& c : track->audioClips) {
            if (c.id != m_clipId) continue;
            const double rate = m_model->project().tempo().sampleRate();
            const auto barLine = c.start + std::int64_t(std::llround(m_result->pattern.firstBarSeconds * rate));
            const auto moved = m_model->alignClipToBars(track->id, c.id, barLine);
            if (!moved) return false;
            m_analysedStart = c.start;
            m_songMovedSeconds += double(*moved) / rate;
            return true;
        }
    return false;
}

QString DrumAnalyzerWrapper::songMovedText()
{
    const double by = m_songMovedSeconds;
    m_songMovedSeconds = 0.0;
    if (std::abs(by) < 0.0005) return {};
    return QStringLiteral(" The song moved %1 s %2 to sit on the bars.").arg(std::abs(by), 0, 'f', 2).arg(by > 0 ? QStringLiteral("later") : QStringLiteral("earlier"));
}

int DrumAnalyzerWrapper::makeClip(int target)
{
    if (!m_result || !m_model || m_busy) { setMessage(QStringLiteral("Analyse a clip first.")); return -1; }
    const auto& p = m_result->pattern;
    if (p.notes.empty() || p.bars <= 0) { setMessage(QStringLiteral("No drum hits were found.")); return -1; }
    if (target >= 0) {
        const auto* track = m_model->project().findTrack(target);
        if (!track || !instrumentcatalog::isDrumKit(track->type)) { setMessage(QStringLiteral("Choose a drum track.")); return -1; }
    }

    // Placed by the project's tempo now: the same beats when it is the found one, otherwise each
    // hit at its real time (so the clip still plays with the audio).
    const auto& tempo = m_model->project().tempo();
    const double scale = p.secondsPerQuarter * tempo.bpm() / 60.0;
    const double barBeats = p.barQuarters() * scale;
    const auto barSample = m_analysedStart + std::int64_t(std::llround(p.firstBarSeconds * tempo.sampleRate()));
    double start = tempo.beatAt(barSample);
    double cut = 0.0;                                      // bars before the song's start are left out
    if (start < 0.0) {
        const double skip = std::ceil(-start / barBeats - 1e-9);
        cut = skip * barBeats;
        start += cut;
    }
    std::vector<MidiNote> notes;
    for (const auto& n : p.notes) {
        MidiNote m = n;
        m.startBeat = n.startBeat * scale - cut;
        m.lengthBeats = n.lengthBeats * scale;
        m.selected = false;
        if (m.startBeat < -1e-9) continue;
        m.startBeat = std::max(0.0, m.startBeat);
        m.id = int(notes.size()) + 1;
        notes.push_back(m);
    }
    const double length = p.bars * barBeats - cut;
    if (notes.empty() || length <= 0.0) { setMessage(QStringLiteral("No drum hits lie after the song's start.")); return -1; }

    int trackId = target;
    if (target == -1 || target == -2) {
        trackId = m_model->addInstrumentTrackOfType(target == -1 ? QStringLiteral("Drums") : QStringLiteral("Electronic Drums"));
        if (trackId < 0) { setMessage(QStringLiteral("The drum track could not be added.")); return -1; }
    }
    const int id = m_model->addPatternClipAtBeat(trackId, start, length, std::move(notes));
    if (id < 0) { setMessage(QStringLiteral("The clip could not be made (not while recording).")); return -1; }
    setMessage(QStringLiteral("The pattern is on the track.") + songMovedText());
    return id;
}

QString DrumAnalyzerWrapper::grooveFolder() const
{
    if (!m_grooveFolder.isEmpty()) return m_grooveFolder;
    // The drum pages' folder (DrumGrooveWrapper::folder).
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/grooves");
}

void DrumAnalyzerWrapper::setGrooveFolder(const QString& folder)
{
    if (folder == m_grooveFolder) return;
    m_grooveFolder = folder;
    emit grooveFolderChanged();
}

QString DrumAnalyzerWrapper::suggestedGrooveName() const
{
    auto name = clipLabel().trimmed();
    if (name.isEmpty()) name = QStringLiteral("Analysed");
    name = name.left(int(drumgroove::MaxNameLength) - 7).trimmed() + QStringLiteral(" groove");
    name.replace(QLatin1Char('/'), QLatin1Char(' ')).replace(QLatin1Char('\\'), QLatin1Char(' '));
    return name;
}

bool DrumAnalyzerWrapper::saveGroove(const QString& name)
{
    if (!m_result) { setMessage(QStringLiteral("Analyse a clip first.")); return false; }
    const auto clean = drumgroove::cleanName(name.toStdString());
    if (!clean) { setMessage(QStringLiteral("Give the groove a name (up to 48 characters, no slashes).")); return false; }
    const auto& p = m_result->pattern;
    if (!drumgroove::validMeter(p.numerator, p.denominator)) { setMessage(QStringLiteral("This time signature cannot be a groove.")); return false; }
    drumgroove::Groove groove;
    groove.name = *clean;
    groove.numerator = p.numerator;
    groove.denominator = p.denominator;
    groove.bars = std::clamp(std::min(PreviewBars, p.bars), 1, drumgroove::MaxBars);
    groove.notes = analysis::typicalBars(p, groove.bars);
    if (groove.notes.empty()) { setMessage(QStringLiteral("No drum hits were found.")); return false; }
    GrooveStore store(grooveFolder().toStdString());
    std::string error;
    if (!store.save(groove, false, error)) { setMessage(QString::fromStdString(error)); return false; }
    setMessage(QStringLiteral("Saved the groove “%1”: it is in the drum pages' grooves.").arg(QString::fromStdString(*clean)));
    return true;
}
