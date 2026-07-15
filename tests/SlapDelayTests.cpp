#include "dsp/SlapDelay.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>

// M1 guarantee 7: slap timing (first echo at the exact configured delay in
// samples) and feedback stability (bounded output for 10 s of noise at
// maximum feedback), plus the mono switch and the delay-line reset
// contract (part of guarantee 10).
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
}

TEST_CASE ("Slap: first echo lands at the exact configured delay in samples", "[dsp][slap][timing]")
{
    for (const auto delayMs : { 60.0f, 110.0f, 180.0f })
    {
        INFO ("delay = " << delayMs << " ms");

        const auto expectedIndex = static_cast<int> (std::round (delayMs * 0.001 * testSampleRate));
        const auto bufferLength = expectedIndex + 4800;

        SlapDelay slap;
        slap.setDelayMs (delayMs);
        slap.setFeedbackProportion (0.0f);
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
    slap.setFeedbackProportion (0.15f);
    slap.prepare (makeTestSpec (1, 8192));

    juce::AudioBuffer<float> buffer (1, 8192);
    buffer.clear();
    buffer.setSample (0, 0, 0.5f);

    juce::dsp::AudioBlock<float> block (buffer);
    slap.process (block);

    const auto expectedIndex = static_cast<int> (std::round (0.110 * testSampleRate)); // 5280

    // Sample 0 (where the dry impulse sat) must now be silent - the bus is
    // a pure echo return; the dry voice belongs to Bus A.
    for (int sample = 0; sample < expectedIndex; ++sample)
    {
        INFO ("sample = " << sample);
        REQUIRE (std::abs (buffer.getSample (0, sample)) < 1.0e-6f);
    }
}

TEST_CASE ("Slap: feedback produces a second echo that is quieter than the first", "[dsp][slap][feedback]")
{
    constexpr float delayMs = 80.0f;
    const auto delaySamples = static_cast<int> (std::round (delayMs * 0.001 * testSampleRate)); // 3840
    const auto bufferLength = delaySamples * 3 + 1000;

    SlapDelay slap;
    slap.setDelayMs (delayMs);
    slap.setFeedbackProportion (0.3f); // maximum
    slap.prepare (makeTestSpec (1, bufferLength));

    juce::AudioBuffer<float> buffer (1, bufferLength);
    buffer.clear();
    buffer.setSample (0, 0, 0.5f);

    juce::dsp::AudioBlock<float> block (buffer);
    slap.process (block);

    // Measure peaks in windows around the first and second echoes (the
    // loop filters smear the repeats slightly, so a window rather than a
    // single sample).
    const auto windowPeak = [&] (int centre)
    {
        float peak = 0.0f;
        for (int i = std::max (0, centre - 100); i < std::min (bufferLength, centre + 100); ++i)
            peak = std::max (peak, std::abs (buffer.getSample (0, i)));
        return peak;
    };

    const auto firstEchoPeak = windowPeak (delaySamples);
    const auto secondEchoPeak = windowPeak (delaySamples * 2);

    REQUIRE (firstEchoPeak > 0.01f);
    CHECK (secondEchoPeak > 1.0e-5f);            // feedback really recirculates
    CHECK (secondEchoPeak < firstEchoPeak * 0.5f); // and decays (30% + loop losses)
}

TEST_CASE ("Slap: zero feedback produces exactly one echo", "[dsp][slap][feedback]")
{
    constexpr float delayMs = 80.0f;
    const auto delaySamples = static_cast<int> (std::round (delayMs * 0.001 * testSampleRate));
    const auto bufferLength = delaySamples * 3;

    SlapDelay slap;
    slap.setDelayMs (delayMs);
    slap.setFeedbackProportion (0.0f);
    slap.prepare (makeTestSpec (1, bufferLength));

    juce::AudioBuffer<float> buffer (1, bufferLength);
    buffer.clear();
    buffer.setSample (0, 0, 0.5f);

    juce::dsp::AudioBlock<float> block (buffer);
    slap.process (block);

    // Everything from just past the first echo on must be silent.
    for (int sample = delaySamples + 10; sample < bufferLength; ++sample)
    {
        INFO ("sample = " << sample);
        REQUIRE (std::abs (buffer.getSample (0, sample)) < 1.0e-6f);
    }
}

TEST_CASE ("Slap: stable and bounded for 10 seconds of noise at maximum feedback", "[dsp][slap][stability]")
{
    constexpr int blockSize = 512;

    SlapDelay slap;
    slap.setDelayMs (60.0f); // shortest delay = most loop round trips in 10 s
    slap.setFeedbackProportion (0.3f);
    slap.prepare (makeTestSpec (2, blockSize));

    std::mt19937 rng (4242);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);

    const auto totalBlocks = static_cast<int> (10.0 * testSampleRate / blockSize);
    float overallPeak = 0.0f;

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
        overallPeak = std::max (overallPeak, TestHelpers::peakAbsolute (buffer));
    }

    // Geometric feedback decay (0.3 per round trip, further reduced by the
    // loop filters/saturator) bounds the worst-case sum well under 2x the
    // input peak; 4.0 is a generous ceiling that still proves stability.
    CHECK (overallPeak < 4.0f);
}

TEST_CASE ("Slap: mono switch makes both output channels identical", "[dsp][slap][mono]")
{
    SlapDelay slap;
    slap.setDelayMs (80.0f);
    slap.setFeedbackProportion (0.2f);
    slap.setMonoEnabled (true);
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

TEST_CASE ("Slap: stereo (non-mono) mode keeps channels independent", "[dsp][slap][mono]")
{
    SlapDelay slap;
    slap.setDelayMs (80.0f);
    slap.setFeedbackProportion (0.0f);
    slap.setMonoEnabled (false);
    slap.prepare (makeTestSpec (2, 16384));

    // Impulse on the left channel only.
    juce::AudioBuffer<float> buffer (2, 16384);
    buffer.clear();
    buffer.setSample (0, 0, 0.5f);

    juce::dsp::AudioBlock<float> block (buffer);
    slap.process (block);

    CHECK (findFirstEchoIndex (buffer, 0) > 0);
    CHECK (findFirstEchoIndex (buffer, 1) == -1); // nothing leaked to the right
}

TEST_CASE ("Slap: reset() clears the delay line (no stale echo after reset)", "[dsp][slap][reset]")
{
    SlapDelay slap;
    slap.setDelayMs (110.0f);
    slap.setFeedbackProportion (0.3f);
    slap.prepare (makeTestSpec (1, 16384));

    // Push an impulse in, but reset before the echo has emerged.
    juce::AudioBuffer<float> buffer (1, 1024);
    buffer.clear();
    buffer.setSample (0, 0, 0.9f);

    juce::dsp::AudioBlock<float> block (buffer);
    slap.process (block);

    slap.reset();

    // Process enough silence to cover the full echo horizon: if the delay
    // line survived reset, the echo would appear here.
    juce::AudioBuffer<float> silence (1, 16384);
    silence.clear();

    juce::dsp::AudioBlock<float> silentBlock (silence);
    slap.process (silentBlock);

    CHECK (findFirstEchoIndex (silence, 0, 1.0e-6f) == -1);
}
