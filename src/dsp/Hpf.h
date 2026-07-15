#pragma once

#include "RealtimeCoefficients.h"

#include <juce_dsp/juce_dsp.h>

// Bus A's high-pass filter: 20-300 Hz, 12 dB/oct (2nd-order Butterworth,
// brief). Minimum-phase IIR, zero added latency - keeps Bus A sample-aligned
// with Busses B/C per the parallel-bus phase discipline (see
// docs/architecture.md).
//
// A highpass has no frequency setting that is an exact identity the way a
// shelf/peak EQ is at 0 dB gain (its transfer function's real+imaginary
// parts both deviate from 1 close to Nyquist-scale ratios, and even at a
// very low cutoff like 20 Hz the *phase* deviation at a mid-band probe tone
// is order (fc/f), not (fc/f)^2 - roughly -31 dB at fc=20 Hz/f=1 kHz, nowhere
// near the M1 null test's <= -120 dBFS bar). So unlike Console EQ/De-Esser/
// Tape Sat, "neutral" for the HPF is an explicit enable toggle, not a
// frequency value - see setEnabled().
class Hpf
{
public:
    Hpf() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    // false is a bit-exact bypass (the filter is skipped entirely, not run
    // at some maximally-open frequency) - see the class comment above.
    void setEnabled (bool shouldBeEnabled) noexcept { enabled = shouldBeEnabled; }

    void setFrequencyHz (float newFrequencyHz) noexcept;

    void process (juce::dsp::AudioBlock<float>& block) noexcept;

private:
    static constexpr float q = juce::MathConstants<float>::sqrt2 / 2.0f; // Butterworth (maximally flat)
    static constexpr double smoothingTimeSeconds = 0.05;

    using Duplicator = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;

    double sampleRate = 44100.0;
    bool enabled = true;

    Duplicator filter { msrr::makeIdentityBiquad() };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> frequencySmoothed;
    float lastFrequencyHz = 80.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Hpf)
};
