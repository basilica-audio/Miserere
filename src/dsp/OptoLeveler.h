#pragma once

#include "RealtimeCoefficients.h"
#include "TapeSaturator.h"

#include <juce_dsp/juce_dsp.h>

// The middle of the SANDWICH bus's Passive EQ -> Opto Leveler -> Passive EQ
// (docs/design-brief.md): a photocell-style leveler.
//
// Modelled behaviours (see docs/research-notes.md for the sourced findings):
//
// - **Raw-audio detector, no rectifier smoothing**: there is no sidechain
//   low-pass "level detector" ahead of the cell model - the (optionally
//   emphasis-filtered) raw audio drives the photocell response directly,
//   consistent with the hardware ("rectification and filtering ... are not
//   necessary"). All attack ballistics live in the photocell model itself.
// - **Two-stage release with memory**: modelled as 3 parallel one-pole
//   "cell conductance" followers, EACH with its own attack AND release time
//   constant (not a shared attack): a fast path (~10 ms attack, ~60 ms
//   release - the documented "50% recovery" stage) charges on any transient;
//   a mid path (~0.3 s attack, ~2 s release) and a slow path (~1.2 s attack,
//   ~10 s release) only build up meaningful level after SUSTAINED material -
//   a brief burst barely charges them at all. Taking the MAX of the three
//   paths reproduces the fast-then-slow release shape and the GR-history
//   dependence in one mechanism: after a short GR event only the fast path
//   was ever charged, so release is fast; after sustained GR the slower
//   paths have caught up too and dominate the max() long after the fast
//   path has decayed away, carrying a much longer tail - this IS the
//   "release grows with amount/duration of previous reduction" behaviour,
//   without a separate history accumulator.
// - **Static curve = lookup, not a fixed ratio**: breakaway at -30 dB, a
//   soft-knee region (~3:1, ~10:1 with Limit engaged) up to -20 dB, then a
//   hard ceiling (<1 dB of output change for +20 dB more input).
// - **Detector-only emphasis**: a low-shelf CUT of up to -10 dB below
//   ~1 kHz applied to the detector copy only (never the audio path) makes
//   the leveler progressively more HF-selective ("like a multiband") as
//   `sand_emphasis` rises.
// - **Clean GR element, coloured makeup**: the attenuation itself is
//   distortion-free; a small, always-on, level-dependent tube/transformer-
//   style nonlinearity sits after the gain multiply.
//
// Minimum-phase/causal, zero added latency (no lookahead, no oversampling) -
// keeps the SANDWICH bus sample-aligned with the Direct path.
//
// Stereo detection defaults to UNLINKED; setLinked(true) mirrors FetCrush's
// combined-envelope behaviour.
class OptoLeveler
{
public:
    OptoLeveler() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    // Drive into the fixed static curve (0-100%, mapped internally to an
    // input gain) - the hardware's Peak Reduction control is itself an
    // input gain into the cell, not a threshold (see class comment).
    void setPeakReductionProportion (float newAmount01) noexcept;

    void setLimitEnabled (bool shouldBeEnabled) noexcept { limitEnabled = shouldBeEnabled; }

    // 0-1 (0-100%): detector-only HF-selective emphasis (0 = flat/equal GR
    // at all frequencies, 1 = up to -10 dB shelf cut below ~1 kHz in the
    // detector).
    void setEmphasisProportion (float newAmount01) noexcept { emphasisAmount = juce::jlimit (0.0f, 1.0f, newAmount01); }

    void setLinked (bool shouldBeLinked) noexcept { linked = shouldBeLinked; }

    // Processes `block` in place. A zero-sample block is a safe no-op. No
    // allocation occurs here.
    void process (juce::dsp::AudioBlock<float>& block) noexcept;

    // Current gain reduction in dB (positive = reduction), peak across
    // channels in the last processed block - exposed for metering/tests.
    float getCurrentGainReductionDb() const noexcept { return currentGainReductionDb; }

    // Exposed for direct unit testing of the static curve independent of
    // the envelope dynamics.
    static float staticCurveOutputDb (float levelDb, bool limitEnabled) noexcept;

private:
    static constexpr float breakawayDb = -30.0f;
    static constexpr float kneeRegionDb = 10.0f;    // breakaway .. breakaway+kneeRegionDb is the soft-knee zone
    static constexpr float normalKneeRatio = 3.0f;
    static constexpr float limitKneeRatio = 10.0f;
    static constexpr float ceilingRatio = 25.0f;     // 20 dB more input -> 0.8 dB more output (< 1 dB, brief)

    // Per-path attack/release time constants - see class comment. Each path
    // is a plain one-pole follower of the (emphasis-filtered) rectified
    // signal; only the fast path reacts to brief transients, so the mid/
    // slow paths' contribution to the max() genuinely depends on how long
    // the signal has been loud, not just how loud the peak was.
    static constexpr double fastAttackSeconds = 0.010;   // "~10 ms effective" (the photocell's own attack)
    static constexpr double fastReleaseSeconds = 0.060;  // "50% recovery" stage
    static constexpr double midAttackSeconds = 0.3;
    static constexpr double midReleaseSeconds = 2.0;
    static constexpr double slowAttackSeconds = 1.2;
    static constexpr double slowReleaseSeconds = 10.0;

    static constexpr float emphasisFreqHz = 1000.0f;
    static constexpr float emphasisMaxCutDb = 10.0f;
    static constexpr float emphasisShelfQ = 0.5f;

    static constexpr float postAttenuatorDriveLinear = 1.15f; // gentle fixed tube/transformer coloration (~1.2 dB)

    static constexpr double smoothingTimeSeconds = 0.05;
    static constexpr double driveSmoothingTimeSeconds = 0.05;

    double sampleRate = 44100.0;

    // Per-channel: three parallel release-stage envelopes, max()'d together
    // - see class comment.
    std::vector<float> fastPathState;
    std::vector<float> midPathState;
    std::vector<float> slowPathState;

    // Emphasis detector filter (per channel, detector-only - never applied
    // to the audio path).
    std::vector<juce::dsp::IIR::Filter<float>> emphasisFilters;
    juce::dsp::IIR::Coefficients<float>::Ptr emphasisCoefficients { msrr::makeIdentityBiquad() };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> driveSmoothed;

    float lastAmount01 = 0.4f;
    bool limitEnabled = false;
    float emphasisAmount = 1.0f;
    bool linked = false;

    float postAttenuatorCompensation = 1.0f;

    float currentGainReductionDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptoLeveler)
};
