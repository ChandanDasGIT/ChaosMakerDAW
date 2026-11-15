#include "electronicWrap.h"
#include "instruments/drums/electronic/electrodrums.h"
#include "instruments/drums/electronic/electromap.h"
#include "wrapper/common/engine/engineController.h"
#include "wrapper/common/trackListModelWrap.h"

#include <QVariantMap>

namespace {

QString text(std::string_view s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

} // namespace

ElectronicWrap::ElectronicWrap(QObject* parent)
    : QObject(parent)
{
    m_hitTimer.setInterval(33);
    connect(&m_hitTimer, &QTimer::timeout, this, &ElectronicWrap::pollHits);
}

QObject* ElectronicWrap::trackModel() const
{
    return m_trackModel;
}

void ElectronicWrap::setTrackModel(QObject* model)
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
        connect(m_trackModel, &TrackListModelWrap::projectChanged, this, &ElectronicWrap::reload);
        connect(m_trackModel, &QObject::destroyed, this, &ElectronicWrap::reload);
    }
    emit trackModelChanged();
    reload();
}

void ElectronicWrap::setTrackId(int id)
{
    if (id == m_trackId) return;
    m_trackId = id;
    m_watched.reset();                                      // another track's kit: new counts
    emit trackIdChanged();
    reload();
}

bool ElectronicWrap::valid() const
{
    return m_trackModel && m_trackModel->electroDrums(m_trackId) != nullptr;
}

void ElectronicWrap::reload()
{
    const auto* stored = m_trackModel ? m_trackModel->electroDrums(m_trackId) : nullptr;
    m_s = stored ? *stored : electrodrums::Settings{};
    emit settingsChanged();
}

bool ElectronicWrap::commit(const electrodrums::Settings& settings)
{
    // setElectroDrums sanitises and emits electroDrumsChanged (reloading every panel on the
    // track). Reload here too: a value clamped back to what was stored emits nothing, but the
    // control that sent it must still snap back.
    const bool ok = m_trackModel->setElectroDrums(m_trackId, settings);
    reload();
    return ok;
}

QString ElectronicWrap::style() const
{
    return text(electrodrums::styleId(m_s.style));
}

void ElectronicWrap::setStyle(const QString& id)
{
    electrodrums::Style style;
    if (!electrodrums::styleFromId(id.toStdString(), style)) return;
    edit([style](auto& s) { s.style = style; });
}

const electrodrums::DrumSettings* ElectronicWrap::find(const QString& drum) const
{
    electrodrums::Drum d;
    if (!electrodrums::drumFromId(drum.toStdString(), d)) return nullptr;
    return &m_s.drum(d);
}

double ElectronicWrap::tune(const QString& drum) const
{
    const auto* d = find(drum);
    return d ? d->tuneSemitones : 0.0;
}

double ElectronicWrap::decay(const QString& drum) const
{
    const auto* d = find(drum);
    return d ? d->decay : 0.0;
}

double ElectronicWrap::level(const QString& drum) const
{
    const auto* d = find(drum);
    return d ? d->levelDb : 0.0;
}

bool ElectronicWrap::editDrum(const QString& drum, double electrodrums::DrumSettings::*field, double value)
{
    electrodrums::Drum d;
    if (!electrodrums::drumFromId(drum.toStdString(), d)) return false;
    return edit([d, field, value](auto& s) { s.drum(d).*field = value; });
}

void ElectronicWrap::setTune(const QString& drum, double semitones)
{
    editDrum(drum, &electrodrums::DrumSettings::tuneSemitones, semitones);
}

void ElectronicWrap::setDecay(const QString& drum, double factor)
{
    editDrum(drum, &electrodrums::DrumSettings::decay, factor);
}

void ElectronicWrap::setLevel(const QString& drum, double db)
{
    editDrum(drum, &electrodrums::DrumSettings::levelDb, db);
}

QString ElectronicWrap::drumTitle(const QString& drum) const
{
    electrodrums::Drum d;
    return electrodrums::drumFromId(drum.toStdString(), d) ? text(electrodrums::drumTitle(d)) : QString();
}

bool ElectronicWrap::resetToDefaults()
{
    return valid() && commit(electrodrums::Settings{});
}

QVariantList ElectronicWrap::styles() const
{
    return {QVariantMap{{"id", "808"}, {"title", "Deep"}},
            QVariantMap{{"id", "909"}, {"title", "Bright"}},
            QVariantMap{{"id", "studio"}, {"title", "Studio"}}};
}

QVariantMap ElectronicWrap::drumSettings() const
{
    QVariantMap map;
    for (int i = 0; i < electrodrums::DrumCount; ++i) {
        const auto& d = m_s.drums[std::size_t(i)];
        map.insert(text(electrodrums::drumId(electrodrums::Drum(i))),
                   QVariantMap{{"tune", d.tuneSemitones}, {"decay", d.decay}, {"level", d.levelDb}});
    }
    return map;
}

QVariantList ElectronicWrap::drums() const
{
    QVariantList list;
    for (int i = 0; i < electrodrums::DrumCount; ++i) {
        const auto d = electrodrums::Drum(i);
        list.append(QVariantMap{{"id", text(electrodrums::drumId(d))}, {"title", text(electrodrums::drumTitle(d))}});
    }
    return list;
}

QVariantList ElectronicWrap::pads() const
{
    QVariantList list;
    for (const auto& pad : electromap::pads())
        list.append(QVariantMap{{"key", pad.key}, {"drum", text(electrodrums::drumId(pad.drum))},
                                {"name", text(pad.name)}});
    return list;
}

QString ElectronicWrap::padName(int key) const
{
    return text(electromap::nameOf(key));
}

QString ElectronicWrap::padDrum(int key) const
{
    const auto* pad = electromap::find(key);
    return pad ? text(electrodrums::drumId(pad->drum)) : QString();
}

QObject* ElectronicWrap::engine() const
{
    return m_engine;
}

void ElectronicWrap::setEngine(QObject* engine)
{
    auto* controller = qobject_cast<EngineController*>(engine);
    if (controller == m_engine) return;
    m_engine = controller;
    m_watched.reset();
    emit engineChanged();
}

void ElectronicWrap::setWatchHits(bool watch)
{
    if (watch == m_watchHits) return;
    m_watchHits = watch;
    m_watched.reset();                                      // start from the counts as they are
    if (watch) { pollHits(); m_hitTimer.start(); } else { m_hitTimer.stop(); }
    emit watchHitsChanged();
}

void ElectronicWrap::play(int key, int velocity)
{
    if (!m_engine || !valid()) return;
    m_engine->sendLiveNoteOn(m_trackId, key, velocity);
    emit played(key, velocity);
}

void ElectronicWrap::stop(int key)
{
    if (m_engine && valid()) m_engine->sendLiveNoteOff(m_trackId, key);
}

void ElectronicWrap::pollHits()
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

QString ElectronicWrap::styleTitle(const QString& id) const
{
    return QString::fromStdString(electrodrums::styleTitle(id.toStdString()));
}

double ElectronicWrap::steppedVolume(double db) const
{
    return electrodrums::steppedVolume(db);
}
