#pragma once

#include "RealtimeCoefficients.h"

#include <juce_dsp/juce_dsp.h>

// A FET-style feed-forward compressor/limiter, used twice in Miserere:
//
//   Bus A ("Direct" chain): ratio {4:1, 8:1}, attack 0.1-10 ms, release
//   50-1100 ms, makeup, GR metering value exposed (brief). All optional
//   character features below stay off.
//
//   Bus C ("Smash"): the all-buttons character - ratio ~20:1, attack
//   0.05-0.8 ms, release 50-200 ms with program-dependent shortening,
//   sidechain tilt +6 dB @ 2 kHz (mid-forward), drive 0-12 dB, output trim
//   (brief). Aggressive by design.
//
// Hand-rolled (mirroring the suite's DeEsser/GentleCompressor pattern)
// rather than wrapping juce::dsp::Compressor, for three reasons:
// 1) direct access to the per-block gain-reduction value for the "GR
//    metering value exposed" requirement,
// 2) the sidechain tilt and program-dependent release have no equivalent in
//    juce::dsp::Compressor's fixed ballistics, and
// 3) with envelope <= threshold the computed reduction is an exact 0 dB (a
//    clamped branch, not an asymptotic limit), so at Threshold == 0 dB (its
//    maximum) any signal below 0 dBFS never trips the compressor - the
//    bit-exact identity the M1 null test's "comp threshold at max" relies
//    on (drive/makeup/trim at 0 dB multiply by exactly 1.0f).
//
// Everything here is minimum-phase/causal with no lookahead: gain is a pure
// per-sample multiply, and the sidechain tilt filter only shapes the
// *detector* copy (never the audio path), so this module adds zero latency
// and zero phase shift - Busses A and C stay sample-aligned with each other
// and with Bus B per the parallel-bus phase discipline
// (docs/architecture.md).
//
// Detection/reduction is per-channel independent (not stereo-linked) - the
// same acceptable v0.1 simplification documented across the suite's other
// hand-rolled dynamics (see e.g. sibling seraph's DeEsser.h).
class FetCompressor
{
public:
    FetCompressor() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    void setRatio (float newRatio) noexcept { ratio = juce::jmax (1.0f, newRatio); }
    void setThresholdDb (float newThresholdDb) noexcept;
    void setAttackMs (float newAttackMs) noexcept { attackMs = juce::jmax (0.01f, newAttackMs); }
    void setReleaseMs (float newReleaseMs) noexcept { releaseMs = juce::jmax (1.0f, newReleaseMs); }
    void setMakeupDb (float newMakeupDb) noexcept { makeupGainLinear = juce::Decibels::decibelsToGain (newMakeupDb); }

    // Bus C character features - all default off/0 dB so Bus A's instance
    // never pays for them.

    // Input drive into the threshold: the all-buttons "slam the input"
    // control. Applied to both the audio path and the detector, deliberately
    // uncompensated - driving harder is *meant* to be louder and more
    // compressed. 0 dB multiplies by exactly 1.0f.
    void setDriveDb (float newDriveDb) noexcept { driveGainLinear = juce::Decibels::decibelsToGain (newDriveDb); }

    // Post output trim. 0 dB multiplies by exactly 1.0f.
    void setOutputTrimDb (float newTrimDb) noexcept { outputTrimLinear = juce::Decibels::decibelsToGain (newTrimDb); }

    // Mid-forward sidechain: a fixed +6 dB peak @ 2 kHz applied to the
    // detector copy only (the audio path is untouched), so midrange-heavy
    // program (vocals) trips the limiter harder than lows/highs.
    void setSidechainTiltEnabled (bool shouldBeEnabled) noexcept { sidechainTiltEnabled = shouldBeEnabled; }

    // All-buttons program-dependent release: the effective release time
    // shortens as gain reduction deepens (heavier GR recovers
    // proportionally faster - the classic all-buttons "pumping forward"
    // feel). Recomputed once per block from the previous block's GR.
    void setProgramDependentReleaseEnabled (bool shouldBeEnabled) noexcept { programDependentRelease = shouldBeEnabled; }

    // Processes `block` in place. A zero-sample block is a safe no-op. No
    // allocation occurs here.
    void process (juce::dsp::AudioBlock<float>& block) noexcept;

    // Current gain reduction in dB (positive value = that much reduction),
    // the peak across channels in the last processed block - exposed for
    // metering/tests, not required for correct audio processing.
    float getCurrentGainReductionDb() const noexcept { return currentGainReductionDb; }

private:
    static constexpr double smoothingTimeSeconds = 0.05;

    static constexpr float sidechainTiltFreqHz = 2000.0f;
    static constexpr float sidechainTiltGainDb = 6.0f;
    static constexpr float sidechainTiltQ = 0.707f; // broad mid-forward emphasis, not a narrow notch

    // Release-shortening slope for the all-buttons character: at ~10 dB GR
    // the effective release is releaseMs / (1 + 10 * 0.12) = ~45% of the
    // set value; clamped so it never collapses below a quarter of it.
    static constexpr float releaseShorteningPerDb = 0.12f;
    static constexpr float minReleaseFactor = 0.25f;

    double sampleRate = 44100.0;

    std::vector<float> envelopeState;

    // Per-channel sidechain tilt filters sharing one coefficients object
    // (fixed frequency/gain/Q, computed once in prepare()).
    std::vector<juce::dsp::IIR::Filter<float>> sidechainFilters;
    juce::dsp::IIR::Coefficients<float>::Ptr sidechainCoefficients { msrr::makeIdentityBiquad() };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> thresholdSmoothed;

    float ratio = 4.0f;
    float attackMs = 3.0f;
    float releaseMs = 150.0f;
    float makeupGainLinear = 1.0f;
    float driveGainLinear = 1.0f;
    float outputTrimLinear = 1.0f;
    float lastThresholdDb = -18.0f;
    bool sidechainTiltEnabled = false;
    bool programDependentRelease = false;

    float currentGainReductionDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FetCompressor)
};
