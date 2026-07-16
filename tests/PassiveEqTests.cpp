#include "dsp/PassiveEq.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// The shared Passive EQ module (used twice per the SANDWICH bus): the
// non-cancelling simultaneous LF boost+cut curve, HF bell/shelf bands, the
// defeatable never-flat residual, and neutral-is-bit-exact-bypass -
// design-brief.md guarantee 5.
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

    double measureGainChangeDb (PassiveEq& eq, double frequencyHz, float amplitude = 0.4f)
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

TEST_CASE ("Passive EQ: simultaneous LF boost+cut is non-cancelling (both signs measurable)", "[dsp][passiveeq][non-cancelling]")
{
    PassiveEq eq;
    eq.setLfFreqHz (30.0f);
    eq.setLfBoostDial (10.0f);
    eq.setLfCutDial (10.0f);
    eq.setResidualEnabled (false); // isolate the boost/cut interaction from the residual tilt

    // Reference frequencies chosen from the module's own corner design (see
    // PassiveEq.h): the boost's resonant corner sits AT the LF selector, the
    // cut's dip is centred 6x above it - see docs/research-notes.md for the
    // documented "bump below corner + dip in low-mids" hardware finding this
    // models (not a literal circuit match - verified numerically during
    // design, see the PR description).
    const auto bumpDb = measureGainChangeDb (eq, 30.0f * 0.5f);
    const auto dipDb = measureGainChangeDb (eq, 30.0f * 6.0f);

    CHECK (bumpDb > 3.0);
    CHECK (dipDb < -3.0);
}

TEST_CASE ("Passive EQ: LF boost alone is measurably positive at low frequency", "[dsp][passiveeq]")
{
    PassiveEq eq;
    eq.setLfFreqHz (60.0f);
    eq.setLfBoostDial (10.0f);
    eq.setResidualEnabled (false);

    const auto gainDb = measureGainChangeDb (eq, 30.0);
    CHECK (gainDb > 3.0);
}

TEST_CASE ("Passive EQ: LF cut alone is measurably negative in the low-mids", "[dsp][passiveeq]")
{
    PassiveEq eq;
    eq.setLfFreqHz (100.0f);
    eq.setLfCutDial (10.0f);
    eq.setResidualEnabled (false);

    const auto gainDb = measureGainChangeDb (eq, 600.0);
    CHECK (gainDb < -3.0);
}

TEST_CASE ("Passive EQ: HF bell boost peaks near its selected frequency", "[dsp][passiveeq]")
{
    PassiveEq eq;
    eq.setHfBellFreqHz (8000.0f);
    eq.setHfBellBoostDial (10.0f);
    eq.setHfBellBandwidthDial (0.0f); // sharp
    eq.setResidualEnabled (false);

    const auto atCentre = measureGainChangeDb (eq, 8000.0);
    const auto farAway = measureGainChangeDb (eq, 500.0);

    CHECK (atCentre > 6.0);
    CHECK (std::abs (farAway) < 1.0);
}

TEST_CASE ("Passive EQ: HF shelf attenuation cuts above its selected frequency", "[dsp][passiveeq]")
{
    PassiveEq eq;
    eq.setHfShelfFreqHz (10000.0f);
    eq.setHfShelfAttenDial (10.0f);
    eq.setResidualEnabled (false);

    const auto aboveShelf = measureGainChangeDb (eq, 16000.0);
    const auto belowShelf = measureGainChangeDb (eq, 500.0);

    CHECK (aboveShelf < -3.0);
    CHECK (std::abs (belowShelf) < 1.0);
}

TEST_CASE ("Passive EQ: all dials at 0 with the residual disabled is a bit-exact bypass", "[dsp][passiveeq][null]")
{
    PassiveEq eq;
    eq.setResidualEnabled (false);
    eq.prepare (makeTestSpec (2));

    juce::AudioBuffer<float> reference (2, 4096);
    TestHelpers::fillWithSine (reference, testSampleRate, 440.0, 0.5f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    eq.process (block);

    CHECK (TestHelpers::maxDifferenceDbfs (processed, reference) <= -120.0);
}

TEST_CASE ("Passive EQ: the vintage residual is defeatable and non-zero when enabled", "[dsp][passiveeq][residual]")
{
    PassiveEq withResidual;
    withResidual.setLfFreqHz (20.0f); // the residual's magnitude varies with the LF selector (class comment)
    withResidual.setResidualEnabled (true);

    PassiveEq withoutResidual;
    withoutResidual.setLfFreqHz (20.0f);
    withoutResidual.setResidualEnabled (false);

    const auto withDb = measureGainChangeDb (withResidual, 10000.0, 0.5f);
    const auto withoutDb = measureGainChangeDb (withoutResidual, 10000.0, 0.5f);

    CHECK (std::abs (withDb - withoutDb) > 0.05);
    CHECK (std::abs (withDb) < 1.0); // "a few tenths of a dB" (brief) - never a dramatic tilt
}

TEST_CASE ("Passive EQ: dial taper is nonlinear (half-dial is not half the max dB)", "[dsp][passiveeq][taper]")
{
    PassiveEq halfDial;
    halfDial.setHfBellFreqHz (8000.0f);
    halfDial.setHfBellBoostDial (5.0f);
    halfDial.setHfBellBandwidthDial (0.0f);
    halfDial.setResidualEnabled (false);

    PassiveEq fullDial;
    fullDial.setHfBellFreqHz (8000.0f);
    fullDial.setHfBellBoostDial (10.0f);
    fullDial.setHfBellBandwidthDial (0.0f);
    fullDial.setResidualEnabled (false);

    const auto halfDb = measureGainChangeDb (halfDial, 8000.0);
    const auto fullDb = measureGainChangeDb (fullDial, 8000.0);

    // A linear taper would give halfDb == fullDb / 2; the power-law taper
    // (exponent 1.5) must NOT.
    CHECK (halfDb != Catch::Approx (fullDb / 2.0).margin (0.3));
}
