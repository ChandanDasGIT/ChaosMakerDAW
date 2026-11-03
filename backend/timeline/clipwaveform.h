#pragma once

#include "core/audiodata.h"
#include "session/project.h"

// Validate/convert the visible clip interval to source samples, outside the audio callback.
std::vector<AudioData::Peak> clipWaveform(const AudioClip& clip, const AudioData& audio,
                                         double projectRate, int bins, double from = 0, double to = 1);
