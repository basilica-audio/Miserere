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

//==============================================================================
// 6.2 - decramping: the matched 12 kHz shelf's magnitude at 20 kHz stays
// within +-1 dB of the analog prototype at 44.1 kHz (the bilinear/RBJ shelf
// is pinned to its plateau at Nyquist and overshoots the analog curve in
// the top octave).

TEST_CASE ("Console EQ: matched 12 kHz shelf tracks the analog prototype at 20 kHz within 1 dB (44.1 k)", "[dsp][consoleeq][matched][decramp]")
{
    constexpr double fs = 44100.0;
    constexpr float shelfGainDb = 10.0f;

    // Analog RBJ high-shelf prototype magnitude at 20 kHz:
    // |H(j*x)|^2 = A^2 * ((1 - A*x^2)^2 + (sqrt(A)/Q * x)^2)
    //                  / ((A - x^2)^2 + (sqrt(A)/Q * x)^2),  x = f/fc.
    const auto analogTargetDb = [&]
    {
        const auto A = std::pow (10.0, shelfGainDb / 40.0);
        const auto Q = 0.5;
        const auto x = 20000.0 / 12000.0;
        const auto bw = std::sqrt (A) / Q * x;
        const auto num = (1.0 - A * x * x) * (1.0 - A * x * x) + bw * bw;
        const auto den = (A - x * x) * (A - x * x) + bw * bw;
        return 10.0 * std::log10 (A * A * num / den);
    }();

    ConsoleEq eq;
    eq.setHighGainDb (shelfGainDb);

    juce::dsp::ProcessSpec spec = makeTestSpec (1);
    spec.sampleRate = fs;
    eq.prepare (spec);

    juce::AudioBuffer<float> reference (1, testBlockSize);
    TestHelpers::fillWithSine (reference, fs, 20000.0, 0.25f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    eq.process (block);

    const auto measuredDb = juce::Decibels::gainToDecibels (
        TestHelpers::tailRms (processed, settleSamples) / TestHelpers::tailRms (reference, settleSamples));

    INFO ("measured " << measuredDb << " dB at 20 kHz, analog target " << analogTargetDb << " dB");
    CHECK (measuredDb == Catch::Approx (analogTargetDb).margin (1.0));
}

//==============================================================================
// 6.6 - flux-domain iron term (brief F8).

namespace
{
    // Harmonic level (dB re fundamental) of the driven output.
    double harmonicDbr (ConsoleEq& eq, double frequencyHz, float amplitude, int harmonic)
    {
        eq.prepare (makeTestSpec (1));

        juce::AudioBuffer<float> buffer (1, testBlockSize);
        TestHelpers::fillWithSine (buffer, testSampleRate, frequencyHz, amplitude);

        juce::dsp::AudioBlock<float> block (buffer);
        eq.process (block);

        const auto cyclesPerWindow = std::floor (frequencyHz * (testBlockSize - settleSamples) / testSampleRate);
        const auto windowSamples = static_cast<int> (cyclesPerWindow * testSampleRate / frequencyHz);

        const auto fundamental = TestHelpers::fftBinMagnitude (buffer, 0, settleSamples, windowSamples, testSampleRate, frequencyHz);
        const auto partial = TestHelpers::fftBinMagnitude (buffer, 0, settleSamples, windowSamples, testSampleRate, frequencyHz * harmonic);

        return 20.0 * std::log10 (juce::jmax (1.0e-12, partial / juce::jmax (1.0e-12, fundamental)));
    }
}

TEST_CASE ("Console EQ iron: H3 rises toward LF by >= 8 dB from 100 Hz to 50 Hz (flux ~ V/f)", "[dsp][consoleeq][iron]")
{
    // Fixed drive giving ~1% THD-class distortion at 100 Hz.
    constexpr float driveDb = 18.0f;
    constexpr float amplitude = 0.25f;

    ConsoleEq at100;
    at100.setDriveDb (driveDb);
    const auto h3At100 = harmonicDbr (at100, 100.0, amplitude, 3);

    ConsoleEq at50;
    at50.setDriveDb (driveDb);
    const auto h3At50 = harmonicDbr (at50, 50.0, amplitude, 3);

    INFO ("H3 @50 Hz = " << h3At50 << " dBr, @100 Hz = " << h3At100 << " dBr");
    CHECK (h3At50 - h3At100 >= 8.0);
}

TEST_CASE ("Console EQ iron: H2/H3 sits in the measured 1073 window [0.5, 1.1] at hot drive", "[dsp][consoleeq][iron][h2h3]")
{
    ConsoleEq eq;
    eq.setDriveDb (24.0f);

    const auto h2 = harmonicDbr (eq, 100.0, 0.25f, 2);

    ConsoleEq eq3;
    eq3.setDriveDb (24.0f);
    const auto h3 = harmonicDbr (eq3, 100.0, 0.25f, 3);

    const auto ratio = std::pow (10.0, (h2 - h3) / 20.0);
    INFO ("H2 = " << h2 << " dBr, H3 = " << h3 << " dBr, H2/H3 = " << ratio);
    CHECK (ratio >= 0.5);
    CHECK (ratio <= 1.1);
}

TEST_CASE ("Console EQ iron: near-zero drive nulls against the EQ-only path < -80 dBFS (inverse pairing)", "[dsp][consoleeq][iron][null]")
{
    // The iron delta is the differentiated ADAA residual of a curve whose
    // drive FACTOR goes to 0 as drive -> 0 dB - the linear signal never
    // passes through the integrator/differentiator pair, so the chain
    // approaches the EQ-only path structurally. Probe at a tiny drive with
    // program-level input.
    ConsoleEq driven;
    driven.setDriveDb (0.05f);
    driven.prepare (makeTestSpec (1));

    ConsoleEq reference;
    reference.setDriveDb (0.0f);
    reference.prepare (makeTestSpec (1));

    // Linear-region probe: -60 dBFS. At this level every intentional
    // nonlinearity (odd residual, iron residual) vanishes as amplitude^3;
    // any remaining energy above the bar would be a LINEAR artefact leaking
    // from the flux path - i.e. a broken integrator/differentiator pairing.
    juce::AudioBuffer<float> input (1, testBlockSize);
    TestHelpers::fillWithSine (input, testSampleRate, 100.0, 1.0e-3f);

    juce::AudioBuffer<float> processedDriven;
    processedDriven.makeCopyOf (input);
    juce::dsp::AudioBlock<float> drivenBlock (processedDriven);
    driven.process (drivenBlock);

    juce::AudioBuffer<float> processedReference;
    processedReference.makeCopyOf (input);
    juce::dsp::AudioBlock<float> referenceBlock (processedReference);
    reference.process (referenceBlock);

    // The odd tanh's small-signal gain at 0.05 dB drive deviates from
    // unity by << 0.01 dB; the assertion measures the residual against the
    // straight EQ-only render.
    const auto residualDb = TestHelpers::maxDifferenceDbfs (processedDriven, processedReference);
    INFO ("residual = " << residualDb << " dBFS");
    CHECK (residualDb < -80.0);
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
