#include "dsp/FetCrush.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

// Bus (1) CRUSH, v0.5.0 feedback-FET engine (brief F3 / section 6.4): the
// measurable circuit signatures - ratio creep, attack/release coupling
// through the shared cap, knee narrowing with ratio, the genuine
// all-buttons bias state, panel-spec ballistics - plus the input-drive
// paradigm, linked/unlinked detection and the GR-gated colour, carried over
// from the previous suites.
namespace
{
    constexpr double testSampleRate = 48000.0;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels, int maxBlockSize)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (maxBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    // Steady-state gain change (tail RMS out/in) for a 1 kHz sine at
    // `amplitude`, 0 dB input drive unless stated.
    double measureSettledGainDb (FetCrush& crush, double frequencyHz, float amplitude, double seconds = 0.5)
    {
        const auto blockSize = 4800;
        crush.prepare (makeTestSpec (1, blockSize));

        const auto numBlocks = juce::jmax (2, static_cast<int> (seconds * testSampleRate / blockSize));

        juce::AudioBuffer<float> lastBlock (1, blockSize);

        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buffer (1, blockSize);
            TestHelpers::fillWithSine (buffer, testSampleRate, frequencyHz, amplitude, b * blockSize);
            juce::dsp::AudioBlock<float> block (buffer);
            crush.process (block);

            if (b == numBlocks - 1)
                lastBlock.makeCopyOf (buffer);
        }

        const auto inRms = static_cast<double> (amplitude) / juce::MathConstants<double>::sqrt2;
        const auto outRms = TestHelpers::rms (lastBlock);
        return juce::Decibels::gainToDecibels (outRms / inRms);
    }

    // Static-curve point: settled GR (meter) for a driven 1 kHz sine at
    // `inputDb` dBFS.
    double measureSettledGrDb (FetCrush::Ratio ratio, double inputDb, float attackStep = 6.0f, float releaseStep = 6.0f)
    {
        FetCrush crush;
        crush.setRatio (ratio);
        crush.setStyle (FetCrush::Style::allButtons);
        crush.setInputDriveDb (0.0f);
        crush.setAttackStep (attackStep);
        crush.setReleaseStep (releaseStep);

        const auto amplitude = static_cast<float> (std::pow (10.0, inputDb / 20.0));
        (void) measureSettledGainDb (crush, 1000.0, amplitude, 0.6);
        return crush.getCurrentGainReductionDb();
    }

    // Input level (dBFS) at which the settled GR first reaches `targetGrDb`
    // (linear interpolation on a 0.5 dB grid).
    double inputLevelForGr (FetCrush::Ratio ratio, double targetGrDb, double startDb, double endDb)
    {
        auto previousLevel = startDb;
        auto previousGr = measureSettledGrDb (ratio, startDb);

        for (auto level = startDb + 0.5; level <= endDb; level += 0.5)
        {
            const auto gr = measureSettledGrDb (ratio, level);

            if (gr >= targetGrDb)
            {
                const auto t = (targetGrDb - previousGr) / juce::jmax (1.0e-9, gr - previousGr);
                return previousLevel + t * (level - previousLevel);
            }

            previousLevel = level;
            previousGr = gr;
        }

        return endDb;
    }

    // GR trace at fine (32-sample) block granularity for a burst that
    // starts at t = 0.
    std::vector<float> measureGrTrace (FetCrush& crush, double frequencyHz, float amplitude, int numBlocks, int blockSize = 32)
    {
        std::vector<float> trace;
        trace.reserve (static_cast<size_t> (numBlocks));

        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buffer (1, blockSize);
            TestHelpers::fillWithSine (buffer, testSampleRate, frequencyHz, amplitude, b * blockSize);
            juce::dsp::AudioBlock<float> block (buffer);
            crush.process (block);
            trace.push_back (crush.getCurrentGainReductionDb());
        }

        return trace;
    }
}

//==============================================================================
// 6.4a - ratio creep: 1 kHz at +6 dB over threshold held 2 s: the
// instantaneous ratio at t = 10 ms is LOWER than at t = 1 s (the feedback
// loop keeps topping the cap - effective ratio grows after the transient).

TEST_CASE ("Crush: ratio creep - instantaneous ratio at 10 ms < ratio at 1 s", "[dsp][crush][feedback][creep]")
{
    FetCrush crush;
    crush.setRatio (FetCrush::Ratio::r4);
    crush.setStyle (FetCrush::Style::allButtons);
    crush.setInputDriveDb (0.0f);
    crush.setAttackStep (5.0f);
    crush.setReleaseStep (4.0f);

    constexpr int blockSize = 96; // 2 ms
    crush.prepare (makeTestSpec (1, blockSize));

    const auto amplitude = static_cast<float> (std::pow (10.0, (-30.0 + 6.0) / 20.0)); // +6 dB over the 4:1 threshold

    const auto numBlocks = static_cast<int> (2.0 * testSampleRate / blockSize);
    const auto trace = measureGrTrace (crush, 1000.0, amplitude, numBlocks, blockSize);

    const auto grAt10ms = trace[static_cast<size_t> (0.010 * testSampleRate / blockSize)];
    const auto grAt1s = trace[static_cast<size_t> (1.0 * testSampleRate / blockSize)];

    // ratio(t) = over / (over - GR(t)) with over = 6 dB: monotone in GR.
    INFO ("GR @10 ms = " << grAt10ms << " dB, GR @1 s = " << grAt1s << " dB");
    REQUIRE (grAt1s < 5.9); // sanity: not pinned at the top of the 6 dB window
    CHECK (grAt10ms < grAt1s);
}

//==============================================================================
// 6.4b - attack/release coupling: the effective attack time at a FIXED
// attack dial strictly decreases across 3 increasing (faster) release-dial
// positions - the one-cap/two-pot signature that kills
// independent-time-constant implementations. Measured as "GR reached a
// fixed 2 ms after burst onset" (more GR after 2 ms == faster effective
// attack).

TEST_CASE ("Crush: release dial changes the effective attack (shared-cap coupling)", "[dsp][crush][feedback][coupling]")
{
    const auto grAfter2ms = [] (float releaseStep)
    {
        FetCrush crush;
        crush.setRatio (FetCrush::Ratio::r20);
        crush.setStyle (FetCrush::Style::allButtons);
        crush.setInputDriveDb (16.0f); // ~+20 dB over the r20 threshold at -0.5 dBFS peak... (driven)
        crush.setAttackStep (2.0f);    // fixed, slow-ish
        crush.setReleaseStep (releaseStep);

        constexpr int blockSize = 32;
        crush.prepare (makeTestSpec (1, blockSize));

        const auto blocksFor2ms = static_cast<int> (0.002 * testSampleRate / blockSize);
        const auto trace = measureGrTrace (crush, 1000.0, 0.5f, blocksFor2ms, blockSize);
        return trace.back();
    };

    const auto slowRelease = grAfter2ms (2.0f);
    const auto midRelease = grAfter2ms (4.0f);
    const auto fastRelease = grAfter2ms (6.0f);

    INFO ("GR after 2 ms: release 2 -> " << slowRelease << " dB, release 4 -> " << midRelease
          << " dB, release 6 -> " << fastRelease << " dB");

    CHECK (midRelease > slowRelease);
    CHECK (fastRelease > midRelease);
}

//==============================================================================
// 6.4c - knee ordering: the measured knee width strictly decreases from
// 4:1 to 20:1 (the emergent feedback knee narrows as the loop steepens).
// Knee width metric: input-level span between the GR = 0.3 dB and
// GR = 2.5 dB points of the settled static curve.

TEST_CASE ("Crush: knee width strictly decreases 4:1 -> 8:1 -> 12:1 -> 20:1", "[dsp][crush][feedback][knee]")
{
    const auto kneeWidth = [] (FetCrush::Ratio ratio, double thresholdDb)
    {
        const auto onset = inputLevelForGr (ratio, 0.3, thresholdDb - 10.0, thresholdDb + 14.0);
        const auto deep = inputLevelForGr (ratio, 2.5, onset - 0.5, thresholdDb + 16.0);
        return deep - onset;
    };

    const auto w4 = kneeWidth (FetCrush::Ratio::r4, -30.0);
    const auto w8 = kneeWidth (FetCrush::Ratio::r8, -28.0);
    const auto w12 = kneeWidth (FetCrush::Ratio::r12, -26.0);
    const auto w20 = kneeWidth (FetCrush::Ratio::r20, -24.0);

    INFO ("knee widths (dB): 4:1 = " << w4 << ", 8:1 = " << w8 << ", 12:1 = " << w12 << ", 20:1 = " << w20);

    CHECK (w4 > w8);
    CHECK (w8 > w12);
    CHECK (w12 > w20);
}

//==============================================================================
// 6.4d - ABI: the all-buttons state is a genuine fifth bias state.

TEST_CASE ("Crush: ABI linear-region gain > 20:1 linear-region gain", "[dsp][crush][abi]")
{
    const auto measureLinearGain = [] (FetCrush::Ratio ratio)
    {
        FetCrush crush;
        crush.setRatio (ratio);
        crush.setStyle (FetCrush::Style::allButtons);
        crush.setInputDriveDb (0.0f);
        crush.setAttackStep (6.0f);
        crush.setReleaseStep (6.0f);
        return measureSettledGainDb (crush, 1000.0, 0.005f); // -46 dBFS, far below every threshold
    };

    const auto allGain = measureLinearGain (FetCrush::Ratio::rAll);
    const auto r20Gain = measureLinearGain (FetCrush::Ratio::r20);

    INFO ("linear-region gain: ALL = " << allGain << " dB, 20:1 = " << r20Gain << " dB");
    CHECK (allGain > r20Gain + 0.3);
    CHECK (allGain - r20Gain < 1.5); // "+0.5-1 dB" class, not a level jump
}

TEST_CASE ("Crush: ABI slope sits in the 12:1..20:1 window", "[dsp][crush][abi][slope]")
{
    // Static slope between +6 and +12 dB over the ALL threshold (-24 dB).
    const auto gr6 = measureSettledGrDb (FetCrush::Ratio::rAll, -18.0);
    const auto gr12 = measureSettledGrDb (FetCrush::Ratio::rAll, -12.0);

    const auto outDelta = 6.0 - (gr12 - gr6); // output rise for 6 dB input rise
    REQUIRE (outDelta > 0.0);
    const auto slope = 6.0 / outDelta;

    INFO ("ABI static slope = " << slope << ":1 (GR " << gr6 << " -> " << gr12 << " dB)");
    CHECK (slope >= 12.0);
    CHECK (slope <= 20.0);
}

TEST_CASE ("Crush: ABI step response overshoots >= 1 dB GR beyond the settled value", "[dsp][crush][abi][overshoot]")
{
    FetCrush crush;
    crush.setRatio (FetCrush::Ratio::rAll);
    crush.setStyle (FetCrush::Style::allButtons);
    crush.setInputDriveDb (18.0f);
    crush.setAttackStep (6.0f);
    crush.setReleaseStep (6.0f);

    constexpr int blockSize = 32;
    crush.prepare (makeTestSpec (1, blockSize));

    const auto numBlocks = static_cast<int> (1.0 * testSampleRate / blockSize);
    const auto trace = measureGrTrace (crush, 1000.0, 0.5f, numBlocks, blockSize);

    float earlyPeak = 0.0f;
    for (size_t i = 0; i < static_cast<size_t> (0.05 * testSampleRate / blockSize); ++i)
        earlyPeak = juce::jmax (earlyPeak, trace[i]);

    const auto settled = trace.back();

    INFO ("ABI GR early peak = " << earlyPeak << " dB, settled = " << settled << " dB");
    CHECK (earlyPeak >= settled + 1.0f);
}

//==============================================================================
// 6.4e - ballistics: panel-spec attack/release ranges, monotone in dial.

TEST_CASE ("Crush: attack 63%-GR times span the panel range and are monotone in the dial", "[dsp][crush][ballistics][attack]")
{
    const auto attackTimeSeconds = [] (float attackStep)
    {
        FetCrush crush;
        crush.setRatio (FetCrush::Ratio::r20);
        crush.setStyle (FetCrush::Style::allButtons);
        crush.setInputDriveDb (16.0f); // tone burst ~+20 dB over threshold
        crush.setAttackStep (attackStep);
        crush.setReleaseStep (4.0f);

        constexpr int blockSize = 1; // sample-accurate trace
        crush.prepare (makeTestSpec (1, blockSize));

        // Settled GR first (long render), then re-measure the onset.
        FetCrush settledCrush;
        settledCrush.setRatio (FetCrush::Ratio::r20);
        settledCrush.setStyle (FetCrush::Style::allButtons);
        settledCrush.setInputDriveDb (16.0f);
        settledCrush.setAttackStep (attackStep);
        settledCrush.setReleaseStep (4.0f);
        (void) measureSettledGainDb (settledCrush, 1000.0, 0.5f, 0.3);
        const auto settledGr = settledCrush.getCurrentGainReductionDb();
        REQUIRE (settledGr > 6.0f);

        const auto target = 0.63f * settledGr;
        const auto maxSamples = static_cast<int> (0.005 * testSampleRate);

        for (int n = 0; n < maxSamples; ++n)
        {
            juce::AudioBuffer<float> buffer (1, 1);
            TestHelpers::fillWithSine (buffer, testSampleRate, 1000.0, 0.5f, n);
            juce::dsp::AudioBlock<float> block (buffer);
            crush.process (block);

            if (crush.getCurrentGainReductionDb() >= target)
                return (n + 1) / testSampleRate;
        }

        return static_cast<double> (maxSamples) / testSampleRate;
    };

    const auto tSlow = attackTimeSeconds (1.0f);
    const auto tMid = attackTimeSeconds (4.0f);
    const auto tFast = attackTimeSeconds (7.0f);

    INFO ("attack 63% times: dial 1 = " << tSlow * 1.0e6 << " us, dial 4 = " << tMid * 1.0e6
          << " us, dial 7 = " << tFast * 1.0e6 << " us");

    CHECK (tSlow > tMid);
    CHECK (tMid > tFast);
    CHECK (tFast >= 18.0e-6);  // 20 us class (1 sample at 48 k = 20.8 us)
    CHECK (tSlow <= 840.0e-6); // 800 us class
}

TEST_CASE ("Crush: release 37%-residual times span the panel range and are monotone in the dial", "[dsp][crush][ballistics][release]")
{
    const auto releaseTimeSeconds = [] (float releaseStep)
    {
        FetCrush crush;
        crush.setRatio (FetCrush::Ratio::r20);
        crush.setStyle (FetCrush::Style::allButtons);
        crush.setInputDriveDb (16.0f);
        crush.setAttackStep (6.0f);
        crush.setReleaseStep (releaseStep);

        constexpr int blockSize = 96; // 2 ms resolution
        crush.prepare (makeTestSpec (1, blockSize));

        // Drive to settled GR.
        const auto burstBlocks = static_cast<int> (0.5 * testSampleRate / blockSize);
        (void) measureGrTrace (crush, 1000.0, 0.5f, burstBlocks, blockSize);
        const auto grAtStop = crush.getCurrentGainReductionDb();
        REQUIRE (grAtStop > 6.0f);

        const auto target = 0.37f * grAtStop;
        const auto maxBlocks = static_cast<int> (3.0 * testSampleRate / blockSize);

        for (int b = 0; b < maxBlocks; ++b)
        {
            juce::AudioBuffer<float> silence (1, blockSize);
            silence.clear();
            juce::dsp::AudioBlock<float> block (silence);
            crush.process (block);

            if (crush.getCurrentGainReductionDb() <= target)
                return (b + 1) * blockSize / testSampleRate;
        }

        return 3.0;
    };

    const auto tSlow = releaseTimeSeconds (1.0f);
    const auto tMid = releaseTimeSeconds (4.0f);
    const auto tFast = releaseTimeSeconds (7.0f);

    INFO ("release 37% times: dial 1 = " << tSlow * 1e3 << " ms, dial 4 = " << tMid * 1e3
          << " ms, dial 7 = " << tFast * 1e3 << " ms");

    CHECK (tSlow > tMid);
    CHECK (tMid > tFast);
    CHECK (tFast >= 0.040); // 50 ms class
    CHECK (tSlow <= 1.20);  // 1100 ms class
}

//==============================================================================
// Carried-over behaviours.

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
        (void) measureSettledGainDb (crush, 1000.0, 0.25f);
        return crush.getCurrentGainReductionDb();
    };

    const auto lowDriveGr = measureAtDrive (0.0f);
    const auto highDriveGr = measureAtDrive (18.0f);

    INFO ("GR at 0 dB drive = " << lowDriveGr << " dB, at 18 dB drive = " << highDriveGr << " dB");
    CHECK (highDriveGr > lowDriveGr);
    CHECK (highDriveGr - lowDriveGr > 6.0f);
}

TEST_CASE ("Crush: Gentle style is a fixed soft voicing independent of the ratio selector", "[dsp][crush][gentle]")
{
    const auto measureGentleGr = [] (FetCrush::Ratio ratio)
    {
        FetCrush crush;
        crush.setRatio (ratio);
        crush.setStyle (FetCrush::Style::gentle);
        crush.setInputDriveDb (0.0f);
        crush.setAttackStep (6.0f);
        crush.setReleaseStep (6.0f);
        (void) measureSettledGainDb (crush, 1000.0, 0.25f);
        return crush.getCurrentGainReductionDb();
    };

    const auto gentleR4 = measureGentleGr (FetCrush::Ratio::r4);
    const auto gentleAll = measureGentleGr (FetCrush::Ratio::rAll);

    CHECK (gentleR4 == Catch::Approx (gentleAll).margin (0.2));

    // Softer than the all-buttons character at the same input level.
    FetCrush allButtons;
    allButtons.setRatio (FetCrush::Ratio::rAll);
    allButtons.setStyle (FetCrush::Style::allButtons);
    allButtons.setInputDriveDb (0.0f);
    allButtons.setAttackStep (6.0f);
    allButtons.setReleaseStep (6.0f);
    (void) measureSettledGainDb (allButtons, 1000.0, 0.25f);

    CHECK (gentleR4 < allButtons.getCurrentGainReductionDb());
}

TEST_CASE ("Crush: reset() clears the loop state", "[dsp][crush][reset]")
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

    float rightMaxAbs = 0.0f;
    const auto* right = buffer.getReadPointer (1);
    for (int i = 0; i < 9600; ++i)
        rightMaxAbs = std::max (rightMaxAbs, std::abs (right[i]));

    CHECK (TestHelpers::peakAbsolute (buffer) > 0.0f);
    CHECK (rightMaxAbs < 1.0e-6f); // right was silent input and unlinked -> stays silent output
}

TEST_CASE ("Crush: linked - a hard-panned L-only burst produces GR on both channels", "[dsp][crush][link]")
{
    constexpr float driveDb = 12.0f;

    // crush_input drives the AUDIO path unconditionally too, so a channel
    // with zero gain reduction still measures ~+driveDb of gain change; the
    // comparison is between conditions, not against 0 dB.
    const auto measureRightChannelGainDb = [] (bool linked)
    {
        constexpr int blockSize = 24000;

        FetCrush crush;
        crush.setRatio (FetCrush::Ratio::r20);
        crush.setInputDriveDb (driveDb);
        crush.setAttackStep (7.0f);
        crush.setReleaseStep (4.0f);
        crush.setLinked (linked);
        crush.prepare (makeTestSpec (2, blockSize));

        juce::AudioBuffer<float> buffer (2, blockSize);
        buffer.clear();

        auto* left = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);
        for (int i = 0; i < blockSize; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * 500.0 * i / testSampleRate;
            left[i] = 0.9f * static_cast<float> (std::sin (phase));
            right[i] = 0.01f * static_cast<float> (std::sin (phase));
        }

        juce::dsp::AudioBlock<float> block (buffer);
        crush.process (block);

        const auto rightInputRms = 0.01 / juce::MathConstants<double>::sqrt2;
        const auto rightOutputRms = TestHelpers::tailRms (
            juce::AudioBuffer<float> (buffer.getArrayOfWritePointers() + 1, 1, 0, blockSize), 12000);

        return juce::Decibels::gainToDecibels (rightOutputRms / rightInputRms);
    };

    const auto unlinkedRightGainDb = measureRightChannelGainDb (false);
    const auto linkedRightGainDb = measureRightChannelGainDb (true);

    INFO ("right-channel gain: unlinked = " << unlinkedRightGainDb << " dB, linked = " << linkedRightGainDb << " dB");

    // Unlinked: the quiet right channel never trips the limiter.
    CHECK (unlinkedRightGainDb == Catch::Approx (driveDb).margin (0.5));
    // Linked: the shared (loud-left-driven) sidechain pulls the right
    // channel's gain down measurably.
    CHECK (linkedRightGainDb < unlinkedRightGainDb - 3.0);
}

//==============================================================================
// GR-gated colour (kept from the M2 voicing pass, now the FET eps-term +
// ADAA'd LF transformer delta).

namespace
{
    double measureThdAtDrive (FetCrush::Ratio ratio, float driveDb, double frequencyHz, float amplitude)
    {
        constexpr int blockSize = 24000;
        constexpr int settle = 12000;

        FetCrush crush;
        crush.setRatio (ratio);
        crush.setStyle (FetCrush::Style::allButtons);
        crush.setInputDriveDb (driveDb);
        crush.setAttackStep (7.0f);
        crush.setReleaseStep (7.0f);
        crush.prepare (makeTestSpec (1, blockSize));

        juce::AudioBuffer<float> buffer (1, blockSize);
        TestHelpers::fillWithSine (buffer, testSampleRate, frequencyHz, amplitude);

        juce::dsp::AudioBlock<float> block (buffer);
        crush.process (block);

        const auto cyclesPerWindow = std::floor (frequencyHz * (blockSize - settle) / testSampleRate);
        const auto windowSamples = static_cast<int> (cyclesPerWindow * testSampleRate / frequencyHz);

        return TestHelpers::estimateThdRatio (buffer, 0, settle, windowSamples, testSampleRate, frequencyHz);
    }
}

TEST_CASE ("Crush: colour stays negligible when the signal never reaches gain reduction", "[dsp][crush][colour]")
{
    const auto thd = measureThdAtDrive (FetCrush::Ratio::r4, 0.0f, 1000.0, 0.02f);
    CHECK (thd < 0.002); // < 0.2%
}

TEST_CASE ("Crush: colour grows with gain reduction (eps-term ~ GR depth)", "[dsp][crush][colour]")
{
    const auto lowGrThd = measureThdAtDrive (FetCrush::Ratio::r20, 6.0f, 1000.0, 0.3f);
    const auto highGrThd = measureThdAtDrive (FetCrush::Ratio::r20, 24.0f, 1000.0, 0.3f);

    INFO ("low-GR THD = " << lowGrThd << ", high-GR THD = " << highGrThd);
    CHECK (highGrThd > lowGrThd);
}

TEST_CASE ("Crush: colour is LF-selective (transformer delta below ~150 Hz)", "[dsp][crush][colour][lf]")
{
    const auto lowFreqThd = measureThdAtDrive (FetCrush::Ratio::r20, 24.0f, 80.0, 0.3f);
    const auto highFreqThd = measureThdAtDrive (FetCrush::Ratio::r20, 24.0f, 5000.0, 0.3f);

    INFO ("80 Hz THD = " << lowFreqThd << ", 5 kHz THD = " << highFreqThd);
    CHECK (lowFreqThd > highFreqThd);
}

TEST_CASE ("Crush: output stays finite and bounded at full-scale drive", "[dsp][crush][robustness]")
{
    FetCrush crush;
    crush.setRatio (FetCrush::Ratio::rAll);
    crush.setInputDriveDb (48.0f);
    crush.setAttackStep (7.0f);
    crush.setReleaseStep (1.0f);
    crush.prepare (makeTestSpec (2, 24000));

    juce::AudioBuffer<float> buffer (2, 24000);
    TestHelpers::fillWithSine (buffer, testSampleRate, 200.0, 1.0f);
    juce::dsp::AudioBlock<float> block (buffer);
    crush.process (block);

    CHECK (TestHelpers::allSamplesFinite (buffer));
    CHECK (TestHelpers::peakAbsolute (buffer) < 16.0f); // 48 dB drive minus ~30 dB max GR class
}
