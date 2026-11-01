#pragma once

#include "plugins/guitarsuite/pedalslot.h"

#include <QString>
#include <QStringList>
#include <memory>

namespace PedalRegistry {
QStringList availablePedals();
QString displayName(const QString& type);
std::shared_ptr<Processor> create(const QString& type);
}
