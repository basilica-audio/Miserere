#pragma once

#include "MatchedBiquad.h"
#include "RealtimeCoefficients.h"

#include <juce_dsp/juce_dsp.h>

// The shared Passive EQ module (docs/design-brief.md): used twice per the
// SANDWICH bus topology (pre-Opto and post-Opto, each its own instance with
// independent parameters).
//
// v0.5.0 "Circuit Engines" (brief F2): the corner laws are re-derived from
// the EQP-1A component values (research-pultec-eqp1a.md sections 1.2/2.1,
// component set from the SMC-2024 WDF reference model):
//
// - **LF boost + LF cut** are no longer two independent RBJ biquads: they
//   are ONE second-order section computed from the actual passive ladder
//   (two reactive elements C_lo1/C_lo2 -> exact 2nd-order rational transfer
//   function, evaluated per block and bilinear-transformed - the SMC paper's
//   own accuracy table shows the LF sections are essentially exact at any
//   rate). The boost element (R_boostLo || C_lo2, in the return leg, ADDED
//   to the output) and the cut element (C_lo1 || R_cutLo, in the series arm
//   BEFORE the output divider) interact exactly as in the hardware:
//   - C_lo1 = C_lo2/30 with the 110k/8.2k pot asymmetry puts the cut's
//     action corner 1.4-2.5x above the boost's (SoS measurement),
//   - full boost + full cut produces the "low end trick": net boost below
//     the selector, a dip in the low mids (200-700 Hz at the 60 Hz
//     selector), back to flat by ~3 kHz - emergent, not hand-drawn.
//   - the flat network loss (1/15.4882) is normalised out exactly, so all
//     dials at 0 remains a structural bit-exact bypass.
// - **HF bell** boost is a Vicanek MATCHED peaking filter (MatchedBiquad.h)
//   so the 10/12/16 kHz selections stay analog-true at 44.1/48 k
//   (tests/PassiveEqTests.cpp asserts the 16 kHz peak within +-4 % - the
//   assertion RBJ fails), with hardware-coupled gain/Q: the bandwidth pot
//   adds series resistance INSIDE the resonant branch, so
//   Q = Z0/(R_T + loading) (Z0 = sqrt(L/C) = 3464 ohm, R_T = 390..4290 ohm)
//   and the peak gain drops ~9 dB from sharp to broad at full boost (SoS).
// - **HF shelf cut** is a matched high shelf (same fit family).
//
// All three bands use 0-10 dials with a nonlinear (power) taper rather than
// a linear dB mapping, matching the hardware's non-calibrated log/audio-
// taper pots. Dial positions keep their names; the curve under them is now
// hardware-true (brief F2 binding).
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
    // ---------------------------------------------------------------------
    // EQP-1A component values (SMC-2024 reference model / ABSounds
    // EQP-WDF-1A, matching resistors removed):
    static constexpr double lfBoostPotMaxOhm = 8200.0;    // R_boostLo
    static constexpr double lfCutPotMaxOhm = 110000.0;    // R_cutLo
    static constexpr double hfSeriesOhm = 12000.0;        // HF boost pot branch, resistive at 0 boost
    static constexpr double hfAttenShuntOhm = 1000.0;     // HF atten pot branch, resistive at 0 atten
    static constexpr double dividerTopOhm = 1000.0;       // R2
    static constexpr double dividerBottomOhm = 10000.0;   // R3 (output tap)
    // C_lo2 (boost cap) per LF selector 20/30/60/100 Hz; C_lo1 = C_lo2/30.
    static constexpr double lfCutCapRatio = 30.0;

    // HF bell: constant L1/C1 ratio -> Z0 = sqrt(L/C) = 3464 ohm; bandwidth
    // pot 0..3.9 k linear + 390 ohm fixed; source/pot loading ~600 ohm.
    static constexpr float hfBellZ0Ohm = 3464.0f;
    static constexpr float hfBellSeriesMinOhm = 390.0f;
    static constexpr float hfBellSeriesMaxOhm = 4290.0f;
    static constexpr float hfBellLoadingOhm = 600.0f;
    static constexpr float hfBellMaxDb = 18.0f;
    // Peak gain falls ~9 dB from sharp to broad at full boost (SoS
    // measurement) - implemented as a proportional gain/bandwidth coupling.
    static constexpr float hfBellBroadGainLossFraction = 0.5f;

    static constexpr float hfShelfAttenMaxDb = 17.0f;
    static constexpr float hfShelfQ = 0.5f;

    static constexpr float dialTaperExponent = 1.5f;   // HF dials (bell/shelf dB taper)
    static constexpr float potTaperExponent = 2.0f;    // LF pots (log/audio-taper resistance approximation)

    static constexpr float residualMaxDb = 0.35f; // "vintage residual" tilt magnitude

    static constexpr double smoothingTimeSeconds = 0.05;
    static constexpr float neutralGainEpsilonDb = 1.0e-3f;
    static constexpr float neutralDialEpsilon = 1.0e-3f;

    using Duplicator = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;

    static float dialToDb (float dial0to10, float maxDb) noexcept;
    static float dialToPotFraction (float dial0to10) noexcept;
    static double lfBoostCapForSelector (float lfFreqHz) noexcept;

    // Computes the LF ladder's exact 2nd-order analog transfer function for
    // the given pot resistances and boost cap, bilinear-transforms it and
    // writes the raw digital coefficients (numerator pre-scaled by the flat
    // makeup so all-dials-0 is exactly unity).
    std::array<float, 6> computeLfNetworkCoefficients (double rBoostOhm, double rCutOhm, double boostCapFarad) const noexcept;

    double sampleRate = 44100.0;

    Duplicator lfNetwork { msrr::makeIdentityBiquad() };
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
