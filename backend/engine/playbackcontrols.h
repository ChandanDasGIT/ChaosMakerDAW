#pragma once

#include <cstdint>
#include <string>

class PlaybackControls {
public:
    enum class State {
        Stopped,
        Playing,
        Paused,
        Recording
    };

    enum class Action {
        None,
        Play,
        Pause,
        Stop,
        TogglePlay,
        Record,
        ToggleRecord,
        Rewind,
        Forward,
        Repeat,
        ToggleRepeat
    };

    struct RepeatRange {
        double startBeat = 0.0;
        double endBeat = 0.0;
    };

    struct LoopSamples {
        int64_t startSample = 0;
        int64_t endSample = 0;
        bool isValid = false;
    };

    struct ActionPlan {
        enum class Command {
            None,
            Play,
            Pause,
            Stop,
            Seek,
            SetLoop,
            ClearLoop,
            Record,
            Error
        };

        Command command = Command::None;
        int64_t argA = 0;
        int64_t argB = 0;
        std::string errorMessage;
    };

    PlaybackControls();
    ~PlaybackControls();

    // Transport state
    State state() const noexcept { return m_state; }
    void setState(State s) noexcept;

    bool isPlaying() const noexcept { return m_state == State::Playing || m_state == State::Recording; }
    bool isRecording() const noexcept { return m_state == State::Recording; }
    bool isRepeating() const noexcept { return m_repeating; }
    void setRepeating(bool r) noexcept { m_repeating = r; }

    bool isCountingIn() const noexcept { return m_countingIn; }
    void setCountingIn(bool c) noexcept { m_countingIn = c; }

    // Repeat loop range
    RepeatRange repeatRange() const noexcept { return m_repeatRange; }
    bool setRepeatRange(double startBeat, double endBeat, double sampleRate, double bpm);
    bool hasExplicitRepeatRange() const noexcept { return m_repeatRange.endBeat > m_repeatRange.startBeat; }

    LoopSamples calculateLoopSamples(double contentEndBeats, int numerator, int denominator,
                                     double sampleRate, double bpm) const;

    // Action evaluation
    static Action parseAction(const std::string& name) noexcept;
    static std::string actionToString(Action action) noexcept;

    ActionPlan evaluateAction(Action action, double currentBeat, double sampleRate, double bpm,
                              int numerator, int denominator, double contentEndBeats,
                              bool audioReady, bool inputConnected, bool outputConnected,
                              bool takeBusy) const;

    // Position conversions and formatting
    static double samplesToBeats(int64_t samples, double sampleRate, double bpm) noexcept;
    static int64_t beatsToSamples(double beats, double sampleRate, double bpm) noexcept;
    static double samplesToSeconds(int64_t samples, double sampleRate) noexcept;
    static int64_t secondsToSamples(double seconds, double sampleRate) noexcept;
    static double calculateForwardSeekBeat(double currentBeat, int numerator, int denominator) noexcept;

    static std::string formatTime(double seconds) noexcept;
    static std::string formatBarsBeats(double beats, int numerator, int denominator) noexcept;

private:
    State m_state = State::Stopped;
    bool m_repeating = false;
    bool m_countingIn = false;
    RepeatRange m_repeatRange;
};
