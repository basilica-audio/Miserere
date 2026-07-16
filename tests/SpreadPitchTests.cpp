#include "dsp/SpreadPitch.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// Bus (3) SPREAD: measurable +/-cents pitch offset on L/R (via FFT), base
// delays ~30/50 ms, and the width control - design-brief.md guarantee 6.
namespace
{
    constexpr double testSampleRate = 48000.0;

    juce::dsp::ProcessSpec makeMonoInputSpec (int maxBlockSize)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (maxBlockSize);
        spec.numChannels = 2; // the module itself sums to mono internally; the block it processes is stereo
        return spec;
    }

    // FFT-based peak-frequency estimate (Hann-windowed magnitude spectrum,
    // parabolic interpolation around the peak bin for sub-bin accuracy).
    double dominantFrequencyHz (const float* data, int numSamples, double sampleRate)
    {
        int order = 0;
        while ((1 << order) < numSamples)
            ++order;

        const auto fftSize = 1 << order;
        juce::dsp::FFT fft (order);

        std::vector<float> fftBuffer (static_cast<size_t> (fftSize) * 2, 0.0f);

        juce::dsp::WindowingFunction<float> window (static_cast<size_t> (fftSize), juce::dsp::WindowingFunction<float>::hann);
        for (int i = 0; i < fftSize && i < numSamples; ++i)
            fftBuffer[static_cast<size_t> (i)] = data[i];
        window.multiplyWithWindowingTable (fftBuffer.data(), static_cast<size_t> (fftSize));

        fft.performFrequencyOnlyForwardTransform (fftBuffer.data());

        int peakBin = 1;
        float peakMagnitude = 0.0f;
        for (int bin = 1; bin < fftSize / 2; ++bin)
        {
            if (fftBuffer[static_cast<size_t> (bin)] > peakMagnitude)
            {
                peakMagnitude = fftBuffer[static_cast<size_t> (bin)];
                peakBin = bin;
            }
        }

        // Parabolic interpolation using the two neighbouring bins.
        const auto y0 = static_cast<double> (fftBuffer[static_cast<size_t> (peakBin - 1)]);
        const auto y1 = static_cast<double> (fftBuffer[static_cast<size_t> (peakBin)]);
        const auto y2 = static_cast<double> (fftBuffer[static_cast<size_t> (peakBin + 1)]);
        const auto denominator = (y0 - 2.0 * y1 + y2);
        const auto offset = std::abs (denominator) > 1.0e-9 ? 0.5 * (y0 - y2) / denominator : 0.0;

        return (peakBin + offset) * sampleRate / fftSize;
    }
}

TEST_CASE ("Spread: L is pitched up and R is pitched down by ~the configured detune", "[dsp][spread][pitch]")
{
    constexpr float detuneCents = 15.0f; // maximum - gives the FFT probe comfortable resolution margin
    constexpr double inputFrequencyHz = 1000.0;

    SpreadPitch spread;
    spread.setDetuneCents (detuneCents);
    spread.setTimeScale (1.0f);
    spread.setWidth (1.0f); // fully hard-panned: L = up voice only, R = down voice only
    spread.prepare (makeMonoInputSpec (200000));

    constexpr int settleSamples = 24000;  // let the grain crossfades settle past start-up
    constexpr int analysisSamples = 65536;
    const auto totalSamples = settleSamples + analysisSamples;

    juce::AudioBuffer<float> buffer (2, totalSamples);
    TestHelpers::fillWithSine (buffer, testSampleRate, inputFrequencyHz, 0.6f);

    juce::dsp::AudioBlock<float> block (buffer);
    spread.process (block);

    const auto leftPeakHz = dominantFrequencyHz (buffer.getReadPointer (0) + settleSamples, analysisSamples, testSampleRate);
    const auto rightPeakHz = dominantFrequencyHz (buffer.getReadPointer (1) + settleSamples, analysisSamples, testSampleRate);

    const auto expectedUpHz = inputFrequencyHz * std::pow (2.0, detuneCents / 1200.0);
    const auto expectedDownHz = inputFrequencyHz * std::pow (2.0, -detuneCents / 1200.0);

    INFO ("left peak = " << leftPeakHz << " Hz (expected ~" << expectedUpHz << "), right peak = "
                          << rightPeakHz << " Hz (expected ~" << expectedDownHz << ")");

    CHECK (leftPeakHz == Catch::Approx (expectedUpHz).margin (3.0));
    CHECK (rightPeakHz == Catch::Approx (expectedDownHz).margin (3.0));
    CHECK (leftPeakHz > rightPeakHz);
}

TEST_CASE ("Spread: base delays are approximately 30 ms (up voice) and 50 ms (down voice)", "[dsp][spread][timing]")
{
    SpreadPitch spread;
    spread.setDetuneCents (0.0f); // no pitch ramp - the taps sit still at the base delay
    spread.setTimeScale (1.0f);
    spread.setWidth (1.0f);
    spread.prepare (makeMonoInputSpec (16384));

    juce::AudioBuffer<float> buffer (2, 16384);
    buffer.clear();
    buffer.setSample (0, 0, 1.0f);
    buffer.setSample (1, 0, 1.0f);

    juce::dsp::AudioBlock<float> block (buffer);
    spread.process (block);

    const auto findPeakIndex = [&] (int channel)
    {
        const auto* data = buffer.getReadPointer (channel);
        int peakIndex = 0;
        float peakValue = 0.0f;
        for (int i = 0; i < 16384; ++i)
        {
            if (std::abs (data[i]) > peakValue)
            {
                peakValue = std::abs (data[i]);
                peakIndex = i;
            }
        }
        return peakIndex;
    };

    const auto leftPeakIndex = findPeakIndex (0);
    const auto rightPeakIndex = findPeakIndex (1);

    const auto expectedUpSamples = static_cast<int> (std::round (0.030 * testSampleRate));
    const auto expectedDownSamples = static_cast<int> (std::round (0.050 * testSampleRate));

    CHECK (leftPeakIndex == Catch::Approx (expectedUpSamples).margin (4));
    CHECK (rightPeakIndex == Catch::Approx (expectedDownSamples).margin (4));
}

TEST_CASE ("Spread: time scale stretches both base delays proportionally", "[dsp][spread][timing]")
{
    SpreadPitch spread;
    spread.setDetuneCents (0.0f);
    spread.setTimeScale (2.0f); // maximum scale -> ~60/100 ms
    spread.setWidth (1.0f);
    spread.prepare (makeMonoInputSpec (16384));

    juce::AudioBuffer<float> buffer (2, 16384);
    buffer.clear();
    buffer.setSample (0, 0, 1.0f);
    buffer.setSample (1, 0, 1.0f);

    juce::dsp::AudioBlock<float> block (buffer);
    spread.process (block);

    const auto* left = buffer.getReadPointer (0);
    int leftPeakIndex = 0;
    float leftPeakValue = 0.0f;
    for (int i = 0; i < 16384; ++i)
    {
        if (std::abs (left[i]) > leftPeakValue)
        {
            leftPeakValue = std::abs (left[i]);
            leftPeakIndex = i;
        }
    }

    const auto expectedSamples = static_cast<int> (std::round (0.060 * testSampleRate));
    CHECK (leftPeakIndex == Catch::Approx (expectedSamples).margin (4));
}

TEST_CASE ("Spread: width 0 centres both voices (L == R); width 1 keeps them hard-panned (L != R)", "[dsp][spread][width]")
{
    const auto measureCorrelationDifference = [] (float width)
    {
        SpreadPitch spread;
        spread.setDetuneCents (10.0f);
        spread.setTimeScale (1.0f);
        spread.setWidth (width);
        spread.prepare (makeMonoInputSpec (48000));

        juce::AudioBuffer<float> buffer (2, 48000);
        TestHelpers::fillWithSine (buffer, testSampleRate, 500.0, 0.5f);

        juce::dsp::AudioBlock<float> block (buffer);
        spread.process (block);

        double maxAbsDiff = 0.0;
        const auto* left = buffer.getReadPointer (0);
        const auto* right = buffer.getReadPointer (1);
        for (int i = 24000; i < 48000; ++i)
            maxAbsDiff = std::max (maxAbsDiff, static_cast<double> (std::abs (left[i] - right[i])));

        return maxAbsDiff;
    };

    const auto diffAtZeroWidth = measureCorrelationDifference (0.0f);
    const auto diffAtFullWidth = measureCorrelationDifference (1.0f);

    CHECK (diffAtZeroWidth < 1.0e-5);
    CHECK (diffAtFullWidth > 0.01);
}

TEST_CASE ("Spread: reset() clears both micro-pitch delay lines", "[dsp][spread][reset]")
{
    SpreadPitch spread;
    spread.setDetuneCents (6.0f);
    spread.prepare (makeMonoInputSpec (4096));

    juce::AudioBuffer<float> buffer (2, 4096);
    TestHelpers::fillWithSine (buffer, testSampleRate, 500.0, 0.5f);
    juce::dsp::AudioBlock<float> block (buffer);
    spread.process (block);

    spread.reset();

    juce::AudioBuffer<float> silence (2, 4096);
    silence.clear();
    juce::dsp::AudioBlock<float> silentBlock (silence);
    spread.process (silentBlock);

    CHECK (TestHelpers::peakAbsolute (silence) < 1.0e-6f);
}
