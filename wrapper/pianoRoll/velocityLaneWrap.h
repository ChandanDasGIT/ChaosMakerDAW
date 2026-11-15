#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// The velocity lane's drag rules for QML (backend velocitylane).
class VelocityLaneWrap : public QObject {
    Q_OBJECT
    QML_ELEMENT

public:
    explicit VelocityLaneWrap(QObject* parent = nullptr);

    // note id (as text) -> its start velocity moved by delta, kept to 0..127.
    Q_INVOKABLE QVariantMap relativePreview(const QVariantList& ids, const QVariantList& startVelocities, int delta) const;
    Q_INVOKABLE QString relativeLabel(int delta) const;
    Q_INVOKABLE QString absoluteLabel(int velocity) const;
};
