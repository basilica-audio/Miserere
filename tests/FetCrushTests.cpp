#include "dsp/FetCrush.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// Bus (1) CRUSH: per-ratio static curves (including the ALL-mode plateau's
// non-monotonic slope), the ALL-mode transient-lag overshoot, dual-rate
// program-dependent release, the input-drive paradigm (no threshold knob),
// and unlinked-vs-linked stereo detection - design-brief.md guarantees 3
// and 10.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 24000; // 0.5 s: lets the envelope fully settle
    constexpr int settleSamples = 12000;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels, int maxBlockSize = testBlockSize)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (maxBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    double measureGainChangeDb (FetCrush& crush, double frequencyHz, float amplitude)
    {
        crush.prepare (makeTestSpec (1));

        juce::AudioBuffer<float> reference (1, testBlockSize);
        TestHelpers::fillWithSine (reference, testSampleRate, frequencyHz, amplitude);

        juce::AudioBuffer<float> processed;
        processed.makeCopyOf (reference);

        juce::dsp::AudioBlock<float> block (processed);
        crush.process (block);

        const auto inputRms = TestHelpers::tailRms (reference, settleSamples);
        const auto outputRms = TestHelpers::tailRms (processed, settleSamples);

        REQUIRE (inputRms > 0.0);
        REQUIRE (outputRms > 0.0);

        return juce::Decibels::gainToDecibels (outputRms / inputRms);
    }
}

//==============================================================================
// Static curve (pure function - no envelope dynamics involved).

TEST_CASE ("Crush: per-ratio thresholds rise and knees harden as ratio increases", "[dsp][crush][static-curve]")
{
    const auto r4 = FetCrush::ratioPointFor (FetCrush::Ratio::r4);
    const auto r8 = FetCrush::ratioPointFor (FetCrush::Ratio::r8);
    const auto r12 = FetCrush::ratioPointFor (FetCrush::Ratio::r12);
    const auto r20 = FetCrush::ratioPointFor (FetCrush::Ratio::r20);

    CHECK (r4.thresholdDb < r8.thresholdDb);
    CHECK (r8.thresholdDb < r12.thresholdDb);
    CHECK (r12.thresholdDb < r20.thresholdDb);

    CHECK (r4.kneeDb > r8.kneeDb);
    CHECK (r8.kneeDb > r12.kneeDb);
    CHECK (r12.kneeDb > r20.kneeDb);
}

TEST_CASE ("Crush: static curve reduction is 0 well below threshold, positive well above it", "[dsp][crush][static-curve]")
{
    for (auto ratio : { FetCrush::Ratio::r4, FetCrush::Ratio::r8, FetCrush::Ratio::r12, FetCrush::Ratio::r20, FetCrush::Ratio::rAll })
    {
        const auto point = FetCrush::ratioPointFor (ratio);

        CHECK (FetCrush::staticCurveReductionDb (point.thresholdDb - 20.0f, ratio, FetCrush::Style::allButtons) == 0.0f);
        CHECK (FetCrush::staticCurveReductionDb (point.thresholdDb + 20.0f, ratio, FetCrush::Style::allButtons) > 10.0f);
    }
}

TEST_CASE ("Crush: ALL mode's plateau is non-monotonic in slope (steep, then a give-back)", "[dsp][crush][static-curve][all-mode]")
{
    const auto point = FetCrush::ratioPointFor (FetCrush::Ratio::rAll);

    // Slope just above the knee (steep zone).
    const auto steepLow = FetCrush::staticCurveReductionDb (point.thresholdDb + 2.0f, FetCrush::Ratio::rAll, FetCrush::Style::allButtons);
    const auto steepHigh = FetCrush::staticCurveReductionDb (point.thresholdDb + 3.0f, FetCrush::Ratio::rAll, FetCrush::Style::allButtons);
    const auto steepSlope = steepHigh - steepLow; // dB of extra reduction per dB of extra input

    // Slope well above the "kink" (give-back zone, kink at +6 dB overshoot).
    const auto givebackLow = FetCrush::staticCurveReductionDb (point.thresholdDb + 9.0f, FetCrush::Ratio::rAll, FetCrush::Style::allButtons);
    const auto givebackHigh = FetCrush::staticCurveReductionDb (point.thresholdDb + 10.0f, FetCrush::Ratio::rAll, FetCrush::Style::allButtons);
    const auto givebackSlope = givebackHigh - givebackLow;

    // Both zones still compress (reduction grows with input)...
    CHECK (steepSlope > 0.0f);
    CHECK (givebackSlope > 0.0f);
    // ...but the give-back zone compresses LESS per dB than the steep zone -
    // a genuinely non-monotonic reduction-slope, the plateau's "kink".
    CHECK (givebackSlope < steepSlope);

    // And the corresponding OUTPUT slope (1 - reduction slope) must
    // therefore be < 1 everywhere (never expanding) but LARGER after the
    // kink (giving back some of the compression).
    CHECK ((1.0f - steepSlope) < 1.0f);
    CHECK ((1.0f - givebackSlope) < 1.0f);
    CHECK ((1.0f - givebackSlope) > (1.0f - steepSlope));
}

TEST_CASE ("Crush: Gentle style is a fixed, soft 2:1 voicing independent of the ratio selector", "[dsp][crush][static-curve][gentle]")
{
    const auto gentleWithR4Selected = FetCrush::staticCurveReductionDb (-10.0f, FetCrush::Ratio::r4, FetCrush::Style::gentle);
    const auto gentleWithAllSelected = FetCrush::staticCurveReductionDb (-10.0f, FetCrush::Ratio::rAll, FetCrush::Style::gentle);

    CHECK (gentleWithR4Selected == Catch::Approx (gentleWithAllSelected));

    // Softer than the ALL-buttons character at the same input level.
    const auto allButtonsReduction = FetCrush::staticCurveReductionDb (-10.0f, FetCrush::Ratio::rAll, FetCrush::Style::allButtons);
    CHECK (gentleWithR4Selected < allButtonsReduction);
}

//==============================================================================
// Envelope dynamics.

TEST_CASE ("Crush: 4:1 static curve within +-2 dB", "[dsp][crush][dynamic]")
{
    FetCrush crush;
    crush.setRatio (FetCrush::Ratio::r4);
    crush.setStyle (FetCrush::Style::allButtons);
    crush.setInputDriveDb (0.0f);
    crush.setAttackStep (7.0f);
    crush.setReleaseStep (4.0f);

    const auto measured = measureGainChangeDb (crush, 1000.0, 0.5f);

    const auto point = FetCrush::ratioPointFor (FetCrush::Ratio::r4);
    const auto inputPeakDb = juce::Decibels::gainToDecibels (0.5);
    const auto expected = -std::max (0.0, inputPeakDb - static_cast<double> (point.thresholdDb)) * (1.0 - 1.0 / static_cast<double> (point.ratio));

    CHECK (measured == Catch::Approx (expected).margin (2.0));
}

TEST_CASE ("Crush: input drive increases measured gain reduction", "[dsp][crush][drive]")
{
    const auto measureAtDrive = [] (float driveDb)
    {
        FetCrush crush;
        crush.setRatio (FetCrush::Ratio::r20);
        crush.setStyle (FetCrush::Style::allButtons);
        crush.setInputDriveDb (driveDb);
        crush.setAttackStep (7.0f);
        crush.setReleaseStep (4.0f);

        (void) measureGainChangeDb (crush, 1000.0, 0.25f);
        return crush.getCurrentGainReductionDb();
    };

    const auto lowDriveGr = measureAtDrive (0.0f);
    const auto highDriveGr = measureAtDrive (18.0f);

    CHECK (highDriveGr > lowDriveGr);
    CHECK (highDriveGr - lowDriveGr > 6.0f); // most of the 18 dB drive step shows up as extra GR at 20:1
}

TEST_CASE ("Crush: ALL mode shows more transient overshoot than a non-ALL ratio at the same ballistics", "[dsp][crush][all-mode][transient]")
{
    const auto measureOvershootRatio = [] (FetCrush::Ratio ratio)
    {
        FetCrush crush;
        crush.setRatio (ratio);
        crush.setStyle (FetCrush::Style::allButtons);
        crush.setInputDriveDb (24.0f);
        crush.setAttackStep (1.0f); // slowest base attack, so the extra ALL-mode lag is proportionally significant
        crush.setReleaseStep (4.0f);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = 4800;
        spec.numChannels = 1;
        crush.prepare (spec);

        juce::AudioBuffer<float> buffer (1, 4800);
        TestHelpers::fillWithSine (buffer, testSampleRate, 500.0, 0.7f);

        juce::dsp::AudioBlock<float> block (buffer);
        crush.process (block);

        const auto* data = buffer.getReadPointer (0);

        float earlyPeak = 0.0f;
        for (int i = 0; i < 96; ++i) // first 2 ms
            earlyPeak = std::max (earlyPeak, std::abs (data[i]));

        float settledPeak = 0.0f;
        for (int i = 4000; i < 4800; ++i) // settled tail
            settledPeak = std::max (settledPeak, std::abs (data[i]));

        REQUIRE (settledPeak > 0.0f);
        return earlyPeak / settledPeak;
    };

    const auto allModeOvershoot = measureOvershootRatio (FetCrush::Ratio::rAll);
    const auto r20Overshoot = measureOvershootRatio (FetCrush::Ratio::r20);

    CHECK (allModeOvershoot > r20Overshoot);
}

TEST_CASE ("Crush: dual-rate release recovers far faster after a short burst than after sustained GR", "[dsp][crush][release]")
{
    const auto measureRecoveryBlocks = [] (double sustainSeconds)
    {
        FetCrush crush;
        crush.setRatio (FetCrush::Ratio::rAll);
        crush.setStyle (FetCrush::Style::allButtons);
        crush.setInputDriveDb (18.0f);
        crush.setAttackStep (7.0f);
        crush.setReleaseStep (7.0f); // fastest base release, so the dual-rate blend is the dominant effect

        constexpr int blockSize = 256;
        crush.prepare (makeTestSpec (1, blockSize));

        const auto sustainBlocks = juce::jmax (1, static_cast<int> (sustainSeconds * testSampleRate / blockSize));

        for (int block = 0; block < sustainBlocks; ++block)
        {
            juce::AudioBuffer<float> buffer (1, blockSize);
            TestHelpers::fillWithSine (buffer, testSampleRate, 500.0, 0.8f, block * blockSize);
            juce::dsp::AudioBlock<float> audioBlock (buffer);
            crush.process (audioBlock);
        }

        REQUIRE (crush.getCurrentGainReductionDb() > 4.0f);

        int blocksUntilRecovered = 0;
        for (; blocksUntilRecovered < 2000; ++blocksUntilRecovered)
        {
            juce::AudioBuffer<float> silence (1, blockSize);
            silence.clear();
            juce::dsp::AudioBlock<float> audioBlock (silence);
            crush.process (audioBlock);

            if (crush.getCurrentGainReductionDb() < 0.5f)
                break;
        }

        return blocksUntilRecovered;
    };

    const auto shortBurstRecovery = measureRecoveryBlocks (0.05);  // 50 ms
    const auto sustainedRecovery = measureRecoveryBlocks (5.0);    // 5 s

    INFO ("short-burst recovery blocks = " << shortBurstRecovery << ", sustained recovery blocks = " << sustainedRecovery);

    CHECK (sustainedRecovery > shortBurstRecovery);
    CHECK (sustainedRecovery >= shortBurstRecovery * 3);
}

TEST_CASE ("Crush: reset() clears the envelope and duration integrator", "[dsp][crush][reset]")
{
    FetCrush crush;
    crush.setRatio (FetCrush::Ratio::rAll);
    crush.setInputDriveDb (24.0f);
    crush.setAttackStep (7.0f);
    crush.setReleaseStep (1.0f);
    crush.prepare (makeTestSpec (1, 4096));

    juce::AudioBuffer<float> loud (1, 4096);
    TestHelpers::fillWithSine (loud, testSampleRate, 500.0, 0.9f);
    juce::dsp::AudioBlock<float> loudBlock (loud);
    crush.process (loudBlock);

    REQUIRE (crush.getCurrentGainReductionDb() > 0.0f);

    crush.reset();
    CHECK (crush.getCurrentGainReductionDb() == 0.0f);
}

//==============================================================================
// Unlinked-vs-linked stereo detection (guarantee 10).

TEST_CASE ("Crush: unlinked (default) - a hard-panned L-only burst produces GR on L only", "[dsp][crush][link]")
{
    FetCrush crush;
    crush.setRatio (FetCrush::Ratio::r20);
    crush.setInputDriveDb (24.0f);
    crush.setAttackStep (7.0f);
    crush.setReleaseStep (4.0f);
    crush.setLinked (false);
    crush.prepare (makeTestSpec (2, 9600));

    juce::AudioBuffer<float> buffer (2, 9600);
    buffer.clear();
    TestHelpers::fillWithSine (buffer, testSampleRate, 500.0, 0.9f);
    buffer.clear (1, 0, 9600); // silence the right channel entirely

    juce::dsp::AudioBlock<float> block (buffer);
    crush.process (block);

    const auto leftPeak = TestHelpers::peakAbsolute (juce::AudioBuffer<float> (buffer.getArrayOfWritePointers(), 1, 0, 9600));
    const auto rightMaxAbs = [&]
    {
        float peak = 0.0f;
        const auto* data = buffer.getReadPointer (1);
        for (int i = 0; i < 9600; ++i)
            peak = std::max (peak, std::abs (data[i]));
        return peak;
    }();

    CHECK (leftPeak > 0.0f);
    CHECK (rightMaxAbs < 1.0e-6f); // right was silent input and unlinked -> stays silent output
}

TEST_CASE ("Crush: linked - a hard-panned L-only burst produces GR on both channels", "[dsp][crush][link]")
{
    constexpr float driveDb = 12.0f;

    // Note: crush_input drives the AUDIO path unconditionally too (see
    // FetCrush.h - "driving harder is meant to be louder", uncompensated),
    // so even a channel with zero gain reduction still measures a gain
    // CHANGE of ~+driveDb (the drive itself), not 0 dB. The comparison
    // below is therefore between the two conditions' measured gain, not
    // against an absolute 0 dB reference.
    const auto measureRightChannelGainDb = [] (bool linked)
    {
        FetCrush crush;
        crush.setRatio (FetCrush::Ratio::r20); // threshold -24 dB - highest (least sensitive) of the table
        crush.setInputDriveDb (driveDb);
        crush.setAttackStep (7.0f);
        crush.setReleaseStep (4.0f);
        crush.setLinked (linked);
        crush.prepare (makeTestSpec (2, testBlockSize));

        juce::AudioBuffer<float> buffer (2, testBlockSize);
        buffer.clear();

        // Left carries a loud tone (well above threshold once driven);
        // right carries a small tone whose OWN driven level (-40 + 12 =
        // -28 dBFS) stays comfortably below the -24 dB threshold - so any
        // gain reduction measured on the right can only come from the
        // linked (shared) detector.
        auto* left = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);
        for (int i = 0; i < testBlockSize; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * 500.0 * i / testSampleRate;
            left[i] = 0.9f * static_cast<float> (std::sin (phase));
            right[i] = 0.01f * static_cast<float> (std::sin (phase));
        }

        juce::dsp::AudioBlock<float> block (buffer);
        crush.process (block);

        const auto rightInputRms = 0.01 / juce::MathConstants<double>::sqrt2;
        const auto rightOutputRms = TestHelpers::tailRms (
            juce::AudioBuffer<float> (buffer.getArrayOfWritePointers() + 1, 1, 0, testBlockSize), settleSamples);

        return juce::Decibels::gainToDecibels (rightOutputRms / rightInputRms);
    };

    const auto unlinkedRightGainDb = measureRightChannelGainDb (false);
    const auto linkedRightGainDb = measureRightChannelGainDb (true);

    // Unlinked: the right channel's own (quiet) signal never trips the
    // limiter, so its only gain change is the drive itself.
    CHECK (unlinkedRightGainDb == Catch::Approx (driveDb).margin (0.5));
    // Linked: the shared (loud-left-driven) detector pulls the right
    // channel's gain down measurably below the drive-only baseline.
    CHECK (linkedRightGainDb < unlinkedRightGainDb - 3.0);
}
