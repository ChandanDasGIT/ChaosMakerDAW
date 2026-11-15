#pragma once

#include <string>

// The velocity lane's rules when velocities are dragged.
namespace velocitylane {
// A velocity moved by `delta`, kept to 0..127.
int shifted(int velocity, int delta);
// The label while dragging: "+5", "-3", "+0" for a relative change; "Vel 64" for one note.
std::string relativeLabel(int delta);
std::string absoluteLabel(int velocity);
}
