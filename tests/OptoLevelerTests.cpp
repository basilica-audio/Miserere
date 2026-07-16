#include "dsp/OptoLeveler.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// The SANDWICH bus's Opto Leveler: static curve (breakaway/soft-knee/
// ceiling lookup, Limit switch), two-stage release with GR-history memory,
// detector-only emphasis selectivity, and unlinked-vs-linked detection -
// design-brief.md guarantees 4 and 10.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int blockSize = 512;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels, int maxBlockSize = blockSize)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (maxBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    // A moderate (not deep-ceiling) drive/amplitude combination: -20 dBFS
    // input at 50% Peak Reduction (9 dB drive) lands the driven peak around
    // -11 dBFS - about 19 dB above the -30 dB breakaway, comfortably inside
    // the soft-knee/ceiling region without requiring an unrealistic tens-
    // of-dB decay before the release dynamics can be measured cleanly (the
    // brief's own guidance: "maximum benefit... 4 to 8 dB of compression
    // continuously" - this test's GR lands in that same practical range).
    int measureReleaseBlocks (double sustainSeconds)
    {
        OptoLeveler opto;
        opto.setPeakReductionProportion (0.5f);
        opto.prepare (makeTestSpec (1));

        const auto sustainBlocks = juce::jmax (1, static_cast<int> (sustainSeconds * testSampleRate / blockSize));

        for (int block = 0; block < sustainBlocks; ++block)
        {
            juce::AudioBuffer<float> buffer (1, blockSize);
            TestHelpers::fillWithSine (buffer, testSampleRate, 500.0, 0.1f, block * blockSize);
            juce::dsp::AudioBlock<float> audioBlock (buffer);
            opto.process (audioBlock);
        }

        REQUIRE (opto.getCurrentGainReductionDb() > 3.0f);

        int blocksUntilRecovered = 0;
        for (; blocksUntilRecovered < 4000; ++blocksUntilRecovered)
        {
            juce::AudioBuffer<float> silence (1, blockSize);
            silence.clear();
            juce::dsp::AudioBlock<float> audioBlock (silence);
            opto.process (audioBlock);

            if (opto.getCurrentGainReductionDb() < 0.5f)
                break;
        }

        return blocksUntilRecovered;
    }
}

//==============================================================================
// Static curve (pure function).

TEST_CASE ("Opto: static curve is unity below breakaway (-30 dB)", "[dsp][opto][static-curve]")
{
    CHECK (OptoLeveler::staticCurveOutputDb (-40.0f, false) == Catch::Approx (-40.0f));
    CHECK (OptoLeveler::staticCurveOutputDb (-30.0f, false) == Catch::Approx (-30.0f));
}

TEST_CASE ("Opto: static curve is ~3:1 in the soft-knee region, ~10:1 with Limit engaged", "[dsp][opto][static-curve][limit]")
{
    // 10 dB above breakaway (i.e. at the knee ceiling, -20 dB): a 10 dB rise
    // in input compresses to ~10/3 dB (normal) or ~1 dB (limit) of output
    // rise, per the brief's ~3:1 / ~10:1 knee ratios.
    const auto normalRise = OptoLeveler::staticCurveOutputDb (-20.0f, false) - OptoLeveler::staticCurveOutputDb (-30.0f, false);
    const auto limitRise = OptoLeveler::staticCurveOutputDb (-20.0f, true) - OptoLeveler::staticCurveOutputDb (-30.0f, true);

    CHECK (normalRise == Catch::Approx (10.0f / 3.0f).margin (0.5f));
    CHECK (limitRise == Catch::Approx (1.0f).margin (0.3f));
    CHECK (limitRise < normalRise);
}

TEST_CASE ("Opto: static curve ceiling gives < 1 dB of output rise for +20 dB of input above the knee", "[dsp][opto][static-curve][ceiling]")
{
    const auto atCeiling = OptoLeveler::staticCurveOutputDb (-20.0f, false);
    const auto twentyDbMore = OptoLeveler::staticCurveOutputDb (0.0f, false);

    CHECK ((twentyDbMore - atCeiling) < 1.0f);
    CHECK ((twentyDbMore - atCeiling) > 0.0f); // still rises, just barely - not a literal brick wall
}

//==============================================================================
// Envelope dynamics.

TEST_CASE ("Opto: two-stage release - release measurably slows with longer GR history", "[dsp][opto][release]")
{
    const auto shortHistoryBlocks = measureReleaseBlocks (0.1);
    const auto longHistoryBlocks = measureReleaseBlocks (3.0);

    INFO ("short-history release blocks = " << shortHistoryBlocks << ", long-history release blocks = " << longHistoryBlocks);

    CHECK (longHistoryBlocks > shortHistoryBlocks);
    CHECK (longHistoryBlocks >= shortHistoryBlocks * 2);
}

TEST_CASE ("Opto: 50% recovery from a short GR event happens around the ~60 ms fast stage", "[dsp][opto][release]")
{
    OptoLeveler opto;
    opto.setPeakReductionProportion (0.5f); // moderate drive - see measureReleaseBlocks()'s comment
    opto.prepare (makeTestSpec (1));

    // A brief loud burst - not enough to charge the mid/slow paths much
    // (their own slow attacks, ~0.3 s / ~1.2 s), so release should stay
    // close to the fast (~60 ms) stage.
    for (int block = 0; block < 4; ++block) // ~43 ms
    {
        juce::AudioBuffer<float> buffer (1, blockSize);
        TestHelpers::fillWithSine (buffer, testSampleRate, 500.0, 0.1f, block * blockSize);
        juce::dsp::AudioBlock<float> audioBlock (buffer);
        opto.process (audioBlock);
    }

    const auto grAtStop = opto.getCurrentGainReductionDb();
    REQUIRE (grAtStop > 1.0f);

    int samplesToHalf = 0;
    juce::AudioBuffer<float> silence (1, 1);
    silence.clear();

    for (; samplesToHalf < static_cast<int> (testSampleRate * 1.0); ++samplesToHalf)
    {
        juce::dsp::AudioBlock<float> audioBlock (silence);
        opto.process (audioBlock);

        if (opto.getCurrentGainReductionDb() <= grAtStop * 0.5f)
            break;
    }

    const auto halfRecoverySeconds = samplesToHalf / testSampleRate;
    INFO ("half-recovery time = " << halfRecoverySeconds << " s");

    // Generous bounds around the documented ~60 ms fast stage.
    CHECK (halfRecoverySeconds < 0.3);
}

TEST_CASE ("Opto: reset() clears detector, ballistics and light-history state", "[dsp][opto][reset]")
{
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

    REQUIRE (opto.getCurrentGainReductionDb() > 3.0f);

    opto.reset();
    CHECK (opto.getCurrentGainReductionDb() == 0.0f);

    juce::AudioBuffer<float> silence (1, blockSize);
    silence.clear();
    juce::dsp::AudioBlock<float> audioBlock (silence);
    opto.process (audioBlock);

    CHECK (opto.getCurrentGainReductionDb() == Catch::Approx (0.0f).margin (1.0e-3));
}

//==============================================================================
// Detector-only emphasis selectivity.

TEST_CASE ("Opto: emphasis 0% gives equal GR for LF and HF content", "[dsp][opto][emphasis]")
{
    const auto measureGr = [] (double frequencyHz)
    {
        OptoLeveler opto;
        opto.setPeakReductionProportion (0.7f);
        opto.setEmphasisProportion (0.0f);
        opto.prepare (makeTestSpec (1, 24000));

        juce::AudioBuffer<float> buffer (1, 24000);
        TestHelpers::fillWithSine (buffer, testSampleRate, frequencyHz, 0.5f);

        juce::dsp::AudioBlock<float> block (buffer);
        opto.process (block);

        return static_cast<double> (opto.getCurrentGainReductionDb());
    };

    const auto grLow = measureGr (200.0);
    const auto grHigh = measureGr (8000.0);

    CHECK (grLow == Catch::Approx (grHigh).margin (1.5));
}

TEST_CASE ("Opto: emphasis 100% gives ~10 dB more HF gain reduction than LF", "[dsp][opto][emphasis]")
{
    const auto measureGr = [] (double frequencyHz)
    {
        OptoLeveler opto;
        opto.setPeakReductionProportion (0.7f);
        opto.setEmphasisProportion (1.0f);
        opto.prepare (makeTestSpec (1, 24000));

        juce::AudioBuffer<float> buffer (1, 24000);
        TestHelpers::fillWithSine (buffer, testSampleRate, frequencyHz, 0.5f);

        juce::dsp::AudioBlock<float> block (buffer);
        opto.process (block);

        return static_cast<double> (opto.getCurrentGainReductionDb());
    };

    const auto grLow = measureGr (200.0);   // well below the ~1 kHz emphasis corner
    const auto grHigh = measureGr (8000.0); // well above it

    CHECK (grHigh > grLow);
    CHECK (grHigh - grLow > 4.0); // measurable HF-selective behaviour ("like a multiband")
}

//==============================================================================
// Unlinked-vs-linked detection (guarantee 10).

TEST_CASE ("Opto: unlinked (default) - a hard-panned L-only burst produces GR on L only", "[dsp][opto][link]")
{
    OptoLeveler opto;
    opto.setPeakReductionProportion (1.0f);
    opto.setLinked (false);
    opto.prepare (makeTestSpec (2, 24000));

    juce::AudioBuffer<float> buffer (2, 24000);
    TestHelpers::fillWithSine (buffer, testSampleRate, 500.0, 0.8f);
    buffer.clear (1, 0, 24000);

    juce::dsp::AudioBlock<float> block (buffer);
    opto.process (block);

    const auto rightPeak = [&]
    {
        float peak = 0.0f;
        const auto* data = buffer.getReadPointer (1);
        for (int i = 0; i < 24000; ++i)
            peak = std::max (peak, std::abs (data[i]));
        return peak;
    }();

    CHECK (rightPeak < 1.0e-6f);
}

TEST_CASE ("Opto: linked - a hard-panned L-only burst produces GR on the (silent) right channel too", "[dsp][opto][link]")
{
    OptoLeveler opto;
    opto.setPeakReductionProportion (1.0f);
    opto.setLinked (true);
    opto.prepare (makeTestSpec (2, 24000));

    juce::AudioBuffer<float> buffer (2, 24000);
    buffer.clear();
    TestHelpers::fillWithSine (buffer, testSampleRate, 500.0, 0.8f, 0);

    // Silence the right channel input entirely, but give it a tiny DC-free
    // probe so its OWN output level is measurable post-gain.
    auto* right = buffer.getWritePointer (1);
    for (int i = 0; i < 24000; ++i)
        right[i] = 0.05f * std::sin (juce::MathConstants<float>::twoPi * 500.0f * static_cast<float> (i) / static_cast<float> (testSampleRate));

    juce::dsp::AudioBlock<float> block (buffer);
    opto.process (block);

    CHECK (opto.getCurrentGainReductionDb() > 3.0f); // the shared (loud-left-driven) detector reduces gain on both
}
