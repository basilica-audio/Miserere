#include "dsp/DeEsser.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// M1 guarantee 5: band selectivity - the sibilance band is reduced >= 6 dB
// at a heavy setting while 1 kHz content stays untouched (+-0.5 dB).
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

    double measureGainChangeDb (DeEsser& deEsser, double frequencyHz, float amplitude)
    {
        deEsser.prepare (makeTestSpec (1));

        juce::AudioBuffer<float> reference (1, testBlockSize);
        TestHelpers::fillWithSine (reference, testSampleRate, frequencyHz, amplitude);

        juce::AudioBuffer<float> processed;
        processed.makeCopyOf (reference);

        juce::dsp::AudioBlock<float> block (processed);
        deEsser.process (block);

        const auto inputRms = TestHelpers::tailRms (reference, settleSamples);
        const auto outputRms = TestHelpers::tailRms (processed, settleSamples);

        REQUIRE (inputRms > 0.0);
        return juce::Decibels::gainToDecibels (outputRms / inputRms);
    }
}

TEST_CASE ("De-esser: 8 kHz sibilance band reduced >= 6 dB at a heavy setting", "[dsp][deesser][band]")
{
    DeEsser deEsser;
    deEsser.setEnabled (true);
    deEsser.setFrequencyHz (8000.0f);
    deEsser.setThresholdDb (-30.0f); // heavy: a -9 dB RMS tone overshoots by ~21 dB, far past the 10 dB clamp

    const auto gainChangeDb = measureGainChangeDb (deEsser, 8000.0, 0.5f);

    CHECK (gainChangeDb <= -6.0);
}

TEST_CASE ("De-esser: 1 kHz content untouched (+-0.5 dB) at the same heavy setting", "[dsp][deesser][band]")
{
    DeEsser deEsser;
    deEsser.setEnabled (true);
    deEsser.setFrequencyHz (8000.0f);
    deEsser.setThresholdDb (-30.0f);

    const auto gainChangeDb = measureGainChangeDb (deEsser, 1000.0, 0.5f);

    CHECK (gainChangeDb == Catch::Approx (0.0).margin (0.5));
}

TEST_CASE ("De-esser: disabled is a bit-exact bypass", "[dsp][deesser][null]")
{
    DeEsser deEsser;
    deEsser.setEnabled (false);
    deEsser.setFrequencyHz (8000.0f);
    deEsser.setThresholdDb (-40.0f);
    deEsser.prepare (makeTestSpec (2));

    juce::AudioBuffer<float> reference (2, 4096);
    TestHelpers::fillWithSine (reference, testSampleRate, 8000.0, 0.9f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    deEsser.process (block);

    CHECK (TestHelpers::maxDifferenceDbfs (processed, reference) <= -200.0);
}

TEST_CASE ("De-esser: gain reduction never exceeds the 10 dB maximum", "[dsp][deesser][clamp]")
{
    DeEsser deEsser;
    deEsser.setEnabled (true);
    deEsser.setFrequencyHz (8000.0f);
    deEsser.setThresholdDb (-40.0f); // minimum threshold: as heavy as it gets

    const auto gainChangeDb = measureGainChangeDb (deEsser, 8000.0, 0.9f);

    // The band itself can never lose more than 10 dB (brief), so the
    // overall level change of a pure in-band tone is bounded by it too.
    CHECK (gainChangeDb >= -10.5);
    CHECK (deEsser.getCurrentGainReductionDb() <= 10.0f + 1.0e-3f);
}

TEST_CASE ("De-esser: detection band follows the tunable frequency", "[dsp][deesser][band]")
{
    // Tuned to 4 kHz, an 8 kHz tone sits an octave above the detector band
    // and must be reduced far less than when the de-esser is tuned onto it.
    DeEsser tunedAway;
    tunedAway.setEnabled (true);
    tunedAway.setFrequencyHz (4000.0f);
    tunedAway.setThresholdDb (-30.0f);

    DeEsser tunedOn;
    tunedOn.setEnabled (true);
    tunedOn.setFrequencyHz (8000.0f);
    tunedOn.setThresholdDb (-30.0f);

    const auto reductionTunedAway = measureGainChangeDb (tunedAway, 8000.0, 0.5f);
    const auto reductionTunedOn = measureGainChangeDb (tunedOn, 8000.0, 0.5f);

    CHECK (reductionTunedOn < reductionTunedAway - 3.0);
}
