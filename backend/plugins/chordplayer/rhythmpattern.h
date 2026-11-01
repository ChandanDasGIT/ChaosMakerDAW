#pragma once
#include <vector>

// One moment in a bar. position runs from 0.0 (start of the bar) to 1.0 (end of the bar).
struct PatternStep {
    double position = 0.0;
    double length = 0.25;                                 // also a fraction of the bar
    int voice = -1;                                       // -1: the whole chord. 0 and up: one note, for arpeggios
    float velocity = 0.8f;
};

struct RhythmPattern {
    int numerator = 4;                                    // the time signature it was written for
    int denominator = 4;
    std::vector<PatternStep> steps;
};
