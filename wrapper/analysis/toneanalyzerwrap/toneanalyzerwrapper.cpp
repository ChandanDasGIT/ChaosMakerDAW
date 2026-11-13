#include "toneanalyzerwrapper.h"

#include "instruments/guitar/plugins/guitarsuite/pedalregistry.h"
#include "session/project.h"
#include "session/tracklist.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSettings>
#include <QStandardPaths>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

namespace {

constexpr int MaxNameLength = 60;
const QString DefaultFolderKey = QStringLiteral("toneAnalyzer/presetFolder");

// A preset name as a file name: trimmed, no slashes or other characters a file name cannot
// hold; empty when nothing is left.
QString cleanName(const QString& name)
{
    QString out;
    for (const QChar c : name.trimmed().left(MaxNameLength)) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c.unicode() < 32) continue;
        out += c;
    }
    out = out.trimmed();
    while (out.startsWith('.')) out.remove(0, 1);
    return out;
}

// One pedal's settings as a line: "Drive 28 dB · Bass 3 dB".
QString settingsText(const PedalPresetData& pedal)
{
    QStringList parts;
    const auto processor = PedalRegistry::create(pedal.type);
    if (!processor) return {};
    for (const auto& p : processor->parameters()) {
        const auto found = pedal.parameters.find(p->id);
        if (found == pedal.parameters.end()) continue;
        const double value = found->second;
        if (pedal.type == "pedal.amp" && p->id == "channel") { parts.prepend(QString::fromLatin1(analysis::toneChannelName(int(std::lround(value))))); continue; }
        if (pedal.type == "pedal.parametriceq" && p->id.ends_with("q")) continue;          // the bands' widths: detail
        if (pedal.type == "pedal.cabinet" && p->id == "model") { parts << QString::fromLatin1(analysis::toneCabinetName(int(std::lround(value)))); continue; }
        if (std::abs(value - p->def) < 1e-4 && pedal.type != "pedal.amp") continue;      // as the pedal comes: not listed
        const QString unit = QString::fromStdString(p->unit);
        const double shown = std::abs(value) >= 10.0 || unit == "Hz" || unit == "ms" ? std::round(value) : std::round(value * 100.0) / 100.0;
        parts << QStringLiteral("%1 %2%3").arg(QString::fromStdString(p->name)).arg(shown).arg(unit.isEmpty() ? QString() : QLatin1Char(' ') + unit);
    }
    return parts.join(QStringLiteral(" · "));
}

} // namespace

ToneAnalyzerWrapper::ToneAnalyzerWrapper(QObject* parent) : QObject(parent) {}

ToneAnalyzerWrapper::~ToneAnalyzerWrapper()
{
    if (m_cancel) m_cancel->store(true);
}

QObject* ToneAnalyzerWrapper::trackModel() const { return m_model; }

void ToneAnalyzerWrapper::setTrackModel(QObject* model)
{
    auto* tracks = qobject_cast<TrackListWrapper*>(model);
    if (tracks == m_model) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = tracks;
    if (m_model) connect(m_model, &TrackListWrapper::clipsChanged, this, &ToneAnalyzerWrapper::clipChanged);
    emit trackModelChanged();
    emit clipChanged();
}

void ToneAnalyzerWrapper::setClipId(int id)
{
    if (id == m_clipId) return;
    m_clipId = id;
    emit clipChanged();
}

void ToneAnalyzerWrapper::setPlaying(int playing)
{
    playing = std::clamp(playing, 0, 2);
    if (playing == m_playing) return;
    m_playing = playing;
    emit playingChanged();
}

const AudioClip* ToneAnalyzerWrapper::clip() const
{
    if (!m_model || m_clipId < 0) return nullptr;
    for (const auto& track : m_model->project().tracks())
        for (const auto& c : track->audioClips)
            if (c.id == m_clipId) return &c;
    return nullptr;
}

QString ToneAnalyzerWrapper::clipLabel() const
{
    if (!m_model || !clip()) return {};
    for (const auto& info : m_model->domain().clips())
        if (info.clipId == m_clipId) return QString::fromStdString(info.label);
    return {};
}

QString ToneAnalyzerWrapper::unavailableReason() const
{
    if (!m_model) return QStringLiteral("No project is open.");
    const auto* c = clip();
    if (!c) return QStringLiteral("Select an audio clip with the guitar tone first.");
    if (m_model->domain().isPatternClip(*c)) return QStringLiteral("This is a MIDI clip: select an audio clip.");
    const double rate = m_model->project().tempo().sampleRate();
    if (double(c->length) < analysis::ToneMinSeconds * rate) return QStringLiteral("The clip is too short: it needs at least 2 seconds.");
    return {};
}

void ToneAnalyzerWrapper::setMessage(const QString& message)
{
    m_message = message;
    emit stateChanged();
}

bool ToneAnalyzerWrapper::analyse()
{
    if (m_busy) return false;
    if (const auto why = unavailableReason(); !why.isEmpty()) { setMessage(why); return false; }
    const auto* c = clip();
    const double rate = m_model->project().tempo().sampleRate();
    // The first minute is enough to know a tone (and all that is measured).
    const auto length = std::min<std::int64_t>(c->length, std::int64_t(analysis::ToneMaxSeconds * rate));
    auto audio = m_model->domain().ownerAudio(Project::clipEffectsId(c->id), c->start, c->start + length);
    if (audio.empty()) { setMessage(QStringLiteral("This clip has no audio to analyse.")); return false; }
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    const int run = ++m_run;
    m_busy = true;
    m_progress = 0.0;
    m_message = QStringLiteral("Trying pedalboards…");
    emit stateChanged();
    emit progressChanged();

    QPointer<ToneAnalyzerWrapper> self(this);
    auto cancel = m_cancel;
    analysis::ToneMatchOptions options;
    options.playing = m_playing == 1 ? analysis::TonePlaying::Chords : m_playing == 2 ? analysis::TonePlaying::Solo : analysis::TonePlaying::Auto;
    auto* watcher = new QFutureWatcher<std::optional<analysis::ToneMatch>>(this);
    connect(watcher, &QFutureWatcher<std::optional<analysis::ToneMatch>>::finished, this, [this, watcher, run] {
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
            m_message = cancelled ? QStringLiteral("Cancelled.") : QStringLiteral("No tone could be read: the clip is silent, or nearly.");
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
        return analysis::matchTone(audio, rate, options, progress);
    }));
    return true;
}

void ToneAnalyzerWrapper::cancel()
{
    if (m_busy && m_cancel) m_cancel->store(true);
}

QString ToneAnalyzerWrapper::playingName() const
{
    return !m_result ? QString() : m_result->solo ? QStringLiteral("Single notes") : QStringLiteral("Chords");
}

QString ToneAnalyzerWrapper::gainName() const
{
    if (!m_result) return {};
    return QString::fromLatin1(analysis::toneChannelName(m_result->channel)) + (m_result->overdrive ? QStringLiteral(" + Overdrive") : QString());
}

QString ToneAnalyzerWrapper::cabinetName() const { return m_result ? QString::fromLatin1(analysis::toneCabinetName(m_result->cabinet)) : QString(); }

QString ToneAnalyzerWrapper::roomName() const
{
    if (!m_result) return {};
    return m_result->delay ? QStringLiteral("Delay and reverb") : m_result->reverb ? QStringLiteral("Reverb") : QStringLiteral("Dry");
}

double ToneAnalyzerWrapper::balanceErrorDb() const { return m_result ? m_result->balanceErrorDb : 0.0; }

QString ToneAnalyzerWrapper::closeness() const
{
    if (!m_result) return {};
    const double off = m_result->balanceErrorDb;
    return off < 1.5 ? QStringLiteral("Very close") : off < 3.0 ? QStringLiteral("Close") : off < 5.0 ? QStringLiteral("Roughly there")
                                                                                              : QStringLiteral("Far: this clip is hard to match");
}

QVariantList ToneAnalyzerWrapper::bands() const
{
    QVariantList list;
    if (!m_result) return list;
    // Each tone against its own mean over the bands that are compared.
    double meanWant = 0.0, meanGot = 0.0;
    int count = 0;
    for (int b = 0; b < analysis::ToneFeatures::Bands; ++b) {
        const double hz = analysis::ToneFeatures::bandHz(b);
        if (hz < 99.0 || hz > 6500.0) continue;
        meanWant += m_result->reference.balanceDb[std::size_t(b)];
        meanGot += m_result->matched.balanceDb[std::size_t(b)];
        ++count;
    }
    if (count == 0) return list;
    meanWant /= count;
    meanGot /= count;
    for (int b = 0; b < analysis::ToneFeatures::Bands; ++b) {
        const double hz = analysis::ToneFeatures::bandHz(b);
        if (hz < 99.0 || hz > 6500.0) continue;
        list.append(QVariantMap{{"hz", hz}, {"want", m_result->reference.balanceDb[std::size_t(b)] - meanWant},
                                {"got", m_result->matched.balanceDb[std::size_t(b)] - meanGot}});
    }
    return list;
}

QVariantList ToneAnalyzerWrapper::pedals() const
{
    QVariantList list;
    if (!m_result) return list;
    for (const auto& pedal : m_result->preset.pedals)
        list.append(QVariantMap{{"name", QString::fromStdString(PedalRegistry::displayName(pedal.type))}, {"settings", settingsText(pedal)}});
    return list;
}

QString ToneAnalyzerWrapper::presetFolder() const
{
    if (!m_presetFolder.isEmpty()) return m_presetFolder;
    if (const QString own = QSettings().value(DefaultFolderKey).toString(); !own.isEmpty()) return own;
    // Where GuitarSuite keeps its presets (GuitarSuiteEditorWrapper's Default.gspreset).
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/GuitarSuite");
}

QUrl ToneAnalyzerWrapper::presetFolderUrl() const { return QUrl::fromLocalFile(presetFolder()); }

bool ToneAnalyzerWrapper::hasOwnDefaultFolder() const { return !QSettings().value(DefaultFolderKey).toString().isEmpty(); }

void ToneAnalyzerWrapper::setDefaultFolder(const QUrl& folder)
{
    QSettings settings;
    const QString path = folder.isLocalFile() ? QDir(folder.toLocalFile()).absolutePath() : QString();
    if (path == settings.value(DefaultFolderKey).toString()) return;
    if (path.isEmpty()) settings.remove(DefaultFolderKey);
    else settings.setValue(DefaultFolderKey, path);
    emit presetFolderChanged();
}

QUrl ToneAnalyzerWrapper::fileFor(const QString& name) const
{
    QString clean = cleanName(name);
    if (clean.isEmpty()) clean = suggestedName();
    return QUrl::fromLocalFile(QDir(presetFolder()).absoluteFilePath(clean + QStringLiteral(".gspreset")));
}

void ToneAnalyzerWrapper::setPresetFolder(const QString& folder)
{
    if (folder == m_presetFolder) return;
    m_presetFolder = folder;
    emit presetFolderChanged();
}

QString ToneAnalyzerWrapper::suggestedName() const
{
    QString name = QFileInfo(clipLabel().trimmed()).completeBaseName();
    if (name.isEmpty()) name = QStringLiteral("Analysed");
    return cleanName(name.left(MaxNameLength - 5) + QStringLiteral(" tone"));
}

bool ToneAnalyzerWrapper::savePreset(const QString& name)
{
    if (!m_result || m_busy) { setMessage(QStringLiteral("Analyse a clip first.")); return false; }
    const QString clean = cleanName(name);
    if (clean.isEmpty()) { setMessage(QStringLiteral("Give the preset a name (no slashes).")); return false; }
    const QDir folder(presetFolder());
    if (!QDir().mkpath(folder.absolutePath())) { setMessage(QStringLiteral("The presets folder could not be made: %1").arg(folder.absolutePath())); return false; }
    const QString path = folder.absoluteFilePath(clean + QStringLiteral(".gspreset"));
    if (QFileInfo::exists(path)) { setMessage(QStringLiteral("A preset named \"%1\" is already there: choose another name.").arg(clean)); return false; }
    return write(path, clean);
}

bool ToneAnalyzerWrapper::savePresetTo(const QUrl& file, bool makeDefault)
{
    if (!m_result || m_busy) { setMessage(QStringLiteral("Analyse a clip first.")); return false; }
    if (!file.isLocalFile()) { setMessage(QStringLiteral("Presets are saved to a file on this computer.")); return false; }
    QFileInfo info(file.toLocalFile());
    const QString name = cleanName(info.completeBaseName());
    if (name.isEmpty()) { setMessage(QStringLiteral("Give the preset a name.")); return false; }
    const QDir folder = info.absoluteDir();
    if (!QDir().mkpath(folder.absolutePath())) { setMessage(QStringLiteral("The folder could not be made: %1").arg(folder.absolutePath())); return false; }
    const QString path = info.suffix().isEmpty() ? info.absoluteFilePath() + QStringLiteral(".gspreset") : info.absoluteFilePath();
    if (!write(path, name)) return false;
    if (makeDefault) setDefaultFolder(QUrl::fromLocalFile(folder.absolutePath()));
    return true;
}

bool ToneAnalyzerWrapper::write(const QString& path, const QString& name)
{
    GuitarSuitePresetData preset = m_result->preset;
    preset.name = name.toStdString();
    std::string error;
    if (!GuitarSuitePreset::saveToFile(path.toStdString(), preset, &error)) {
        setMessage(QStringLiteral("The preset could not be saved: %1").arg(QString::fromStdString(error)));
        return false;
    }
    m_savedPreset = QUrl::fromLocalFile(path);
    emit savedPresetChanged();
    setMessage(QStringLiteral("Saved as \"%1\" in %2. In GuitarSuite, Load preset opens it.")
                   .arg(name, QDir::toNativeSeparators(QFileInfo(path).absolutePath())));
    return true;
}
