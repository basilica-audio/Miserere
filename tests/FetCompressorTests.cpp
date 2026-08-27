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

TEST_CASE ("Direct FET: static curve at 4:1 within +-0.3 dB", "[dsp][fet][static-curve]")
{
    FetCompressor comp;
    comp.setRatio (4.0f);
    comp.setThresholdDb (-30.0f);
    comp.setAttackMs (1.0f);
    comp.setReleaseMs (100.0f);
    comp.setMakeupDb (0.0f);

    const auto measured = measureGainChangeDb (comp, 1000.0, 0.5f);
    const auto expected = -expectedGainReductionDb (0.5f, -30.0f, 4.0f);

    // Derived model-error bound: the squared-signal envelope under-reads the
    // sine's true peak. Fixed-point of the attack/release balance (attack
    // tau_a = 1 ms, release tau_r = 100 ms, ripple period T = 0.5 ms at
    // 1 kHz): d = dr * e^-k / (1 - e^-k) <= dr / k with dr <= A^2 * T/tau_r
    // and k = 2*sqrt(d/A^2) / (w*tau_a), giving (d/A^2)^1.5 <= w*tau_a*T /
    // (2*tau_r) => under-read <= 0.28 dB, so the measured curve can sit at
    // most 0.28 * (1 - 1/ratio) ~ 0.25 dB above the peak-based model (and
    // never below it). Simulated actual: +0.16 dB (4:1) / +0.19 dB (8:1).
    CHECK (measured == Catch::Approx (expected).margin (0.3));
}

TEST_CASE ("Direct FET: static curve at 8:1 within +-0.3 dB", "[dsp][fet][static-curve]")
{
    FetCompressor comp;
    comp.setRatio (8.0f);
    comp.setThresholdDb (-30.0f);
    comp.setAttackMs (1.0f);
    comp.setReleaseMs (100.0f);
    comp.setMakeupDb (0.0f);

    const auto measured = measureGainChangeDb (comp, 1000.0, 0.5f);
    const auto expected = -expectedGainReductionDb (0.5f, -30.0f, 8.0f);

    // Same derived bound as the 4:1 case above: model error in [0, 0.28 dB]
    // scaled by (1 - 1/8), simulated actual +0.19 dB.
    CHECK (measured == Catch::Approx (expected).margin (0.3));
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
    // Derived: the meter publishes the block's PEAK reduction; the one-pole
    // envelope converges monotonically from below, so that peak is the
    // settled ripple-band top, while -measured is the RMS-weighted band
    // average. Their gap is bounded by the band width, i.e. the release
    // decay across one ripple period: 10*log10(e) * 0.5 ms / 100 ms
    // ~ 0.022 dB. 0.1 covers it with 4x headroom (simulated gap: 0.004 dB).
    CHECK (static_cast<double> (comp.getCurrentGainReductionDb()) == Catch::Approx (-measured).margin (0.1));
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

//==============================================================================
// Issue #20 - the Character switch (FET / VCA / Tube Mu). FET is index 0 and
// all-zeros/unity by construction, so every test above (which never calls
// setCharacter) doubles as the FET-is-the-legacy-voicing regression suite.

TEST_CASE ("Direct FET: VCA character below the knee is a bit-exact null (transparency claim)", "[dsp][fet][character][null]")
{
    FetCompressor comp;
    comp.setCharacter (FetCompressor::Character::vca);
    comp.setRatio (4.0f);
    comp.setThresholdDb (-18.0f);
    comp.setMakeupDb (0.0f);
    comp.prepare (makeTestSpec (2));

    // VCA knee = 6 dB: reduction is an exact clamped 0 below
    // threshold - 3 dB. -26 dBFS peak sits 5 dB under that edge.
    juce::AudioBuffer<float> reference (2, 8192);
    TestHelpers::fillWithSine (reference, testSampleRate, 440.0, 0.05f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    comp.process (block);

    CHECK (TestHelpers::maxDifferenceDbfs (processed, reference) <= -200.0);
    CHECK (comp.getCurrentGainReductionDb() == 0.0f);
}

TEST_CASE ("Direct FET: knee ordering near threshold - Mu compresses first, FET last", "[dsp][fet][character][knee]")
{
    const auto measureGr = [] (FetCompressor::Character character, float amplitude)
    {
        FetCompressor comp;
        comp.setCharacter (character);
        comp.setRatio (4.0f);
        comp.setThresholdDb (-20.0f);
        comp.setAttackMs (1.0f);
        comp.setReleaseMs (100.0f);
        comp.setMakeupDb (0.0f);
        (void) measureGainChangeDb (comp, 1000.0, amplitude);
        return comp.getCurrentGainReductionDb();
    };

    // -24 dBFS peak = 4 dB below threshold: inside Mu's 12 dB knee, outside
    // VCA's 6 dB knee, and outside FET's hard knee.
    const auto below4Amplitude = juce::Decibels::decibelsToGain (-24.0f);
    CHECK (measureGr (FetCompressor::Character::fet, below4Amplitude) == 0.0f);
    CHECK (measureGr (FetCompressor::Character::vca, below4Amplitude) == 0.0f);
    CHECK (measureGr (FetCompressor::Character::tubeMu, below4Amplitude) > 0.05f);

    // -22 dBFS peak = 2 dB below threshold: inside VCA's knee too, FET
    // still exactly clean.
    const auto below2Amplitude = juce::Decibels::decibelsToGain (-22.0f);
    CHECK (measureGr (FetCompressor::Character::fet, below2Amplitude) == 0.0f);
    CHECK (measureGr (FetCompressor::Character::vca, below2Amplitude) > 0.02f);
}

TEST_CASE ("Direct FET: Tube Mu adds GR-gated 2nd harmonic; FET and VCA stay clean", "[dsp][fet][character][harmonics]")
{
    const auto measureH2Ratio = [] (FetCompressor::Character character)
    {
        FetCompressor comp;
        comp.setCharacter (character);
        comp.setRatio (4.0f);
        comp.setThresholdDb (-20.0f);
        comp.setAttackMs (1.0f);
        comp.setReleaseMs (100.0f);
        comp.setMakeupDb (0.0f);
        comp.prepare (makeTestSpec (1));

        juce::AudioBuffer<float> buffer (1, testBlockSize);
        TestHelpers::fillWithSine (buffer, testSampleRate, 1000.0, 0.9f); // ~19 dB over threshold -> deep GR

        juce::dsp::AudioBlock<float> block (buffer);
        comp.process (block);

        const auto fundamental = TestHelpers::fftBinMagnitude (buffer, 0, settleSamples,
                                                               testBlockSize - settleSamples, testSampleRate, 1000.0);
        const auto second = TestHelpers::fftBinMagnitude (buffer, 0, settleSamples,
                                                          testBlockSize - settleSamples, testSampleRate, 2000.0);
        return second / juce::jmax (1.0e-12, fundamental);
    };

    const auto fetH2 = measureH2Ratio (FetCompressor::Character::fet);
    const auto vcaH2 = measureH2Ratio (FetCompressor::Character::vca);
    const auto muH2 = measureH2Ratio (FetCompressor::Character::tubeMu);

    INFO ("H2/H1: FET = " << fetH2 << ", VCA = " << vcaH2 << ", Tube Mu = " << muH2);
    CHECK (muH2 > 0.01);       // >= 1% second harmonic under deep GR
    CHECK (fetH2 < 0.002);
    CHECK (vcaH2 < 0.002);
    CHECK (muH2 > fetH2 * 5.0);
}

TEST_CASE ("Direct FET: Tube Mu's attack is measurably slower than FET's at the same dial", "[dsp][fet][character][ballistics]")
{
    const auto earlyGr = [] (FetCompressor::Character character)
    {
        FetCompressor comp;
        comp.setCharacter (character);
        comp.setRatio (8.0f);
        comp.setThresholdDb (-30.0f);
        comp.setAttackMs (8.0f); // the layout default
        comp.setReleaseMs (200.0f);
        comp.prepare (makeTestSpec (1));

        // 5 ms burst well above threshold + both knees: the knee no longer
        // differentiates the characters up here, only the timing does.
        juce::AudioBuffer<float> burst (1, 240);
        TestHelpers::fillWithSine (burst, testSampleRate, 1000.0, 0.5f);
        juce::dsp::AudioBlock<float> block (burst);
        comp.process (block);

        return comp.getCurrentGainReductionDb();
    };

    const auto fetEarly = earlyGr (FetCompressor::Character::fet);
    const auto muEarly = earlyGr (FetCompressor::Character::tubeMu);

    INFO ("GR after 5 ms: FET = " << fetEarly << " dB, Tube Mu = " << muEarly << " dB");
    REQUIRE (fetEarly > 1.0f);
    CHECK (muEarly < fetEarly - 1.0f);
}

TEST_CASE ("Direct FET: switching character mid-stream ramps instead of stepping", "[dsp][fet][character][automation]")
{
    constexpr int blockSize = 480;
    constexpr int numBlocks = 60; // 600 ms
    constexpr int switchBlock = 30;

    const auto render = [&] (bool switchCharacter)
    {
        FetCompressor comp;
        comp.setRatio (4.0f);
        comp.setThresholdDb (-20.0f);
        comp.setAttackMs (8.0f);
        comp.setReleaseMs (200.0f);
        comp.setMakeupDb (0.0f);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = blockSize;
        spec.numChannels = 1;
        comp.prepare (spec);

        juce::AudioBuffer<float> output (1, blockSize * numBlocks);

        for (int b = 0; b < numBlocks; ++b)
        {
            if (switchCharacter && b == switchBlock)
                comp.setCharacter (FetCompressor::Character::tubeMu);

            juce::AudioBuffer<float> chunk (1, blockSize);
            TestHelpers::fillWithSine (chunk, testSampleRate, 1000.0, 0.35f, b * blockSize);
            juce::dsp::AudioBlock<float> block (chunk);
            comp.process (block);

            output.copyFrom (0, b * blockSize, chunk, 0, 0, blockSize);
        }

        return output;
    };

    const auto maxStepFrom = [&] (const juce::AudioBuffer<float>& buffer, int fromSample)
    {
        const auto* data = buffer.getReadPointer (0);
        float maxStep = 0.0f;

        for (int i = juce::jmax (1, fromSample); i < buffer.getNumSamples(); ++i)
            maxStep = juce::jmax (maxStep, std::abs (data[i] - data[i - 1]));

        return maxStep;
    };

    const auto staticRender = render (false);
    const auto switchedRender = render (true);

    // The switch must not add a discontinuity beyond the sine's own
    // sample-to-sample slope (50 ms smoothed knee/colour ramps).
    const auto staticWorst = maxStepFrom (staticRender, blockSize * (switchBlock - 1));
    const auto switchedWorst = maxStepFrom (switchedRender, blockSize * (switchBlock - 1));

    INFO ("max per-sample step: static = " << staticWorst << ", switched = " << switchedWorst);
    CHECK (switchedWorst <= staticWorst * 1.5f);
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
