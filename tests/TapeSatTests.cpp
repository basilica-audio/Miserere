#include "dsp/TapeSat.h"
#include "dsp/TapeSaturator.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

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
}

TEST_CASE ("Tape sat: drive 0 dB is a bit-exact bypass", "[dsp][tapesat][null]")
{
    TapeSat sat;
    sat.setDriveDb (0.0f);
    sat.prepare (makeTestSpec (2));

    juce::AudioBuffer<float> reference (2, 4096);
    TestHelpers::fillWithSine (reference, testSampleRate, 440.0, 0.9f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    sat.process (block);

    CHECK (TestHelpers::maxDifferenceDbfs (processed, reference) <= -200.0);
}

TEST_CASE ("Tape sat: driven output is genuinely nonlinear (adds harmonic content)", "[dsp][tapesat]")
{
    TapeSat sat;
    sat.setDriveDb (18.0f);
    sat.prepare (makeTestSpec (1));

    juce::AudioBuffer<float> reference (1, testBlockSize);
    TestHelpers::fillWithSine (reference, testSampleRate, 440.0, 0.25f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    sat.process (block);

    // A driven tanh compresses the sine's peaks: the crest factor
    // (peak/RMS) must drop measurably vs the clean sine's sqrt(2).
    const auto processedTailPeak = [&]
    {
        float peak = 0.0f;
        const auto* data = processed.getReadPointer (0);
        for (int i = settleSamples; i < testBlockSize; ++i)
            peak = std::max (peak, std::abs (data[i]));
        return peak;
    }();

    const auto processedCrest = processedTailPeak / static_cast<float> (TestHelpers::tailRms (processed, settleSamples));

    CHECK (TestHelpers::allSamplesFinite (processed));
    CHECK (processedCrest < juce::MathConstants<float>::sqrt2 * 0.98f);
}

TEST_CASE ("Tape sat: auto-compensation keeps a nominal-level signal within +-3 dB across the drive range", "[dsp][tapesat][compensation]")
{
    for (const auto driveDb : { 3.0f, 6.0f, 12.0f, 18.0f, 24.0f })
    {
        INFO ("drive = " << driveDb << " dB");

        TapeSat sat;
        sat.setDriveDb (driveDb);
        sat.prepare (makeTestSpec (1));

        // -18 dBFS: the saturator's nominal operating level (see
        // TapeSaturator.h).
        juce::AudioBuffer<float> reference (1, testBlockSize);
        TestHelpers::fillWithSine (reference, testSampleRate, 440.0, TapeSaturator::nominalLevel);

        juce::AudioBuffer<float> processed;
        processed.makeCopyOf (reference);

        juce::dsp::AudioBlock<float> block (processed);
        sat.process (block);

        const auto inputRms = TestHelpers::tailRms (reference, settleSamples);
        const auto outputRms = TestHelpers::tailRms (processed, settleSamples);

        REQUIRE (inputRms > 0.0);
        CHECK (juce::Decibels::gainToDecibels (outputRms / inputRms) == Catch::Approx (0.0).margin (3.0));
    }
}

TEST_CASE ("Tape sat: full-scale input at maximum drive stays bounded and finite", "[dsp][tapesat][robustness]")
{
    TapeSat sat;
    sat.setDriveDb (24.0f);
    sat.prepare (makeTestSpec (2));

    juce::AudioBuffer<float> buffer (2, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, 1000.0, 1.0f);

    juce::dsp::AudioBlock<float> block (buffer);
    sat.process (block);

    CHECK (TestHelpers::allSamplesFinite (buffer));
    // tanh(g*x) * comp(g) is strictly bounded by comp(g) <= ~1 for any
    // drive >= 0 dB; the emphasis shelves add a little swing on top - 2.0
    // is a generous ceiling that still proves the stage cannot blow up.
    CHECK (TestHelpers::peakAbsolute (buffer) < 2.0f);
}
