#include "electrodrumsWrap.h"
#include "common/trackEffectsPanel/instruments/electrodrums/electrodrums.h"
#include "common/trackEffectsPanel/instruments/electrodrums/electromap.h"
#include "wrapper/common/engine/engineController.h"
#include "wrapper/common/trackList/trackListModelWrap.h"

#include <QVariantMap>

namespace {

QString text(std::string_view s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

} // namespace

ElectrodrumsWrap::ElectrodrumsWrap(QObject* parent)
    : QObject(parent)
{
    m_hitTimer.setInterval(33);
    connect(&m_hitTimer, &QTimer::timeout, this, &ElectrodrumsWrap::pollHits);
}

QObject* ElectrodrumsWrap::trackModel() const
{
    return m_trackModel;
}

void ElectrodrumsWrap::setTrackModel(QObject* model)
{
    auto* tracks = qobject_cast<TrackListModelWrap*>(model);
    if (tracks == m_trackModel) return;
    if (m_trackModel) disconnect(m_trackModel, nullptr, this, nullptr);
    m_trackModel = tracks;
    if (m_trackModel) {
        connect(m_trackModel, &TrackListModelWrap::electroDrumsChanged, this, [this](int id) {
            if (id == m_trackId) reload();
        });
        // Loading a project or bringing a track back replaces the settings without a signal.
        connect(m_trackModel, &TrackListModelWrap::projectChanged, this, &ElectrodrumsWrap::reload);
        connect(m_trackModel, &QObject::destroyed, this, &ElectrodrumsWrap::reload);
    }
    emit trackModelChanged();
    reload();
}

void ElectrodrumsWrap::setTrackId(int id)
{
    if (id == m_trackId) return;
    m_trackId = id;
    m_watched.reset();                                      // another track's kit: new counts
    emit trackIdChanged();
    reload();
}

bool ElectrodrumsWrap::valid() const
{
    return m_trackModel && m_trackModel->electroDrums(m_trackId) != nullptr;
}

void ElectrodrumsWrap::reload()
{
    const auto* stored = m_trackModel ? m_trackModel->electroDrums(m_trackId) : nullptr;
    m_s = stored ? *stored : electrodrums::Settings{};
    emit settingsChanged();
}

bool ElectrodrumsWrap::commit(const electrodrums::Settings& settings)
{
    // setElectroDrums sanitises and emits electroDrumsChanged (reloading every panel on the
    // track). Reload here too: a value clamped back to what was stored emits nothing, but the
    // control that sent it must still snap back.
    const bool ok = m_trackModel->setElectroDrums(m_trackId, settings);
    reload();
    return ok;
}

QString ElectrodrumsWrap::style() const
{
    return text(electrodrums::styleId(m_s.style));
}

void ElectrodrumsWrap::setStyle(const QString& id)
{
    electrodrums::Style style;
    if (!electrodrums::styleFromId(id.toStdString(), style)) return;
    edit([style](auto& s) { s.style = style; });
}

const electrodrums::DrumSettings* ElectrodrumsWrap::find(const QString& drum) const
{
    electrodrums::Drum d;
    if (!electrodrums::drumFromId(drum.toStdString(), d)) return nullptr;
    return &m_s.drum(d);
}

double ElectrodrumsWrap::tune(const QString& drum) const
{
    const auto* d = find(drum);
    return d ? d->tuneSemitones : 0.0;
}

double ElectrodrumsWrap::decay(const QString& drum) const
{
    const auto* d = find(drum);
    return d ? d->decay : 0.0;
}

double ElectrodrumsWrap::level(const QString& drum) const
{
    const auto* d = find(drum);
    return d ? d->levelDb : 0.0;
}

bool ElectrodrumsWrap::editDrum(const QString& drum, double electrodrums::DrumSettings::*field, double value)
{
    electrodrums::Drum d;
    if (!electrodrums::drumFromId(drum.toStdString(), d)) return false;
    return edit([d, field, value](auto& s) { s.drum(d).*field = value; });
}

void ElectrodrumsWrap::setTune(const QString& drum, double semitones)
{
    editDrum(drum, &electrodrums::DrumSettings::tuneSemitones, semitones);
}

void ElectrodrumsWrap::setDecay(const QString& drum, double factor)
{
    editDrum(drum, &electrodrums::DrumSettings::decay, factor);
}

void ElectrodrumsWrap::setLevel(const QString& drum, double db)
{
    editDrum(drum, &electrodrums::DrumSettings::levelDb, db);
}

QString ElectrodrumsWrap::drumTitle(const QString& drum) const
{
    electrodrums::Drum d;
    return electrodrums::drumFromId(drum.toStdString(), d) ? text(electrodrums::drumTitle(d)) : QString();
}

bool ElectrodrumsWrap::resetToDefaults()
{
    return valid() && commit(electrodrums::Settings{});
}

QVariantList ElectrodrumsWrap::styles() const
{
    return {QVariantMap{{"id", "808"}, {"title", "Deep"}},
            QVariantMap{{"id", "909"}, {"title", "Bright"}},
            QVariantMap{{"id", "studio"}, {"title", "Studio"}}};
}

QVariantMap ElectrodrumsWrap::drumSettings() const
{
    QVariantMap map;
    for (int i = 0; i < electrodrums::DrumCount; ++i) {
        const auto& d = m_s.drums[std::size_t(i)];
        map.insert(text(electrodrums::drumId(electrodrums::Drum(i))),
                   QVariantMap{{"tune", d.tuneSemitones}, {"decay", d.decay}, {"level", d.levelDb}});
    }
    return map;
}

QVariantList ElectrodrumsWrap::drums() const
{
    QVariantList list;
    for (int i = 0; i < electrodrums::DrumCount; ++i) {
        const auto d = electrodrums::Drum(i);
        list.append(QVariantMap{{"id", text(electrodrums::drumId(d))}, {"title", text(electrodrums::drumTitle(d))}});
    }
    return list;
}

QVariantList ElectrodrumsWrap::pads() const
{
    QVariantList list;
    for (const auto& pad : electromap::pads())
        list.append(QVariantMap{{"key", pad.key}, {"drum", text(electrodrums::drumId(pad.drum))},
                                {"name", text(pad.name)}});
    return list;
}

QString ElectrodrumsWrap::padName(int key) const
{
    return text(electromap::nameOf(key));
}

QString ElectrodrumsWrap::padDrum(int key) const
{
    const auto* pad = electromap::find(key);
    return pad ? text(electrodrums::drumId(pad->drum)) : QString();
}

QObject* ElectrodrumsWrap::engine() const
{
    return m_engine;
}

void ElectrodrumsWrap::setEngine(QObject* engine)
{
    auto* controller = qobject_cast<EngineController*>(engine);
    if (controller == m_engine) return;
    m_engine = controller;
    m_watched.reset();
    emit engineChanged();
}

void ElectrodrumsWrap::setWatchHits(bool watch)
{
    if (watch == m_watchHits) return;
    m_watchHits = watch;
    m_watched.reset();                                      // start from the counts as they are
    if (watch) { pollHits(); m_hitTimer.start(); } else { m_hitTimer.stop(); }
    emit watchHitsChanged();
}

void ElectrodrumsWrap::play(int key, int velocity)
{
    if (!m_engine || !valid()) return;
    m_engine->sendLiveNoteOn(m_trackId, key, velocity);
    emit played(key, velocity);
}

void ElectrodrumsWrap::stop(int key)
{
    if (m_engine && valid()) m_engine->sendLiveNoteOff(m_trackId, key);
}

void ElectrodrumsWrap::pollHits()
{
    if (!m_engine || !valid()) return;
    const auto instrument = m_engine->trackInstrument(m_trackId);
    if (!instrument || instrument->typeId() != "electrodrums") return;
    const auto& kit = static_cast<const ElectroDrums&>(*instrument);
    // A new kit (a new track, a buffer change): take its counts without lighting anything.
    const bool fresh = m_watched.lock() != instrument;
    m_watched = instrument;
    for (const auto& pad : electromap::pads()) {
        const auto hits = kit.hits(pad.key);
        auto& seen = m_seen[std::size_t(pad.key)];
        if (!fresh && hits.count != seen) emit hit(pad.key, hits.velocity);
        seen = hits.count;
    }
}

QString ElectrodrumsWrap::styleTitle(const QString& id) const
{
    return QString::fromStdString(electrodrums::styleTitle(id.toStdString()));
}

double ElectrodrumsWrap::steppedVolume(double db) const
{
    return electrodrums::steppedVolume(db);
}
