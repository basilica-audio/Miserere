#pragma once

#include <juce_dsp/juce_dsp.h>

// Bus B's opto-style leveler: program-dependent two-stage release (~60 ms
// fast stage into ~600 ms slow stage, release slows with sustained GR),
// soft ~3:1 effective ratio, fixed ~10 ms attack, Peak Reduction 0-100%,
// makeup (brief).
//
// The optical-cell character is modelled with two coupled mechanisms:
//
// 1. Gain-domain ballistics: the target gain reduction (from a soft 3:1
//    curve over a Peak-Reduction-derived threshold) is smoothed in the dB
//    domain with a fixed ~10 ms attack and a *variable* release.
//
// 2. Light-history memory: a slow accumulator integrates how much gain
//    reduction has been applied recently (the "how long has the LED been
//    bright" state of a real opto cell). The release time interpolates
//    from the fast stage (~60 ms, cold cell / short transient GR) to the
//    slow stage (~600 ms, hot cell / sustained GR) as the history builds -
//    so a brief peak recovers quickly while material that has been leaning
//    on the leveler for a while releases lazily. This is the two-stage,
//    program-dependent release the brief specifies and the M1 guarantee
//    tests ("release measurably slows with longer GR history").
//
// Peak Reduction == 0% is a BIT-EXACT bypass: the threshold sits at 0 dB
// and the ratio at 1:1, and process() skips the gain multiply entirely (the
// detector/history state still advances so engaging reduction mid-stream
// starts from a settled state). Makeup at 0 dB multiplies by exactly 1.0f.
//
// Zero latency (causal envelope, no lookahead), pure gain multiply - Bus B
// stays sample-aligned per the parallel-bus phase discipline.
//
// Detection is per-channel independent (not stereo-linked) - the same
// acceptable v0.1 simplification as the suite's other hand-rolled dynamics.
class OptoLeveler
{
public:
    OptoLeveler() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    // 0-1 (0-100%). 0 is a bit-exact bypass; 1 pulls the threshold down to
    // thresholdMinDb and the effective ratio up to maxRatio (~3:1).
    void setPeakReductionProportion (float newAmount01) noexcept;

    void setMakeupDb (float newMakeupDb) noexcept { makeupGainLinear = juce::Decibels::decibelsToGain (newMakeupDb); }

    // Processes `block` in place. A zero-sample block is a safe no-op. No
    // allocation occurs here.
    void process (juce::dsp::AudioBlock<float>& block) noexcept;

    // Current gain reduction in dB (positive = reduction), peak across
    // channels in the last processed block - exposed for metering/tests.
    float getCurrentGainReductionDb() const noexcept { return currentGainReductionDb; }

private:
    static constexpr float thresholdMinDb = -32.0f; // threshold at Peak Reduction == 100%
    static constexpr float maxRatio = 3.0f;          // soft ~3:1 effective ratio (brief)
    static constexpr double attackTimeSeconds = 0.010;      // ~10 ms fixed (brief)
    static constexpr double fastReleaseSeconds = 0.060;     // fast stage (brief: ~60 ms)
    static constexpr double slowReleaseSeconds = 0.600;     // slow stage (brief: ~600 ms)
    static constexpr double detectorTimeSeconds = 0.005;    // RMS-ish level detector, faster than the gain ballistics
    static constexpr double historyTimeSeconds = 1.5;       // light-history integration time
    static constexpr float historyFullScaleGrDb = 6.0f;     // GR (dB) that saturates the history toward the slow stage
    static constexpr double smoothingTimeSeconds = 0.05;

    double sampleRate = 44100.0;

    std::vector<float> detectorState;   // per-channel squared-signal envelope
    std::vector<float> grSmoothedDb;    // per-channel smoothed gain reduction (dB domain)
    std::vector<float> historyState;    // per-channel light-history accumulator, 0..1

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> amountSmoothed;

    float lastAmount01 = 0.4f;
    float makeupGainLinear = 1.0f;

    float currentGainReductionDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptoLeveler)
};
