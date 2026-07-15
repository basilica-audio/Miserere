#pragma once

#include <juce_dsp/juce_dsp.h>

#include "ConsoleEq.h"
#include "DeEsser.h"
#include "FetCompressor.h"
#include "Hpf.h"
#include "OptoLeveler.h"
#include "PassiveEq.h"
#include "SlapDelay.h"
#include "TapeSat.h"

// The complete Miserere signal path, independent of juce::AudioProcessor so
// it can be exercised directly by unit tests without instantiating a full
// plugin. Owns all DSP state; every buffer/filter/delay line is allocated
// in prepare() and never reallocated on the audio thread.
//
// Signal topology (docs/design-brief.md, binding):
//
//                  +- BUS A "Direct": HPF -> Console EQ -> FET Comp -> De-Esser -> Tape Sat - fader -+
//  in - [In Trim] -+- BUS B "Opto":   Passive EQ in -> Opto Leveler -> Passive Air out ------ fader -+- Sum - [Out Trim] - out
//                  +- BUS C "Smash":  FET Limiter (all-buttons, mid-forward sidechain) ------ fader -+
//                  +- BUS D "Slap":   Slap Delay (60-180 ms, filtered tape-soft feedback) --- fader -+
//
// PHASE DISCIPLINE (the suite invariant this plugin is built around):
// Busses A-C contain only minimum-phase IIR filters and causal (no
// lookahead) gain computers - every one of their samples leaves at the same
// index it arrived, so the parallel sum can never comb against the Direct
// bus. Bus D is a delay by design and exempt (see
// docs/adr/0003-parallel-bus-topology.md). Reported latency is always 0.
//
// Mute/Solo semantics (resolved at the summing stage): Mute always silences
// its bus; if any bus is soloed, only soloed (and unmuted) busses reach the
// sum; Mute wins over Solo on the same bus. Solo *exclusivity* (engaging
// one solo releases the others) is a parameter-level behaviour enforced in
// PluginProcessor via an APVTS listener - the engine itself deliberately
// handles any combination of flags it is given.
//
// The final sum also sanitises non-finite samples to 0 (a NaN/Inf that
// enters via the host buffer would otherwise both propagate to the output
// AND permanently poison IIR state; the output-side guarantee is handled
// here, the state-side recovery is reset()'s job - see the M1 guarantee
// "NaN/Inf sweep -> finite output; state recovers after reset()").
class MiserereEngine
{
public:
    MiserereEngine();

    // Allocates all DSP state (including the slap delay line at full 180 ms
    // capacity). Must be called (and completed) before the first process()
    // call, and again whenever sample rate/block size/channel count change.
    void prepare (const juce::dsp::ProcessSpec& spec);

    // Clears ALL module state - filters, envelopes, opto history, and the
    // slap delay line - without deallocating.
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

    //==========================================================================
    // Bus A - Direct
    void setHpfEnabled (bool enabled) noexcept { hpf.setEnabled (enabled); }
    void setHpfFreqHz (float hz) noexcept { hpf.setFrequencyHz (hz); }
    void setEqLowGainDb (float db) noexcept { consoleEq.setLowGainDb (db); }
    void setEqMidFreqHz (float hz) noexcept { consoleEq.setMidFreqHz (hz); }
    void setEqMidGainDb (float db) noexcept { consoleEq.setMidGainDb (db); }
    void setEqMidQ (float q) noexcept { consoleEq.setMidQ (q); }
    void setEqHighGainDb (float db) noexcept { consoleEq.setHighGainDb (db); }
    void setCompRatio (float ratio) noexcept { fetComp.setRatio (ratio); }
    void setCompThresholdDb (float db) noexcept { fetComp.setThresholdDb (db); }
    void setCompAttackMs (float ms) noexcept { fetComp.setAttackMs (ms); }
    void setCompReleaseMs (float ms) noexcept { fetComp.setReleaseMs (ms); }
    void setCompMakeupDb (float db) noexcept { fetComp.setMakeupDb (db); }
    void setDeessEnabled (bool enabled) noexcept { deEsser.setEnabled (enabled); }
    void setDeessFreqHz (float hz) noexcept { deEsser.setFrequencyHz (hz); }
    void setDeessThresholdDb (float db) noexcept { deEsser.setThresholdDb (db); }
    void setSatDriveDb (float db) noexcept { tapeSat.setDriveDb (db); }

    //==========================================================================
    // Bus B - Opto
    void setPassiveLowBoost (float freqHz, float gainDb) noexcept { passiveEqIn.setLowBoost (freqHz, gainDb); }
    void setPassiveHighBoost (float freqHz, float gainDb) noexcept { passiveEqIn.setHighBoost (freqHz, gainDb); }
    void setOptoPeakReduction (float amount01) noexcept { opto.setPeakReductionProportion (amount01); }
    void setOptoMakeupDb (float db) noexcept { opto.setMakeupDb (db); }
    void setPassiveAirGainDb (float db) noexcept { passiveAirOut.setAirGainDb (db); }

    //==========================================================================
    // Bus C - Smash
    void setSmashAttackMs (float ms) noexcept { smash.setAttackMs (ms); }
    void setSmashReleaseMs (float ms) noexcept { smash.setReleaseMs (ms); }
    void setSmashDriveDb (float db) noexcept { smash.setDriveDb (db); }
    void setSmashOutputTrimDb (float db) noexcept { smash.setOutputTrimDb (db); }

    //==========================================================================
    // Bus D - Slap
    void setSlapDelayMs (float ms) noexcept { slap.setDelayMs (ms); }
    void setSlapFeedback (float amount01) noexcept { slap.setFeedbackProportion (amount01); }
    void setSlapLoopHighPassHz (float hz) noexcept { slap.setLoopHighPassHz (hz); }
    void setSlapLoopLowPassHz (float hz) noexcept { slap.setLoopLowPassHz (hz); }
    void setSlapMonoEnabled (bool mono) noexcept { slap.setMonoEnabled (mono); }

    //==========================================================================
    // Per-bus fader/mute/solo (indices 0..3 = A..D). Level in dB; at or
    // below the fader floor (-60 dB) the bus contributes exactly 0.
    void setBusLevelDb (int busIndex, float levelDb) noexcept;
    void setBusMute (int busIndex, bool muted) noexcept;
    void setBusSolo (int busIndex, bool soloed) noexcept;

    // GR metering values (brief: "GR metering value exposed" for the Bus A
    // comp; the others come along for free and future GUI meters).
    float getDirectCompGainReductionDb() const noexcept { return fetComp.getCurrentGainReductionDb(); }
    float getOptoGainReductionDb() const noexcept { return opto.getCurrentGainReductionDb(); }
    float getSmashGainReductionDb() const noexcept { return smash.getCurrentGainReductionDb(); }

    // Always 0: Busses A-C are minimum-phase/causal, and Bus D's delay is
    // the effect itself, not a compensation artefact (see class comment).
    static constexpr int getLatencySamples() noexcept { return 0; }

    static constexpr int numBusses = 4;
    static constexpr float faderFloorDb = -60.0f;
    static constexpr float smashRatio = 20.0f;       // all-buttons ~20:1 (brief)
    static constexpr float smashThresholdDb = -20.0f; // fixed - Drive pushes into it

private:
    static constexpr double smoothingTimeSeconds = 0.05;

    double sampleRate = 44100.0;

    juce::dsp::Gain<float> inTrimGain;
    juce::dsp::Gain<float> outTrimGain;

    // Bus A
    Hpf hpf;
    ConsoleEq consoleEq;
    FetCompressor fetComp;
    DeEsser deEsser;
    TapeSat tapeSat;

    // Bus B
    PassiveEq passiveEqIn;
    OptoLeveler opto;
    PassiveEq passiveAirOut;

    // Bus C
    FetCompressor smash;

    // Bus D
    SlapDelay slap;

    // Per-bus working buffers, sized to the maximum block/channel count in
    // prepare() and never reallocated on the audio thread.
    std::array<juce::AudioBuffer<float>, numBusses> busBuffers;

    // Per-bus fader gains, smoothed. A separate mute/solo-resolved 0/1
    // multiplier is applied at the sum (unsmoothed - a discrete mix
    // decision, matching the suite's console semantics elsewhere).
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>, numBusses> busGainSmoothed;
    std::array<float, numBusses> lastBusLevelDb { 0.0f, faderFloorDb, faderFloorDb, faderFloorDb };
    std::array<bool, numBusses> busMuted {};
    std::array<bool, numBusses> busSoloed {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MiserereEngine)
};
