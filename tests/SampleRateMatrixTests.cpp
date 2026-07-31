#include "PluginProcessor.h"
#include "dsp/MiserereEngine.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <random>

// Suite-wide hardening wave (2026-07-31): sample-rate-matrix reprepare
// coverage. LatencyTests.cpp's "Latency stays zero across sample-rate and
// block-size changes" case moves 44.1k -> 96k -> 192k once, with no
// processing or parameter movement between reprepares and no assertion
// beyond getLatencySamples(). Hosts do reprepare repeatedly (sample-rate
// changes, buffer-size renegotiation), each time expecting a clean engine
// reset, so this exercises a full sequence - prepare, process with
// parameter churn across all four parallel busses, reprepare at a new rate
// with both a small and a large block, process again - on a single
// long-lived processor instance, and checks state (an APVTS parameter
// value) survives every reprepare unperturbed, which none of the existing
// latency/robustness tests do.
namespace
{
    void setParam (MiserereAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    float getParamNormalised (MiserereAudioProcessor& processor, const char* id)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param->getValue();
    }

    // Brings all four parallel busses up (Crush/Sandwich/Spread/Slap, plus
    // the Direct-path optional sections) and churns their level/drive
    // parameters across a few blocks - the "process with parameter churn"
    // step between reprepares. Mirrors AllocationGuardTests.cpp's
    // bringUpAllBusses() and the v0.5.0 "hard paths" case's parameter
    // selection (feedback engines, matched coefficients, flutter/age noise).
    void churnAndProcess (MiserereAudioProcessor& processor,
                           double sampleRate,
                           int blockSize,
                           std::mt19937& rng,
                           int numBlocks = 6)
    {
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);
        juce::MidiBuffer midi;

        for (int block = 0; block < numBlocks; ++block)
        {
            setParam (processor, ParamIDs::crushInput, unit (rng) * 36.0f);
            setParam (processor, ParamIDs::sandPeakRed, unit (rng) * 90.0f);
            setParam (processor, ParamIDs::sandEmphasis, unit (rng) * 100.0f);
            setParam (processor, ParamIDs::slapWobble, unit (rng) * 100.0f);
            setParam (processor, ParamIDs::slapAge, unit (rng) * 100.0f);
            // Note: crush_level is deliberately NOT churned here - it is the
            // state-survival marker both TEST_CASEs below set once and
            // check is unperturbed by every reprepare.
            setParam (processor, ParamIDs::sandLevel, -6.0f + unit (rng) * 6.0f);
            setParam (processor, ParamIDs::spreadLevel, -6.0f + unit (rng) * 6.0f);
            setParam (processor, ParamIDs::slapLevel, -6.0f + unit (rng) * 6.0f);

            juce::AudioBuffer<float> buffer (2, blockSize);

            if (blockSize > 0)
                TestHelpers::fillWithSine (buffer, sampleRate, 220.0 + unit (rng) * 2000.0, 0.6f);

            CHECK_NOTHROW (processor.processBlock (buffer, midi));

            if (blockSize > 0)
                CHECK (TestHelpers::allSamplesFinite (buffer));
        }
    }
}

TEST_CASE ("Sample-rate matrix: reprepare 44.1k -> 96k -> 192k (small and large blocks) "
           "survives with latency staying zero and parameter state intact",
           "[latency][sample-rate-matrix][reprepare]")
{
    MiserereAudioProcessor processor;
    std::mt19937 rng (4242);

    // Every bus and the F5/F3/F4 "hard" paths engaged, same selection as
    // AllocationGuardTests.cpp's v0.5.0 case, so reprepare is exercised
    // with every circuit engine actually doing work, not idling at a
    // bit-transparent default.
    setParam (processor, ParamIDs::slapTone, 100.0f);
    setParam (processor, ParamIDs::directEqHighGain, 12.0f);
    setParam (processor, ParamIDs::directEqDrive, 18.0f);
    setParam (processor, ParamIDs::directSatDrive, 12.0f);
    setParam (processor, ParamIDs::sandPreHfBellBoost, 8.0f);
    setParam (processor, ParamIDs::sandPreLfBoost, 8.0f);
    setParam (processor, ParamIDs::sandPreLfCut, 8.0f);

    // A state-survival marker: an explicit, non-default Crush return-fader
    // value set once, before the very first prepare(), that must still
    // read back identically after every reprepare below - reprepareToPlay()
    // must never reset APVTS-owned parameter state, only the DSP engine's
    // internal filter/detector/delay-line buffers. Compared via the
    // normalised [0,1] representation directly so there is no ambiguity
    // about which side introduces rounding.
    setParam (processor, ParamIDs::crushLevel, -4.5f);
    const auto markerLevelNormalised = getParamNormalised (processor, ParamIDs::crushLevel);

    // --- 44.1 kHz, the starting rate ------------------------------------
    processor.prepareToPlay (44100.0, 512);
    CHECK (processor.getLatencySamples() == 0);
    CHECK (MiserereEngine::getLatencySamples() == 0);
    churnAndProcess (processor, 44100.0, 512, rng);
    CHECK (getParamNormalised (processor, ParamIDs::crushLevel) == markerLevelNormalised);

    // --- 96 kHz: small block, then large block --------------------------
    processor.prepareToPlay (96000.0, 32);
    CHECK (processor.getLatencySamples() == 0);
    churnAndProcess (processor, 96000.0, 32, rng);
    CHECK (getParamNormalised (processor, ParamIDs::crushLevel) == markerLevelNormalised);

    processor.prepareToPlay (96000.0, 8192);
    CHECK (processor.getLatencySamples() == 0);
    churnAndProcess (processor, 96000.0, 8192, rng);
    CHECK (getParamNormalised (processor, ParamIDs::crushLevel) == markerLevelNormalised);

    // --- 192 kHz: small block, then large block --------------------------
    processor.prepareToPlay (192000.0, 16);
    CHECK (processor.getLatencySamples() == 0);
    churnAndProcess (processor, 192000.0, 16, rng);
    CHECK (getParamNormalised (processor, ParamIDs::crushLevel) == markerLevelNormalised);

    processor.prepareToPlay (192000.0, 16384);
    CHECK (processor.getLatencySamples() == 0);
    churnAndProcess (processor, 192000.0, 16384, rng);
    CHECK (getParamNormalised (processor, ParamIDs::crushLevel) == markerLevelNormalised);

    // Finally, back down to 44.1 kHz (round trip): latency must still be
    // zero and a fresh block must still come out finite, proving no
    // reprepare along the way left the engine in a state that depends on
    // prepare *history* rather than just the current spec.
    processor.prepareToPlay (44100.0, 512);
    CHECK (processor.getLatencySamples() == 0);
    churnAndProcess (processor, 44100.0, 512, rng);
    CHECK (getParamNormalised (processor, ParamIDs::crushLevel) == markerLevelNormalised);

    juce::AudioBuffer<float> finalBuffer (2, 512);
    TestHelpers::fillWithSine (finalBuffer, 44100.0, 1000.0, 0.9f);
    juce::MidiBuffer midi;
    processor.processBlock (finalBuffer, midi);
    CHECK (TestHelpers::allSamplesFinite (finalBuffer));
}

TEST_CASE ("Sample-rate matrix: reprepare with a zero-sample buffer immediately after does not crash",
           "[latency][sample-rate-matrix][reprepare]")
{
    // A narrower, cheap companion to the case above: some hosts hand over a
    // zero-length buffer on the very first callback after a reprepare
    // (buffer-size renegotiation mid-stream).
    MiserereAudioProcessor processor;

    setParam (processor, ParamIDs::crushLevel, 0.0f);
    setParam (processor, ParamIDs::sandLevel, 0.0f);
    setParam (processor, ParamIDs::spreadLevel, 0.0f);
    setParam (processor, ParamIDs::slapLevel, 0.0f);
    setParam (processor, ParamIDs::sandPeakRed, 60.0f);
    setParam (processor, ParamIDs::crushInput, 12.0f);

    juce::MidiBuffer midi;

    for (double rate : { 44100.0, 96000.0, 192000.0 })
    {
        for (int blockSize : { 1, 4096 })
        {
            processor.prepareToPlay (rate, blockSize);
            CHECK (processor.getLatencySamples() == 0);

            juce::AudioBuffer<float> zeroBuffer (2, 0);
            CHECK_NOTHROW (processor.processBlock (zeroBuffer, midi));
            CHECK (zeroBuffer.getNumSamples() == 0);

            juce::AudioBuffer<float> normalBuffer (2, blockSize);
            TestHelpers::fillWithSine (normalBuffer, rate, 500.0, 0.5f);
            CHECK_NOTHROW (processor.processBlock (normalBuffer, midi));
            CHECK (TestHelpers::allSamplesFinite (normalBuffer));
        }
    }
}
