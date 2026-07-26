#pragma once

#include "AdaaSaturator.h"

#include <juce_dsp/juce_dsp.h>

#include <vector>

// Bus (1) CRUSH: a FET-style limiter driven the way an "all-buttons" unit
// actually is - by input drive into a fixed per-ratio threshold, never by a
// threshold knob (docs/design-brief.md).
//
// v0.5.0 "Circuit Engines" (brief F3): the previous feed-forward detector
// (static soft-knee table + hand-built dual-rate release blend + duration
// integrator) is REPLACED by the real feedback-FET circuit structure
// (research-fet-comp-1176.md sections 2.1-2.3, 3.2):
//
//   x -> inputDrive (crush_input, 0-48 dB) -> FET divider gain G(vC, x)
//     -> makeup/trim -> y
//          ^ feedback: y[n-1] -> full-wave rectifier -> single-cap RC -> vC
//
// - **Single-cap two-path sidechain ODE** (attack pot in the charge path,
//   release pot in the discharge path, C = 0.22 uF normalised):
//
//     vRect = max(0, kSc*|y[n-1]| - Vth(ratio))
//     vC   += aA*(vRect - vC)   if vRect > vC     (charge through attack pot)
//     vC   *= (1 - aR)          otherwise         (discharge through release pot)
//
//   One cap, two pots: attack and release interact through the same state
//   variable - the release dial measurably changes the effective attack
//   (the Eichas/Gerat servo-rig finding), and program-dependent release +
//   ratio creep emerge from the feedback drive of the rectifier. No
//   hand-built dual-rate blend remains.
// - **Two fixed-point iterations per sample** (evaluate cell -> recompute
//   vRect from the current output estimate -> re-evaluate cell), branch-
//   free: the research file's explicit 1x prescription that makes the 20 us
//   attack genuinely act sub-sample at 44.1/48 k instead of an impossible
//   one-pole coefficient.
// - **FET cell with imperfect square-law cancellation**: the FET is a
//   voltage-controlled resistor shunting the signal node,
//   conductance ~ vCell (Shichman-Hodges triode region after the
//   half-drain-to-gate cancellation), G = 1/(1 + Rs*gds(vCell)), plus the
//   residual-mismatch epsilon term
//     G *= 1 - eps*vds/(2*(vCell - Vp))
//   whose 2nd-harmonic "hair" grows with GR depth. This replaces the old
//   0.15*x*|x| colour term.
//
//   NOTE on the cell formula: the brief's pseudo-code writes
//   rds = rOn/(1 - vC*invVp); with the ODE's vC being a POSITIVE control
//   voltage in [0, |Vp|), that literal expression caps the divider swing at
//   ~6 dB. The physical reading (v_gs = Vp + vC rising from pinch-off
//   toward 0, r_ds = rOn/(1 - v_gs/Vp) = rOn*|Vp|/vC, i.e. conductance
//   proportional to vC - exactly the research file's Shichman-Hodges
//   g_ds ~ (v_gs - Vp)) restores the specified >= 30 dB GR range and
//   exact unity at idle; implemented in conductance form (no division by
//   zero at vC = 0). Flagged as a documented correction in the PR.
// - **Ratio buttons = discrete bias tuples** {Vth, kSc, vBias, epsScale,
//   loopGain} for 4:1/8:1/12:1/20:1; ALL is a genuine FIFTH tuple (never an
//   interpolation): slight linear-region gain (+0.7 dB), loopGain x1.3
//   (deliberate under-damping -> transient overshoot/plateau), epsScale x2.
//   Ratio switches crossfade the tuple over 10 ms. `crush_style` selects
//   between the ABI tuple set and a damped ("Gentle", fixed soft 2:1-class
//   voicing that ignores the ratio selector) tuple, preserving its existing
//   semantic.
// - **Inverted-taper ballistics** kept: crush_attack/crush_release are 1-7
//   dials, HIGHER = FASTER, panel-spec bracketing Ratt*C in [20 us, 800 us],
//   Rrel*C in [50 ms, 1100 ms] (the dial->RC mapping carries a calibration
//   factor so the MEASURED 63%/37% times match the panel spec - the
//   feedback loop lengthens raw RC times, see tests/FetCrushTests.cpp).
// - **GR-gated LF transformer colour** kept (150 Hz one-pole extract into a
//   tanh), now routed through residual-form ADAA per brief F1's
//   parallel-delta rule (the delta IS the ADAA residual).
//
// Minimum-phase/causal: per-sample gain multiplies with no lookahead, zero
// added latency - keeps the CRUSH bus sample-aligned with the Direct path
// per the suite's phase discipline (docs/adr/0003).
//
// Stereo detection defaults to UNLINKED (independent per-channel sidechains
// - "dual mono is key", design brief); setLinked(true) shares one vC driven
// by the max-abs of both channels' feedback samples.
class FetCrush
{
public:
    enum class Ratio
    {
        r4,
        r8,
        r12,
        r20,
        rAll
    };

    enum class Style
    {
        allButtons,
        gentle
    };

    FetCrush() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    // 0-48 dB, uncompensated on the audio path by design (see class
    // comment) - 0 dB multiplies by exactly 1.0f.
    void setInputDriveDb (float newDriveDb) noexcept { inputDriveLinear = juce::Decibels::decibelsToGain (juce::jmax (0.0f, newDriveDb)); }

    void setRatio (Ratio newRatio) noexcept;
    void setStyle (Style newStyle) noexcept;

    // 1-7, inverted taper (7 = fastest): 800 -> 20 us attack, 1100 -> 50 ms
    // release (panel-spec bracketing of the RC paths).
    void setAttackStep (float step1to7) noexcept;
    void setReleaseStep (float step1to7) noexcept;

    void setOutputTrimDb (float newTrimDb) noexcept { outputTrimLinear = juce::Decibels::decibelsToGain (newTrimDb); }

    void setLinked (bool shouldBeLinked) noexcept { linked = shouldBeLinked; }

    // Processes `block` in place. A zero-sample block is a safe no-op. No
    // allocation occurs here.
    void process (juce::dsp::AudioBlock<float>& block) noexcept;

    // Current gain reduction in dB (positive = reduction), peak across
    // channels in the last processed block - exposed for metering/tests.
    float getCurrentGainReductionDb() const noexcept { return currentGainReductionDb; }

    // The discrete per-ratio bias tuple (exposed for tests).
    struct BiasTuple
    {
        float thresholdDb;   // rectifier bias threshold, dBFS on the driven signal
        float vthScale;      // Vth in cap-voltage units (kSc = vthScale/10^(thresholdDb/20))
        float loopGain;      // cell-side gain on vC (under-damping control)
        float epsScale;      // epsilon-term scale (2nd-harmonic "hair")
        float linGainDb;     // linear-region gain trim (ABI: +0.7 dB)
    };

    static BiasTuple biasTupleFor (Ratio r, Style s) noexcept;

private:
    // FET cell constants (calibration constants, tuned in tests):
    // rOn = 400 ohm, Vp = -3 V class, Rs chosen so max GR ~ 30 dB.
    static constexpr double rOnOhm = 400.0;
    static constexpr double pinchOffVolts = 3.0;    // |Vp|
    static constexpr double seriesOhm = 12250.0;    // Rs -> 20*log10(1 + Rs/rOn) ~ 30 dB
    static constexpr double epsilonBase = 0.1;      // eps (epsScale multiplies)

    // Sidechain RC bracketing (research section 2.2, C = 0.22 uF
    // normalised) + measurement calibration factors (see class comment).
    static constexpr float attackMaxUs = 800.0f;
    static constexpr float attackMinUs = 20.0f;
    static constexpr float releaseMaxMs = 1100.0f;
    static constexpr float releaseMinMs = 50.0f;
    static constexpr float attackRcCalibration = 0.55f;
    static constexpr float releaseRcCalibration = 0.62f;

    static constexpr float tupleCrossfadeSeconds = 0.010f;

    // GR-gated LF transformer colour (kept from the M2 voicing pass).
    static constexpr float harmonicReferenceGrDb = 12.0f;
    static constexpr float lfSaturationCutoffHz = 150.0f;
    static constexpr float lfColourDrive = 4.0f;    // fixed curve drive; the GR gate scales the residual

    double sampleRate = 44100.0;

    // Per-channel loop state (index 0 shared when linked).
    std::vector<double> capVoltage;      // vC in [0, |Vp|)
    std::vector<float> feedbackSample;   // y[n-1] per channel
    std::vector<float> lfSaturationState;
    std::vector<msrr::adaa::TanhStage> lfColourStages;

    // Smoothed (10 ms crossfaded) tuple state.
    BiasTuple currentTuple { -24.0f, 2.4f, 1.56f, 2.0f, 0.7f };
    BiasTuple targetTuple { -24.0f, 2.4f, 1.56f, 2.0f, 0.7f };
    int tupleFadeSamplesLeft = 0;
    int tupleFadeLengthSamples = 1;

    float inputDriveLinear = 1.0f;
    float outputTrimLinear = 1.0f;
    Ratio ratio = Ratio::rAll;
    Style style = Style::allButtons;
    bool linked = false;

    float attackUs = attackMaxUs;
    float releaseMs = releaseMaxMs;

    float currentGainReductionDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FetCrush)
};
