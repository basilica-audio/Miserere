#pragma once

#include "RealtimeCoefficients.h"

#include <juce_dsp/juce_dsp.h>

// Bus A's "British console" character EQ: fixed LowShelf @ 100 Hz, a
// sweepable mid Peak (250 Hz-5 kHz, Q 0.7-2), and a fixed HighShelf @ 8 kHz,
// each +/-15 dB (brief). Minimum-phase IIR, zero added latency.
//
// At 0 dB every band's RBJ cookbook biquad coefficients collapse to an exact
// identity transfer function (b0==a0, b1==a1, b2==a2 term-by-term, so
// H(z) == 1 identically, not just approximately close to unity) - this is
// what keeps the M1 null test's "EQ flat" bit-exact regardless of probe
// frequency, unlike Hpf's enable toggle (see Hpf.h).
class ConsoleEq
{
public:
    ConsoleEq() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    void setLowGainDb (float newGainDb) noexcept;
    void setMidFreqHz (float newFrequencyHz) noexcept;
    void setMidGainDb (float newGainDb) noexcept;
    void setMidQ (float newQ) noexcept;
    void setHighGainDb (float newGainDb) noexcept;

    void process (juce::dsp::AudioBlock<float>& block) noexcept;

private:
    static constexpr float lowShelfFreqHz = 100.0f;
    static constexpr float highShelfFreqHz = 8000.0f;
    static constexpr float shelfQ = juce::MathConstants<float>::sqrt2 / 2.0f;
    static constexpr double smoothingTimeSeconds = 0.05;

    // Bands whose |gain| sits inside this dead zone are skipped entirely -
    // see the comment in process() for why this is required for the null
    // guarantee (fp-contract residue + APVTS denormalisation noise).
    static constexpr float neutralGainEpsilonDb = 1.0e-3f;

    using Duplicator = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;

    double sampleRate = 44100.0;

    Duplicator lowShelf { msrr::makeIdentityBiquad() };
    Duplicator midPeak { msrr::makeIdentityBiquad() };
    Duplicator highShelf { msrr::makeIdentityBiquad() };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lowGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> midFreqSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> midGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> midQSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> highGainSmoothed;

    float lastLowGainDb = 0.0f;
    float lastMidFreqHz = 1000.0f;
    float lastMidGainDb = 0.0f;
    float lastMidQ = 1.0f;
    float lastHighGainDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConsoleEq)
};
