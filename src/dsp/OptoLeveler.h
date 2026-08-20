#pragma once

#include "MatchedBiquad.h"
#include "OptoCell.h"
#include "RealtimeCoefficients.h"
#include "TapeSaturator.h"

#include <juce_dsp/juce_dsp.h>

#include <atomic>

#include <vector>

// The middle of the SANDWICH bus's Passive EQ -> Opto Leveler -> Passive EQ
// (docs/design-brief.md): a photocell-style leveler.
//
// v0.5.0 "Circuit Engines" (brief F4): the previous hand-authored detector
// (3 parallel max()'d one-pole followers + a 3:1/10:1/25:1 static-curve
// lookup) is REPLACED by real T4B carrier physics in a feedback loop
// (research-opto-la2a.md sections 2.3-2.5, 3.1-3.3):
//
//   x -> divider gain a[n-1] -> colour (tanh 1.15, ADAA-EXEMPT - see below) -> y
//         ^ sidechain (one per stereo pair when linked, per-channel otherwise):
//         y[n-1] -> peakReduction gain (0..+40 dB) -> R37 emphasis shelf
//                -> driver tanh -> EL-panel law -> photocell carrier ODE
//                -> Rcell -> divider -> a[n]
//
// - FEEDBACK topology: the sidechain sees the already-compressed output
//   (one-sample tap - physically benign, the photocell states are orders of
//   magnitude slower than a sample period). Soft knee, ratio rising gently
//   with overdrive and the static curve are EMERGENT from the loop - there
//   is no static-curve lookup anymore (calibrated against the recorded
//   v0.4.0 curve at moderate settings, tests/OptoLevelerTests.cpp).
// - The EL panel is driven by the (amplified) audio itself - no rectifier.
//   Light pulses at 2f; the photocell's sluggishness does the averaging.
//   The sidechain runs at audio rate so the 2f ripple survives (the LF
//   "thickening" odd harmonics under compression - GR-ripple test).
// - Two-stage release, release-vs-GR-history MEMORY and program-dependent
//   attack all emerge from the carrier ODE (OptoCell.h).
// - Divider law: a = Rp/(Rs + Rp), Rp = Rcell*RL/(Rcell + RL) with
//   Rs = 9.5 kOhm (R6+R7) and RL = 100 kOhm (gain-pot load), normalised by
//   the dark gain so no-light == exactly unity.
// - The photocell states are never smoothed ("they smooth themselves");
//   reset() restores dark equilibrium.
//
// Parameter mapping (no ID changes): sand_peakred -> sidechain gain
// 0..+40 dB; sand_limit -> loop-gain/EL-drive tuple (Compress ~ emergent
// 3:1-ish knee, Limit = high-ratio tuple); sand_emphasis -> R37 low-shelf
// depth 0..-10 dB below ~1 kHz in the sidechain only (matched shelf per
// brief F2) - same audible intent as the previous detector shelf.
//
// The always-on post-attenuator colour tanh (fixed drive 1.15) is
// **ADAA-EXEMPT** (brief F1 exemption rule): it stays a memoryless
// per-sample evaluation to preserve the SANDWICH bus's exact sample
// alignment; its alias floor is guarded by tests/AliasingTests.cpp
// (<= -80 dBFS) - a failing guard is a project-owner escalation, never a
// silent ADAA retrofit.
//
// Minimum-phase/causal, zero added latency (no lookahead, no oversampling) -
// keeps the SANDWICH bus sample-aligned with the Direct path.
//
// Stereo detection defaults to UNLINKED; setLinked(true) shares ONE
// sidechain (cell + emphasis + feedback tap = max-abs of both channels)
// across the pair, matching the hardware stereo-link behaviour.
//
// Colour switch (issue #20, `sand_colour`): a per-slot voicing tuple over
// the carrier kinetics and the post-attenuator colour stage - generic era
// descriptors only:
//
//   - **Classic** (default): the T4B calibration exactly as shipped in
//     v0.5.0 (all scales 1.0, colour drive 1.15) - bit-identical to the
//     previous release, so the golden-static-curve and release-target tests
//     continue to hold unchanged.
//   - **Quick**: later solid-state-era optical voicing - faster carrier
//     recombination (nuN x6) with the electron mobility raised by sqrt(6)
//     so the settled GR depth stays in the same class (qn_ss ~ sqrt(g/nuN),
//     so muN*qn_ss is invariant under {nuN*k, muN*sqrt(k)}), and a reduced
//     trap fraction (etaP x0.25, much less release memory). Colour stage
//     nearly clean (drive 1.02).
//   - **Deep**: earlier-era voicing - slower recombination (nuN x0.4,
//     muN x sqrt(0.4)), tripled trap fraction (longer memory/tail), thicker
//     colour stage (drive 1.45).
//
// Kinetics swap without a cell reset (OptoCell::updateParamsPreservingState;
// the charge states are continuous, so the swap cannot step the gain);
// only the colour-stage drive needs an explicit 50 ms smoothing ramp
// (block-rate skip, like the peak-reduction drive) because it IS a gain
// term. Every colour clamps Rcell to the same [Rlight, Rdark], so unity at
// idle holds for all three.
class OptoLeveler
{
public:
    enum class Colour
    {
        classic,
        quick,
        deep
    };

    // The per-colour voicing tuple (exposed for tests).
    struct ColourTuple
    {
        double nuNScale;          // electron recombination-rate scale
        double muNScale;          // electron mobility scale (GR-depth compensation)
        double etaPScale;         // trap-fraction scale (release memory)
        float colourDriveLinear;  // post-attenuator colour-stage drive
    };

    static ColourTuple colourTupleFor (Colour c) noexcept;

    OptoLeveler() = default;

    void setColour (Colour newColour) noexcept;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    // Drive into the sidechain (0-100%, mapped to 0..+40 dB of sidechain
    // gain) - the hardware's Peak Reduction control is a sidechain gain,
    // threshold and depth in one knob (fixed internal threshold = EL panel
    // turn-on).
    void setPeakReductionProportion (float newAmount01) noexcept;

    void setLimitEnabled (bool shouldBeEnabled) noexcept { limitEnabled = shouldBeEnabled; }

    // 0-1 (0-100%): detector-only HF-selective emphasis (0 = flat/equal GR
    // at all frequencies, 1 = up to -10 dB shelf cut below ~1 kHz in the
    // sidechain).
    void setEmphasisProportion (float newAmount01) noexcept { emphasisAmount = juce::jlimit (0.0f, 1.0f, newAmount01); }

    void setLinked (bool shouldBeLinked) noexcept { linked = shouldBeLinked; }

    // Processes `block` in place. A zero-sample block is a safe no-op. No
    // allocation occurs here.
    void process (juce::dsp::AudioBlock<float>& block) noexcept;

    // Current gain reduction in dB (positive = reduction), peak across
    // channels in the last processed block - exposed for metering/tests.
    float getCurrentGainReductionDb() const noexcept { return currentGainReductionDb.load (std::memory_order_relaxed); }

private:
    // Divider network (research-opto-la2a.md section 2.1).
    static constexpr double seriesOhm = 9500.0;   // Rs = R6 + R7
    static constexpr double loadOhm = 100000.0;   // RL = gain pot

    // Sidechain calibration (tuned against the v0.4.0 golden static curve
    // at peakRed 50% and the T4B release/memory targets - see
    // tests/OptoLevelerTests.cpp).
    static constexpr float maxSidechainGainDb = 40.0f;
    static constexpr double driverRailVolts = 2.5;      // 6AQ5 soft-clip rail
    static constexpr double compressDriverGain = 0.9;   // Compress tuple
    static constexpr double limitDriverGain = 3.2;      // Limit tuple (higher loop gain)
    static constexpr double elCoefficientB0 = 1.0e17;   // B0 * kOpt (absorbed)
    static constexpr double elKneeB = 17.0;             // EL turn-on sharpness
    static constexpr double limitElKneeB = 16.0;        // slightly sharper effective knee under Limit
    static constexpr double generationCeiling = 20.0;   // abuse clamp on g

    static constexpr float emphasisFreqHz = 1000.0f;
    static constexpr float emphasisMaxCutDb = 10.0f;
    static constexpr float emphasisShelfQ = 0.5f;

    static constexpr float postAttenuatorDriveLinear = 1.15f; // Classic colour's tube/transformer coloration (~1.2 dB); Quick/Deep scale it via ColourTuple

    static constexpr double driveSmoothingTimeSeconds = 0.05;

    double sampleRate = 44100.0;

    // Per-sidechain state (index 0 is the shared sidechain when linked;
    // per-channel otherwise).
    std::vector<msrr::OptoCell> cells;
    std::vector<float> feedbackSample;   // previous output sample per channel
    std::vector<float> currentGain;      // a[n-1]/a_dark per sidechain

    // Emphasis sidechain filter (per sidechain, never the audio path).
    std::vector<juce::dsp::IIR::Filter<float>> emphasisFilters;
    juce::dsp::IIR::Coefficients<float>::Ptr emphasisCoefficients { msrr::makeIdentityBiquad() };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> driveSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> colourDriveSmoothed;

    float lastAmount01 = 0.4f;
    bool limitEnabled = false;
    float emphasisAmount = 1.0f;
    bool linked = false;
    Colour colour = Colour::classic;

    double darkGain = 1.0;

    // Written once per processed block on the audio thread, read by the
    // GUI meter timer (M3 needle meters) - relaxed atomic on both sides.
    std::atomic<float> currentGainReductionDb { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptoLeveler)
};
