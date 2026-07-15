#pragma once

#include "RealtimeCoefficients.h"

#include <juce_dsp/juce_dsp.h>

// Bus A's de-esser: split-band 4-9 kHz tunable, threshold, max 10 dB
// reduction (brief). Minimum-phase, zero added latency (no oversampling, no
// lookahead).
//
// Technique ("spectral subtraction" dynamic EQ, matching sibling seraph's
// DeEsser): a 2nd-order bandpass filter isolates the sibilance band from the
// signal; an envelope follower measures that band's level and a hard-knee
// downward compressor (threshold-based, clamped to a fixed max reduction)
// computes a gain-reduction factor for the band. The reduction is applied by
// adding back the bandpassed component scaled by (gainFactor - 1):
//
//   output = input + bandpassed * (gainFactor - 1)
//
// which is exactly "input, with the isolated band attenuated by gainFactor"
// (gainFactor == 1 => output == input identically).
//
// Unlike seraph's DeEsser, "off" here is an explicit enable toggle (brief:
// "de-esser off" in the M1 null-test guarantee) rather than an amount
// parameter reaching zero - see setEnabled().
class DeEsser
{
public:
    DeEsser() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    // false is a bit-exact bypass: process() returns without touching
    // `block` at all (the detector filter/envelope state does not advance
    // while disabled - there is nothing to keep continuous since, unlike
    // seraph's amount-based design, there is no partial-reduction blend to
    // protect against a discontinuity here).
    void setEnabled (bool shouldBeEnabled) noexcept { enabled = shouldBeEnabled; }

    void setFrequencyHz (float newFrequencyHz) noexcept;
    void setThresholdDb (float newThresholdDb) noexcept;

    void process (juce::dsp::AudioBlock<float>& block) noexcept;

    // Current gain reduction in dB, averaged across channels - exposed for
    // metering/tests, not required for correct audio processing.
    float getCurrentGainReductionDb() const noexcept { return currentGainReductionDb; }

private:
    static constexpr float detectorQ = 2.0f; // narrower than a general tone filter - isolates the sibilance band
    static constexpr float maxReductionDb = 10.0f; // brief: "max 10 dB reduction"
    static constexpr double attackTimeSeconds = 0.001;
    static constexpr double releaseTimeSeconds = 0.08;
    static constexpr double smoothingTimeSeconds = 0.05;

    double sampleRate = 44100.0;
    bool enabled = true;

    // One IIR bandpass filter per channel (not a ProcessorDuplicator): the
    // per-sample combination below needs both the raw input sample and its
    // bandpassed value in the same loop. All channels share the same
    // coefficients object, recomputed once per block via
    // msrr::applyBiquadCoefficients (see RealtimeCoefficients.h).
    std::vector<juce::dsp::IIR::Filter<float>> detectorFilters;
    juce::dsp::IIR::Coefficients<float>::Ptr detectorCoefficients { msrr::makeIdentityBiquad() };

    std::vector<float> envelopeState;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> frequencySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> thresholdSmoothed;

    float lastFrequencyHz = 6500.0f;
    float lastThresholdDb = -24.0f;

    float currentGainReductionDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeEsser)
};
