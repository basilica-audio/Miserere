#include "dsp/OptoCell.h"
#include "dsp/OptoLeveler.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

// The SANDWICH bus's Opto Leveler, v0.5.0 T4B carrier-physics engine
// (brief F4 / section 6.5): two-stage release, trap-state memory, GR
// ripple from the unrectified EL detection, detector-only emphasis, the
// deliberate-voicing continuity guardrail against the recorded v0.4.0
// static curve, and linked/unlinked detection.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int blockSize = 480; // 10 ms

    juce::dsp::ProcessSpec makeTestSpec (int numChannels, int maxBlockSize = blockSize)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (maxBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    // Drives `opto` with a 1 kHz sine at `amplitude` for `seconds`,
    // returning the settled GR meter value.
    float driveToSettledGr (OptoLeveler& opto, float amplitude, double seconds, double frequencyHz = 1000.0)
    {
        const auto numBlocks = juce::jmax (1, static_cast<int> (seconds * testSampleRate / blockSize));

        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buffer (1, blockSize);
            TestHelpers::fillWithSine (buffer, testSampleRate, frequencyHz, amplitude, b * blockSize);
            juce::dsp::AudioBlock<float> block (buffer);
            opto.process (block);
        }

        return opto.getCurrentGainReductionDb();
    }

    // GR trace during release (quiet followers at -60 dB of the previous
    // level keep the loop numerically alive without recharging it).
    std::vector<float> releaseTrace (OptoLeveler& opto, double seconds)
    {
        std::vector<float> trace;
        const auto numBlocks = static_cast<int> (seconds * testSampleRate / blockSize);
        trace.reserve (static_cast<size_t> (numBlocks));

        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> silence (1, blockSize);
            silence.clear();
            juce::dsp::AudioBlock<float> block (silence);
            opto.process (block);
            trace.push_back (opto.getCurrentGainReductionDb());
        }

        return trace;
    }

    double timeToRecoverBelow (const std::vector<float>& trace, float thresholdDb)
    {
        for (size_t b = 0; b < trace.size(); ++b)
            if (trace[b] <= thresholdDb)
                return static_cast<double> (b + 1) * blockSize / testSampleRate;

        return static_cast<double> (trace.size()) * blockSize / testSampleRate;
    }

    // Amplitude that produces `targetGrDb` +- tolerance of settled GR at
    // the given frequency (bisection on level).
    float amplitudeForGr (float peakRed, double frequencyHz, float targetGrDb, float emphasis)
    {
        auto loDb = -60.0f;
        auto hiDb = 0.0f;

        for (int iteration = 0; iteration < 9; ++iteration)
        {
            const auto midDb = 0.5f * (loDb + hiDb);

            OptoLeveler opto;
            opto.setPeakReductionProportion (peakRed);
            opto.setEmphasisProportion (emphasis);
            opto.prepare (makeTestSpec (1));

            const auto gr = driveToSettledGr (opto, juce::Decibels::decibelsToGain (midDb), 3.0, frequencyHz);

            if (gr < targetGrDb)
                loDb = midDb;
            else
                hiDb = midDb;
        }

        return juce::Decibels::decibelsToGain (0.5f * (loDb + hiDb));
    }
}

//==============================================================================
// 6.5a - two-stage release: hold ~10 dB GR for 1 s, drop the input 30 dB:
// 50 % recovery in 30-120 ms AND the last 10 % takes > 400 ms; the
// early/late decay-rate ratio (bi-exponential surrogate) > 8.

TEST_CASE ("Opto: two-stage release - fast 50% stage, seconds-scale tail, tau2/tau1 > 8", "[dsp][opto][release]")
{
    OptoLeveler opto;
    opto.setPeakReductionProportion (0.5f);
    opto.prepare (makeTestSpec (1));

    // Calibrated drive: ~10 dB settled GR at peakRed 50% (-20 dBFS input;
    // the golden static curve puts this at ~9.4 dB GR).
    const auto grAtStop = driveToSettledGr (opto, 0.1f, 1.0);
    REQUIRE (grAtStop > 6.0f);
    REQUIRE (grAtStop < 16.0f);

    const auto trace = releaseTrace (opto, 4.0);

    const auto t50 = timeToRecoverBelow (trace, 0.5f * grAtStop);
    const auto t90 = timeToRecoverBelow (trace, 0.1f * grAtStop);
    const auto t98 = timeToRecoverBelow (trace, 0.02f * grAtStop);

    INFO ("GR0 = " << grAtStop << " dB; t50 = " << t50 * 1e3 << " ms, t90 = " << t90 * 1e3
          << " ms, t98 = " << t98 * 1e3 << " ms");

    CHECK (t50 >= 0.030);
    CHECK (t50 <= 0.120);
    CHECK (t98 - t90 > 0.400); // the last 10 % crawls

    // Early vs late decay rate (log-GR slope over the first 50 ms vs the
    // 0.5-1.5 s window) - the bi-exponential tau2/tau1 surrogate.
    const auto blocksIn = [] (double seconds) { return static_cast<size_t> (seconds * testSampleRate / blockSize); };

    const auto earlySlope = (std::log (juce::jmax (1.0e-3f, trace[blocksIn (0.050)]))
                             - std::log (juce::jmax (1.0e-3f, trace[0])))
                            / 0.050;
    const auto lateSlope = (std::log (juce::jmax (1.0e-4f, trace[blocksIn (1.5)]))
                            - std::log (juce::jmax (1.0e-4f, trace[blocksIn (0.5)])))
                           / 1.0;

    const auto tau1 = -1.0 / juce::jmin (-1.0e-6, static_cast<double> (earlySlope));
    const auto tau2 = -1.0 / juce::jmin (-1.0e-6, static_cast<double> (lateSlope));

    INFO ("tau1 ~ " << tau1 << " s, tau2 ~ " << tau2 << " s, ratio " << tau2 / tau1);
    CHECK (tau2 / tau1 > 8.0);
}

//==============================================================================
// 6.5b - memory: the same GR held 0.5 s vs 10 s -> time-to-90%-recovery
// ratio >= 1.2 (trap-state q_p charges with illumination history).

TEST_CASE ("Opto: release memory - longer GR history releases measurably slower", "[dsp][opto][memory]")
{
    const auto timeTo90 = [] (double holdSeconds)
    {
        OptoLeveler opto;
        opto.setPeakReductionProportion (0.5f);
        opto.prepare (makeTestSpec (1));

        const auto grAtStop = driveToSettledGr (opto, 0.1f, holdSeconds);
        REQUIRE (grAtStop > 5.0f);

        const auto trace = releaseTrace (opto, 6.0);
        return timeToRecoverBelow (trace, 0.1f * grAtStop);
    };

    const auto shortHold = timeTo90 (0.5);
    const auto longHold = timeTo90 (10.0);

    INFO ("t90 after 0.5 s hold = " << shortHold * 1e3 << " ms, after 10 s hold = " << longHold * 1e3 << " ms");
    CHECK (longHold / shortHold >= 1.2);
}

//==============================================================================
// 6.5c - GR ripple: 50 Hz at ~15 dB GR shows H3 enrichment >= 6 dB vs a
// rectified-detector control build (asserts the unrectified EL detection is
// alive). The control replaces the audio-rate EL drive with the research
// file's Tier-B "vactrol heuristic" smoothing network (rectifier + switched
// one-pole, 2 ms toward more GR / 60 ms toward less -
// research-opto-la2a.md section 4) ahead of the same EL law + carrier cell -
// the classic detector the T4B does NOT have. Tier B's documented tradeoff
// is exactly "loses GR ripple harmonics (LF thickening)".

TEST_CASE ("Opto: 50 Hz GR ripple - H3 enrichment >= 6 dB vs rectified-detector control", "[dsp][opto][ripple]")
{
    constexpr double f0 = 50.0;
    constexpr int total = 1 << 17; // ~2.7 s
    constexpr int settle = 1 << 16;

    // Full engine render at ~15 dB GR.
    OptoLeveler opto;
    opto.setPeakReductionProportion (0.65f);
    opto.setEmphasisProportion (0.0f); // flat detector - isolate the EL ripple mechanism
    opto.prepare (makeTestSpec (1, total));

    const auto amplitude = amplitudeForGr (0.65f, f0, 15.0f, 0.0f);

    juce::AudioBuffer<float> buffer (1, total);
    TestHelpers::fillWithSine (buffer, testSampleRate, f0, amplitude);
    juce::dsp::AudioBlock<float> block (buffer);
    opto.process (block);

    const auto grReached = opto.getCurrentGainReductionDb();
    INFO ("engine GR = " << grReached << " dB at amplitude " << amplitude);
    REQUIRE (grReached > 10.0f);

    const auto h3Engine = TestHelpers::fftBinMagnitude (buffer, 0, settle, total - settle, testSampleRate, 3.0 * f0)
                          / juce::jmax (1.0e-12, TestHelpers::fftBinMagnitude (buffer, 0, settle, total - settle, testSampleRate, f0));

    // Control build: same divider/cell, but the EL panel is lit by a
    // RECTIFIED+SMOOTHED envelope (switched one-pole: 2 ms attack, 60 ms
    // release - the Tier-B smoothing network) of the sidechain signal, so
    // the 2f light ripple is gone. Gain-matched to the same mean GR by
    // scaling the sidechain drive.
    const auto renderControlH3 = [&] (double sidechainScale, float& meanGrOut)
    {
        msrr::OptoCell cell;
        cell.reset();

        juce::AudioBuffer<float> control (1, total);
        TestHelpers::fillWithSine (control, testSampleRate, f0, amplitude);
        auto* data = control.getWritePointer (0);

        const double T = 1.0 / testSampleRate;
        const auto attackCoeff = std::exp (-T / 0.002);
        const auto releaseCoeff = std::exp (-T / 0.060);
        double envelope = 0.0;
        double feedback = 0.0;
        double grSum = 0.0;

        // Same constants as the engine (Compress tuple, peakRed 65%).
        const auto sidechainGain = std::pow (10.0, 0.65 * 40.0 / 20.0) * sidechainScale;
        constexpr double rail = 2.5;
        constexpr double b0el = 1.0e17;
        constexpr double kneeB = 17.0;
        constexpr double rs = 9500.0, rl = 100000.0;
        const auto rDark = msrr::OptoCellParams().rDarkOhm;
        const auto rpDark = rDark * rl / (rDark + rl);
        const auto aDark = rpDark / (rs + rpDark);

        for (int n = 0; n < total; ++n)
        {
            const auto driven = rail * std::tanh (sidechainGain * feedback / rail);
            const auto rectified = std::abs (driven); // RECTIFIER
            const auto envCoeff = rectified > envelope ? attackCoeff : releaseCoeff;
            envelope = envCoeff * envelope + (1.0 - envCoeff) * rectified;
            const auto g = juce::jmin (msrr::electroluminance (envelope, b0el, kneeB), 20.0);

            const auto rCell = cell.processSample (g, T);
            const auto rp = rCell * rl / (rCell + rl);
            const auto a = (rp / (rs + rp)) / aDark;

            const auto y = a * static_cast<double> (data[n]);
            data[n] = static_cast<float> (y);
            feedback = y;
            grSum += -20.0 * std::log10 (juce::jmax (1.0e-6, a));
        }

        meanGrOut = static_cast<float> (grSum / total);

        return TestHelpers::fftBinMagnitude (control, 0, settle, total - settle, testSampleRate, 3.0 * f0)
               / juce::jmax (1.0e-12, TestHelpers::fftBinMagnitude (control, 0, settle, total - settle, testSampleRate, f0));
    };

    // Gain-match the control by bisection on its sidechain scale.
    double loScale = 0.25, hiScale = 16.0;
    float controlGr = 0.0f;
    double h3Control = 0.0;

    for (int iteration = 0; iteration < 10; ++iteration)
    {
        const auto midScale = std::sqrt (loScale * hiScale);
        h3Control = renderControlH3 (midScale, controlGr);

        if (std::abs (controlGr - grReached) < 1.0f)
            break;

        if (controlGr < grReached)
            loScale = midScale;
        else
            hiScale = midScale;
    }

    INFO ("H3/H1 engine = " << h3Engine << ", control = " << h3Control
          << " (control GR " << controlGr << " dB vs engine " << grReached << " dB)");

    REQUIRE (std::abs (controlGr - grReached) < 2.0f);
    CHECK (20.0 * std::log10 (juce::jmax (1.0e-12, h3Engine))
           - 20.0 * std::log10 (juce::jmax (1.0e-12, h3Control)) >= 6.0);
}

//==============================================================================
// 6.5d - emphasis: input level for 6 dB GR at 100 Hz vs 4 kHz.

TEST_CASE ("Opto: emphasis 0% gives near-equal GR sensitivity for LF and HF", "[dsp][opto][emphasis]")
{
    const auto level100 = juce::Decibels::gainToDecibels (amplitudeForGr (0.7f, 100.0, 6.0f, 0.0f));
    const auto level4k = juce::Decibels::gainToDecibels (amplitudeForGr (0.7f, 4000.0, 6.0f, 0.0f));

    INFO ("level for 6 dB GR: 100 Hz = " << level100 << " dBFS, 4 kHz = " << level4k << " dBFS");
    CHECK (std::abs (level100 - level4k) <= 1.5);
}

TEST_CASE ("Opto: emphasis 100% needs 8-12 dB more LF level for the same GR", "[dsp][opto][emphasis]")
{
    const auto level100 = juce::Decibels::gainToDecibels (amplitudeForGr (0.7f, 100.0, 6.0f, 1.0f));
    const auto level4k = juce::Decibels::gainToDecibels (amplitudeForGr (0.7f, 4000.0, 6.0f, 1.0f));

    const auto delta = level100 - level4k;
    INFO ("level for 6 dB GR: 100 Hz = " << level100 << " dBFS, 4 kHz = " << level4k << " dBFS, delta = " << delta);
    CHECK (delta >= 8.0);
    CHECK (delta <= 12.0);
}

//==============================================================================
// 6.5e - static-curve continuity vs v0.4.0 (the deliberate-voicing
// guardrail): 1 kHz sweep -40..0 dBFS at peakRed 50%, Compress:
// steady-state GR within +-1.5 dB of the recorded v0.4.0 golden curve
// (captured from the shipping v0.4.0 build, emphasis at its default).

TEST_CASE ("Opto: static curve stays within +-1.5 dB of the v0.4.0 golden curve at moderate settings", "[dsp][opto][golden]")
{
    struct GoldenPoint
    {
        int levelDb;
        float goldenGrDb;
    };

    // Captured from v0.4.0 (commit 7a95272), 1 kHz, peakRed 50%, Compress,
    // default emphasis, 6 s settle per level.
    static constexpr GoldenPoint golden[] = {
        { -40, 0.0000f }, { -36, 0.0000f }, { -32, 0.5798f }, { -28, 3.2461f },
        { -24, 5.9132f }, { -20, 9.4218f }, { -16, 13.2612f }, { -12, 17.1016f },
        { -8, 20.9411f }, { -4, 24.7822f }, { 0, 28.6213f },
    };

    for (const auto& point : golden)
    {
        OptoLeveler opto;
        opto.setPeakReductionProportion (0.5f);
        opto.setLimitEnabled (false);
        opto.prepare (makeTestSpec (1));

        const auto amplitude = juce::Decibels::decibelsToGain (static_cast<float> (point.levelDb));
        const auto gr = driveToSettledGr (opto, amplitude, 6.0);

        INFO ("level " << point.levelDb << " dBFS: GR = " << gr << " dB, golden = " << point.goldenGrDb << " dB");
        CHECK (gr == Catch::Approx (point.goldenGrDb).margin (1.5));
    }
}

//==============================================================================
// Limit switch: a higher-ratio tuple, not a different topology.

TEST_CASE ("Opto: Limit engages deeper GR than Compress at the same drive", "[dsp][opto][limit]")
{
    const auto measure = [] (bool limit)
    {
        OptoLeveler opto;
        opto.setPeakReductionProportion (0.5f);
        opto.setLimitEnabled (limit);
        opto.prepare (makeTestSpec (1));
        return driveToSettledGr (opto, 0.1f, 2.0);
    };

    const auto compressGr = measure (false);
    const auto limitGr = measure (true);

    INFO ("GR: Compress = " << compressGr << " dB, Limit = " << limitGr << " dB");
    CHECK (limitGr > compressGr + 1.0f);
}

//==============================================================================
// Housekeeping.

TEST_CASE ("Opto: reset() restores the dark equilibrium", "[dsp][opto][reset]")
{
    OptoLeveler opto;
    opto.setPeakReductionProportion (1.0f);
    opto.prepare (makeTestSpec (1));

    (void) driveToSettledGr (opto, 0.5f, 2.0);
    REQUIRE (opto.getCurrentGainReductionDb() > 3.0f);

    opto.reset();
    CHECK (opto.getCurrentGainReductionDb() == 0.0f);

    juce::AudioBuffer<float> silence (1, blockSize);
    silence.clear();
    juce::dsp::AudioBlock<float> block (silence);
    opto.process (block);

    CHECK (opto.getCurrentGainReductionDb() == Catch::Approx (0.0f).margin (1.0e-3));
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

    float rightPeak = 0.0f;
    const auto* data = buffer.getReadPointer (1);
    for (int i = 0; i < 24000; ++i)
        rightPeak = std::max (rightPeak, std::abs (data[i]));

    CHECK (rightPeak < 1.0e-6f);
}

TEST_CASE ("Opto: linked - a loud left channel reduces the quiet right channel too", "[dsp][opto][link]")
{
    const auto measureRightGainDb = [] (bool linked)
    {
        OptoLeveler opto;
        opto.setPeakReductionProportion (1.0f);
        opto.setLinked (linked);
        opto.prepare (makeTestSpec (2, 48000));

        juce::AudioBuffer<float> buffer (2, 48000);
        buffer.clear();

        auto* left = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);
        for (int i = 0; i < 48000; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * 500.0 * i / testSampleRate;
            left[i] = 0.8f * static_cast<float> (std::sin (phase));
            right[i] = 0.01f * static_cast<float> (std::sin (phase));
        }

        juce::dsp::AudioBlock<float> block (buffer);
        opto.process (block);

        const auto rightInputRms = 0.01 / juce::MathConstants<double>::sqrt2;
        const auto rightOutputRms = TestHelpers::tailRms (
            juce::AudioBuffer<float> (buffer.getArrayOfWritePointers() + 1, 1, 0, 48000), 24000);
        return juce::Decibels::gainToDecibels (rightOutputRms / rightInputRms);
    };

    const auto unlinkedGain = measureRightGainDb (false);
    const auto linkedGain = measureRightGainDb (true);

    INFO ("right-channel gain: unlinked = " << unlinkedGain << " dB, linked = " << linkedGain << " dB");
    CHECK (linkedGain < unlinkedGain - 3.0);
}
