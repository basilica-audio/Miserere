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
