#pragma once

#include <juce_dsp/juce_dsp.h>

#include <vector>

// Bus (1) CRUSH: a FET-style limiter driven the way an "all-buttons" unit
// actually is - by input drive into a fixed per-ratio threshold, never by a
// threshold knob (docs/design-brief.md).
//
// Modelled behaviours (see docs/research-notes.md for the sourced findings
// this is derived from):
//
// - **Input-drive paradigm**: no threshold parameter. `crush_input` (0-48 dB)
//   drives both the audio path and the detector into a FIXED per-ratio
//   threshold/knee table - threshold rises and the knee hardens as ratio
//   increases (right down to the near-hard-knee ALL setting).
// - **Inverted-taper ballistics**: `crush_attack`/`crush_release` are 1-7
//   dials where a HIGHER number is FASTER (800->20 us attack, 1100->50 ms
//   release), matching the hardware convention.
// - **Dual-rate, program-dependent release**: a fast release after brief
//   transients, a slow release (several times longer) after sustained
//   high-RMS compression, blended by a compression-duration integrator (a
//   slow one-pole tracking how long the signal has been actively
//   compressing) rather than a fixed switch - the classic "pumping forward"
//   feel.
// - **ALL-mode plateau**: the all-buttons setting is modelled as a steep
//   ratio just above the knee that gives back to a softer ratio above a
//   fixed overshoot "kink" (a genuinely non-monotonic gain-reduction slope,
//   not a single fixed ratio), plus a short extra attack lag that lets the
//   initial transient overshoot through before the limiter clamps down
//   hard - the "snap" the brief specifies.
// - **Gentle style**: a fixed, softer 2:1 voicing (the later-era rear-bus
//   flavour) that ignores the ratio/ALL selector entirely.
//
// Minimum-phase/causal: a pure per-sample gain multiply with no lookahead,
// zero added latency - keeps the CRUSH bus sample-aligned with the Direct
// path per the suite's phase discipline (docs/adr/0003).
//
// Stereo detection defaults to UNLINKED (independent per-channel envelopes -
// "dual mono is key", design brief); setLinked(true) combines both channels'
// detector input into a single shared envelope.
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

    void setRatio (Ratio newRatio) noexcept { ratio = newRatio; }
    void setStyle (Style newStyle) noexcept { style = newStyle; }

    // 1-7, inverted taper (7 = fastest): 800 -> 20 us attack, 1100 -> 50 ms
    // (63%-recovery, one-pole definition) base/fast release.
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

    struct RatioPoint
    {
        float ratio;
        float thresholdDb;
        float kneeDb;
    };

    // Exposed for direct unit testing of the static curve (per-ratio table
    // + ALL-mode plateau/give-back) independent of the envelope dynamics.
    static RatioPoint ratioPointFor (Ratio r) noexcept;
    static float staticCurveReductionDb (float levelDb, Ratio r, Style s) noexcept;

private:
    static constexpr double smoothingTimeSeconds = 0.05;

    static constexpr float attackMaxUs = 800.0f;
    static constexpr float attackMinUs = 20.0f;
    static constexpr float releaseMaxMs = 1100.0f;
    static constexpr float releaseMinMs = 50.0f;

    static constexpr float slowReleaseMultiplier = 6.0f;
    static constexpr double durationRiseTauSeconds = 1.0;  // how quickly "sustained compression" is recognised
    static constexpr double durationFallTauSeconds = 0.3;
    static constexpr float durationGrEpsilonDb = 0.1f;

    static constexpr float allButtonsAttackLagMs = 4.0f; // extra attack lag -> transient overshoot ("snap")

    double sampleRate = 44100.0;

    std::vector<float> envelopeState;         // per-channel squared-signal envelope (detector)
    std::vector<float> compressionDuration;   // per-channel 0..1 "how long have we been compressing" integrator

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
