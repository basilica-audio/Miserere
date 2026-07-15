#pragma once

#include "RealtimeCoefficients.h"
#include "TapeSaturator.h"

#include <juce_dsp/juce_dsp.h>

#include <vector>

// Bus D's slap delay: fractional delay 60-180 ms (default 110 ms), feedback
// 0-30%, a bandpass (HP 200 Hz / LP 5 kHz defaults, tunable) plus soft tape
// saturation inside the feedback loop, and a mono switch (brief).
//
// This bus outputs the WET (delayed) signal only - the dry voice is Bus A's
// job in the parallel topology, so Bus D's fader is effectively the slap's
// send/return level.
//
// Loop structure, per channel:
//
//   in -> [+] -> delay line -> out (wet)
//          ^                     |
//          |                     v
//          +-- x fb <- sat <- LP <- HP   (feedback path)
//
// Each round trip through the loop is high-passed, low-passed and softly
// saturated (a gentle fixed tanh drive - see TapeSaturator.h), so repeats
// get progressively darker, thinner and softer like a worn tape echo. With
// feedback capped at 30% and the saturator strictly non-expanding
// (|tanh(gx)*comp| <= comp ~= 1.06 bounded), the loop is unconditionally
// stable: even full-scale input decays geometrically (see
// tests/SlapDelayTests.cpp's 10 s noise stability test).
//
// Bus D is the one bus exempt from the suite's sample-alignment discipline -
// it is a delay BY DESIGN (see docs/adr/0003-parallel-bus-topology.md). It
// still reports zero *latency*: the delay is the effect, not a compensation
// artefact.
//
// Mono switch: when enabled, the delay is fed the mono sum of the input and
// both output channels carry the identical echo - the classic mono tape
// slap behind a stereo-widened vocal.
class SlapDelay
{
public:
    SlapDelay() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);

    // Clears the delay line and every loop filter/state - the M1 reset()
    // guarantee explicitly includes the delay line (a stale echo surviving
    // a transport stop is a shipped-bug class the suite has seen before).
    void reset();

    void setDelayMs (float newDelayMs) noexcept;
    void setFeedbackProportion (float newFeedback01) noexcept; // 0..0.3
    void setLoopHighPassHz (float newFrequencyHz) noexcept;
    void setLoopLowPassHz (float newFrequencyHz) noexcept;
    void setMonoEnabled (bool shouldBeMono) noexcept { monoEnabled = shouldBeMono; }

    // Replaces `block`'s contents with the wet (delayed) signal. A
    // zero-sample block is a safe no-op. No allocation occurs here.
    void process (juce::dsp::AudioBlock<float>& block) noexcept;

private:
    static constexpr float maxDelayMs = 180.0f;
    static constexpr float minDelayMs = 60.0f;
    // Gentle fixed tape-soft drive inside the loop (~+2.5 dB into tanh):
    // enough to round repeat transients, far below distortion territory.
    static constexpr float loopDriveLinear = 1.33f;
    static constexpr double smoothingTimeSeconds = 0.05;
    // Delay-time changes are smoothed much slower than other parameters so
    // dragging the Delay knob produces a mild tape-style pitch slur rather
    // than a zipper.
    static constexpr double delaySmoothingSeconds = 0.1;

    double sampleRate = 44100.0;
    bool monoEnabled = false;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine;

    // Per-channel 2nd-order loop filters; all channels share one
    // coefficients object per filter type, updated once per block via
    // ArrayCoefficients (see RealtimeCoefficients.h).
    std::vector<juce::dsp::IIR::Filter<float>> loopHighPass;
    std::vector<juce::dsp::IIR::Filter<float>> loopLowPass;
    juce::dsp::IIR::Coefficients<float>::Ptr highPassCoefficients { msrr::makeIdentityBiquad() };
    juce::dsp::IIR::Coefficients<float>::Ptr lowPassCoefficients { msrr::makeIdentityBiquad() };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> delayMsSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> feedbackSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> highPassSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowPassSmoothed;

    float lastDelayMs = 110.0f;
    float lastFeedback01 = 0.15f;
    float lastHighPassHz = 200.0f;
    float lastLowPassHz = 5000.0f;

    float loopSatCompensation = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlapDelay)
};
