#include "dsp/SlapDelay.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>

// Bus (4) SLAP: single-repeat timing (first echo at the exact configured
// delay, no second echo - feedback is fixed at 0 in v2), the repeat being
// measurably darker than the input, the mono/stereo switch, and the
// delay-line reset contract - design-brief.md guarantees 7 and 10.
namespace
{
    constexpr double testSampleRate = 48000.0;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels, int maxBlockSize)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (maxBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    int findFirstEchoIndex (const juce::AudioBuffer<float>& buffer, int channel, float thresholdAbs = 1.0e-4f)
    {
        const auto* data = buffer.getReadPointer (channel);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (std::abs (data[sample]) > thresholdAbs)
                return sample;

        return -1;
    }

    // Simple spectral-centroid estimate (magnitude-weighted mean frequency)
    // via a naive DFT - the buffers here are short enough that this stays
    // fast without pulling in juce::dsp::FFT for a one-off measurement.
    double spectralCentroidHz (const juce::AudioBuffer<float>& buffer, int channel, int startSample, int numSamples, double sampleRate)
    {
        const auto* data = buffer.getReadPointer (channel) + startSample;

        constexpr int numBins = 64;
        double weightedSum = 0.0;
        double magnitudeSum = 0.0;

        for (int bin = 1; bin < numBins; ++bin)
        {
            const auto frequencyHz = bin * sampleRate / (2.0 * numBins);
            const auto omega = juce::MathConstants<double>::twoPi * frequencyHz / sampleRate;

            double real = 0.0;
            double imag = 0.0;
            for (int n = 0; n < numSamples; ++n)
            {
                real += data[n] * std::cos (omega * n);
                imag -= data[n] * std::sin (omega * n);
            }

            const auto magnitude = std::sqrt (real * real + imag * imag);
            weightedSum += magnitude * frequencyHz;
            magnitudeSum += magnitude;
        }

        return magnitudeSum > 0.0 ? weightedSum / magnitudeSum : 0.0;
    }
}

TEST_CASE ("Slap: first echo lands at the exact configured delay in samples", "[dsp][slap][timing]")
{
    for (const auto delayMs : { 50.0f, 110.0f, 160.0f })
    {
        INFO ("delay = " << delayMs << " ms");

        const auto expectedIndex = static_cast<int> (std::round (delayMs * 0.001 * testSampleRate));
        const auto bufferLength = expectedIndex + 4800;

        SlapDelay slap;
        slap.setDelayMs (delayMs);
        slap.prepare (makeTestSpec (1, bufferLength));

        juce::AudioBuffer<float> buffer (1, bufferLength);
        buffer.clear();
        buffer.setSample (0, 0, 0.5f);

        juce::dsp::AudioBlock<float> block (buffer);
        slap.process (block);

        const auto firstEcho = findFirstEchoIndex (buffer, 0);

        REQUIRE (firstEcho >= 0);
        CHECK (firstEcho == expectedIndex);
    }
}

TEST_CASE ("Slap: the output is wet-only (no dry bleed before the first echo)", "[dsp][slap]")
{
    SlapDelay slap;
    slap.setDelayMs (110.0f);
    slap.prepare (makeTestSpec (1, 8192));

    juce::AudioBuffer<float> buffer (1, 8192);
    buffer.clear();
    buffer.setSample (0, 0, 0.5f);

    juce::dsp::AudioBlock<float> block (buffer);
    slap.process (block);

    const auto expectedIndex = static_cast<int> (std::round (0.110 * testSampleRate));

    for (int sample = 0; sample < expectedIndex; ++sample)
    {
        INFO ("sample = " << sample);
        REQUIRE (std::abs (buffer.getSample (0, sample)) < 1.0e-6f);
    }
}

TEST_CASE ("Slap: feedback is fixed at 0 - no second echo above -80 dBFS", "[dsp][slap][feedback]")
{
    constexpr float delayMs = 80.0f;
    const auto delaySamples = static_cast<int> (std::round (delayMs * 0.001 * testSampleRate));
    const auto bufferLength = delaySamples * 4;

    SlapDelay slap;
    slap.setDelayMs (delayMs);
    slap.prepare (makeTestSpec (1, bufferLength));

    juce::AudioBuffer<float> buffer (1, bufferLength);
    buffer.clear();
    buffer.setSample (0, 0, 0.9f);

    juce::dsp::AudioBlock<float> block (buffer);
    slap.process (block);

    constexpr float minus80Dbfs = 1.0e-4f; // 10^(-80/20)

    // The single repeat's own darkening lowpass has a genuine (short)
    // impulse-response tail immediately after the onset - not a second
    // echo, just that filter settling. Give it a generous settling window
    // (a few hundred samples) before asserting silence, so this test
    // specifically proves "no SECOND repeat", not "the filter has zero
    // group delay".
    for (int sample = delaySamples + 500; sample < bufferLength; ++sample)
    {
        INFO ("sample = " << sample);
        REQUIRE (std::abs (buffer.getSample (0, sample)) < minus80Dbfs);
    }
}

TEST_CASE ("Slap: the repeat is measurably darker than the input (lower spectral centroid)", "[dsp][slap][tone]")
{
    constexpr float delayMs = 80.0f;
    const auto delaySamples = static_cast<int> (std::round (delayMs * 0.001 * testSampleRate));
    const auto bufferLength = delaySamples + 512;

    SlapDelay slap;
    slap.setDelayMs (delayMs);
    slap.setToneProportion (1.0f); // "darker" end of the range
    slap.prepare (makeTestSpec (1, bufferLength));

    // A broadband impulse so the spectral-centroid probe has full-spectrum
    // content to compare before/after the repeat's darkening filter.
    juce::AudioBuffer<float> buffer (1, bufferLength);
    buffer.clear();
    buffer.setSample (0, 0, 1.0f);

    juce::dsp::AudioBlock<float> block (buffer);
    slap.process (block);

    // Compare the impulse response's spectral centroid in a window right at
    // the input (an unfiltered impulse - flat spectrum, so this is really
    // measuring the delay+darkening filter's own response) against a probe
    // fed through nothing (an ideal flat reference at Nyquist/2-ish).
    const auto repeatCentroid = spectralCentroidHz (buffer, 0, delaySamples, 256, testSampleRate);
    const auto nyquistQuarter = testSampleRate / 4.0; // a flat spectrum's centroid over [0, Nyquist/2] sits near here

    CHECK (repeatCentroid < nyquistQuarter);
}

TEST_CASE ("Slap: mono (default) - both output channels carry the identical echo", "[dsp][slap][mono]")
{
    SlapDelay slap;
    slap.setDelayMs (80.0f);
    slap.setStereoEnabled (false);
    slap.prepare (makeTestSpec (2, 16384));

    // Deliberately different L/R content.
    juce::AudioBuffer<float> buffer (2, 16384);
    TestHelpers::fillWithSine (buffer, testSampleRate, 440.0, 0.5f);
    {
        auto* right = buffer.getWritePointer (1);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            right[sample] *= -0.5f; // opposite polarity, different level
    }

    juce::dsp::AudioBlock<float> block (buffer);
    slap.process (block);

    const auto* left = buffer.getReadPointer (0);
    const auto* right = buffer.getReadPointer (1);

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        INFO ("sample = " << sample);
        REQUIRE (left[sample] == right[sample]);
    }
}

TEST_CASE ("Slap: stereo mode keeps channels independent", "[dsp][slap][mono]")
{
    SlapDelay slap;
    slap.setDelayMs (80.0f);
    slap.setStereoEnabled (true);
    slap.prepare (makeTestSpec (2, 16384));

    juce::AudioBuffer<float> buffer (2, 16384);
    buffer.clear();
    buffer.setSample (0, 0, 0.5f); // impulse on the left channel only

    juce::dsp::AudioBlock<float> block (buffer);
    slap.process (block);

    CHECK (findFirstEchoIndex (buffer, 0) > 0);
    CHECK (findFirstEchoIndex (buffer, 1) == -1); // nothing leaked to the right
}

TEST_CASE ("Slap: stable and finite for 10 seconds of full-scale noise", "[dsp][slap][stability]")
{
    constexpr int blockSize = 512;

    SlapDelay slap;
    slap.setDelayMs (60.0f);
    slap.prepare (makeTestSpec (2, blockSize));

    std::mt19937 rng (4242);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);

    const auto totalBlocks = static_cast<int> (10.0 * testSampleRate / blockSize);

    for (int blockIndex = 0; blockIndex < totalBlocks; ++blockIndex)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);

        for (int channel = 0; channel < 2; ++channel)
        {
            auto* data = buffer.getWritePointer (channel);
            for (int sample = 0; sample < blockSize; ++sample)
                data[sample] = noise (rng);
        }

        juce::dsp::AudioBlock<float> block (buffer);
        slap.process (block);

        REQUIRE (TestHelpers::allSamplesFinite (buffer));
        REQUIRE (TestHelpers::peakAbsolute (buffer) < 4.0f); // no feedback loop - bounded well under any runaway threshold
    }
}

TEST_CASE ("Slap: reset() clears the delay line (no stale echo after reset)", "[dsp][slap][reset]")
{
    SlapDelay slap;
    slap.setDelayMs (110.0f);
    slap.prepare (makeTestSpec (1, 16384));

    juce::AudioBuffer<float> buffer (1, 1024);
    buffer.clear();
    buffer.setSample (0, 0, 0.9f);

    juce::dsp::AudioBlock<float> block (buffer);
    slap.process (block);

    slap.reset();

    juce::AudioBuffer<float> silence (1, 16384);
    silence.clear();

    juce::dsp::AudioBlock<float> silentBlock (silence);
    slap.process (silentBlock);

    CHECK (findFirstEchoIndex (silence, 0, 1.0e-6f) == -1);
}
