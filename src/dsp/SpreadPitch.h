#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cstdint>
#include <vector>

// Bus (3) SPREAD (docs/design-brief.md): dual micro-pitch, modelled on the
// documented "dual 910s... 30 and 50 milliseconds approximately, up a tiny
// bit, down a tiny bit" technique (docs/research-notes.md) - two short
// delay taps (~30 ms pitched up, ~50 ms pitched down), hard-panned L/R, at a
// low enough return level that the ear reads "pushed to the outside" rather
// than chorusing.
//
// The module sums its input to mono (the technique is applied to a mono
// vocal source) and feeds two independent "micro-pitch voices", each a
// classic delay-line Doppler pitch shifter: a single delay line read by TWO
// crossfading taps whose read position glides at a rate proportional to the
// desired pitch ratio (read speed != write speed = a pitch shift), each tap
// wrapping and resetting to the opposite phase inside a crossfade window
// before it runs out of buffer.
//
// v0.5.0 quality pass (brief F6):
// - 3rd-order Lagrange interpolation on both shifter delay lines (replaces
//   linear - the interp loss at 10 kHz drops from ~2 dB to under 1 dB,
//   tests/SpreadPitchTests.cpp).
// - detune/timeScale ride per-sample SmoothedValue ramps (50 ms) instead of
//   block-boundary steps - automation is click-free.
// - grain window 40 ms -> 60 ms with an EQUAL-POWER (sin) crossfade
//   replacing the raised-cosine window, taps still half-grain offset: the
//   two tap gains obey g1^2 + g2^2 == 1, so the summed POWER of the
//   (decorrelated) taps stays constant and the periodic envelope ripple on
//   sustained vowels shrinks by more than 6 dB vs v0.4.0.
//
// Period-adaptive splice (issue #19). On a sustained periodic input the two
// taps carry the SAME tone at a fixed relative delay tau = grain/2, so
// their sum has a coherent interference term cos(2*pi*f*tau): with the
// equal-power window the envelope is 1 + sin(2*pi*pos)*cos(2*pi*f*tau),
// which passes through a true null mid-crossfade whenever the note lands
// near an anti-phase tooth of the 1/tau comb (measured: >20 dB envelope
// ripple at (k+0.5)/tau, ~3 dB at k/tau). No static window shape or splice
// timing can remove this - the null is set purely by tau against the input
// period. The fix implemented here therefore adapts tau itself:
// - a decimated-domain normalized autocorrelation (PeriodDetector) searches
//   the input for its strongest period alignment in [tau - 1/80 Hz, tau],
// - when the input is confidently periodic, the SEPARATION target snaps
//   DOWN to m * period: taps then sum in phase at the fundamental and every
//   harmonic simultaneously, and splices land phase-continuous,
// - the live separation converges on the target two ways: a capped
//   micro-slew (~<=3.5 cents transient, applied to whichever tap currently
//   carries less window gain, so it is masked) acts continuously WITHIN a
//   note, and each wrap re-seats the wrapping tap relative to the OTHER tap
//   at exactly the target separation (the other tap is provably at window
//   centre at that instant, so the re-seat is click-free and exact),
// - simultaneously the window law blends from equal-power sin (optimal for
//   decorrelated material) to amplitude-complementary sin^2 (flat envelope
//   for in-phase taps), gated on the separation actually having arrived -
//   a misaligned sin^2 pair is strictly worse than sin,
// - on non-periodic material the detector's confidence gate (with
//   hysteresis, so borderline material cannot flap the target) keeps the
//   nominal 30 ms separation and sin window: the un-engaged path computes
//   the same values as the fixed-separation code, identical up to float
//   rounding at wrap re-seats, so the doubled-vocal character on programme
//   material is preserved.
// Because the normalized autocorrelation is scale-invariant, a treble-only
// tone aliased through the decimator would fake full confidence: a spectral
// plausibility gate (energy below the detector lowpass vs full-band energy)
// keeps the detector out of material with no fundamental in its range.
//
// Voice 0 (base ~30 ms) is detuned UP by `spread_detune` cents and panned
// toward the left; voice 1 (base ~50 ms) is detuned DOWN by the same amount
// and panned toward the right. `spread_width` blends between the two
// voices' fully hard-panned outputs (100%) and an equal centre blend of
// both (0%).
//
// This bus is a delay by design (like SLAP) and is therefore exempt from
// the sample-alignment invariant that busses (1)/(2) must honour - see
// docs/adr/0003. It still reports zero *latency*: the delay is the effect,
// not a compensation artefact (the period detector only ever reads already
// written history - no lookahead).
class SpreadPitch
{
public:
    SpreadPitch() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    void setDetuneCents (float cents) noexcept;
    void setTimeScale (float scale) noexcept;
    void setWidth (float amount01) noexcept;

    // A/B hook for the period-adaptive splice (issue #19). Default ON. Not
    // click-free: intended for tests and offline comparison, set it before
    // processing, not while audio runs.
    void setSmartSplice (bool enabled) noexcept;

    // Replaces `block`'s contents with the wet (pitch-shifted) stereo
    // signal. A zero-sample block is a safe no-op. No allocation occurs
    // here. Mono hosts receive the L channel's content only (the caller's
    // block is already channel-count-limited to what prepare() promised).
    void process (juce::dsp::AudioBlock<float>& block) noexcept;

private:
    static constexpr float baseDelayUpMs = 30.0f;
    static constexpr float baseDelayDownMs = 50.0f;
    static constexpr float grainMs = 60.0f; // nominal crossfade window width per voice (F6: was 40 ms)
    static constexpr float maxTimeScale = 2.0f;
    static constexpr float capacityHeadroomMs = 20.0f;
    static constexpr double smoothingTimeSeconds = 0.05;

    // Period-adaptive splice (issue #19).
    static constexpr double targetSmoothingSeconds = 0.1;
    static constexpr float detectorFloorHz = 80.0f;       // lowest trackable fundamental
    static constexpr double detectorTargetRateHz = 12000.0;
    static constexpr float confidenceGateLow = 0.6f;      // engage above this: snap the separation
    static constexpr float confidenceGateRelease = 0.5f;  // release below this (hysteresis - no target flapping at the boundary)
    static constexpr float confidenceGateHigh = 0.9f;     // fully sin^2 window at/above this
    static constexpr float maxSepSlewPerSample = 0.002f;  // ~3.5 cents transient on the quieter tap

    struct Voice
    {
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delayLine;
        std::array<float, 2> tapDelaySamples { 0.0f, 0.0f }; // current read delay per crossfading tap
        float pitchRatio = 1.0f;
        float sepSamples = 0.0f; // live tap separation == half the crossfade window width

        void prepare (const juce::dsp::ProcessSpec& monoSpec, float maxDelaySamples);
        void reset (float baseSamples, float halfGrainSamples);
        float processSample (float input, float baseSamples, float sepTarget, float windowBlend) noexcept;
    };

    // Decimated-domain normalized autocorrelation around the nominal tap
    // separation. Progressive: ONE lag is evaluated per decimated sample
    // against a reference window frozen at sweep start, so the audio-thread
    // cost is a flat ~2*winLen MACs per decimated sample (~100/sample at
    // 48 kHz) with no per-block spikes. All sizes fixed in prepare().
    struct PeriodDetector
    {
        std::vector<float> history;       // decimated, lowpassed mono input (power-of-2 ring)
        std::array<float, 512> sweepR {}; // NACF per lag of the current sweep
        std::uint64_t writeCount = 0;
        int histMask = 0;
        int decFactor = 4;
        int decPhase = 0;
        float decAccum = 0.0f;
        float lp1 = 0.0f, lp2 = 0.0f;     // 2x one-pole lowpass (fundamentals only)
        float lpCoeff = 0.0f;
        float lowPower = 0.0f, fullPower = 0.0f; // spectral plausibility gate state
        float powerCoeff = 0.0f;
        int winLen = 0;                   // correlation window (decimated samples)
        int lagMin = 0, lagMax = 0, numLags = 0;
        std::uint64_t warmupCount = 0;
        int sweepLag = 0;                 // next lag index; 0 also means "start a new sweep"
        std::uint64_t sweepAnchor = 0;    // frozen write position of the current sweep
        float refEnergy = 0.0f;

        float lastLagFullRate = 0.0f;     // refined best alignment lag, full-rate samples
        float lastConfidence = 0.0f;      // gated NACF peak value, 0..1

        void prepare (double fullRate, float nominalTauSeconds, float floorHz);
        void reset() noexcept;
        // Feed one full-rate sample; returns true when a sweep just
        // completed and lastLagFullRate/lastConfidence hold fresh values.
        bool pushSample (float x) noexcept;
    };

    double sampleRate = 44100.0;

    Voice voiceUp;   // ~30 ms, pitched up
    Voice voiceDown; // ~50 ms, pitched down

    PeriodDetector detector;
    bool smartSplice = true;
    bool detectorEngaged = false;   // hysteresis state of the confidence gate
    float nominalSepSamples = 0.0f; // grain/2 at the prepared sample rate

    // Rate-invariant tolerances, set in prepare(): the detector's lag
    // quantum is decFactor full-rate samples, so absolute-sample constants
    // would be defeated at 96/192 kHz (estimate jitter scales with the
    // quantum, and phase tolerance is a TIME quantity).
    float sepDeadbandSamples = 1.0f;  // ignore target moves below ~1/3 lag quantum
    float blendAlignTolInv = 0.125f;  // sin^2 fades out within ~170 us of misalignment

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> widthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> detuneSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> timeScaleSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> sepTargetSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> windowBlendSmoothed;

    float detuneCents = 6.0f;
    float timeScale = 1.0f;
    float width = 0.7f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpreadPitch)
};
