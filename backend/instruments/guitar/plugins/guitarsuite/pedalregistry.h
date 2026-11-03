#pragma once

#include "instruments/guitar/plugins/guitarsuite/pedalslot.h"

#include <memory>
#include <string>
#include <vector>

namespace PedalRegistry {

std::vector<std::string> availablePedals();
std::string displayName(const std::string& type);
std::shared_ptr<Processor> create(const std::string& type);

} // namespace PedalRegistry
