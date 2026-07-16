#pragma once

#include "RealtimeCoefficients.h"
#include "TapeSaturator.h"

#include <juce_dsp/juce_dsp.h>

#include <vector>

// Bus (4) SLAP (docs/design-brief.md): a single-repeat delay, 50-160 ms
// (default 110 ms, deliberately NOT tempo-synced), with feedback FIXED at 0
// in v2 (dropped as a user parameter entirely - see the "single repeat"
// finding in docs/research-notes.md: "no feedback or filtering ever
// mentioned - the darkness comes from the BBD emulation itself"). Mono
// return by default (`slap_stereo` off).
//
// Because there is no feedback loop, the "progressively darker" BBD
// character has to live in the single repeat itself: a fixed lowpass
// character (tunable dark...darker via `slap_tone`) plus a soft, fixed
// saturation are applied once to the delayed tap. This is a structural
// difference from the v1 module (which voiced its character inside a
// feedback loop that no longer exists).
//
// This bus outputs the WET (delayed) signal only - the dry voice is the
// Direct path's job in the v2 parallel topology, so this bus's fader is
// effectively the slap's send/return level.
//
// Bus (4) is exempt from the busses-(1)/(2) sample-alignment invariant - it
// is a delay BY DESIGN (see docs/adr/0003). It still reports zero
// *latency*: the delay is the effect, not a compensation artefact.
//
// Stereo switch: when `slap_stereo` is OFF (default), the delay is fed the
// mono sum of the input and both output channels carry the identical echo -
// the classic mono tape slap behind a stereo-widened vocal. When ON, L/R
// are delayed and voiced independently.
class SlapDelay
{
public:
    SlapDelay() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);

    // Clears the delay line and every loop filter/state - the reset()
    // guarantee explicitly includes the delay line (a stale echo surviving
    // a transport stop is a shipped-bug class the suite has seen before).
    void reset();

    void setDelayMs (float newDelayMs) noexcept;
    void setStereoEnabled (bool shouldBeStereo) noexcept { stereoEnabled = shouldBeStereo; }

    // 0-1 (0-100%): dark...darker BBD-style voicing - scales both the
    // repeat's lowpass darkening and its saturation drive.
    void setToneProportion (float newAmount01) noexcept;

    // Replaces `block`'s contents with the wet (delayed) signal. A
    // zero-sample block is a safe no-op. No allocation occurs here.
    void process (juce::dsp::AudioBlock<float>& block) noexcept;

private:
    static constexpr float maxDelayMs = 160.0f;
    static constexpr float minDelayMs = 50.0f;

    // Tone range: the repeat's lowpass sweeps from a bright-ish 6 kHz (tone
    // = 0) down to a properly dark 2.5 kHz (tone = 1), and saturation drive
    // scales alongside it (brief: "gentle progressive HF loss (~3-5 kHz
    // lowpass character) + soft saturation baked into the repeat").
    static constexpr float toneLowPassBrightHz = 6000.0f;
    static constexpr float toneLowPassDarkHz = 2500.0f;
    static constexpr float toneSatDriveMin = 1.1f;
    static constexpr float toneSatDriveMax = 1.8f;
    static constexpr float loopFilterQ = 0.70710678f; // Butterworth

    static constexpr double smoothingTimeSeconds = 0.05;
    // Delay-time changes are smoothed much slower than other parameters so
    // dragging the Delay knob produces a mild tape-style pitch slur rather
    // than a zipper.
    static constexpr double delaySmoothingSeconds = 0.1;

    double sampleRate = 44100.0;
    bool stereoEnabled = false;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine;

    // Per-channel repeat-darkening lowpass; both channels share one
    // coefficients object, updated once per block via ArrayCoefficients
    // (see RealtimeCoefficients.h).
    std::vector<juce::dsp::IIR::Filter<float>> repeatLowPass;
    juce::dsp::IIR::Coefficients<float>::Ptr lowPassCoefficients { msrr::makeIdentityBiquad() };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> delayMsSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> toneSmoothed;

    float lastDelayMs = 110.0f;
    float lastTone01 = 0.5f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlapDelay)
};
