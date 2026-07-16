#include "dsp/ConsoleEq.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// The Direct path's 1073-class Console EQ: HPF slope, neutral shelves/bell
// bit-exact bypass, and the Drive control's nonlinearity.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 24000;
    constexpr int settleSamples = 12000;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    double measureGainChangeDb (ConsoleEq& eq, double frequencyHz, float amplitude = 0.4f)
    {
        eq.prepare (makeTestSpec (1));

        juce::AudioBuffer<float> reference (1, testBlockSize);
        TestHelpers::fillWithSine (reference, testSampleRate, frequencyHz, amplitude);

        juce::AudioBuffer<float> processed;
        processed.makeCopyOf (reference);

        juce::dsp::AudioBlock<float> block (processed);
        eq.process (block);

        const auto inputRms = TestHelpers::tailRms (reference, settleSamples);
        const auto outputRms = TestHelpers::tailRms (processed, settleSamples);

        REQUIRE (inputRms > 0.0);
        return juce::Decibels::gainToDecibels (outputRms / inputRms);
    }
}

TEST_CASE ("Console EQ: HPF at 18 dB/oct attenuates roughly 18 dB per octave below cutoff", "[dsp][consoleeq][hpf]")
{
    ConsoleEq eq;
    eq.setHpfEnabled (true);
    eq.setHpfFreqHz (160.0f);

    // One octave below cutoff (80 Hz) vs two octaves below (40 Hz): a true
    // 18 dB/oct filter attenuates the lower tone by ~18 dB more.
    const auto oneOctaveBelow = measureGainChangeDb (eq, 80.0);
    const auto twoOctavesBelow = measureGainChangeDb (eq, 40.0);

    const auto slopePerOctave = oneOctaveBelow - twoOctavesBelow;
    CHECK (slopePerOctave == Catch::Approx (18.0).margin (4.0));
}

TEST_CASE ("Console EQ: HPF disabled is a bit-exact bypass", "[dsp][consoleeq][null]")
{
    ConsoleEq eq;
    eq.setHpfEnabled (false);
    eq.setHpfFreqHz (300.0f);
    eq.prepare (makeTestSpec (2));

    juce::AudioBuffer<float> reference (2, 4096);
    TestHelpers::fillWithSine (reference, testSampleRate, 100.0, 0.5f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    eq.process (block);

    CHECK (TestHelpers::maxDifferenceDbfs (processed, reference) <= -120.0);
}

TEST_CASE ("Console EQ: all bands at 0 dB gain (HPF off) is a bit-exact bypass", "[dsp][consoleeq][null]")
{
    ConsoleEq eq;
    eq.setHpfEnabled (false);
    eq.setLowGainDb (0.0f);
    eq.setMidGainDb (0.0f);
    eq.setHighGainDb (0.0f);
    eq.setDriveDb (0.0f);
    eq.prepare (makeTestSpec (2));

    juce::AudioBuffer<float> reference (2, 4096);
    TestHelpers::fillWithSine (reference, testSampleRate, 1000.0, 0.5f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    eq.process (block);

    CHECK (TestHelpers::maxDifferenceDbfs (processed, reference) <= -120.0);
}

TEST_CASE ("Console EQ: low shelf boosts low frequencies by roughly its gain setting", "[dsp][consoleeq]")
{
    ConsoleEq eq;
    eq.setLowFreqHz (110.0f);
    eq.setLowGainDb (10.0f);

    const auto gainDb = measureGainChangeDb (eq, 40.0);
    CHECK (gainDb == Catch::Approx (10.0).margin (1.5));
}

TEST_CASE ("Console EQ: mid bell boosts at its selected centre frequency", "[dsp][consoleeq]")
{
    ConsoleEq eq;
    eq.setMidFreqHz (1600.0f);
    eq.setMidGainDb (12.0f);

    const auto atCentre = measureGainChangeDb (eq, 1600.0);
    const auto farAway = measureGainChangeDb (eq, 100.0);

    CHECK (atCentre == Catch::Approx (12.0).margin (1.0));
    CHECK (std::abs (farAway) < 1.5);
}

TEST_CASE ("Console EQ: high shelf boosts above 12 kHz", "[dsp][consoleeq]")
{
    ConsoleEq eq;
    eq.setHighGainDb (10.0f);

    const auto above = measureGainChangeDb (eq, 18000.0);
    const auto below = measureGainChangeDb (eq, 1000.0);

    CHECK (above > 4.0);
    CHECK (std::abs (below) < 1.5);
}

TEST_CASE ("Console EQ: Drive 0 dB is a bit-exact bypass, driven output is nonlinear", "[dsp][consoleeq][drive]")
{
    ConsoleEq neutral;
    neutral.setDriveDb (0.0f);
    neutral.prepare (makeTestSpec (2));

    juce::AudioBuffer<float> reference (2, 4096);
    TestHelpers::fillWithSine (reference, testSampleRate, 440.0, 0.5f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    neutral.process (block);

    CHECK (TestHelpers::maxDifferenceDbfs (processed, reference) <= -120.0);

    ConsoleEq driven;
    driven.setDriveDb (18.0f);
    driven.prepare (makeTestSpec (1));

    juce::AudioBuffer<float> drivenReference (1, testBlockSize);
    TestHelpers::fillWithSine (drivenReference, testSampleRate, 440.0, 0.3f);

    juce::AudioBuffer<float> drivenProcessed;
    drivenProcessed.makeCopyOf (drivenReference);

    juce::dsp::AudioBlock<float> drivenBlock (drivenProcessed);
    driven.process (drivenBlock);

    CHECK (TestHelpers::allSamplesFinite (drivenProcessed));

    const auto processedCrest = TestHelpers::peakAbsolute (drivenProcessed) / static_cast<float> (TestHelpers::tailRms (drivenProcessed, settleSamples));
    CHECK (processedCrest < juce::MathConstants<float>::sqrt2 * 0.99f); // measurably compressed peaks -> genuinely nonlinear
}
