#include "dsp/FetCompressor.h"
#include "dsp/MiserereEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// M1 guarantees 3 and 6: the Bus A FET comp's static curve at both ratios,
// and the Bus C all-buttons character (20:1 sanity, mid-forward sidechain,
// program-dependent release, finite at max drive).
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

    // Runs a steady sine through the compressor and returns the measured
    // steady-state gain change in dB (output tail RMS vs input tail RMS).
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

    // Expected feed-forward static-curve gain reduction. The FET detector's
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

TEST_CASE ("FET comp: static curve at 4:1 within +-1.5 dB", "[dsp][fet][static-curve]")
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

TEST_CASE ("FET comp: static curve at 8:1 within +-1.5 dB", "[dsp][fet][static-curve]")
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

TEST_CASE ("FET comp: GR metering value matches the measured static reduction", "[dsp][fet][metering]")
{
    FetCompressor comp;
    comp.setRatio (4.0f);
    comp.setThresholdDb (-30.0f);
    comp.setAttackMs (1.0f);
    comp.setReleaseMs (100.0f);

    const auto measured = measureGainChangeDb (comp, 1000.0, 0.5f);

    CHECK (comp.getCurrentGainReductionDb() > 0.0f);
    // The meter reports the block's peak GR; the measured value is a tail
    // RMS average, so allow a slightly wider margin than the static-curve
    // tests use.
    CHECK (static_cast<double> (comp.getCurrentGainReductionDb()) == Catch::Approx (-measured).margin (2.0));
}

TEST_CASE ("FET comp: threshold at maximum (0 dB) is a bit-exact identity", "[dsp][fet][null]")
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

TEST_CASE ("FET comp: makeup gain shifts the output level by its dB value", "[dsp][fet]")
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
// Bus C - all-buttons character (guarantee 6 + the brief's Bus C spec).

TEST_CASE ("Smash: ~20:1 static curve sanity", "[dsp][smash][static-curve]")
{
    FetCompressor smash;
    smash.setRatio (MiserereEngine::smashRatio);
    smash.setThresholdDb (MiserereEngine::smashThresholdDb);
    smash.setAttackMs (0.3f);
    smash.setReleaseMs (100.0f);
    smash.setProgramDependentReleaseEnabled (true);

    // 500 Hz probe: far from the 2 kHz sidechain tilt centre, so the
    // expected-GR formula (which ignores the tilt) stays a fair reference.
    // The tilt still contributes a fraction of a dB at 500 Hz, hence the
    // 2 dB "sanity" margin rather than the Bus A tests' 1.5 dB.
    smash.setSidechainTiltEnabled (true);

    const auto measured = measureGainChangeDb (smash, 500.0, 0.5f);
    const auto expected = -expectedGainReductionDb (0.5f, MiserereEngine::smashThresholdDb, MiserereEngine::smashRatio);

    CHECK (measured == Catch::Approx (expected).margin (2.0));
}

TEST_CASE ("Smash: mid-forward sidechain tilt produces more GR at 2 kHz than at 500 Hz", "[dsp][smash][sidechain]")
{
    const auto measureGr = [] (double frequencyHz)
    {
        FetCompressor smash;
        smash.setRatio (MiserereEngine::smashRatio);
        smash.setThresholdDb (MiserereEngine::smashThresholdDb);
        smash.setAttackMs (0.3f);
        smash.setReleaseMs (100.0f);
        smash.setSidechainTiltEnabled (true);

        (void) measureGainChangeDb (smash, frequencyHz, 0.5f);
        return static_cast<double> (smash.getCurrentGainReductionDb());
    };

    const auto grAt500 = measureGr (500.0);
    const auto grAt2k = measureGr (2000.0);

    // +6 dB of detector tilt at ~20:1 translates to nearly +6 dB more GR.
    CHECK (grAt2k > grAt500 + 3.0);
}

TEST_CASE ("Smash: program-dependent release recovers faster after deep GR", "[dsp][smash][release]")
{
    const auto measureRecoveryBlocks = [] (bool programDependent)
    {
        FetCompressor smash;
        smash.setRatio (MiserereEngine::smashRatio);
        smash.setThresholdDb (MiserereEngine::smashThresholdDb);
        smash.setAttackMs (0.3f);
        smash.setReleaseMs (200.0f);
        smash.setProgramDependentReleaseEnabled (programDependent);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = 512;
        spec.numChannels = 1;
        smash.prepare (spec);

        // Slam it: loud sine for 0.5 s builds deep GR.
        juce::MidiBuffer midi;
        for (int block = 0; block < 47; ++block)
        {
            juce::AudioBuffer<float> buffer (1, 512);
            TestHelpers::fillWithSine (buffer, testSampleRate, 500.0, 0.9f, block * 512);
            juce::dsp::AudioBlock<float> audioBlock (buffer);
            smash.process (audioBlock);
        }

        REQUIRE (smash.getCurrentGainReductionDb() > 6.0f);

        // Feed silence and count blocks until the GR falls below 1 dB.
        int blocksUntilRecovered = 0;
        for (; blocksUntilRecovered < 200; ++blocksUntilRecovered)
        {
            juce::AudioBuffer<float> silence (1, 512);
            silence.clear();
            juce::dsp::AudioBlock<float> audioBlock (silence);
            smash.process (audioBlock);

            if (smash.getCurrentGainReductionDb() < 1.0f)
                break;
        }

        return blocksUntilRecovered;
    };

    const auto withPdr = measureRecoveryBlocks (true);
    const auto withoutPdr = measureRecoveryBlocks (false);

    // The all-buttons shortening must be measurable, not marginal.
    CHECK (withPdr < withoutPdr);
    CHECK (withoutPdr - withPdr >= 2);
}

TEST_CASE ("Smash: output finite and bounded at maximum drive into full-scale input", "[dsp][smash][robustness]")
{
    FetCompressor smash;
    smash.setRatio (MiserereEngine::smashRatio);
    smash.setThresholdDb (MiserereEngine::smashThresholdDb);
    smash.setAttackMs (0.05f);
    smash.setReleaseMs (50.0f);
    smash.setDriveDb (12.0f);
    smash.setOutputTrimDb (12.0f);
    smash.setSidechainTiltEnabled (true);
    smash.setProgramDependentReleaseEnabled (true);
    smash.prepare (makeTestSpec (2));

    juce::AudioBuffer<float> buffer (2, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, 1000.0, 1.0f);

    juce::dsp::AudioBlock<float> block (buffer);
    smash.process (block);

    CHECK (TestHelpers::allSamplesFinite (buffer));
    CHECK (TestHelpers::peakAbsolute (buffer) < 100.0f); // sane bound, not just "finite"
}
