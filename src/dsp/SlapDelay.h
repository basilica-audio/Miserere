#pragma once

#include "AdaaSaturator.h"
#include "FlutterGenerator.h"
#include "RealtimeCoefficients.h"
#include "TapeSaturator.h"

#include <juce_dsp/juce_dsp.h>

#include <cstdint>
#include <vector>

// Bus (4) SLAP (docs/design-brief.md): a single-repeat delay, 50-160 ms
// (default 110 ms, deliberately NOT tempo-synced), with feedback FIXED at 0
// in v2 (dropped as a user parameter entirely). Mono return by default
// (`slap_stereo` off). This bus outputs the WET (delayed) signal only.
//
// v0.5.0 "Circuit Engines" (brief F5, research-tape-echo-slap.md): the
// single repeat becomes a tape TRANSPORT done right:
//
// - **Hermite-4 fractional read**: 4-point cubic Hermite (Catmull-Rom)
//   interpolation with a double delay accumulator replaces linear
//   interpolation (linear = a fraction-dependent lowpass that pumps with
//   modulation). Exact pass-through at integer delays.
// - **Record/play ordering** (section 2.2 placement rule): saturation
//   happens on the RECORD side (tape magnetisation, before the delay
//   write - now the asymmetric ADAA tanh pair per brief F1,
//   research-neve-1073.md section 2.4), losses on the PLAY side (after the
//   read). Audible on repeats-vs-input brightness.
// - **Play-side loss voicing**: the existing Butterworth lowpass (tone
//   6 kHz -> 2.5 kHz, unchanged mapping) + one fixed head-bump peak
//   (+2 dB @ 90 Hz, Q 1.2) + a `slap_age`-scaled extra spacing-loss lowpass
//   (1st order, 8 kHz -> 3.5 kHz as age -> 1; structurally skipped at
//   age = 0) - the e^(-kd) spacing-loss law with d growing with wear.
// - **Wow/flutter** (`slap_wobble`, NEW param, default 0 = structurally
//   off): FlutterGenerator.h - two quasi-periodic oscillators + drift,
//   calibrated 0..0.5 % W&F, modulating the read position via
//   tau(t) = tau_target/(1 + m(t)). Deterministically seeded; wobble = 0
//   advances no RNG (bit-identical neutral path).
// - **Age layer** (`slap_age`, NEW param, default 0 = structurally silent):
//   tape hiss (pink-shaped, -58 dBFS at age 1) + asperity noise
//   (hiss * (0.3 + 0.7*env(y)) - signal-correlated "reads instantly as
//   tape"), injected on the wet tap only.
// - The existing 100 ms delay-time smoother stays: it IS the motor-inertia
//   behaviour (repitching glide on delay changes).
//
// Bus (4) is exempt from the busses-(1)/(2) sample-alignment invariant - it
// is a delay BY DESIGN (see docs/adr/0003). It still reports zero
// *latency*: the delay is the effect, not a compensation artefact.
//
// Stereo switch: when `slap_stereo` is OFF (default), the delay is fed the
// mono sum of the input and both output channels carry the identical echo
// (noise/flutter shared, so L == R stays exact). When ON, L/R are delayed
// and voiced independently (transport modulation and noise remain shared -
// one machine).
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

    // 0-1 (0-100%): dark...darker voicing - scales both the repeat's
    // play-side lowpass darkening and the record-side saturation drive.
    void setToneProportion (float newAmount01) noexcept;

    // 0-1: wow/flutter depth (slap_wobble). 0 = modulation structurally off.
    void setWobbleProportion (float newAmount01) noexcept;

    // 0-1: tape age (slap_age): hiss + asperity + extra spacing loss.
    // 0 = structurally silent/neutral.
    void setAgeProportion (float newAmount01) noexcept;

    // Replaces `block`'s contents with the wet (delayed) signal. A
    // zero-sample block is a safe no-op. No allocation occurs here.
    void process (juce::dsp::AudioBlock<float>& block) noexcept;

private:
    static constexpr float maxDelayMs = 160.0f;
    static constexpr float minDelayMs = 50.0f;

    // Tone range: the repeat's lowpass sweeps from a bright-ish 6 kHz (tone
    // = 0) down to a properly dark 2.5 kHz (tone = 1), and record-side
    // saturation drive scales alongside it.
    static constexpr float toneLowPassBrightHz = 6000.0f;
    static constexpr float toneLowPassDarkHz = 2500.0f;
    static constexpr float toneSatDriveMin = 1.1f;
    static constexpr float toneSatDriveMax = 1.8f;
    static constexpr float loopFilterQ = 0.70710678f; // Butterworth

    // Record-side saturator asymmetry (research-neve-1073.md section 2.4:
    // b ~ 0.05-0.15 sets the single-ended H2 signature).
    static constexpr float recordSatBias = 0.1f;

    // Play-side voicing (brief F5).
    static constexpr float headBumpFreqHz = 90.0f;
    static constexpr float headBumpGainDb = 2.0f;
    static constexpr float headBumpQ = 1.2f;
    static constexpr float spacingLossFreshHz = 8000.0f;
    static constexpr float spacingLossWornHz = 3500.0f;

    // Age noise layer.
    static constexpr float hissLevelDb = -58.0f;    // at age = 1
    static constexpr float asperityFloor = 0.3f;
    static constexpr float asperityDepth = 0.7f;
    static constexpr float asperityEnvelopeHz = 30.0f;
    static constexpr float hissShapingHz = 1500.0f; // pink-ish shaping one-pole

    static constexpr double smoothingTimeSeconds = 0.05;
    // Delay-time changes are smoothed much slower than other parameters so
    // dragging the Delay knob produces a mild tape-style pitch slur rather
    // than a zipper (motor inertia - research 3.2 item 3).
    static constexpr double delaySmoothingSeconds = 0.1;

    float readInterpolated (size_t channel, double delaySamples) const noexcept;

    double sampleRate = 44100.0;
    bool stereoEnabled = false;

    // Custom circular buffer (Hermite-4 read, double accumulator).
    std::vector<std::vector<float>> delayBuffers;
    int bufferLength = 0;
    int writeIndex = 0;

    std::vector<msrr::adaa::AsymTanhStage> recordSaturators;

    // Per-channel play-side filters; shared coefficient objects, block-rate
    // updates via ArrayCoefficients / matched fits (RealtimeCoefficients.h).
    std::vector<juce::dsp::IIR::Filter<float>> repeatLowPass;
    juce::dsp::IIR::Coefficients<float>::Ptr lowPassCoefficients { msrr::makeIdentityBiquad() };
    std::vector<juce::dsp::IIR::Filter<float>> headBump;
    juce::dsp::IIR::Coefficients<float>::Ptr headBumpCoefficients { msrr::makeIdentityBiquad() };
    std::vector<juce::dsp::IIR::Filter<float>> spacingLoss;
    juce::dsp::IIR::Coefficients<float>::Ptr spacingLossCoefficients { new juce::dsp::IIR::Coefficients<float> (1.0f, 0.0f, 1.0f, 0.0f) };

    FlutterGenerator flutter;

    // Age noise state (shared across channels - one machine).
    std::uint32_t noiseRng = 0x00C0FFEEu;
    float hissShapingState = 0.0f;
    float asperityEnvelope = 0.0f;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> delayMsSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> toneSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wobbleSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> ageSmoothed;

    float lastDelayMs = 110.0f;
    float lastTone01 = 0.5f;
    float lastWobble01 = 0.0f;
    float lastAge01 = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlapDelay)
};
