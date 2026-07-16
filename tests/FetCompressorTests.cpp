#include "dsp/FetCompressor.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// The Direct path's "FET Comp light" module: threshold-based static curve,
// GR metering, null-at-max-threshold identity, makeup gain. The all-buttons
// input-drive character (per-ratio table, ALL-mode plateau, dual-rate
// release) moved to FetCrush - see FetCrushTests.cpp.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 24000; // 0.5 s: lets the envelope fully settle
    constexpr int settleSamples = 12000;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    double measureGainChangeDb (FetCompressor& comp, double frequencyHz, float amplitude)
    {
        comp.prepare (makeTestSpec (1));

        juce::AudioBuffer<float> reference (1, testBlockSize);
        TestHelpers::fillWithSine (reference, testSampleRate, frequencyHz, amplitude);

        juce::AudioBuffer<float> processed;
        processed.makeCopyOf (reference);

        juce::dsp::AudioBlock<float> block (processed);
        comp.process (block);

        const auto inputRms = TestHelpers::tailRms (reference, settleSamples);
        const auto outputRms = TestHelpers::tailRms (processed, settleSamples);

        REQUIRE (inputRms > 0.0);
        REQUIRE (outputRms > 0.0);

        return juce::Decibels::gainToDecibels (outputRms / inputRms);
    }

    // Expected feed-forward static-curve gain reduction. The detector's
    // ballistics are strongly asymmetric (attack orders of magnitude faster
    // than release), so its envelope rides at the *peak* of the squared
    // signal rather than its mean - the expected level is therefore the
    // sine's peak (amplitude) in dB, not its RMS:
    // GR = max(0, peakDb - thresholdDb) * (1 - 1/ratio).
    double expectedGainReductionDb (float amplitude, float thresholdDb, float ratio)
    {
        const auto inputPeakDb = juce::Decibels::gainToDecibels (static_cast<double> (amplitude));
        return std::max (0.0, inputPeakDb - static_cast<double> (thresholdDb)) * (1.0 - 1.0 / static_cast<double> (ratio));
    }
}

TEST_CASE ("Direct FET: static curve at 4:1 within +-1.5 dB", "[dsp][fet][static-curve]")
{
    FetCompressor comp;
    comp.setRatio (4.0f);
    comp.setThresholdDb (-30.0f);
    comp.setAttackMs (1.0f);
    comp.setReleaseMs (100.0f);
    comp.setMakeupDb (0.0f);

    const auto measured = measureGainChangeDb (comp, 1000.0, 0.5f);
    const auto expected = -expectedGainReductionDb (0.5f, -30.0f, 4.0f);

    CHECK (measured == Catch::Approx (expected).margin (1.5));
}

TEST_CASE ("Direct FET: static curve at 8:1 within +-1.5 dB", "[dsp][fet][static-curve]")
{
    FetCompressor comp;
    comp.setRatio (8.0f);
    comp.setThresholdDb (-30.0f);
    comp.setAttackMs (1.0f);
    comp.setReleaseMs (100.0f);
    comp.setMakeupDb (0.0f);

    const auto measured = measureGainChangeDb (comp, 1000.0, 0.5f);
    const auto expected = -expectedGainReductionDb (0.5f, -30.0f, 8.0f);

    CHECK (measured == Catch::Approx (expected).margin (1.5));
}

TEST_CASE ("Direct FET: GR metering value matches the measured static reduction", "[dsp][fet][metering]")
{
    FetCompressor comp;
    comp.setRatio (4.0f);
    comp.setThresholdDb (-30.0f);
    comp.setAttackMs (1.0f);
    comp.setReleaseMs (100.0f);

    const auto measured = measureGainChangeDb (comp, 1000.0, 0.5f);

    CHECK (comp.getCurrentGainReductionDb() > 0.0f);
    CHECK (static_cast<double> (comp.getCurrentGainReductionDb()) == Catch::Approx (-measured).margin (2.0));
}

TEST_CASE ("Direct FET: threshold at maximum (0 dB) is a bit-exact identity", "[dsp][fet][null]")
{
    FetCompressor comp;
    comp.setRatio (8.0f);
    comp.setThresholdDb (0.0f);
    comp.setMakeupDb (0.0f);
    comp.prepare (makeTestSpec (2));

    juce::AudioBuffer<float> reference (2, 4096);
    TestHelpers::fillWithSine (reference, testSampleRate, 440.0, 0.5f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    comp.process (block);

    CHECK (TestHelpers::maxDifferenceDbfs (processed, reference) <= -200.0);
    CHECK (comp.getCurrentGainReductionDb() == 0.0f);
}

TEST_CASE ("Direct FET: makeup gain shifts the output level by its dB value", "[dsp][fet]")
{
    FetCompressor withoutMakeup;
    withoutMakeup.setRatio (4.0f);
    withoutMakeup.setThresholdDb (-30.0f);
    withoutMakeup.setMakeupDb (0.0f);

    FetCompressor withMakeup;
    withMakeup.setRatio (4.0f);
    withMakeup.setThresholdDb (-30.0f);
    withMakeup.setMakeupDb (6.0f);

    const auto reference = measureGainChangeDb (withoutMakeup, 1000.0, 0.5f);
    const auto boosted = measureGainChangeDb (withMakeup, 1000.0, 0.5f);

    CHECK (boosted - reference == Catch::Approx (6.0).margin (0.2));
}

TEST_CASE ("Direct FET: reset() clears the envelope", "[dsp][fet][reset]")
{
    FetCompressor comp;
    comp.setRatio (8.0f);
    comp.setThresholdDb (-30.0f);
    comp.setAttackMs (1.0f);
    comp.setReleaseMs (500.0f);
    comp.prepare (makeTestSpec (1));

    juce::AudioBuffer<float> loud (1, testBlockSize);
    TestHelpers::fillWithSine (loud, testSampleRate, 1000.0, 0.9f);
    juce::dsp::AudioBlock<float> loudBlock (loud);
    comp.process (loudBlock);

    REQUIRE (comp.getCurrentGainReductionDb() > 0.0f);

    comp.reset();
    CHECK (comp.getCurrentGainReductionDb() == 0.0f);
}
