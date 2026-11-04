#pragma once
#include "processor/processor.h"

#include <memory>
#include <string_view>

std::shared_ptr<Processor> createInstrument(std::string_view typeId);
