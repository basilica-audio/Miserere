#include "dsp/PassiveEq.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <memory>

// The shared Passive EQ module (used twice per the SANDWICH bus), v0.5.0
// component-derived corner laws (brief F2 / sections 6.2-6.3): the exact
// LF ladder ("low end trick" emergent from C_lo1 = C_lo2/30 + the 110k/8.2k
// pot asymmetry), the matched (decramped) HF bell with hardware-coupled
// gain/Q, the matched HF shelf, the defeatable never-flat residual and the
// neutral-is-bit-exact-bypass invariant.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 48000;
    constexpr int settleSamples = 24000;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    double measureGainChangeDb (PassiveEq& eq, double frequencyHz, float amplitude = 0.2f, double sampleRate = testSampleRate)
    {
        juce::dsp::ProcessSpec spec = makeTestSpec (1);
        spec.sampleRate = sampleRate;
        eq.prepare (spec);

        juce::AudioBuffer<float> reference (1, testBlockSize);
        TestHelpers::fillWithSine (reference, sampleRate, frequencyHz, amplitude);

        juce::AudioBuffer<float> processed;
        processed.makeCopyOf (reference);

        juce::dsp::AudioBlock<float> block (processed);
        eq.process (block);

        const auto inputRms = TestHelpers::tailRms (reference, settleSamples);
        const auto outputRms = TestHelpers::tailRms (processed, settleSamples);

        REQUIRE (inputRms > 0.0);
        return juce::Decibels::gainToDecibels (outputRms / inputRms);
    }

    // "Action corner" (SoS-style shelf turnover): the frequency at which a
    // shelf's response crosses +-3 dB from unity, found by bisection on a
    // log-frequency grid.
    double findActionCornerHz (const std::function<std::unique_ptr<PassiveEq>()>& makeEq, double crossingDb,
                                double loHz, double hiHz)
    {
        auto lo = loHz;
        auto hi = hiHz;

        for (int iteration = 0; iteration < 12; ++iteration)
        {
            const auto mid = std::sqrt (lo * hi);
            auto eq = makeEq();
            const auto gain = measureGainChangeDb (*eq, mid);

            // Boost shelves fall with frequency; cut shelves rise. The
            // crossing is where |gain| passes |crossingDb| coming from the
            // plateau side (low frequencies).
            if (std::abs (gain) > std::abs (crossingDb))
                lo = mid;
            else
                hi = mid;
        }

        return std::sqrt (lo * hi);
    }
}

//==============================================================================
// 6.3 - EQP LF corner law: boost = cut = max at the 60 Hz selection.

TEST_CASE ("Passive EQ: EQP low-end trick at 60 Hz - net LF boost, low-mid dip, flat top, corner ratio", "[dsp][passiveeq][eqp][trick]")
{
    const auto makeBothMax = []
    {
        auto eq = std::make_unique<PassiveEq>();
        eq->setLfFreqHz (60.0f);
        eq->setLfBoostDial (10.0f);
        eq->setLfCutDial (10.0f);
        eq->setResidualEnabled (false);
        return eq;
    };

    // (a) net gain >= +4 dB at 30 Hz.
    {
        auto eq = makeBothMax();
        const auto at30 = measureGainChangeDb (*eq, 30.0);
        INFO ("net gain at 30 Hz = " << at30 << " dB");
        CHECK (at30 >= 4.0);
    }

    // (b) dip minimum located 200-700 Hz, depth <= -1.5 dB.
    {
        double dipMinDb = 0.0;
        double dipMinHz = 0.0;

        for (double f = 120.0; f <= 1200.0; f *= 1.15)
        {
            auto eq = makeBothMax();
            const auto gain = measureGainChangeDb (*eq, f);

            if (gain < dipMinDb)
            {
                dipMinDb = gain;
                dipMinHz = f;
            }
        }

        INFO ("dip minimum " << dipMinDb << " dB at " << dipMinHz << " Hz");
        CHECK (dipMinDb <= -1.5);
        CHECK (dipMinHz >= 200.0);
        CHECK (dipMinHz <= 700.0);
    }

    // (c) |H| within +-0.5 dB of 0 dB above 3 kHz.
    {
        for (const auto f : { 3000.0, 5000.0, 10000.0 })
        {
            auto eq = makeBothMax();
            const auto gain = measureGainChangeDb (*eq, f);
            INFO ("gain at " << f << " Hz = " << gain << " dB");
            CHECK (std::abs (gain) <= 0.5);
        }
    }

    // (d) cut/boost action-corner ratio in [1.4, 2.5] (SoS: "half an octave
    // to an octave"). Corners measured as the +-3 dB-from-unity crossings
    // of each shelf alone.
    {
        const auto boostCorner = findActionCornerHz ([]
        {
            auto eq = std::make_unique<PassiveEq>();
            eq->setLfFreqHz (60.0f);
            eq->setLfBoostDial (10.0f);
            eq->setResidualEnabled (false);
            return eq;
        }, 3.0, 30.0, 2000.0);

        const auto cutCorner = findActionCornerHz ([]
        {
            auto eq = std::make_unique<PassiveEq>();
            eq->setLfFreqHz (60.0f);
            eq->setLfCutDial (10.0f);
            eq->setResidualEnabled (false);
            return eq;
        }, -3.0, 30.0, 4000.0);

        const auto ratio = cutCorner / boostCorner;
        INFO ("boost corner " << boostCorner << " Hz, cut corner " << cutCorner << " Hz, ratio " << ratio);
        CHECK (ratio >= 1.4);
        CHECK (ratio <= 2.5);
    }
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

TEST_CASE ("Passive EQ: LF cut alone attenuates below its corner, not the mids", "[dsp][passiveeq]")
{
    PassiveEq low;
    low.setLfFreqHz (100.0f);
    low.setLfCutDial (10.0f);
    low.setResidualEnabled (false);
    const auto atLf = measureGainChangeDb (low, 40.0);

    PassiveEq mid;
    mid.setLfFreqHz (100.0f);
    mid.setLfCutDial (10.0f);
    mid.setResidualEnabled (false);
    const auto atMid = measureGainChangeDb (mid, 2000.0);

    INFO ("cut at 40 Hz = " << atLf << " dB, at 2 kHz = " << atMid << " dB");
    CHECK (atLf < -10.0);
    CHECK (std::abs (atMid) < 1.0);
}

//==============================================================================
// 6.2 - decramping: 16 kHz bell at full sharp boost at 44.1 kHz peaks
// within +-4 % of the analog target (fails with the old bilinear/RBJ
// rendering - the digital peak got pulled and its upper skirt squeezed).

TEST_CASE ("Passive EQ: 16 kHz sharp bell at 44.1 k peaks within +-4% of the analog target", "[dsp][passiveeq][matched][decramp]")
{
    constexpr double fs = 44100.0;

    double peakHz = 0.0;
    double peakDb = -100.0;

    for (double f = 10000.0; f <= 20500.0; f *= 1.01)
    {
        PassiveEq eq;
        eq.setHfBellFreqHz (16000.0f);
        eq.setHfBellBoostDial (10.0f);
        eq.setHfBellBandwidthDial (0.0f); // sharp
        eq.setResidualEnabled (false);

        const auto gain = measureGainChangeDb (eq, f, 0.2f, fs);

        if (gain > peakDb)
        {
            peakDb = gain;
            peakHz = f;
        }
    }

    INFO ("measured peak " << peakDb << " dB at " << peakHz << " Hz (target 16000 Hz)");
    CHECK (peakHz >= 16000.0 * 0.96);
    CHECK (peakHz <= 16000.0 * 1.04);
    CHECK (peakDb > 12.0);
}

//==============================================================================
// 6.3 - sharp-vs-broad peak-gain delta = 9 +- 1.5 dB at full boost (the
// bandwidth pot adds series resistance inside the resonant branch).

TEST_CASE ("Passive EQ: sharp-vs-broad HF bell peak-gain delta is 9 +- 1.5 dB at full boost", "[dsp][passiveeq][eqp][bandwidth]")
{
    const auto measurePeak = [] (float bandwidthDial)
    {
        PassiveEq eq;
        eq.setHfBellFreqHz (8000.0f);
        eq.setHfBellBoostDial (10.0f);
        eq.setHfBellBandwidthDial (bandwidthDial);
        eq.setResidualEnabled (false);
        return measureGainChangeDb (eq, 8000.0);
    };

    const auto sharpPeak = measurePeak (0.0f);
    const auto broadPeak = measurePeak (10.0f);

    const auto delta = sharpPeak - broadPeak;
    INFO ("sharp peak = " << sharpPeak << " dB, broad peak = " << broadPeak << " dB, delta = " << delta);
    CHECK (delta == Catch::Approx (9.0).margin (1.5));
}

TEST_CASE ("Passive EQ: HF bell Q is monotone in the bandwidth dial", "[dsp][passiveeq][eqp][bandwidth]")
{
    // Narrower bell = faster falloff away from centre: measure the level
    // 1.5 octaves below centre relative to the peak.
    const auto skirtDepth = [] (float bandwidthDial)
    {
        PassiveEq atCentre;
        atCentre.setHfBellFreqHz (8000.0f);
        atCentre.setHfBellBoostDial (10.0f);
        atCentre.setHfBellBandwidthDial (bandwidthDial);
        atCentre.setResidualEnabled (false);
        const auto centre = measureGainChangeDb (atCentre, 8000.0);

        PassiveEq atSkirt;
        atSkirt.setHfBellFreqHz (8000.0f);
        atSkirt.setHfBellBoostDial (10.0f);
        atSkirt.setHfBellBandwidthDial (bandwidthDial);
        atSkirt.setResidualEnabled (false);
        const auto skirt = measureGainChangeDb (atSkirt, 8000.0 / 2.8);

        return centre - skirt;
    };

    const auto sharpDepth = skirtDepth (0.0f);
    const auto midDepth = skirtDepth (5.0f);
    const auto broadDepth = skirtDepth (10.0f);

    INFO ("skirt depth: sharp = " << sharpDepth << " dB, mid = " << midDepth << " dB, broad = " << broadDepth << " dB");
    CHECK (sharpDepth > midDepth);
    CHECK (midDepth > broadDepth);
}

TEST_CASE ("Passive EQ: HF shelf attenuation cuts above its selected frequency", "[dsp][passiveeq]")
{
    PassiveEq above;
    above.setHfShelfFreqHz (10000.0f);
    above.setHfShelfAttenDial (10.0f);
    above.setResidualEnabled (false);
    const auto aboveShelf = measureGainChangeDb (above, 16000.0);

    PassiveEq below;
    below.setHfShelfFreqHz (10000.0f);
    below.setHfShelfAttenDial (10.0f);
    below.setResidualEnabled (false);
    const auto belowShelf = measureGainChangeDb (below, 500.0);

    CHECK (aboveShelf < -3.0);
    CHECK (std::abs (belowShelf) < 1.0);
}

//==============================================================================
// Invariants carried over.

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
    withResidual.setLfFreqHz (20.0f);
    withResidual.setResidualEnabled (true);

    PassiveEq withoutResidual;
    withoutResidual.setLfFreqHz (20.0f);
    withoutResidual.setResidualEnabled (false);

    const auto withDb = measureGainChangeDb (withResidual, 10000.0, 0.5f);
    const auto withoutDb = measureGainChangeDb (withoutResidual, 10000.0, 0.5f);

    CHECK (std::abs (withDb - withoutDb) > 0.05);
    CHECK (std::abs (withDb) < 1.0);
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

    CHECK (halfDb != Catch::Approx (fullDb / 2.0).margin (0.3));
}
