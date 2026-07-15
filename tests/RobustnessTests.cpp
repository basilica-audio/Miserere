#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <random>

// M1 guarantees 8 and 9: NaN/Inf sweep -> finite output with state recovery
// after reset(), and the Release-safe oversized-block clamp - plus the
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

    // Brings up all four bus faders so every path (including the slap
    // delay line) actually processes signal during the sweeps.
    void bringUpAllBusses (MiserereAudioProcessor& processor)
    {
        setParam (processor, ParamIDs::busALevel, 0.0f);
        setParam (processor, ParamIDs::busBLevel, 0.0f);
        setParam (processor, ParamIDs::busCLevel, 0.0f);
        setParam (processor, ParamIDs::busDLevel, 0.0f);
        setParam (processor, ParamIDs::busBOptoReduction, 60.0f);
        setParam (processor, ParamIDs::busCDrive, 6.0f);
        setParam (processor, ParamIDs::busDFeedback, 20.0f);
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

    CHECK (TestHelpers::peakAbsolute (buffer) < 1.0e-6f);
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

    // Poison the whole engine with Inf.
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

    // reset() must clear every module's poisoned state (IIR filters,
    // envelopes, the slap delay line)...
    CHECK_NOTHROW (processor.reset());

    // ...so a clean signal afterwards processes normally: finite AND at a
    // sane level (a silent/zeroed output here would mean a filter is still
    // stuck on NaN and being masked by the output sanitiser).
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

    // The full buffer must actually have been processed - the tail (well
    // past the declared 128 samples) still carries signal, proving the
    // clamp chunks rather than truncating.
    double tailEnergy = 0.0;
    for (int sample = 4096; sample < 8192; ++sample)
        tailEnergy += std::abs (buffer.getSample (0, sample));

    CHECK (tailEnergy > 1.0);
}

TEST_CASE ("Oversized block: the neutral null property survives chunking", "[robustness][oversized][null]")
{
    MiserereAudioProcessor processor;

    // Neutral Direct bus solo (the guarantee-1 configuration).
    setBool (processor, ParamIDs::busAHpfEnabled, false);
    setParam (processor, ParamIDs::busAEqLowGain, 0.0f);
    setParam (processor, ParamIDs::busAEqMidGain, 0.0f);
    setParam (processor, ParamIDs::busAEqHighGain, 0.0f);
    setParam (processor, ParamIDs::busACompThreshold, 0.0f);
    setParam (processor, ParamIDs::busACompMakeup, 0.0f);
    setBool (processor, ParamIDs::busADeessEnabled, false);
    setParam (processor, ParamIDs::busASatDrive, 0.0f);
    setParam (processor, ParamIDs::busALevel, 0.0f);
    setBool (processor, ParamIDs::busASolo, true);

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
        setParam (processor, ParamIDs::busAHpfFreq, useMinimum ? 20.0f : 300.0f);
        setParam (processor, ParamIDs::busAEqLowGain, useMinimum ? -15.0f : 15.0f);
        setParam (processor, ParamIDs::busAEqMidFreq, useMinimum ? 250.0f : 5000.0f);
        setParam (processor, ParamIDs::busAEqMidGain, useMinimum ? -15.0f : 15.0f);
        setParam (processor, ParamIDs::busAEqMidQ, useMinimum ? 0.7f : 2.0f);
        setParam (processor, ParamIDs::busAEqHighGain, useMinimum ? -15.0f : 15.0f);
        setParam (processor, ParamIDs::busACompThreshold, useMinimum ? -40.0f : 0.0f);
        setParam (processor, ParamIDs::busACompAttack, useMinimum ? 0.1f : 10.0f);
        setParam (processor, ParamIDs::busACompRelease, useMinimum ? 50.0f : 1100.0f);
        setParam (processor, ParamIDs::busACompMakeup, useMinimum ? 0.0f : 24.0f);
        setParam (processor, ParamIDs::busADeessFreq, useMinimum ? 4000.0f : 9000.0f);
        setParam (processor, ParamIDs::busADeessThreshold, useMinimum ? -40.0f : 0.0f);
        setParam (processor, ParamIDs::busASatDrive, useMinimum ? 0.0f : 24.0f);
        setParam (processor, ParamIDs::busBLowBoostGain, useMinimum ? 0.0f : 10.0f);
        setParam (processor, ParamIDs::busBHighBoostGain, useMinimum ? 0.0f : 10.0f);
        setParam (processor, ParamIDs::busBOptoReduction, useMinimum ? 0.0f : 100.0f);
        setParam (processor, ParamIDs::busBOptoMakeup, useMinimum ? 0.0f : 24.0f);
        setParam (processor, ParamIDs::busBAirGain, useMinimum ? 0.0f : 8.0f);
        setParam (processor, ParamIDs::busCAttack, useMinimum ? 0.05f : 0.8f);
        setParam (processor, ParamIDs::busCRelease, useMinimum ? 50.0f : 200.0f);
        setParam (processor, ParamIDs::busCDrive, useMinimum ? 0.0f : 12.0f);
        setParam (processor, ParamIDs::busCOutputTrim, useMinimum ? -12.0f : 12.0f);
        setParam (processor, ParamIDs::busDDelayMs, useMinimum ? 60.0f : 180.0f);
        setParam (processor, ParamIDs::busDFeedback, useMinimum ? 0.0f : 30.0f);
        setParam (processor, ParamIDs::busDHpFreq, useMinimum ? 50.0f : 1000.0f);
        setParam (processor, ParamIDs::busDLpFreq, useMinimum ? 2000.0f : 10000.0f);

        for (const auto* levelId : { ParamIDs::busALevel, ParamIDs::busBLevel, ParamIDs::busCLevel, ParamIDs::busDLevel })
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
        for (const auto* id : { ParamIDs::inTrim, ParamIDs::outTrim,
                                 ParamIDs::busAHpfFreq, ParamIDs::busAEqLowGain, ParamIDs::busAEqMidFreq,
                                 ParamIDs::busAEqMidGain, ParamIDs::busAEqMidQ, ParamIDs::busAEqHighGain,
                                 ParamIDs::busACompRatio, ParamIDs::busACompThreshold, ParamIDs::busACompAttack,
                                 ParamIDs::busACompRelease, ParamIDs::busACompMakeup,
                                 ParamIDs::busADeessFreq, ParamIDs::busADeessThreshold, ParamIDs::busASatDrive,
                                 ParamIDs::busBLowBoostFreq, ParamIDs::busBLowBoostGain,
                                 ParamIDs::busBHighBoostFreq, ParamIDs::busBHighBoostGain,
                                 ParamIDs::busBOptoReduction, ParamIDs::busBOptoMakeup, ParamIDs::busBAirGain,
                                 ParamIDs::busCAttack, ParamIDs::busCRelease, ParamIDs::busCDrive, ParamIDs::busCOutputTrim,
                                 ParamIDs::busDDelayMs, ParamIDs::busDFeedback, ParamIDs::busDHpFreq, ParamIDs::busDLpFreq,
                                 ParamIDs::busALevel, ParamIDs::busBLevel, ParamIDs::busCLevel, ParamIDs::busDLevel })
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
