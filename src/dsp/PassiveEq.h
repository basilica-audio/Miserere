#pragma once

#include "RealtimeCoefficients.h"

#include <juce_dsp/juce_dsp.h>

// Bus B's passive-style EQ, used twice per the "opto sandwich" topology:
//
//   Passive EQ in  (before the Opto Leveler): Low Boost (60/100 Hz select,
//     0-10 dB) + High Boost (8/10/12/16 kHz select, 0-10 dB) - broad,
//     gentle, boost-only passive-style curves (brief).
//   Passive Air out (after the Opto Leveler): a single fixed 12 kHz high
//     shelf, 0-8 dB boost only (brief).
//
// One class serves both slots: each instance exposes three bands (low
// boost shelf, high boost shelf, air shelf) and the engine simply leaves
// unused bands at 0 dB. At 0 dB gain every band's RBJ shelf coefficients
// collapse to an exact identity biquad (b == a term-by-term), so unused/
// neutral bands are transparent to the null/alignment tests - the same
// property ConsoleEq relies on (see ConsoleEq.h).
//
// "Passive-style" here means the curve character, not a component
// simulation: broad shelves at a gentle fixed Q (~0.5, wider than the
// console EQ's 0.707), boost-only ranges, and no cut bands - the passive
// tube EQ workflow of "boost low and high, let the following leveler catch
// the excess" that the opto sandwich is built around.
//
// Minimum-phase IIR shelves only: zero added latency, keeping Bus B
// sample-aligned per the parallel-bus phase discipline.
class PassiveEq
{
public:
    PassiveEq() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    // Frequency comes from an AudioParameterChoice mapped to concrete Hz by
    // the caller (see msrr::busBLowBoostFreqHz/busBHighBoostFreqHz in
    // ParameterLayout.h). Gains are boost-only by parameter range; values
    // are clamped >= 0 defensively anyway.
    void setLowBoost (float freqHz, float gainDb) noexcept;
    void setHighBoost (float freqHz, float gainDb) noexcept;
    void setAirGainDb (float newGainDb) noexcept;

    void process (juce::dsp::AudioBlock<float>& block) noexcept;

private:
    static constexpr float passiveQ = 0.5f; // broad, gentle passive-style slope
    static constexpr float airFreqHz = 12000.0f;
    static constexpr float airQ = juce::MathConstants<float>::sqrt2 / 2.0f;
    static constexpr double smoothingTimeSeconds = 0.05;

    // Bands whose gain sits inside this dead zone are skipped entirely -
    // see ConsoleEq.h/.cpp for the rationale.
    static constexpr float neutralGainEpsilonDb = 1.0e-3f;

    using Duplicator = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;

    double sampleRate = 44100.0;

    Duplicator lowBoost { msrr::makeIdentityBiquad() };
    Duplicator highBoost { msrr::makeIdentityBiquad() };
    Duplicator airShelf { msrr::makeIdentityBiquad() };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowFreqSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lowGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> highFreqSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> highGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> airGainSmoothed;

    float lastLowFreqHz = 100.0f;
    float lastLowGainDb = 0.0f;
    float lastHighFreqHz = 12000.0f;
    float lastHighGainDb = 0.0f;
    float lastAirGainDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PassiveEq)
};
