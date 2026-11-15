#include "velocitylane.h"

#include <algorithm>

int velocitylane::shifted(int velocity, int delta)
{
    return std::clamp(velocity + delta, 0, 127);
}

std::string velocitylane::relativeLabel(int delta)
{
    return (delta >= 0 ? "+" : "") + std::to_string(delta);
}

std::string velocitylane::absoluteLabel(int velocity)
{
    return "Vel " + std::to_string(velocity);
}
