#pragma once

#include <juce_dsp/juce_dsp.h>

#include <atomic>

// The Direct path's "FET Comp light" (docs/design-brief.md): a simple,
// threshold-based feed-forward FET-style compressor in classic insert
// voicing - default 4:1, slow-ish attack, fast release, aiming for a light
// 3-4 dB of peak GR - "the one place serial compression is authentic" in
// the v2 topology. The all-buttons character (input-drive paradigm,
// per-ratio threshold/knee table, dual-rate release, ALL-mode plateau) lives
// in the separate FetCrush module (the CRUSH bus) - the two are
// deliberately different classes because their control paradigms don't
// share a parameter surface (this module is threshold-driven; FetCrush is
// drive-driven with no threshold knob at all).
//
// Character switch (issue #20, `direct_fet_colour`): a per-slot voicing
// tuple {kneeDb, attackScale, releaseScale, harmonicAmount} selecting one
// of three classic insert-compressor families, generic descriptors only:
//
//   - **FET** (default): hard knee, panel timing, no added harmonics - the
//     exact pre-#20 arithmetic (kneeDb == 0 short-circuits to the legacy
//     max(0, over) branch and harmonicAmount == 0 skips the colour term
//     entirely, so a default instance is bit-identical to the previous
//     release; the default-wire null test and the threshold-at-maximum
//     identity both continue to hold unchanged).
//   - **VCA**: clean bus-style voicing - 6 dB soft knee, a snappier attack
//     floor, still zero added harmonics. Claimed TRANSPARENT below the knee:
//     with the envelope below threshold - kneeDb/2 the computed reduction is
//     an exact 0 dB (clamped branch), so at 0 dB makeup the output is a
//     bit-exact copy of the input (null-tested).
//   - **Tube Mu**: variable-gain tube-family voicing - 12 dB soft knee,
//     slower attack, longer release, plus a GR-gated second-harmonic colour
//     term: delta = amount * min(1, GR/12 dB) * (y^2 - dc), where dc is a
//     one-pole (~5 Hz) DC estimate of y^2 so the even-order term adds no DC
//     offset. The delta is memoryless-plus-one-pole (minimum-phase, causal,
//     no lookahead) - sample alignment of the direct path is untouched.
//
// Knee/harmonic amounts are smoothed (50 ms, block-rate skip like the
// threshold) so switching character mid-stream ramps instead of stepping;
// the attack/release scales step at block rate exactly like the existing
// attack/release parameters themselves.
//
// With envelope <= threshold the computed reduction is an exact 0 dB (a
// clamped branch, not an asymptotic limit), so at Threshold == 0 dB (its
// maximum) any signal below 0 dBFS never trips the compressor - the
// bit-exact identity the default-wire null test's "FET Comp light disabled"
// relies on (the module is off by default anyway; this is a secondary
// safety net if it is ever enabled with threshold parked at its ceiling).
//
// Minimum-phase/causal, no lookahead: a pure per-sample gain multiply, zero
// added latency - keeps the Direct path (and everything summed with it)
// sample-aligned.
//
// Detection is per-channel independent (not stereo-linked) - matching the
// module's simple insert-voicing role; only the CRUSH bus exposes the
// unlinked/linked detector choice (see design-brief.md's Link switch).
class FetCompressor
{
public:
    enum class Character
    {
        fet,
        vca,
        tubeMu
    };

    // The per-character voicing tuple (exposed for tests).
    struct CharacterTuple
    {
        float kneeDb;         // soft-knee width (0 = the legacy hard-knee branch)
        float attackScale;    // multiplies the attack dial's ms value
        float releaseScale;   // multiplies the release dial's ms value
        float harmonicAmount; // GR-gated 2nd-harmonic colour amount (0 = clean)
    };

    static CharacterTuple characterTupleFor (Character c) noexcept;

    FetCompressor() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    void setCharacter (Character newCharacter) noexcept;

    void setRatio (float newRatio) noexcept { ratio = juce::jmax (1.0f, newRatio); }
    void setThresholdDb (float newThresholdDb) noexcept;
    void setAttackMs (float newAttackMs) noexcept { attackMs = juce::jmax (0.01f, newAttackMs); }
    void setReleaseMs (float newReleaseMs) noexcept { releaseMs = juce::jmax (1.0f, newReleaseMs); }
    void setMakeupDb (float newMakeupDb) noexcept { makeupGainLinear = juce::Decibels::decibelsToGain (newMakeupDb); }

    // Processes `block` in place. A zero-sample block is a safe no-op. No
    // allocation occurs here.
    void process (juce::dsp::AudioBlock<float>& block) noexcept;

    // Current gain reduction in dB (positive value = that much reduction),
    // the peak across channels in the last processed block - exposed for
    // metering/tests, not required for correct audio processing.
    float getCurrentGainReductionDb() const noexcept { return currentGainReductionDb.load (std::memory_order_relaxed); }

private:
    static constexpr double smoothingTimeSeconds = 0.05;

    // GR level at which the Tube Mu colour term reaches full depth, and the
    // cutoff of the one-pole DC estimate that keeps the even-order term
    // DC-free.
    static constexpr float harmonicReferenceGrDb = 12.0f;
    static constexpr float harmonicDcCutoffHz = 5.0f;

    double sampleRate = 44100.0;

    std::vector<float> envelopeState;
    std::vector<float> harmonicDcState;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> thresholdSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> kneeSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> harmonicSmoothed;

    Character character = Character::fet;
    float ratio = 4.0f;
    float attackMs = 8.0f;
    float releaseMs = 200.0f;
    float makeupGainLinear = 1.0f;
    float lastThresholdDb = -18.0f;

    // Written once per processed block on the audio thread, read by the
    // GUI meter timer (M3 needle meters) - relaxed atomic on both sides.
    std::atomic<float> currentGainReductionDb { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FetCompressor)
};
