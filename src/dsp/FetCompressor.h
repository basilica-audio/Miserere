#pragma once

#include <juce_dsp/juce_dsp.h>

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
    FetCompressor() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

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
    float getCurrentGainReductionDb() const noexcept { return currentGainReductionDb; }

private:
    static constexpr double smoothingTimeSeconds = 0.05;

    double sampleRate = 44100.0;

    std::vector<float> envelopeState;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> thresholdSmoothed;

    float ratio = 4.0f;
    float attackMs = 8.0f;
    float releaseMs = 200.0f;
    float makeupGainLinear = 1.0f;
    float lastThresholdDb = -18.0f;

    float currentGainReductionDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FetCompressor)
};
