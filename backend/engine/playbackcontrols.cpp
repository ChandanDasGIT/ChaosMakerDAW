#include "engine/playbackcontrols.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

PlaybackControls::PlaybackControls() = default;
PlaybackControls::~PlaybackControls() = default;

void PlaybackControls::setState(State s) noexcept {
    m_state = s;
}

bool PlaybackControls::setRepeatRange(double startBeat, double endBeat, double sampleRate, double bpm) {
    if (!std::isfinite(startBeat) || !std::isfinite(endBeat) || startBeat < 0.0)
        return false;
    if (endBeat <= startBeat && (startBeat != 0.0 || endBeat != 0.0))
        return false;

    if (sampleRate <= 1000.0)
        sampleRate = 48000.0;
    if (bpm <= 0.0)
        bpm = 120.0;

    const double samples = endBeat * 60.0 * sampleRate / bpm;
    if (!std::isfinite(samples) || samples >= double(INT64_MAX / 4))
        return false;

    m_repeatRange.startBeat = startBeat;
    m_repeatRange.endBeat = endBeat;
    return true;
}

PlaybackControls::LoopSamples PlaybackControls::calculateLoopSamples(
    double contentEndBeats, int numerator, int denominator, double sampleRate, double bpm) const {
    LoopSamples res;
    if (sampleRate <= 1000.0)
        sampleRate = 48000.0;
    if (bpm <= 0.0)
        bpm = 120.0;
    if (numerator <= 0)
        numerator = 4;
    if (denominator <= 0)
        denominator = 4;

    double start = hasExplicitRepeatRange() ? m_repeatRange.startBeat : 0.0;
    double end = hasExplicitRepeatRange() ? m_repeatRange.endBeat
                                          : std::max(contentEndBeats, numerator * 4.0 / denominator);

    if (end <= start)
        end = start + (numerator * 4.0 / denominator);

    const double a = start * 60.0 * sampleRate / bpm;
    const double b = end * 60.0 * sampleRate / bpm;

    if (!std::isfinite(a) || !std::isfinite(b) || b >= double(INT64_MAX / 4) ||
        std::llround(b) <= std::llround(a)) {
        return res;
    }

    res.startSample = std::llround(a);
    res.endSample = std::llround(b);
    res.isValid = true;
    return res;
}

PlaybackControls::Action PlaybackControls::parseAction(const std::string& name) noexcept {
    if (name == "play") return Action::Play;
    if (name == "pause") return Action::Pause;
    if (name == "stop") return Action::Stop;
    if (name == "togglePlay") return Action::TogglePlay;
    if (name == "record") return Action::Record;
    if (name == "toggleRecord") return Action::ToggleRecord;
    if (name == "rewind") return Action::Rewind;
    if (name == "forward") return Action::Forward;
    if (name == "repeat") return Action::Repeat;
    if (name == "toggleRepeat") return Action::ToggleRepeat;
    return Action::None;
}

std::string PlaybackControls::actionToString(Action action) noexcept {
    switch (action) {
        case Action::Play: return "play";
        case Action::Pause: return "pause";
        case Action::Stop: return "stop";
        case Action::TogglePlay: return "togglePlay";
        case Action::Record: return "record";
        case Action::ToggleRecord: return "toggleRecord";
        case Action::Rewind: return "rewind";
        case Action::Forward: return "forward";
        case Action::Repeat: return "repeat";
        case Action::ToggleRepeat: return "toggleRepeat";
        case Action::None: return "none";
    }
    return "none";
}

PlaybackControls::ActionPlan PlaybackControls::evaluateAction(
    Action action, double currentBeat, double sampleRate, double bpm,
    int numerator, int denominator, double contentEndBeats,
    bool audioReady, bool inputConnected, bool outputConnected, bool takeBusy) const {
    ActionPlan plan;

    if (action == Action::TogglePlay) {
        action = isPlaying() ? Action::Pause : Action::Play;
    } else if (action == Action::ToggleRepeat) {
        action = Action::Repeat;
    } else if (action == Action::ToggleRecord) {
        action = Action::Record;
    }

    if (action == Action::Stop) {
        plan.command = ActionPlan::Command::Stop;
        return plan;
    }

    if (action == Action::Pause) {
        plan.command = ActionPlan::Command::Pause;
        return plan;
    }

    if (action == Action::Play) {
        if (!audioReady) {
            plan.command = ActionPlan::Command::Error;
            plan.errorMessage = "Audio is unavailable; connect JACK/PipeWire before using transport.";
            return plan;
        }
        plan.command = ActionPlan::Command::Play;
        return plan;
    }

    if (action == Action::Rewind) {
        if (takeBusy) {
            plan.command = ActionPlan::Command::Error;
            plan.errorMessage = "Finish the take before seeking.";
            return plan;
        }
        plan.command = ActionPlan::Command::Seek;
        plan.argA = 0;
        return plan;
    }

    if (action == Action::Forward) {
        if (takeBusy) {
            plan.command = ActionPlan::Command::Error;
            plan.errorMessage = "Finish the take before seeking.";
            return plan;
        }
        double nextBeat = calculateForwardSeekBeat(currentBeat, numerator, denominator);
        int64_t targetSample = beatsToSamples(nextBeat, sampleRate, bpm);
        plan.command = ActionPlan::Command::Seek;
        plan.argA = targetSample;
        return plan;
    }

    if (action == Action::Repeat) {
        if (takeBusy) {
            plan.command = ActionPlan::Command::Error;
            plan.errorMessage = "Finish the take before changing repeat.";
            return plan;
        }
        if (m_repeating) {
            plan.command = ActionPlan::Command::ClearLoop;
        } else {
            auto loop = calculateLoopSamples(contentEndBeats, numerator, denominator, sampleRate, bpm);
            if (!loop.isValid) {
                plan.command = ActionPlan::Command::Error;
                plan.errorMessage = "Invalid loop range.";
                return plan;
            }
            plan.command = ActionPlan::Command::SetLoop;
            plan.argA = loop.startSample;
            plan.argB = loop.endSample;
        }
        return plan;
    }

    if (action == Action::Record) {
        if (takeBusy) {
            plan.command = ActionPlan::Command::Pause;
            return plan;
        }
        if (!audioReady || !inputConnected || !outputConnected) {
            plan.command = ActionPlan::Command::Error;
            plan.errorMessage = "Recording needs a connected audio input and output.";
            return plan;
        }
        plan.command = ActionPlan::Command::Record;
        return plan;
    }

    return plan;
}

double PlaybackControls::samplesToBeats(int64_t samples, double sampleRate, double bpm) noexcept {
    if (sampleRate <= 0.0 || bpm <= 0.0)
        return 0.0;
    return (double(samples) / sampleRate) * (bpm / 60.0);
}

int64_t PlaybackControls::beatsToSamples(double beats, double sampleRate, double bpm) noexcept {
    if (sampleRate <= 0.0 || bpm <= 0.0 || beats <= 0.0)
        return 0;
    double s = beats * 60.0 * sampleRate / bpm;
    if (!std::isfinite(s) || s < 0.0)
        return 0;
    if (s >= double(INT64_MAX / 4))
        return INT64_MAX / 4;
    return std::llround(s);
}

double PlaybackControls::samplesToSeconds(int64_t samples, double sampleRate) noexcept {
    if (sampleRate <= 0.0)
        return 0.0;
    return double(samples) / sampleRate;
}

int64_t PlaybackControls::secondsToSamples(double seconds, double sampleRate) noexcept {
    if (sampleRate <= 0.0 || seconds <= 0.0)
        return 0;
    double s = seconds * sampleRate;
    if (!std::isfinite(s) || s < 0.0)
        return 0;
    return std::llround(s);
}

double PlaybackControls::calculateForwardSeekBeat(double currentBeat, int numerator, int denominator) noexcept {
    if (numerator <= 0) numerator = 4;
    if (denominator <= 0) denominator = 4;
    double barBeats = numerator * 4.0 / denominator;
    return currentBeat + barBeats;
}

std::string PlaybackControls::formatTime(double seconds) noexcept {
    if (!std::isfinite(seconds) || seconds < 0.0)
        seconds = 0.0;
    int64_t totalMs = static_cast<int64_t>(std::floor(seconds * 1000.0));
    int64_t hours = totalMs / 3600000;
    int64_t minutes = (totalMs / 60000) % 60;
    int64_t secs = (totalMs / 1000) % 60;
    int64_t ms = totalMs % 1000;

    std::ostringstream ss;
    ss << std::setfill('0')
       << std::setw(2) << hours << ":"
       << std::setw(2) << minutes << ":"
       << std::setw(2) << secs << "."
       << std::setw(3) << ms;
    return ss.str();
}

std::string PlaybackControls::formatBarsBeats(double beats, int numerator, int denominator) noexcept {
    if (!std::isfinite(beats) || beats < 0.0)
        beats = 0.0;
    if (numerator <= 0) numerator = 4;
    if (denominator <= 0) denominator = 4;

    double barLength = numerator * 4.0 / denominator;
    int bar = static_cast<int>(std::floor(beats / barLength)) + 1;
    double remBeats = beats - (bar - 1) * barLength;
    int beatInBar = static_cast<int>(std::floor(remBeats)) + 1;
    int tick = static_cast<int>(std::floor((remBeats - std::floor(remBeats)) * 100.0));

    std::ostringstream ss;
    ss << bar << "." << beatInBar << "." << std::setfill('0') << std::setw(2) << tick;
    return ss.str();
}
