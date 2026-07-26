#include "dsp/AdaaSaturator.h"
#include "dsp/ConsoleEq.h"
#include "dsp/FetCrush.h"
#include "dsp/OptoLeveler.h"
#include "dsp/SlapDelay.h"
#include "dsp/TapeSat.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <vector>

// v0.5.0 brief section 6.1: measurable aliasing assertions for every ADAA
// stage, at 44.1 kHz, with the input level stated in every assertion (it
// dominates the result).
//
// Measurement method: probe frequencies land EXACTLY on FFT bins
// (f = k*fs/N, rectangular window, N = 16384), so harmonics and their
// folded (aliased) images land on exact bins too. "Sum of non-harmonic
// spurs" = the power sum over every bin EXCEPT DC, the fundamental, and the
// DIRECT (below-Nyquist, unfolded) harmonics of the fundamental. Folded
// harmonic images are exactly the aliases and are counted as spurs.
namespace
{
    constexpr double fs = 44100.0;
    constexpr int fftSize = 16384;
    constexpr int settleSamples = 2 * fftSize;
    constexpr int totalSamples = settleSamples + fftSize;

    constexpr int stressBin = 4458;   // ~12.0 kHz ("12 kHz" stress probe)
    constexpr int anchorBin = 1115;   // ~3.0 kHz ("3 kHz" program-realistic anchor)

    double binFrequency (int bin) noexcept { return bin * fs / fftSize; }

    juce::dsp::ProcessSpec makeSpec (int numChannels)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = fs;
        spec.maximumBlockSize = static_cast<juce::uint32> (totalSamples);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    // RAII guard for the ADAA test hook.
    struct AdaaBypassGuard
    {
        explicit AdaaBypassGuard (bool bypass) { msrr::adaa::bypassForTests() = bypass; }
        ~AdaaBypassGuard() { msrr::adaa::bypassForTests() = false; }
    };

    // Renders `totalSamples` of a bin-exact sine through `processor` (a
    // callable taking an AudioBlock) and returns the analysis buffer.
    juce::AudioBuffer<float> renderSine (int bin, float amplitude,
                                          const std::function<void (juce::dsp::AudioBlock<float>&)>& processor)
    {
        juce::AudioBuffer<float> buffer (1, totalSamples);
        TestHelpers::fillWithSine (buffer, fs, binFrequency (bin), amplitude);

        juce::dsp::AudioBlock<float> block (buffer);
        processor (block);
        return buffer;
    }

    // Power sum of all non-harmonic spur bins in dBFS (amplitude of an
    // equivalent single sine). Direct harmonics k*f0 below Nyquist are
    // excluded (+-1 bin); everything else above `ignoreBelowBin` counts.
    double spurSumDbfs (const juce::AudioBuffer<float>& buffer, int fundamentalBin)
    {
        juce::dsp::FFT fft (14); // 2^14 == fftSize
        std::vector<float> fftData (static_cast<size_t> (fftSize) * 2, 0.0f);

        const auto* data = buffer.getReadPointer (0) + settleSamples;
        for (int i = 0; i < fftSize; ++i)
            fftData[static_cast<size_t> (i)] = data[i];

        fft.performFrequencyOnlyForwardTransform (fftData.data());

        // Exclusion mask: DC region, fundamental, direct harmonics.
        std::vector<bool> excluded (static_cast<size_t> (fftSize / 2), false);
        for (int bin = 0; bin <= 4; ++bin)
            excluded[static_cast<size_t> (bin)] = true;

        for (int harmonic = 1; harmonic * fundamentalBin < fftSize / 2; ++harmonic)
        {
            const auto centre = harmonic * fundamentalBin;
            for (int bin = juce::jmax (0, centre - 1); bin <= juce::jmin (fftSize / 2 - 1, centre + 1); ++bin)
                excluded[static_cast<size_t> (bin)] = true;
        }

        double powerSum = 0.0;
        for (int bin = 0; bin < fftSize / 2; ++bin)
            if (! excluded[static_cast<size_t> (bin)])
                powerSum += static_cast<double> (fftData[static_cast<size_t> (bin)]) * fftData[static_cast<size_t> (bin)];

        // Bin magnitude of a full-scale sine is N/2.
        const auto amplitude = std::sqrt (powerSum) * 2.0 / fftSize;
        return 20.0 * std::log10 (juce::jmax (amplitude, 1.0e-12));
    }

    // --- Per-stage worst-case / anchor renderers ------------------------

    juce::AudioBuffer<float> renderTapeSat (int bin, float amplitude, float driveDb)
    {
        TapeSat sat;
        sat.setDriveDb (driveDb);
        sat.prepare (makeSpec (1));
        return renderSine (bin, amplitude, [&] (juce::dsp::AudioBlock<float>& b) { sat.process (b); });
    }

    juce::AudioBuffer<float> renderConsoleEq (int bin, float amplitude, float driveDb)
    {
        ConsoleEq eq;
        eq.setDriveDb (driveDb);
        eq.prepare (makeSpec (1));
        return renderSine (bin, amplitude, [&] (juce::dsp::AudioBlock<float>& b) { eq.process (b); });
    }

    juce::AudioBuffer<float> renderSlap (int bin, float amplitude, float tone01)
    {
        SlapDelay slap;
        slap.setDelayMs (110.0f);
        slap.setToneProportion (tone01);
        slap.prepare (makeSpec (1));
        return renderSine (bin, amplitude, [&] (juce::dsp::AudioBlock<float>& b) { slap.process (b); });
    }

    juce::AudioBuffer<float> renderCrush (int bin, float amplitude, float driveDb,
                                           FetCrush::Ratio ratio, float attackStep)
    {
        FetCrush crush;
        crush.setRatio (ratio);
        crush.setStyle (FetCrush::Style::allButtons);
        crush.setInputDriveDb (driveDb);
        crush.setAttackStep (attackStep);
        crush.setReleaseStep (7.0f);
        crush.prepare (makeSpec (1));
        return renderSine (bin, amplitude, [&] (juce::dsp::AudioBlock<float>& b) { crush.process (b); });
    }
}

//==============================================================================
// 6.1a - merge gate (relative): 12 kHz at 0 dBFS at the stage's worst-case
// setting, ADAA on vs off (test hook): spur sum >= 12 dB lower with ADAA.
// Published ADAA1 results are 10-20 dB improvement; this is the honest
// attainable range at 1x and the primary gate.

TEST_CASE ("Aliasing merge gate: TapeSat +24 dB drive, 12 kHz @ 0 dBFS - ADAA improves spur sum >= 12 dB", "[aliasing][adaa][tapesat]")
{
    double withAdaa, withoutAdaa;

    {
        AdaaBypassGuard guard (false);
        withAdaa = spurSumDbfs (renderTapeSat (stressBin, 1.0f, 24.0f), stressBin);
    }
    {
        AdaaBypassGuard guard (true);
        withoutAdaa = spurSumDbfs (renderTapeSat (stressBin, 1.0f, 24.0f), stressBin);
    }

    INFO ("with ADAA = " << withAdaa << " dBFS, without = " << withoutAdaa << " dBFS");
    CHECK (withoutAdaa - withAdaa >= 12.0);
}

TEST_CASE ("Aliasing merge gate: ConsoleEq drive max, 12 kHz @ 0 dBFS - ADAA improves spur sum >= 12 dB", "[aliasing][adaa][consoleeq]")
{
    double withAdaa, withoutAdaa;

    {
        AdaaBypassGuard guard (false);
        withAdaa = spurSumDbfs (renderConsoleEq (stressBin, 1.0f, 24.0f), stressBin);
    }
    {
        AdaaBypassGuard guard (true);
        withoutAdaa = spurSumDbfs (renderConsoleEq (stressBin, 1.0f, 24.0f), stressBin);
    }

    INFO ("with ADAA = " << withAdaa << " dBFS, without = " << withoutAdaa << " dBFS");
    CHECK (withoutAdaa - withAdaa >= 12.0);
}

TEST_CASE ("Aliasing merge gate: SlapDelay tone max, 12 kHz @ 0 dBFS - ADAA improves spur sum >= 12 dB", "[aliasing][adaa][slap]")
{
    double withAdaa, withoutAdaa;

    {
        AdaaBypassGuard guard (false);
        withAdaa = spurSumDbfs (renderSlap (stressBin, 1.0f, 1.0f), stressBin);
    }
    {
        AdaaBypassGuard guard (true);
        withoutAdaa = spurSumDbfs (renderSlap (stressBin, 1.0f, 1.0f), stressBin);
    }

    INFO ("with ADAA = " << withAdaa << " dBFS, without = " << withoutAdaa << " dBFS");
    CHECK (withoutAdaa - withAdaa >= 12.0);
}

// NOTE (documented deviation from brief 6.1a): CRUSH has no ADAA-vs-bypass
// merge gate. Its only ADAA-treated nonlinearity is the GR-gated LF
// transformer tanh behind a 150 Hz one-pole extract - a 12 kHz probe is
// ~38 dB down at that stage's input and produces no measurable distortion
// with OR without ADAA, so the relative comparison is structurally
// meaningless for this bus. The FET cell's time-varying gain (the actual
// spur source at 12 kHz) is not ADAA-able by construction (brief F3: "the
// time-varying G(vC) multiply is not an aliasing source; the eps-term is
// memoryless-per-sample and mild - measured, not assumed"). CRUSH is
// covered by the absolute stress ceiling and anchor below.

//==============================================================================
// 6.1b - stress ceiling (absolute, sanity only - NOT the marketing number):
// worst case, 12 kHz at 0 dBFS: spur sum <= -20 dBFS. (-60 dBFS is
// mathematically unattainable with ADAA1 at 1x: H3 of a near-square lands
// at 36 kHz ~ -10 dBr, the ADAA1 boxcar kernel only attenuates it ~13 dB.)

TEST_CASE ("Aliasing stress ceiling: all four stages, 12 kHz @ 0 dBFS worst case <= -20 dBFS spur sum", "[aliasing][stress]")
{
    const auto tapeSat = spurSumDbfs (renderTapeSat (stressBin, 1.0f, 24.0f), stressBin);
    const auto consoleEq = spurSumDbfs (renderConsoleEq (stressBin, 1.0f, 24.0f), stressBin);
    const auto slap = spurSumDbfs (renderSlap (stressBin, 1.0f, 1.0f), stressBin);
    const auto crush = spurSumDbfs (renderCrush (stressBin, 1.0f, 24.0f, FetCrush::Ratio::rAll, 7.0f), stressBin);

    INFO ("TapeSat = " << tapeSat << ", ConsoleEq = " << consoleEq << ", Slap = " << slap << ", Crush = " << crush << " (dBFS)");

    CHECK (tapeSat <= -20.0);
    CHECK (consoleEq <= -20.0);
    CHECK (slap <= -20.0);
    CHECK (crush <= -20.0);
}

//==============================================================================
// 6.1c - program-realistic anchor (absolute - the number the CHANGELOG
// cites, always with its conditions): 3 kHz at -12 dBFS, "hot vocal"
// settings: spur sum <= -60 dBFS.

TEST_CASE ("Aliasing anchor: 3 kHz @ -12 dBFS, hot-vocal settings, spur sum <= -60 dBFS per stage", "[aliasing][anchor]")
{
    constexpr float anchorAmplitude = 0.2512f; // -12 dBFS

    const auto tapeSat = spurSumDbfs (renderTapeSat (anchorBin, anchorAmplitude, 12.0f), anchorBin);
    const auto consoleEq = spurSumDbfs (renderConsoleEq (anchorBin, anchorAmplitude, 18.0f), anchorBin); // 75 % of 24 dB
    const auto slap = spurSumDbfs (renderSlap (anchorBin, anchorAmplitude, 0.5f), anchorBin);
    const auto crush = spurSumDbfs (renderCrush (anchorBin, anchorAmplitude, 12.0f, FetCrush::Ratio::r20, 4.0f), anchorBin);

    INFO ("TapeSat = " << tapeSat << ", ConsoleEq = " << consoleEq << ", Slap = " << slap << ", Crush = " << crush << " (dBFS)");

    CHECK (tapeSat <= -60.0);
    CHECK (consoleEq <= -60.0);
    CHECK (slap <= -60.0);
    CHECK (crush <= -60.0);
}

//==============================================================================
// 6.1d - golden regression: the measured spur sums from a-c must not
// regress by > 3 dB in later commits. Values recorded from the v0.5.0
// implementation run (this machine, Debug, fs 44.1 k).

TEST_CASE ("Aliasing golden regression: spur sums must not regress > 3 dB", "[aliasing][golden]")
{
    struct GoldenCase
    {
        const char* name;
        double measured;
        double golden;
    };

    const GoldenCase cases[] = {
        { "TapeSat stress", spurSumDbfs (renderTapeSat (stressBin, 1.0f, 24.0f), stressBin), -25.3 },
        { "ConsoleEq stress", spurSumDbfs (renderConsoleEq (stressBin, 1.0f, 24.0f), stressBin), -24.7 },
        { "Slap stress", spurSumDbfs (renderSlap (stressBin, 1.0f, 1.0f), stressBin), -46.7 },
        { "Crush stress", spurSumDbfs (renderCrush (stressBin, 1.0f, 24.0f, FetCrush::Ratio::rAll, 7.0f), stressBin), -32.9 },
        { "TapeSat anchor", spurSumDbfs (renderTapeSat (anchorBin, 0.2512f, 12.0f), anchorBin), -75.6 },
        { "ConsoleEq anchor", spurSumDbfs (renderConsoleEq (anchorBin, 0.2512f, 18.0f), anchorBin), -74.5 },
        { "Slap anchor", spurSumDbfs (renderSlap (anchorBin, 0.2512f, 0.5f), anchorBin), -77.0 },
        { "Crush anchor", spurSumDbfs (renderCrush (anchorBin, 0.2512f, 12.0f, FetCrush::Ratio::r20, 4.0f), anchorBin), -72.7 },
    };

    for (const auto& goldenCase : cases)
    {
        INFO (goldenCase.name << ": measured " << goldenCase.measured << " dBFS, golden " << goldenCase.golden << " dBFS");
        CHECK (goldenCase.measured <= goldenCase.golden + 3.0);
    }
}

//==============================================================================
// 6.1e - opto exemption guard: OptoLeveler's ADAA-exempt post-colour tanh
// (fixed drive 1.15): alias contribution at 12 kHz / 0 dBFS input
// <= -80 dBFS. Failure = project-owner escalation, never a silent ADAA
// retrofit (which would break bus alignment).

TEST_CASE ("Aliasing opto exemption guard: exempt colour stage <= -80 dBFS spur sum at 12 kHz / 0 dBFS", "[aliasing][opto][exemption]")
{
    OptoLeveler opto;
    opto.setPeakReductionProportion (1.0f);
    opto.setEmphasisProportion (1.0f);
    opto.prepare (makeSpec (1));

    const auto buffer = renderSine (stressBin, 1.0f, [&] (juce::dsp::AudioBlock<float>& b) { opto.process (b); });
    const auto spurs = spurSumDbfs (buffer, stressBin);

    INFO ("opto exempt-stage spur sum = " << spurs << " dBFS (GR " << opto.getCurrentGainReductionDb() << " dB)");
    CHECK (spurs <= -80.0);
}

//==============================================================================
// 6.1f - linear-path flatness (the residual-form guarantee): -60 dBFS sines
// through TapeSat at +24 dB drive and ConsoleEq at drive max: response
// deviation from the constant small-signal gain <= 0.1 dB up to 20 kHz at
// 44.1 k, measured group delay 0 samples. Pins that ADAA adds no
// cos(pi*f/fs) droop and no half-sample delay to the LINEAR component of
// in-line stages.

TEST_CASE ("Aliasing linear-path flatness: TapeSat/ConsoleEq small-signal response flat within 0.1 dB, group delay 0", "[aliasing][flatness]")
{
    constexpr float tinyAmplitude = 1.0e-3f; // -60 dBFS

    const int probeBins[] = { 372, 1115, 2230, 4458, 6320, 7430 }; // ~1k..20k

    const auto measureGainDb = [&] (int bin, auto&& makeProcessor)
    {
        auto processor = makeProcessor();
        juce::AudioBuffer<float> buffer (1, totalSamples);
        TestHelpers::fillWithSine (buffer, fs, binFrequency (bin), tinyAmplitude);

        juce::AudioBuffer<float> reference;
        reference.makeCopyOf (buffer);

        juce::dsp::AudioBlock<float> block (buffer);
        processor->process (block);

        const auto inRms = TestHelpers::tailRms (reference, settleSamples);
        const auto outRms = TestHelpers::tailRms (buffer, settleSamples);
        return 20.0 * std::log10 (outRms / inRms);
    };

    // TapeSat +24 dB drive.
    {
        std::vector<double> gains;
        for (const auto bin : probeBins)
            gains.push_back (measureGainDb (bin, []
            {
                auto sat = std::make_unique<TapeSat>();
                sat->setDriveDb (24.0f);
                sat->prepare (makeSpec (1));
                return sat;
            }));

        const auto reference = gains.front();
        for (size_t i = 0; i < gains.size(); ++i)
        {
            INFO ("TapeSat bin " << probeBins[i] << " (" << binFrequency (probeBins[i]) << " Hz): " << gains[i] << " dB vs " << reference);
            CHECK (std::abs (gains[i] - reference) <= 0.1);
        }
    }

    // ConsoleEq drive max.
    {
        std::vector<double> gains;
        for (const auto bin : probeBins)
            gains.push_back (measureGainDb (bin, []
            {
                auto eq = std::make_unique<ConsoleEq>();
                eq->setDriveDb (24.0f);
                eq->prepare (makeSpec (1));
                return eq;
            }));

        const auto reference = gains.front();
        for (size_t i = 0; i < gains.size(); ++i)
        {
            INFO ("ConsoleEq bin " << probeBins[i] << " (" << binFrequency (probeBins[i]) << " Hz): " << gains[i] << " dB vs " << reference);
            CHECK (std::abs (gains[i] - reference) <= 0.1);
        }
    }

    // Group delay 0: a small impulse leaves at the index it arrived (the
    // linear component is exactly aligned; the emphasis pair is an exact
    // reciprocal, so TapeSat's linear path is the identity times k).
    {
        TapeSat sat;
        sat.setDriveDb (24.0f);
        sat.prepare (makeSpec (1));

        juce::AudioBuffer<float> buffer (1, 512);
        buffer.clear();
        constexpr int impulseIndex = 64;
        buffer.setSample (0, impulseIndex, tinyAmplitude);

        juce::dsp::AudioBlock<float> block (buffer);
        sat.process (block);

        int peakIndex = 0;
        float peakValue = 0.0f;
        for (int i = 0; i < 512; ++i)
        {
            if (std::abs (buffer.getSample (0, i)) > peakValue)
            {
                peakValue = std::abs (buffer.getSample (0, i));
                peakIndex = i;
            }
        }

        CHECK (peakIndex == impulseIndex);
    }
}
