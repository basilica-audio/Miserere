#pragma once

#include "RealtimeCoefficients.h"

#include <juce_dsp/juce_dsp.h>

// The shared Passive EQ module (docs/design-brief.md): used twice per the
// SANDWICH bus topology (pre-Opto and post-Opto, each its own instance with
// independent parameters).
//
// Three bands:
//
// - **LF boost + LF cut, simultaneously, non-cancelling**: both act on the
//   same frequency SELECTOR (20/30/60/100 Hz) but are deliberately
//   different filter shapes at different effective corners - boost is a
//   resonant low shelf AT the selected frequency (Q > 0.707, so it peaks
//   right at its corner rather than settling flat), cut is a broader, gentle
//   peaking dip centred well above the selector (6x). Dialling both up at
//   once therefore does NOT cancel to flat: it produces a bump near/just
//   below the selector and a dip in the low-mids above it (see
//   docs/research-notes.md's "low-end trick" finding) - verified numerically
//   during design (see PR description) and by
//   tests/PassiveEqTests.cpp. Hardware-style asymmetric calibration: boost
//   maxes out lower than cut (13.5 dB vs 17.5 dB).
// - **HF bell boost**: a peaking filter, frequency selectable (3/4/5/8/10/
//   12/16 kHz) with continuously variable bandwidth (dial 0 = sharp, 10 =
//   broad, i.e. HIGH dial value = LOW Q).
// - **HF shelf cut**: a high shelf, frequency selectable (5/10/20 kHz),
//   attenuation only.
//
// All three bands use 0-10 dials with a nonlinear (power) taper rather than
// a linear dB mapping, matching the hardware's non-calibrated knobs.
//
// A defeatable "vintage residual" (default ON) imprints a small, always-on
// tilt (a few tenths of a dB) that varies with the LF selector - the
// documented "never truly flat" behaviour of the passive network even at
// all-zero dial settings.
//
// Minimum-phase IIR only: zero added latency, keeping the SANDWICH bus
// sample-aligned with the Direct path.
class PassiveEq
{
public:
    PassiveEq() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    // Frequency comes from an AudioParameterChoice mapped to concrete Hz by
    // the caller (see the msrr::sandLf*/sandHfBell*/sandHfShelf* tables in
    // ParameterLayout.h).
    void setLfFreqHz (float freqHz) noexcept;
    void setLfBoostDial (float dial0to10) noexcept;
    void setLfCutDial (float dial0to10) noexcept;

    void setHfBellFreqHz (float freqHz) noexcept;
    void setHfBellBoostDial (float dial0to10) noexcept;
    void setHfBellBandwidthDial (float dial0to10) noexcept; // 0 = sharp (high Q), 10 = broad (low Q)

    void setHfShelfFreqHz (float freqHz) noexcept;
    void setHfShelfAttenDial (float dial0to10) noexcept;

    void setResidualEnabled (bool shouldBeEnabled) noexcept { residualEnabled = shouldBeEnabled; }

    void process (juce::dsp::AudioBlock<float>& block) noexcept;

private:
    static constexpr float lfBoostMaxDb = 13.5f;
    static constexpr float lfCutMaxDb = 17.5f;
    static constexpr float lfBoostCornerMultiplier = 1.0f; // boost corner == the LF selector frequency
    static constexpr float lfBoostQ = 1.2f;                // resonant - peaks at its own corner
    static constexpr float lfCutCentreMultiplier = 6.0f;   // cut's dip centre == 6x the LF selector frequency
    static constexpr float lfCutQ = 1.0f;

    static constexpr float hfBellMaxDb = 18.0f;
    static constexpr float hfShelfAttenMaxDb = 17.0f;
    static constexpr float hfShelfQ = 0.5f;

    static constexpr float dialTaperExponent = 1.5f; // nonlinear (power-law) 0-10 dial -> dB taper

    static constexpr float residualMaxDb = 0.35f; // "vintage residual" tilt magnitude

    static constexpr double smoothingTimeSeconds = 0.05;
    static constexpr float neutralGainEpsilonDb = 1.0e-3f;

    using Duplicator = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;

    static float dialToDb (float dial0to10, float maxDb) noexcept;
    static float bandwidthDialToQ (float dial0to10) noexcept;

    double sampleRate = 44100.0;

    Duplicator lfBoost { msrr::makeIdentityBiquad() };
    Duplicator lfCut { msrr::makeIdentityBiquad() };
    Duplicator hfBell { msrr::makeIdentityBiquad() };
    Duplicator hfShelf { msrr::makeIdentityBiquad() };
    Duplicator residualShelf { msrr::makeIdentityBiquad() };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lfFreqSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lfBoostSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lfCutSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> hfBellFreqSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> hfBellBoostSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> hfBellBandwidthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> hfShelfFreqSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> hfShelfAttenSmoothed;

    float lastLfFreqHz = 30.0f;
    float lastLfBoostDial = 0.0f;
    float lastLfCutDial = 0.0f;
    float lastHfBellFreqHz = 8000.0f;
    float lastHfBellBoostDial = 0.0f;
    float lastHfBellBandwidthDial = 5.0f;
    float lastHfShelfFreqHz = 10000.0f;
    float lastHfShelfAttenDial = 0.0f;

    bool residualEnabled = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PassiveEq)
};
