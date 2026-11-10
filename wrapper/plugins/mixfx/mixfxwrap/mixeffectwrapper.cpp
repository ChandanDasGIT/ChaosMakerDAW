#include "mixeffectwrapper.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"
#include "plugins/mixfx/mixfxspec.h"

#include <QMetaObject>
#include <algorithm>
#include <cmath>

namespace {
QString text(std::string_view s) { return QString::fromUtf8(s.data(), qsizetype(s.size())); }
}

MixEffectWrapper::MixEffectWrapper(QObject* parent) : QObject(parent)
{
    m_meterTimer.setInterval(33);
    connect(&m_meterTimer, &QTimer::timeout, this, &MixEffectWrapper::pollMeter);
}

QObject* MixEffectWrapper::trackModel() const { return m_model; }

void MixEffectWrapper::setTrackModel(QObject* model)
{
    auto* tracks = qobject_cast<TrackListWrapper*>(model);
    if (tracks == m_model) return;
    if (m_changeConnection) disconnect(m_changeConnection);
    m_model = tracks;
    if (m_model)
        m_changeConnection = connect(m_model, &TrackListWrapper::mixEffectChanged, this, [this](int id, const QString& type) {
            if (id == m_trackId && type == m_type) emit valuesChanged();
        });
    emit trackModelChanged();
    emit valuesChanged();
}

void MixEffectWrapper::setTrackId(int id)
{
    if (id == m_trackId) return;
    m_trackId = id;
    m_reductionDb = 0.0;
    emit trackIdChanged();
    emit valuesChanged();
    emit reductionChanged();
}

void MixEffectWrapper::setEffectType(const QString& type)
{
    if (type == m_type) return;
    m_type = type;
    emit effectTypeChanged();
    emit valuesChanged();
}

void MixEffectWrapper::setEngine(QObject* engine)
{
    if (engine == m_engine) return;
    m_engine = engine;
    emit engineChanged();
}

void MixEffectWrapper::setMetering(bool on)
{
    if (on == m_metering) return;
    m_metering = on;
    if (on && hasMeter()) m_meterTimer.start();
    else {
        m_meterTimer.stop();
        m_reductionDb = 0.0;
        emit reductionChanged();
    }
    emit meteringChanged();
}

QString MixEffectWrapper::title() const
{
    const auto* spec = mixfx::find(m_type.toStdString());
    return spec ? text(spec->title) : QString();
}

QVariantList MixEffectWrapper::cards() const
{
    QVariantList result;
    const auto* spec = mixfx::find(m_type.toStdString());
    if (!spec) return result;
    for (const auto& c : spec->controls) {
        const QString card = text(c.card);
        if (result.isEmpty() || result.last().toMap().value("title").toString() != card)
            result.append(QVariantMap{{"title", card}, {"controls", QVariantList{}}});
        QStringList choices;
        for (const auto& choice : c.choices) choices << text(choice);
        const char* kind = c.kind == mixfx::ControlKind::Choice ? "choice"
                         : c.kind == mixfx::ControlKind::Switch ? "switch" : "slider";
        auto map = result.last().toMap();
        auto controls = map.value("controls").toList();
        controls.append(QVariantMap{{"id", text(c.id)}, {"name", text(c.name)}, {"kind", QString::fromLatin1(kind)},
                                    {"min", c.min}, {"max", c.max}, {"default", c.def}, {"unit", text(c.unit)},
                                    {"choices", choices}});
        map["controls"] = controls;
        result.last() = map;
    }
    return result;
}

bool MixEffectWrapper::hasMeter() const
{
    // The dynamics plugins turn things down; the reverbs do not.
    return m_type == QLatin1String("compressor") || m_type == QLatin1String("deesser");
}

QVariantMap MixEffectWrapper::values() const
{
    QVariantMap result;
    if (!m_model) return result;
    const auto settings = m_model->mixEffect(m_trackId, m_type.toStdString());
    if (!settings) return result;
    for (const auto& [id, v] : settings->values) result.insert(QString::fromStdString(id), v);
    return result;
}

bool MixEffectWrapper::bypassed() const
{
    if (!m_model) return false;
    const auto settings = m_model->mixEffect(m_trackId, m_type.toStdString());
    return settings && settings->bypassed;
}

void MixEffectWrapper::setBypassed(bool bypassed)
{
    if (!m_model) return;
    auto settings = m_model->mixEffect(m_trackId, m_type.toStdString());
    if (!settings || settings->bypassed == bypassed) return;
    settings->bypassed = bypassed;
    m_model->setMixEffect(m_trackId, m_type, *settings);
}

bool MixEffectWrapper::setValue(const QString& id, double value)
{
    const auto* spec = mixfx::find(m_type.toStdString());
    if (!m_model || !spec || !mixfx::findControl(*spec, id.toStdString()) || !std::isfinite(value)) return false;
    auto settings = m_model->mixEffect(m_trackId, m_type.toStdString());
    if (!settings) return false;
    settings->values[id.toStdString()] = float(value);
    return m_model->setMixEffect(m_trackId, m_type, *settings);
}

bool MixEffectWrapper::resetValue(const QString& id)
{
    const auto* spec = mixfx::find(m_type.toStdString());
    const auto* control = spec ? mixfx::findControl(*spec, id.toStdString()) : nullptr;
    return control && setValue(id, control->def);
}

QString MixEffectWrapper::formatValue(const QString& id, double value) const
{
    const auto* spec = mixfx::find(m_type.toStdString());
    const auto* c = spec ? mixfx::findControl(*spec, id.toStdString()) : nullptr;
    if (!c || !std::isfinite(value)) return QString();
    if (c->kind == mixfx::ControlKind::Switch) return value >= 0.5 ? QStringLiteral("On") : QStringLiteral("Off");
    if (c->kind == mixfx::ControlKind::Choice) {
        const auto index = std::size_t(std::clamp<long>(std::lround(value), 0, long(c->choices.size()) - 1));
        return c->choices.empty() ? QString() : text(c->choices[index]);
    }
    const QString unit = text(c->unit);
    const QString minus = QStringLiteral("−");
    const auto signedNumber = [&](double v, int decimals) {
        const QString n = QString::number(std::abs(v), 'f', decimals);
        return v < -0.0005 ? minus + n : n;
    };
    if (unit == QLatin1String("Hz"))
        return value >= 1000.0 ? QString::number(value / 1000.0, 'f', value >= 10000.0 ? 1 : 2) + QStringLiteral(" kHz")
                               : QString::number(value, 'f', 0) + QStringLiteral(" Hz");
    if (unit == QLatin1String("ms"))
        return value >= 1000.0 ? QString::number(value / 1000.0, 'f', 2) + QStringLiteral(" s")
                               : QString::number(value, 'f', value < 10.0 ? 1 : 0) + QStringLiteral(" ms");
    if (unit == QLatin1String("s")) return QString::number(value, 'f', value < 10.0 ? 2 : 1) + QStringLiteral(" s");
    if (unit == QLatin1String(":1")) return QString::number(value, 'f', 1) + QStringLiteral(":1");
    if (unit == QLatin1String("x")) return QString::number(value, 'f', 2) + QStringLiteral("\u00d7");
    if (unit == QLatin1String("%")) return QString::number(value, 'f', 0) + QStringLiteral(" %");
    if (unit == QLatin1String("dB")) return signedNumber(value, 1) + QStringLiteral(" dB");
    if (unit.isEmpty()) return signedNumber(value, 2);
    return signedNumber(value, 1) + QLatin1Char(' ') + unit;
}

double MixEffectWrapper::step(const QString& id) const
{
    const auto* spec = mixfx::find(m_type.toStdString());
    const auto* c = spec ? mixfx::findControl(*spec, id.toStdString()) : nullptr;
    if (!c) return 0.0;
    if (c->kind != mixfx::ControlKind::Slider) return 1.0;
    const double range = c->max - c->min;
    return range <= 2.0 ? 0.01 : range <= 30.0 ? 0.1 : 1.0;
}

void MixEffectWrapper::pollMeter()
{
    double db = 0.0;
    if (m_engine)
        QMetaObject::invokeMethod(m_engine, "mixEffectReductionDb", Q_RETURN_ARG(double, db),
                                  Q_ARG(int, m_trackId), Q_ARG(QString, m_type));
    if (std::abs(db - m_reductionDb) < 0.05) return;
    m_reductionDb = db;
    emit reductionChanged();
}
