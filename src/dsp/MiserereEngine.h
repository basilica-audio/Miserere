#pragma once

#include <juce_dsp/juce_dsp.h>

#include "ConsoleEq.h"
#include "DeEsser.h"
#include "FetCompressor.h"
#include "FetCrush.h"
#include "OptoLeveler.h"
#include "PassiveEq.h"
#include "SlapDelay.h"
#include "SpreadPitch.h"
#include "TapeSat.h"

// The complete Miserere v2 signal path, independent of juce::AudioProcessor
// so it can be exercised directly by unit tests without instantiating a full
// plugin. Owns all DSP state; every buffer/filter/delay line is allocated in
// prepare() and never reallocated on the audio thread.
//
// Signal topology (docs/design-brief.md, binding - "supersedes v1
// entirely"):
//
//   in -> [In Trim] -> DIRECT PATH (serial, every section optional, ALL OFF
//                        by default): De-Esser (pre) -> FET Comp light ->
//                        Console EQ -> Sat -> De-Esser (post)
//           = "the channel". Output feeds the sum at unity AND all four
//             sends (unity taps):
//             -> (1) CRUSH    : FET limiter, all-buttons character
//             -> (2) SANDWICH : Passive EQ -> Opto Leveler -> Passive EQ
//             -> (3) SPREAD   : dual micro-pitch (~30/50 ms, +/-cents, L/R)
//             -> (4) SLAP     : ~110 ms dark single-repeat delay
//        Sum (direct + returns, each scaled by its fader AND the Parallel
//        macro trim) -> [Out Trim] -> out
//
// THE CORE INVARIANT (v2's binding correction over v1): the direct path is
// bit-transparent by default - every one of its sections defaults OFF - so
// a fresh instance passes audio essentially untouched (see the
// default-wire null test, guarantee 1). Everything else is ADDED underneath
// via the four parallel return busses, which are silent by default (fader
// floor).
//
// PHASE DISCIPLINE (the suite invariant this plugin is still built around,
// docs/adr/0003): busses (1) CRUSH and (2) SANDWICH contain only
// minimum-phase IIR filters and causal (no lookahead) gain computers -
// every one of their samples leaves at the same index it arrived, so the
// parallel sum can never comb against the direct path. Busses (3) SPREAD
// and (4) SLAP are delays BY DESIGN and exempt. Reported latency is always
// 0 regardless.
//
// Mute/Audition semantics (resolved at the summing stage): Mute always
// silences its bus; if ANY bus is auditioned, ONLY the auditioned
// (unmuted) bus(ses) reach the output and the direct path itself is
// excluded too (Audition isolates exactly what it names, the same as a
// console aux-return solo - "labelled Audition, because the technique
// forbids judging solo sound" is a UI framing choice, not a different
// signal-flow rule). Mute wins over Audition on the same bus. Audition
// *exclusivity* (engaging one audition releases the others) is
// parameter-level behaviour enforced in PluginProcessor via an APVTS
// listener - the engine itself handles any combination of flags it is
// given.
//
// The final sum also sanitises non-finite samples to 0 (see
// docs/architecture.md's NaN/Inf policy) - the output-side guarantee is
// handled here, the state-side recovery is reset()'s job.
class MiserereEngine
{
public:
    MiserereEngine();

    // Allocates all DSP state (including the Slap delay line and the two
    // Spread micro-pitch delay lines at full capacity). Must be called (and
    // completed) before the first process() call, and again whenever sample
    // rate/block size/channel count change.
    void prepare (const juce::dsp::ProcessSpec& spec);

    // Clears ALL module state - filters, envelopes, opto memory, and every
    // delay line - without deallocating.
    void reset();

    // Processes `block` in place. Real-time safe: no allocation once
    // prepare() has completed. A zero-sample block is a safe no-op; a block
    // larger than what prepare() was sized for is defensively trimmed to
    // the prepared capacity (PluginProcessor additionally chunks oversized
    // host blocks so no audio is dropped - this trim is the engine's own
    // last-resort guard).
    void process (juce::dsp::AudioBlock<float>& block) noexcept;

    //==========================================================================
    // Global
    void setInTrimDb (float newTrimDb) { inTrimGain.setGainDecibels (newTrimDb); }
    void setOutTrimDb (float newTrimDb) { outTrimGain.setGainDecibels (newTrimDb); }
    void setLinked (bool shouldBeLinked) noexcept
    {
        crush.setLinked (shouldBeLinked);
        opto.setLinked (shouldBeLinked);
    }
    void setParallelTrimDb (float newTrimDb) noexcept;

    //==========================================================================
    // Direct path
    void setDeessPreEnabled (bool enabled) noexcept { deesserPre.setEnabled (enabled); }
    void setDeessPreFreqHz (float hz) noexcept { deesserPre.setFrequencyHz (hz); }
    void setDeessPreThresholdDb (float db) noexcept { deesserPre.setThresholdDb (db); }

    void setDirectFetEnabled (bool enabled) noexcept { directFetEnabled = enabled; }
    void setDirectFetThresholdDb (float db) noexcept { directFet.setThresholdDb (db); }
    void setDirectFetAttackMs (float ms) noexcept { directFet.setAttackMs (ms); }
    void setDirectFetReleaseMs (float ms) noexcept { directFet.setReleaseMs (ms); }
    void setDirectFetMakeupDb (float db) noexcept { directFet.setMakeupDb (db); }

    void setEqHpfEnabled (bool enabled) noexcept { consoleEq.setHpfEnabled (enabled); }
    void setEqHpfFreqHz (float hz) noexcept { consoleEq.setHpfFreqHz (hz); }
    void setEqLowFreqHz (float hz) noexcept { consoleEq.setLowFreqHz (hz); }
    void setEqLowGainDb (float db) noexcept { consoleEq.setLowGainDb (db); }
    void setEqMidFreqHz (float hz) noexcept { consoleEq.setMidFreqHz (hz); }
    void setEqMidGainDb (float db) noexcept { consoleEq.setMidGainDb (db); }
    void setEqHighGainDb (float db) noexcept { consoleEq.setHighGainDb (db); }
    void setEqDriveDb (float db) noexcept { consoleEq.setDriveDb (db); }

    void setSatDriveDb (float db) noexcept { tapeSat.setDriveDb (db); }

    void setDeessPostEnabled (bool enabled) noexcept { deesserPost.setEnabled (enabled); }
    void setDeessPostFreqHz (float hz) noexcept { deesserPost.setFrequencyHz (hz); }
    void setDeessPostThresholdDb (float db) noexcept { deesserPost.setThresholdDb (db); }

    //==========================================================================
    // Bus (1) CRUSH
    void setCrushInputDriveDb (float db) noexcept { crush.setInputDriveDb (db); }
    void setCrushRatio (FetCrush::Ratio r) noexcept { crush.setRatio (r); }
    void setCrushStyle (FetCrush::Style s) noexcept { crush.setStyle (s); }
    void setCrushAttackStep (float step) noexcept { crush.setAttackStep (step); }
    void setCrushReleaseStep (float step) noexcept { crush.setReleaseStep (step); }
    void setCrushOutputTrimDb (float db) noexcept { crush.setOutputTrimDb (db); }

    //==========================================================================
    // Bus (2) SANDWICH
    void setSandPreLfFreqHz (float hz) noexcept { sandwichPreEq.setLfFreqHz (hz); }
    void setSandPreLfBoostDial (float dial) noexcept { sandwichPreEq.setLfBoostDial (dial); }
    void setSandPreLfCutDial (float dial) noexcept { sandwichPreEq.setLfCutDial (dial); }
    void setSandPreHfBellFreqHz (float hz) noexcept { sandwichPreEq.setHfBellFreqHz (hz); }
    void setSandPreHfBellBoostDial (float dial) noexcept { sandwichPreEq.setHfBellBoostDial (dial); }
    void setSandPreHfBellBandwidthDial (float dial) noexcept { sandwichPreEq.setHfBellBandwidthDial (dial); }
    void setSandPreHfShelfFreqHz (float hz) noexcept { sandwichPreEq.setHfShelfFreqHz (hz); }
    void setSandPreHfShelfAttenDial (float dial) noexcept { sandwichPreEq.setHfShelfAttenDial (dial); }

    void setSandPeakReductionProportion (float amount01) noexcept { opto.setPeakReductionProportion (amount01); }
    void setSandLimitEnabled (bool enabled) noexcept { opto.setLimitEnabled (enabled); }
    void setSandEmphasisProportion (float amount01) noexcept { opto.setEmphasisProportion (amount01); }
    void setSandResidualEnabled (bool enabled) noexcept
    {
        sandwichPreEq.setResidualEnabled (enabled);
        sandwichPostEq.setResidualEnabled (enabled);
    }

    void setSandPostLfFreqHz (float hz) noexcept { sandwichPostEq.setLfFreqHz (hz); }
    void setSandPostLfBoostDial (float dial) noexcept { sandwichPostEq.setLfBoostDial (dial); }
    void setSandPostLfCutDial (float dial) noexcept { sandwichPostEq.setLfCutDial (dial); }
    void setSandPostHfBellFreqHz (float hz) noexcept { sandwichPostEq.setHfBellFreqHz (hz); }
    void setSandPostHfBellBoostDial (float dial) noexcept { sandwichPostEq.setHfBellBoostDial (dial); }
    void setSandPostHfBellBandwidthDial (float dial) noexcept { sandwichPostEq.setHfBellBandwidthDial (dial); }
    void setSandPostHfShelfFreqHz (float hz) noexcept { sandwichPostEq.setHfShelfFreqHz (hz); }
    void setSandPostHfShelfAttenDial (float dial) noexcept { sandwichPostEq.setHfShelfAttenDial (dial); }

    //==========================================================================
    // Bus (3) SPREAD
    void setSpreadDetuneCents (float cents) noexcept { spread.setDetuneCents (cents); }
    void setSpreadTimeScale (float scale) noexcept { spread.setTimeScale (scale); }
    void setSpreadWidth (float amount01) noexcept { spread.setWidth (amount01); }

    //==========================================================================
    // Bus (4) SLAP
    void setSlapDelayMs (float ms) noexcept { slap.setDelayMs (ms); }
    void setSlapStereoEnabled (bool enabled) noexcept { slap.setStereoEnabled (enabled); }
    void setSlapToneProportion (float amount01) noexcept { slap.setToneProportion (amount01); }

    //==========================================================================
    // Per-bus fader/mute/audition (indices 0..3 = Crush/Sandwich/Spread/
    // Slap). Level in dB; at or below the fader floor (-60 dB) the bus
    // contributes exactly 0.
    void setBusLevelDb (int busIndex, float levelDb) noexcept;
    void setBusMute (int busIndex, bool muted) noexcept;
    void setBusAudition (int busIndex, bool auditioned) noexcept;

    // GR metering values for a future GUI and for tests.
    float getDirectFetGainReductionDb() const noexcept { return directFet.getCurrentGainReductionDb(); }
    float getCrushGainReductionDb() const noexcept { return crush.getCurrentGainReductionDb(); }
    float getSandGainReductionDb() const noexcept { return opto.getCurrentGainReductionDb(); }

    // Always 0: busses (1)/(2) are minimum-phase/causal, and busses (3)/(4)'s
    // delays are the effects themselves, not compensation artefacts.
    static constexpr int getLatencySamples() noexcept { return 0; }

    static constexpr int numBusses = 4; // Crush, Sandwich, Spread, Slap (Direct is not a fader-bearing "bus")
    static constexpr float faderFloorDb = -60.0f;

private:
    static constexpr double smoothingTimeSeconds = 0.05;

    double sampleRate = 44100.0;

    juce::dsp::Gain<float> inTrimGain;
    juce::dsp::Gain<float> outTrimGain;

    // A plain per-sample-smoothed value (not juce::dsp::Gain, whose
    // getGainLinear() returns the unsmoothed *target* rather than a
    // per-sample-interpolated current value) so the Parallel macro trim
    // ramps click-free even though it is read inside the per-sample summing
    // loop below.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> parallelTrimSmoothed;
    float lastParallelTrimDb = 0.0f;

    // Direct path (serial).
    DeEsser deesserPre;
    FetCompressor directFet;
    bool directFetEnabled = false;
    ConsoleEq consoleEq;
    TapeSat tapeSat;
    DeEsser deesserPost;

    // Parallel busses.
    FetCrush crush;
    PassiveEq sandwichPreEq;
    OptoLeveler opto;
    PassiveEq sandwichPostEq;
    SpreadPitch spread;
    SlapDelay slap;

    // Working buffers: the processed direct-path signal, and one per
    // parallel bus, sized to the maximum block/channel count in prepare()
    // and never reallocated on the audio thread.
    juce::AudioBuffer<float> directBuffer;
    std::array<juce::AudioBuffer<float>, numBusses> busBuffers;

    // Per-bus fader gains, smoothed. A separate mute/audition-resolved 0/1
    // multiplier is applied at the sum (unsmoothed - a discrete mix
    // decision, matching the suite's console semantics elsewhere).
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>, numBusses> busGainSmoothed;
    std::array<float, numBusses> lastBusLevelDb { -9.0f, -12.0f, -18.0f, -15.0f }; // brief's defaults: Crush/Sandwich/Spread/Slap
    std::array<bool, numBusses> busMuted {};
    std::array<bool, numBusses> busAuditioned {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MiserereEngine)
};
