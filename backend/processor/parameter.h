#pragma once
#include <algorithm>
#include <atomic>
#include <string>

// One knob. Any thread may get or set it, without locks.
class Parameter {
public:
    Parameter(std::string id, std::string name, float min, float max, float def, std::string unit = {})
        : id(std::move(id)), name(std::move(name)), unit(std::move(unit)),
          min(min), max(max), def(def), m_value(def) {}

    float get() const noexcept { return m_value.load(std::memory_order_relaxed); }
    void set(float value) noexcept { m_value.store(std::clamp(value, min, max), std::memory_order_relaxed); }

    const std::string id, name, unit;
    const float min, max, def;

private:
    std::atomic<float> m_value;
};
