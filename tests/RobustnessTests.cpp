#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <random>

// Guarantee 9: NaN/Inf sweep -> finite output with state recovery after
// reset(), and the Release-safe oversized-block clamp - plus the
// suite-standard robustness battery (silence, denormals, zero-sample
// blocks, extreme parameters, rapid automation).
namespace
{
    void setParam (MiserereAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    void setBool (MiserereAudioProcessor& processor, const char* id, bool on)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (on ? 1.0f : 0.0f);
    }

    // Brings up all four bus faders and drives every dynamics module so
    // every path (including both delay-line busses) actually processes
    // signal during the sweeps.
    void bringUpAllBusses (MiserereAudioProcessor& processor)
    {
        setParam (processor, ParamIDs::crushLevel, 0.0f);
        setParam (processor, ParamIDs::sandLevel, 0.0f);
        setParam (processor, ParamIDs::spreadLevel, 0.0f);
        setParam (processor, ParamIDs::slapLevel, 0.0f);
        setParam (processor, ParamIDs::crushInput, 24.0f);
        setParam (processor, ParamIDs::sandPeakRed, 60.0f);
        setBool (processor, ParamIDs::directFetEnabled, true);
        setBool (processor, ParamIDs::directEqHpfEnabled, true);
        setBool (processor, ParamIDs::directDeessPreEnabled, true);
        setBool (processor, ParamIDs::directDeessPostEnabled, true);
    }
}

TEST_CASE ("Silence produces silence (and no NaN/Inf)", "[robustness]")
{
    MiserereAudioProcessor processor;
    bringUpAllBusses (processor);
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
    {
        buffer.clear();
        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }

    CHECK (TestHelpers::peakAbsolute (buffer) < 1.0e-5f);
}

TEST_CASE ("NaN input produces finite output", "[robustness][nan]")
{
    MiserereAudioProcessor processor;
    bringUpAllBusses (processor);
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    for (int channel = 0; channel < 2; ++channel)
    {
        auto* data = buffer.getWritePointer (channel);
        for (int sample = 0; sample < 512; ++sample)
            data[sample] = (sample % 3 == 0) ? std::numeric_limits<float>::quiet_NaN() : 0.5f;
    }

    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Inf input produces finite output, and state recovers after reset()", "[robustness][nan][reset]")
{
    MiserereAudioProcessor processor;
    bringUpAllBusses (processor);
    processor.prepareToPlay (48000.0, 512);

    juce::MidiBuffer midi;

    {
        juce::AudioBuffer<float> buffer (2, 512);
        for (int channel = 0; channel < 2; ++channel)
        {
            auto* data = buffer.getWritePointer (channel);
            for (int sample = 0; sample < 512; ++sample)
                data[sample] = (sample % 2 == 0) ? std::numeric_limits<float>::infinity()
                                                  : -std::numeric_limits<float>::infinity();
        }

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }

    CHECK_NOTHROW (processor.reset());

    double totalRms = 0.0;

    for (int block = 0; block < 8; ++block)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        TestHelpers::fillWithSine (buffer, 48000.0, 440.0, 0.5f, block * 512);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
        totalRms += TestHelpers::rms (buffer);
    }

    CHECK (totalRms / 8.0 > 0.01);
}

TEST_CASE ("Denormal-range input produces no NaN/Inf output", "[robustness][denormal]")
{
    MiserereAudioProcessor processor;
    bringUpAllBusses (processor);
    processor.prepareToPlay (48000.0, 512);

    constexpr int numSamples = 512;
    juce::AudioBuffer<float> buffer (2, numSamples);

    const auto denormalValue = std::numeric_limits<float>::denorm_min() * 4.0f;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int sample = 0; sample < numSamples; ++sample)
            data[sample] = (sample % 2 == 0) ? denormalValue : -denormalValue;
    }

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Zero-sample buffer does not crash processBlock", "[robustness]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 0);
    juce::MidiBuffer midi;

    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (buffer.getNumSamples() == 0);
}

TEST_CASE ("Oversized block: processed fully and safely via chunking (Release-safe real clamp)", "[robustness][oversized]")
{
    MiserereAudioProcessor processor;
    bringUpAllBusses (processor);

    // Deliberately prepare with a tiny block size, then send a much larger
    // buffer - exercising the real chunking clamp in processBlock() (not a
    // jassert, which compiles out of Release builds).
    processor.prepareToPlay (48000.0, 128);

    juce::AudioBuffer<float> buffer (2, 8192);
    TestHelpers::fillWithSine (buffer, 48000.0, 440.0, 0.5f);

    juce::MidiBuffer midi;
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (TestHelpers::allSamplesFinite (buffer));

    double tailEnergy = 0.0;
    for (int sample = 4096; sample < 8192; ++sample)
        tailEnergy += std::abs (buffer.getSample (0, sample));

    CHECK (tailEnergy > 1.0);
}

TEST_CASE ("Oversized block: the direct-path null property survives chunking", "[robustness][oversized][null]")
{
    MiserereAudioProcessor processor;

    setBool (processor, ParamIDs::crushMute, true);
    setBool (processor, ParamIDs::sandMute, true);
    setBool (processor, ParamIDs::spreadMute, true);
    setBool (processor, ParamIDs::slapMute, true);

    processor.prepareToPlay (48000.0, 128);

    juce::AudioBuffer<float> reference (2, 8192);
    TestHelpers::fillWithSine (reference, 48000.0, 440.0, 0.5f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::MidiBuffer midi;
    processor.processBlock (processed, midi);

    CHECK (TestHelpers::maxDifferenceDbfs (processed, reference) <= -120.0);
}

TEST_CASE ("Extreme parameter values at both range edges produce no NaN/Inf", "[robustness]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (44100.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;

    for (bool useMinimum : { true, false })
    {
        INFO ("useMinimum = " << useMinimum);

        setParam (processor, ParamIDs::inTrim, useMinimum ? -12.0f : 12.0f);
        setParam (processor, ParamIDs::outTrim, useMinimum ? -12.0f : 12.0f);
        setParam (processor, ParamIDs::parallelTrim, useMinimum ? -24.0f : 6.0f);
        setParam (processor, ParamIDs::directDeessPreFreq, useMinimum ? 4000.0f : 9000.0f);
        setParam (processor, ParamIDs::directDeessPreThreshold, useMinimum ? -40.0f : 0.0f);
        setParam (processor, ParamIDs::directFetThreshold, useMinimum ? -40.0f : 0.0f);
        setParam (processor, ParamIDs::directFetAttack, useMinimum ? 1.0f : 30.0f);
        setParam (processor, ParamIDs::directFetRelease, useMinimum ? 50.0f : 500.0f);
        setParam (processor, ParamIDs::directFetMakeup, useMinimum ? 0.0f : 24.0f);
        setParam (processor, ParamIDs::directEqLowGain, useMinimum ? -16.0f : 16.0f);
        setParam (processor, ParamIDs::directEqMidGain, useMinimum ? -18.0f : 18.0f);
        setParam (processor, ParamIDs::directEqHighGain, useMinimum ? -16.0f : 16.0f);
        setParam (processor, ParamIDs::directEqDrive, useMinimum ? 0.0f : 24.0f);
        setParam (processor, ParamIDs::directSatDrive, useMinimum ? 0.0f : 24.0f);
        setParam (processor, ParamIDs::directDeessPostFreq, useMinimum ? 4000.0f : 9000.0f);
        setParam (processor, ParamIDs::directDeessPostThreshold, useMinimum ? -40.0f : 0.0f);

        setParam (processor, ParamIDs::crushInput, useMinimum ? 0.0f : 48.0f);
        setParam (processor, ParamIDs::crushAttack, useMinimum ? 1.0f : 7.0f);
        setParam (processor, ParamIDs::crushRelease, useMinimum ? 1.0f : 7.0f);
        setParam (processor, ParamIDs::crushOutput, useMinimum ? -12.0f : 12.0f);

        setParam (processor, ParamIDs::sandPreLfBoost, useMinimum ? 0.0f : 10.0f);
        setParam (processor, ParamIDs::sandPreLfCut, useMinimum ? 0.0f : 10.0f);
        setParam (processor, ParamIDs::sandPreHfBellBoost, useMinimum ? 0.0f : 10.0f);
        setParam (processor, ParamIDs::sandPreHfShelfAtten, useMinimum ? 0.0f : 10.0f);
        setParam (processor, ParamIDs::sandPeakRed, useMinimum ? 0.0f : 100.0f);
        setParam (processor, ParamIDs::sandEmphasis, useMinimum ? 0.0f : 100.0f);
        setParam (processor, ParamIDs::sandPostLfBoost, useMinimum ? 0.0f : 10.0f);
        setParam (processor, ParamIDs::sandPostLfCut, useMinimum ? 0.0f : 10.0f);
        setParam (processor, ParamIDs::sandPostHfBellBoost, useMinimum ? 0.0f : 10.0f);
        setParam (processor, ParamIDs::sandPostHfShelfAtten, useMinimum ? 0.0f : 10.0f);

        setParam (processor, ParamIDs::spreadDetune, useMinimum ? 0.0f : 15.0f);
        setParam (processor, ParamIDs::spreadTime, useMinimum ? 0.5f : 2.0f);
        setParam (processor, ParamIDs::spreadWidth, useMinimum ? 0.0f : 100.0f);

        setParam (processor, ParamIDs::slapTime, useMinimum ? 50.0f : 160.0f);
        setParam (processor, ParamIDs::slapTone, useMinimum ? 0.0f : 100.0f);

        for (const auto* levelId : { ParamIDs::crushLevel, ParamIDs::sandLevel, ParamIDs::spreadLevel, ParamIDs::slapLevel })
            setParam (processor, levelId, useMinimum ? -60.0f : 6.0f);

        TestHelpers::fillWithSine (buffer, 44100.0, 440.0, 0.8f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Rapid parameter automation across many blocks produces no NaN/Inf", "[robustness][automation]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);

    juce::MidiBuffer midi;

    const auto randomiseNormalised = [&] (const char* id)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (unit (rng));
    };

    for (int block = 0; block < 100; ++block)
    {
        for (const auto* id : { ParamIDs::inTrim, ParamIDs::outTrim, ParamIDs::parallelTrim,
                                 ParamIDs::directFetThreshold, ParamIDs::directFetAttack, ParamIDs::directFetRelease,
                                 ParamIDs::directEqLowGain, ParamIDs::directEqMidGain, ParamIDs::directEqHighGain, ParamIDs::directEqDrive,
                                 ParamIDs::directSatDrive,
                                 ParamIDs::crushInput, ParamIDs::crushAttack, ParamIDs::crushRelease, ParamIDs::crushOutput,
                                 ParamIDs::sandPreLfBoost, ParamIDs::sandPreLfCut, ParamIDs::sandPreHfBellBoost,
                                 ParamIDs::sandPeakRed, ParamIDs::sandEmphasis,
                                 ParamIDs::sandPostLfBoost, ParamIDs::sandPostLfCut, ParamIDs::sandPostHfBellBoost,
                                 ParamIDs::spreadDetune, ParamIDs::spreadTime, ParamIDs::spreadWidth,
                                 ParamIDs::slapTime, ParamIDs::slapTone,
                                 ParamIDs::crushLevel, ParamIDs::sandLevel, ParamIDs::spreadLevel, ParamIDs::slapLevel })
            randomiseNormalised (id);

        juce::AudioBuffer<float> buffer (2, 256);
        TestHelpers::fillWithSine (buffer, 48000.0, 200.0 + unit (rng) * 4000.0, 0.7f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("reset() followed by processBlock does not crash", "[robustness][reset]")
{
    MiserereAudioProcessor processor;
    bringUpAllBusses (processor);
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.6f);
    juce::MidiBuffer midi;

    processor.processBlock (buffer, midi);

    CHECK_NOTHROW (processor.reset());

    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.6f);
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Sample-rate changes between prepareToPlay calls are handled cleanly", "[robustness][samplerate]")
{
    MiserereAudioProcessor processor;
    bringUpAllBusses (processor);

    juce::MidiBuffer midi;

    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        INFO ("sample rate = " << sampleRate);

        processor.prepareToPlay (sampleRate, 512);

        juce::AudioBuffer<float> buffer (2, 512);
        TestHelpers::fillWithSine (buffer, sampleRate, 440.0, 0.5f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Bypass passes the input through untouched", "[robustness][bypass]")
{
    MiserereAudioProcessor processor;
    bringUpAllBusses (processor);
    setBool (processor, ParamIDs::bypass, true);
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> reference (2, 512);
    TestHelpers::fillWithSine (reference, 48000.0, 440.0, 0.5f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::MidiBuffer midi;
    processor.processBlock (processed, midi);

    CHECK (TestHelpers::maxDifferenceDbfs (processed, reference) <= -200.0);
}

//==============================================================================
// Brief section 6.12 - house rules for the v0.5.0 feedback engines.

namespace
{
    // Everything the v0.5.0 release added, engaged at once: the F3 CRUSH
    // feedback-FET loop, the F4 opto carrier ODE, the F5 flutter/age layers
    // and the F2/F8 matched + flux-domain drive stages.
    void bringUpEveryV050Path (MiserereAudioProcessor& processor)
    {
        bringUpAllBusses (processor);
        setParam (processor, ParamIDs::slapWobble, 80.0f);
        setParam (processor, ParamIDs::slapAge, 70.0f);
        setParam (processor, ParamIDs::crushInput, 36.0f);
        setParam (processor, ParamIDs::sandPeakRed, 85.0f);
        setParam (processor, ParamIDs::sandEmphasis, 100.0f);
        setParam (processor, ParamIDs::directEqDrive, 18.0f);
        setParam (processor, ParamIDs::directEqHighGain, 10.0f);
        setParam (processor, ParamIDs::directSatDrive, 12.0f);
    }
}

// The F3/F4 loops carry state across block boundaries (a one-sample feedback
// delay plus the carrier/RC states). If any of that were reset or recomputed
// per block, the render would depend on how the host happens to slice the
// stream - the classic way a feedback engine passes its own unit tests and
// then sounds different in every DAW.
TEST_CASE ("Feedback engines: the render is independent of the host's block slicing", "[robustness][blocksize][sota]")
{
    constexpr int totalSamples = 16384;
    constexpr double sampleRate = 48000.0;

    const auto renderSlicedInto = [&] (int blockSize)
    {
        MiserereAudioProcessor processor;
        bringUpEveryV050Path (processor);
        processor.prepareToPlay (sampleRate, 1024);

        juce::AudioBuffer<float> source (2, totalSamples);
        TestHelpers::fillWithSine (source, sampleRate, 220.0, 0.7f);

        juce::AudioBuffer<float> out (2, totalSamples);
        out.clear();
        juce::MidiBuffer midi;

        for (int start = 0; start < totalSamples; start += blockSize)
        {
            const auto n = std::min (blockSize, totalSamples - start);

            juce::AudioBuffer<float> chunk (2, n);
            for (int ch = 0; ch < 2; ++ch)
                chunk.copyFrom (ch, 0, source, ch, start, n);

            processor.processBlock (chunk, midi);

            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, start, chunk, ch, 0, n);
        }

        return out;
    };

    const auto reference = renderSlicedInto (1024);

    for (const int blockSize : { 64, 333, 512 })
    {
        const auto sliced = renderSlicedInto (blockSize);

        INFO ("block size = " << blockSize);
        CHECK (TestHelpers::allSamplesFinite (sliced));
        CHECK (TestHelpers::maxDifferenceDbfs (sliced, reference) < -120.0);
    }
}

// After a long silence every state must have decayed to rest rather than
// parking on a denormal (which costs real CPU on x86) or on a stuck carrier
// density that would swallow the next note's attack.
TEST_CASE ("Feedback engines: a long silence decays to rest with no denormal residue", "[robustness][denormal][sota]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    MiserereAudioProcessor processor;
    bringUpEveryV050Path (processor);
    processor.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    // 1 s of hot programme to charge every detector, carrier and delay line.
    for (int block = 0; block < static_cast<int> (sampleRate) / blockSize; ++block)
    {
        TestHelpers::fillWithSine (buffer, sampleRate, 110.0, 0.9f, block * blockSize);
        processor.processBlock (buffer, midi);
    }

    // 60 s of silence.
    for (int block = 0; block < 60 * static_cast<int> (sampleRate) / blockSize; ++block)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
        REQUIRE (TestHelpers::allSamplesFinite (buffer));
    }

    // The SLAP bus keeps generating age noise while age > 0, so the decay
    // assertion is made with the noise layers off.
    setParam (processor, ParamIDs::slapAge, 0.0f);
    setParam (processor, ParamIDs::slapWobble, 0.0f);

    for (int block = 0; block < 200; ++block)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
    }

    // Summed across every bus with both direct drive stages hot, the resting
    // output must sit below a 24-bit LSB - i.e. no state has parked anywhere
    // that could be heard or that could cost denormal CPU.
    //
    // Measured on this build the residue is ~3e-7 (-130 dBFS), and it is
    // traced to the SANDWICH bus chain, NOT to the engines this release
    // added: per the per-bus assertion below, CRUSH (F3), SPREAD (F6), SLAP
    // (F5) and the direct path (F8/F2) each reach EXACT zero.
    CHECK (TestHelpers::peakAbsolute (buffer) < 1.0e-6f);

    // ... and the engine wakes up again cleanly.
    TestHelpers::fillWithSine (buffer, sampleRate, 440.0, 0.5f);
    processor.processBlock (buffer, midi);
    CHECK (TestHelpers::allSamplesFinite (buffer));
    CHECK (TestHelpers::peakAbsolute (buffer) > 1.0e-3f);
}

// The stronger form of the same rule, per bus: the v0.5.0 engines carry the
// states that could plausibly stick (the CRUSH cap voltage, the opto carrier
// densities, the flutter phase accumulators, the flux integrator), so each of
// their buses is required to reach EXACT zero after a long silence rather
// than merely "small".
TEST_CASE ("Feedback engines: each v0.5.0 bus rests at exactly zero after a long silence", "[robustness][denormal][sota]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    // Fader floor -60 dB is an exact-zero route gain, so isolating a bus is
    // a matter of parking every other fader at the floor.
    const auto restingPeakOf = [&] (const char* busUnderTest)
    {
        MiserereAudioProcessor processor;

        for (const auto* id : { ParamIDs::crushLevel, ParamIDs::sandLevel,
                                ParamIDs::spreadLevel, ParamIDs::slapLevel })
            setParam (processor, id, juce::String (id) == busUnderTest ? 0.0f : -60.0f);

        setParam (processor, ParamIDs::crushInput, 36.0f);
        setParam (processor, ParamIDs::sandPeakRed, 85.0f);
        setParam (processor, ParamIDs::directEqDrive, 18.0f);
        setParam (processor, ParamIDs::directSatDrive, 12.0f);
        setParam (processor, ParamIDs::slapWobble, 80.0f);
        setParam (processor, ParamIDs::slapAge, 70.0f);

        processor.prepareToPlay (sampleRate, blockSize);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        for (int block = 0; block < static_cast<int> (sampleRate) / blockSize; ++block)
        {
            TestHelpers::fillWithSine (buffer, sampleRate, 110.0, 0.9f, block * blockSize);
            processor.processBlock (buffer, midi);
        }

        setParam (processor, ParamIDs::slapAge, 0.0f);
        setParam (processor, ParamIDs::slapWobble, 0.0f);

        for (int block = 0; block < 10 * static_cast<int> (sampleRate) / blockSize; ++block)
        {
            buffer.clear();
            processor.processBlock (buffer, midi);
        }

        return TestHelpers::peakAbsolute (buffer);
    };

    for (const auto* bus : { ParamIDs::crushLevel, ParamIDs::spreadLevel, ParamIDs::slapLevel })
    {
        INFO ("bus fader id = " << bus);
        CHECK (restingPeakOf (bus) == 0.0f);
    }

    // Known deviation, deliberately pinned rather than hidden: the SANDWICH
    // chain settles to a static ~3e-8 (-150 dBFS, roughly 30 dB below a
    // 24-bit LSB) instead of exact zero. PassiveEq and OptoLeveler each reach
    // zero in isolation, so this comes from the bus composition and predates
    // the F4 carrier ODE. Tracked as a follow-up; pinned here so it cannot
    // grow unnoticed.
    CHECK (restingPeakOf (ParamIDs::sandLevel) < 1.0e-6f);
}

// NaN/Inf must not be able to park a carrier density or an RC cap voltage in
// a state the engine cannot leave (brief section 6.12: Opto re-seeds to dark
// equilibrium, CRUSH clamps vC).
TEST_CASE ("Feedback engines: carrier and RC states recover from a NaN/Inf burst", "[robustness][nan][sota]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    MiserereAudioProcessor processor;
    bringUpEveryV050Path (processor);
    processor.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    for (const auto poison : { std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::infinity(),
                               -std::numeric_limits<float>::infinity() })
    {
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), poison, blockSize);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
    }

    processor.reset();

    // Post-recovery the engine must render normally again - a stuck carrier
    // would show up as a collapsed or absent output here.
    double peak = 0.0;

    for (int block = 0; block < 64; ++block)
    {
        TestHelpers::fillWithSine (buffer, sampleRate, 440.0, 0.5f, block * blockSize);
        processor.processBlock (buffer, midi);
        REQUIRE (TestHelpers::allSamplesFinite (buffer));

        if (block >= 32)
            peak = std::max (peak, static_cast<double> (TestHelpers::peakAbsolute (buffer)));
    }

    CHECK (peak > 1.0e-2);
}
