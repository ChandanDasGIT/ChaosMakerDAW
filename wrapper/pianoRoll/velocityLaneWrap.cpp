#include "velocityLaneWrap.h"

#include "pianoRoll/velocitylane.h"

VelocityLaneWrap::VelocityLaneWrap(QObject* parent) : QObject(parent) {}

QVariantMap VelocityLaneWrap::relativePreview(const QVariantList& ids, const QVariantList& startVelocities, int delta) const
{
    QVariantMap preview;
    for (qsizetype i = 0; i < ids.size() && i < startVelocities.size(); ++i)
        preview.insert(ids[i].toString(), velocitylane::shifted(startVelocities[i].toInt(), delta));
    return preview;
}

QString VelocityLaneWrap::relativeLabel(int delta) const
{
    return QString::fromStdString(velocitylane::relativeLabel(delta));
}

QString VelocityLaneWrap::absoluteLabel(int velocity) const
{
    return QString::fromStdString(velocitylane::absoluteLabel(velocity));
}
