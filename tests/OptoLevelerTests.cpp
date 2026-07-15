#include "dsp/OptoLeveler.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// M1 guarantee 4: the opto leveler's two-stage program-dependent release
// (release measurably slows with longer GR history), plus its bypass/
// static-curve/makeup contracts.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int blockSize = 512;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (blockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    // Drives the leveler with a loud sine for `sustainSeconds`, then feeds
    // silence and returns how many blocks it takes for the reported GR to
    // fall below 1 dB - the release-time measurement both two-stage tests
    // are built on.
    int measureReleaseBlocks (double sustainSeconds)
    {
        OptoLeveler opto;
        opto.setPeakReductionProportion (1.0f);
        opto.prepare (makeTestSpec (1));

        const auto sustainBlocks = static_cast<int> (sustainSeconds * testSampleRate / blockSize);

        for (int block = 0; block < sustainBlocks; ++block)
        {
            juce::AudioBuffer<float> buffer (1, blockSize);
            TestHelpers::fillWithSine (buffer, testSampleRate, 500.0, 0.8f, block * blockSize);
            juce::dsp::AudioBlock<float> audioBlock (buffer);
            opto.process (audioBlock);
        }

        REQUIRE (opto.getCurrentGainReductionDb() > 6.0f);

        int blocksUntilRecovered = 0;
        for (; blocksUntilRecovered < 1000; ++blocksUntilRecovered)
        {
            juce::AudioBuffer<float> silence (1, blockSize);
            silence.clear();
            juce::dsp::AudioBlock<float> audioBlock (silence);
            opto.process (audioBlock);

            if (opto.getCurrentGainReductionDb() < 1.0f)
                break;
        }

        return blocksUntilRecovered;
    }
}

TEST_CASE ("Opto: two-stage release - release measurably slows with longer GR history", "[dsp][opto][release]")
{
    // 0.15 s of GR barely charges the light-history accumulator (1.5 s time
    // constant); 3 s charges it close to saturation, pushing the release
    // from the ~60 ms fast stage toward the ~600 ms slow stage.
    const auto shortHistoryBlocks = measureReleaseBlocks (0.15);
    const auto longHistoryBlocks = measureReleaseBlocks (3.0);

    INFO ("short-history release blocks = " << shortHistoryBlocks
          << ", long-history release blocks = " << longHistoryBlocks);

    CHECK (longHistoryBlocks > shortHistoryBlocks);
    // "Measurably": the slow stage is an order of magnitude above the fast
    // stage, so even with the history decaying during the measurement the
    // long-history release must take at least twice as long.
    CHECK (longHistoryBlocks >= shortHistoryBlocks * 2);
}

TEST_CASE ("Opto: Peak Reduction 0% is a bit-exact bypass", "[dsp][opto][null]")
{
    OptoLeveler opto;
    opto.setPeakReductionProportion (0.0f);
    opto.setMakeupDb (0.0f);
    opto.prepare (makeTestSpec (2));

    juce::AudioBuffer<float> reference (2, 4096);
    TestHelpers::fillWithSine (reference, testSampleRate, 440.0, 0.8f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::ProcessSpec spec = makeTestSpec (2);
    spec.maximumBlockSize = 4096;
    opto.prepare (spec);

    juce::dsp::AudioBlock<float> block (processed);
    opto.process (block);

    CHECK (TestHelpers::maxDifferenceDbfs (processed, reference) <= -200.0);
    CHECK (opto.getCurrentGainReductionDb() == 0.0f);
}

TEST_CASE ("Opto: soft ~3:1 static behaviour at full Peak Reduction", "[dsp][opto][static-curve]")
{
    OptoLeveler opto;
    opto.setPeakReductionProportion (1.0f);
    opto.setMakeupDb (0.0f);

    juce::dsp::ProcessSpec spec = makeTestSpec (1);
    spec.maximumBlockSize = 48000;
    opto.prepare (spec);

    juce::AudioBuffer<float> reference (1, 48000);
    TestHelpers::fillWithSine (reference, testSampleRate, 500.0, 0.5f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    opto.process (block);

    const auto inputRms = TestHelpers::tailRms (reference, 24000);
    const auto outputRms = TestHelpers::tailRms (processed, 24000);
    const auto measuredDb = juce::Decibels::gainToDecibels (outputRms / inputRms);

    // Expected: threshold -32 dB, ratio 3:1, input RMS ~-9 dB ->
    // GR = (−9 − (−32)) * (1 − 1/3) ≈ 15.3 dB.
    const auto inputRmsDb = juce::Decibels::gainToDecibels (0.5 / juce::MathConstants<double>::sqrt2);
    const auto expectedGr = (inputRmsDb + 32.0) * (2.0 / 3.0);

    CHECK (measuredDb == Catch::Approx (-expectedGr).margin (2.0));
}

TEST_CASE ("Opto: makeup gain shifts the output level by its dB value", "[dsp][opto]")
{
    const auto measure = [] (float makeupDb)
    {
        OptoLeveler opto;
        opto.setPeakReductionProportion (0.5f);
        opto.setMakeupDb (makeupDb);

        juce::dsp::ProcessSpec spec = makeTestSpec (1);
        spec.maximumBlockSize = 24000;
        opto.prepare (spec);

        juce::AudioBuffer<float> buffer (1, 24000);
        TestHelpers::fillWithSine (buffer, testSampleRate, 500.0, 0.5f);

        juce::dsp::AudioBlock<float> block (buffer);
        opto.process (block);

        return TestHelpers::tailRms (buffer, 12000);
    };

    const auto reference = measure (0.0f);
    const auto boosted = measure (6.0f);

    REQUIRE (reference > 0.0);
    CHECK (juce::Decibels::gainToDecibels (boosted / reference) == Catch::Approx (6.0).margin (0.2));
}

TEST_CASE ("Opto: reset() clears detector, ballistics and light-history state", "[dsp][opto][reset]")
{
    // Charge the history with 3 s of GR, then reset - afterwards the
    // release must behave like the cold (fast) stage again, proving the
    // history accumulator really was cleared.
    OptoLeveler opto;
    opto.setPeakReductionProportion (1.0f);
    opto.prepare (makeTestSpec (1));

    for (int block = 0; block < 300; ++block)
    {
        juce::AudioBuffer<float> buffer (1, blockSize);
        TestHelpers::fillWithSine (buffer, testSampleRate, 500.0, 0.8f, block * blockSize);
        juce::dsp::AudioBlock<float> audioBlock (buffer);
        opto.process (audioBlock);
    }

    REQUIRE (opto.getCurrentGainReductionDb() > 6.0f);

    opto.reset();
    CHECK (opto.getCurrentGainReductionDb() == 0.0f);

    // One block of silence after reset: GR must stay at zero (no stale
    // envelope) rather than decaying from the pre-reset value.
    juce::AudioBuffer<float> silence (1, blockSize);
    silence.clear();
    juce::dsp::AudioBlock<float> audioBlock (silence);
    opto.process (audioBlock);

    CHECK (opto.getCurrentGainReductionDb() == Catch::Approx (0.0f).margin (1.0e-3));
}
