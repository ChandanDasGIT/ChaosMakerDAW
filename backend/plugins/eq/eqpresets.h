#pragma once
#include <array>
namespace eqpresets {
struct Preset { const char* name; std::array<float,10> gains; };
inline constexpr std::array<Preset,7> factory{{
    {"Flat", {0,0,0,0,0,0,0,0,0,0}},
    {"Electric Guitar", {-6,-3,0,1,2,1,2,0,-2,-4}},
    {"Warm", {0,1,2,2,1,0,-1,-2,-3,-4}},
    {"Bright", {-2,-1,0,-1,-1,0,1,2,3,2}},
    {"Mid Scoop", {0,1,2,0,-3,-4,-2,1,2,0}},
    {"Vocal Clarity", {-8,-5,-2,-1,-1,0,2,3,1,0}},
    {"Bass Cut", {-12,-9,-5,-2,0,0,0,0,0,0}}
}};
}
