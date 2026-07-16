#pragma once

#include "RealtimeCoefficients.h"
#include "TapeSaturator.h"

#include <juce_dsp/juce_dsp.h>

// The Direct path's "1073-class" console EQ (docs/design-brief.md, module
// "Console EQ"): HPF 18 dB/oct @ {50, 80, 160, 300} Hz; low shelf +/-16 dB @
// {35, 60, 110, 220} Hz; mid bell +/-18 dB fixed-Q @ {360, 700, 1600, 3200,
// 4800, 7200} Hz; high shelf +/-16 dB fixed @ 12 kHz (shallow first-order-
// style shelves, 3-pole HPF); a Drive control blending near-equal 2nd+3rd
// (3rd-leaning) transformer-style harmonics, clean at nominal level.
//
// The HPF is folded into this class (rather than kept as the standalone v1
// Hpf module) because the brief specifies it as part of the console EQ
// module's stepped-frequency grid, not an independent v1-style continuously
// tunable filter.
//
// HPF at 18 dB/oct (3rd order) is built as a cascade of one 1st-order
// highpass (6 dB/oct) and one 2nd-order Butterworth highpass (12 dB/oct) -
// both minimum-phase/causal, zero added latency, keeping the Direct path
// (and everything summed with it) sample-aligned.
//
// At 0 dB every shelf/peak band's RBJ cookbook biquad coefficients collapse
// to an exact identity transfer function (see RealtimeCoefficients.h's
// applyBiquadCoefficients() and the msrr::isNeutralGainDb() dead-zone
// rationale) - this is what keeps the v2 default-wire null test bit-exact
// regardless of probe frequency. The HPF's enable toggle is a bit-exact
// bypass for the same reason a highpass has no "neutral" frequency (see the
// v1 Hpf.h design note, preserved here).
class ConsoleEq
{
public:
    ConsoleEq() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    void setHpfEnabled (bool shouldBeEnabled) noexcept { hpfEnabled = shouldBeEnabled; }
    void setHpfFreqHz (float newFrequencyHz) noexcept;

    void setLowFreqHz (float newFrequencyHz) noexcept;
    void setLowGainDb (float newGainDb) noexcept;
    void setMidFreqHz (float newFrequencyHz) noexcept;
    void setMidGainDb (float newGainDb) noexcept;
    void setHighGainDb (float newGainDb) noexcept;

    // 0 dB (the parameter's minimum) is a bit-exact bypass.
    void setDriveDb (float newDriveDb) noexcept;

    void process (juce::dsp::AudioBlock<float>& block) noexcept;

private:
    static constexpr float lowShelfQ = 0.5f;   // shallow, first-order-style shelf slope
    static constexpr float highShelfQ = 0.5f;
    static constexpr float midQ = 0.8f;        // fixed Q regardless of selected centre frequency (1073 spec)
    static constexpr float highShelfFreqHz = 12000.0f;
    static constexpr float hpfSecondOrderQ = juce::MathConstants<float>::sqrt2 / 2.0f; // Butterworth
    static constexpr double smoothingTimeSeconds = 0.05;
    static constexpr float evenHarmonicAmount = 0.06f; // small 2nd-harmonic add-on alongside the tanh (3rd-leaning) curve

    static constexpr float neutralGainEpsilonDb = 1.0e-3f;

    using Duplicator = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;

    double sampleRate = 44100.0;

    Duplicator hpfFirstOrder { new juce::dsp::IIR::Coefficients<float> (1.0f, 0.0f, 1.0f, 0.0f) };
    Duplicator hpfSecondOrder { msrr::makeIdentityBiquad() };
    Duplicator lowShelf { msrr::makeIdentityBiquad() };
    Duplicator midPeak { msrr::makeIdentityBiquad() };
    Duplicator highShelf { msrr::makeIdentityBiquad() };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> hpfFreqSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowFreqSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lowGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> midFreqSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> midGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> highGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> driveDbSmoothed;

    bool hpfEnabled = false;
    float lastHpfFreqHz = 80.0f;
    float lastLowFreqHz = 110.0f;
    float lastLowGainDb = 0.0f;
    float lastMidFreqHz = 1600.0f;
    float lastMidGainDb = 0.0f;
    float lastHighGainDb = 0.0f;
    float lastDriveDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConsoleEq)
};
