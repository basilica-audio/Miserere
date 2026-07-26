#include "dsp/SlapDelay.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>
#include <utility>
#include <vector>

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

    // The single repeat's own voicing filters have a genuine impulse-
    // response tail immediately after the onset - not a second echo, just
    // those filters settling. Since v0.5.0 the play-side voicing includes
    // the fixed head-bump peak (+2 dB @ 90 Hz, Q 1.2, brief F5) whose
    // low-frequency ring needs ~50 ms to fall below the -80 dBFS bar, so
    // the settling window is 2600 samples. The assertion still specifically
    // proves "no SECOND repeat" - the next repeat slot (2*delay) sits well
    // beyond the window.
    for (int sample = delaySamples + 2600; sample < bufferLength; ++sample)
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

//==============================================================================
// v0.5.0 tape-transport voicing (brief F5 / section 6.7).

namespace
{
    // Instantaneous relative frequency deviation of a nominal-`f0` tone via
    // quadrature demodulation (I/Q mix -> one-pole LP -> unwrapped phase
    // derivative), decimated to controlRate for spectrum/autocorrelation
    // work. Returns the deviation series (relative, e.g. 0.0025 = 0.25 %).
    std::vector<double> frequencyDeviation (const juce::AudioBuffer<float>& buffer, int startSample,
                                             double f0, int decimation)
    {
        const auto* data = buffer.getReadPointer (0);
        const auto numSamples = buffer.getNumSamples();

        const auto lpCoeff = std::exp (-2.0 * juce::MathConstants<double>::pi * 40.0 / testSampleRate);
        double iState = 0.0, qState = 0.0;
        double previousPhase = 0.0;
        bool hasPrevious = false;

        std::vector<double> deviation;
        deviation.reserve (static_cast<size_t> ((numSamples - startSample) / decimation));

        double phaseAccumulator = 0.0;
        int sinceDecimation = 0;
        double decimSum = 0.0;
        int decimCount = 0;

        for (int n = 0; n < numSamples; ++n)
        {
            const auto lo = juce::MathConstants<double>::twoPi * f0 * n / testSampleRate;
            const auto i = data[n] * std::cos (lo);
            const auto q = -data[n] * std::sin (lo);

            iState = lpCoeff * iState + (1.0 - lpCoeff) * i;
            qState = lpCoeff * qState + (1.0 - lpCoeff) * q;

            if (n < startSample)
                continue;

            const auto phase = std::atan2 (qState, iState);

            if (hasPrevious)
            {
                auto delta = phase - previousPhase;
                while (delta > juce::MathConstants<double>::pi)
                    delta -= juce::MathConstants<double>::twoPi;
                while (delta < -juce::MathConstants<double>::pi)
                    delta += juce::MathConstants<double>::twoPi;

                // delta = 2*pi*(f_inst - f0)/fs -> relative deviation.
                const auto relativeDeviation = delta * testSampleRate / (juce::MathConstants<double>::twoPi * f0);
                decimSum += relativeDeviation;
                ++decimCount;

                if (++sinceDecimation >= decimation)
                {
                    deviation.push_back (decimSum / decimCount);
                    sinceDecimation = 0;
                    decimSum = 0.0;
                    decimCount = 0;
                }
            }

            previousPhase = phase;
            hasPrevious = true;
            juce::ignoreUnused (phaseAccumulator);
        }

        return deviation;
    }

    // Renders `seconds` of a steady `f0` tone through a SlapDelay at the
    // given wobble and returns the wet output.
    juce::AudioBuffer<float> renderWobbleTone (float wobble01, double seconds, double f0 = 3150.0)
    {
        constexpr int blockSize = 4800;

        SlapDelay slap;
        slap.setDelayMs (110.0f);
        slap.setToneProportion (0.0f);
        slap.setWobbleProportion (wobble01);
        slap.prepare (makeTestSpec (1, blockSize));

        const auto total = static_cast<int> (seconds * testSampleRate);
        juce::AudioBuffer<float> output (1, total);

        for (int offset = 0; offset < total; offset += blockSize)
        {
            const auto length = juce::jmin (blockSize, total - offset);
            juce::AudioBuffer<float> buffer (1, length);
            TestHelpers::fillWithSine (buffer, testSampleRate, f0, 0.5f, offset);
            juce::dsp::AudioBlock<float> block (buffer);
            slap.process (block);

            for (int i = 0; i < length; ++i)
                output.setSample (0, offset + i, buffer.getSample (0, i));
        }

        return output;
    }
}

TEST_CASE ("Slap: W&F calibration - 3150 Hz peak deviation matches the configured percentage", "[dsp][slap][wobble][calibration]")
{
    // wobble 50 % -> configured W&F = 0.25 % peak relative deviation,
    // +-20 % relative window (brief 6.7a).
    const auto buffer = renderWobbleTone (0.5f, 10.0);

    constexpr int decimation = 128; // deviation series at 375 Hz
    const auto deviation = frequencyDeviation (buffer, 48000, 3150.0, decimation);
    REQUIRE (deviation.size() > 1000);

    double peak = 0.0;
    for (const auto value : deviation)
        peak = std::max (peak, std::abs (value));

    INFO ("peak relative deviation = " << peak * 100.0 << " % (configured 0.25 %)");
    CHECK (peak >= 0.0020);
    CHECK (peak <= 0.0030);
}

TEST_CASE ("Slap: W&F spectrum peaks at the 0.9 / 5.2 Hz transport components", "[dsp][slap][wobble][spectrum]")
{
    const auto buffer = renderWobbleTone (0.5f, 12.0);

    constexpr int decimation = 128;
    const auto decimatedRate = testSampleRate / decimation; // 375 Hz
    auto deviation = frequencyDeviation (buffer, 48000, 3150.0, decimation);

    // Remove the mean, FFT (4096 points at 375 Hz -> 0.0916 Hz/bin).
    const int order = 12;
    const int fftLen = 1 << order;
    REQUIRE (static_cast<int> (deviation.size()) >= fftLen);

    double mean = 0.0;
    for (int i = 0; i < fftLen; ++i)
        mean += deviation[static_cast<size_t> (i)];
    mean /= fftLen;

    juce::dsp::FFT fft (order);
    std::vector<float> fftData (static_cast<size_t> (fftLen) * 2, 0.0f);
    for (int i = 0; i < fftLen; ++i)
        fftData[static_cast<size_t> (i)] = static_cast<float> (deviation[static_cast<size_t> (i)] - mean);

    juce::dsp::WindowingFunction<float> window (static_cast<size_t> (fftLen), juce::dsp::WindowingFunction<float>::hann);
    window.multiplyWithWindowingTable (fftData.data(), static_cast<size_t> (fftLen));
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const auto binHz = decimatedRate / fftLen;

    const auto peakNear = [&] (double targetHz, double windowHz)
    {
        const auto lo = static_cast<int> ((targetHz - windowHz) / binHz);
        const auto hi = static_cast<int> ((targetHz + windowHz) / binHz);
        double peakMag = 0.0;
        int peakBin = lo;
        for (int bin = juce::jmax (1, lo); bin <= hi; ++bin)
        {
            if (fftData[static_cast<size_t> (bin)] > peakMag)
            {
                peakMag = fftData[static_cast<size_t> (bin)];
                peakBin = bin;
            }
        }
        return std::make_pair (peakBin * binHz, peakMag);
    };

    // Local maxima within +-0.5 Hz search windows must sit within +-0.2 Hz
    // of the configured component frequencies.
    const auto [wowHz, wowMag] = peakNear (0.9, 0.5);
    const auto [flutterHz, flutterMag] = peakNear (5.2, 1.0);

    INFO ("wow peak at " << wowHz << " Hz (mag " << wowMag << "), flutter peak at " << flutterHz << " Hz (mag " << flutterMag << ")");
    CHECK (std::abs (wowHz - 0.9) <= 0.2);
    CHECK (std::abs (flutterHz - 5.2) <= 0.2);
    CHECK (wowMag > 0.0);
    CHECK (flutterMag > 0.0);
}

TEST_CASE ("Slap: W&F is quasi-periodic, not locked (autocorrelation at the flutter lag < 0.95)", "[dsp][slap][wobble][aperiodic]")
{
    const auto buffer = renderWobbleTone (0.5f, 12.0);

    constexpr int decimation = 128;
    const auto decimatedRate = testSampleRate / decimation;
    auto deviation = frequencyDeviation (buffer, 48000, 3150.0, decimation);

    const auto n = static_cast<int> (deviation.size());
    double mean = 0.0;
    for (const auto value : deviation)
        mean += value;
    mean /= n;
    for (auto& value : deviation)
        value -= mean;

    const auto lag = static_cast<int> (decimatedRate / 5.2); // one flutter period

    double num = 0.0, den = 0.0;
    for (int i = 0; i + lag < n; ++i)
    {
        num += deviation[static_cast<size_t> (i)] * deviation[static_cast<size_t> (i + lag)];
        den += deviation[static_cast<size_t> (i)] * deviation[static_cast<size_t> (i)];
    }

    const auto autocorrelation = num / juce::jmax (1.0e-12, den);
    INFO ("autocorrelation at lag 1/5.2 s = " << autocorrelation);
    CHECK (autocorrelation < 0.95);
}

TEST_CASE ("Slap: wobble 0 / age 0 is deterministic - two fresh renders are bit-identical", "[dsp][slap][neutral]")
{
    const auto render = []
    {
        SlapDelay slap;
        slap.setDelayMs (110.0f);
        slap.setWobbleProportion (0.0f);
        slap.setAgeProportion (0.0f);
        slap.prepare (makeTestSpec (1, 16384));

        juce::AudioBuffer<float> buffer (1, 16384);
        TestHelpers::fillWithSine (buffer, testSampleRate, 440.0, 0.5f);
        juce::dsp::AudioBlock<float> block (buffer);
        slap.process (block);
        return buffer;
    };

    const auto first = render();
    const auto second = render();

    for (int i = 0; i < 16384; ++i)
    {
        INFO ("sample " << i);
        REQUIRE (first.getSample (0, i) == second.getSample (0, i));
    }
}

TEST_CASE ("Slap: age asperity - in-band noise floor rises >= 6 dB during a burst; age 0 is silent", "[dsp][slap][age]")
{
    // 6 kHz burst (above the 300-5000 Hz measurement band, so the band
    // contains only the noise layer), age = 1.
    constexpr int blockSize = 4800;
    constexpr int analysis = 1 << 15;

    const auto bandEnergyDb = [] (const juce::AudioBuffer<float>& buffer, int start)
    {
        juce::dsp::FFT fft (15);
        std::vector<float> fftData (static_cast<size_t> (analysis) * 2, 0.0f);
        for (int i = 0; i < analysis; ++i)
            fftData[static_cast<size_t> (i)] = buffer.getSample (0, start + i);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        const auto binHz = testSampleRate / analysis;
        double power = 0.0;
        for (int bin = static_cast<int> (300.0 / binHz); bin <= static_cast<int> (5000.0 / binHz); ++bin)
            power += static_cast<double> (fftData[static_cast<size_t> (bin)]) * fftData[static_cast<size_t> (bin)];

        return 10.0 * std::log10 (juce::jmax (1.0e-24, power));
    };

    const auto renderWithAge = [] (float age01)
    {
        SlapDelay slap;
        slap.setDelayMs (110.0f);
        slap.setToneProportion (0.0f);
        slap.setAgeProportion (age01);
        slap.prepare (makeTestSpec (1, blockSize));

        // 2 s burst followed by 3 s silence.
        const auto total = static_cast<int> (5.0 * testSampleRate);
        juce::AudioBuffer<float> output (1, total);

        for (int offset = 0; offset < total; offset += blockSize)
        {
            juce::AudioBuffer<float> buffer (1, blockSize);
            if (offset < 2.0 * testSampleRate)
                TestHelpers::fillWithSine (buffer, testSampleRate, 6000.0, 0.5f, offset);
            else
                buffer.clear();

            juce::dsp::AudioBlock<float> block (buffer);
            slap.process (block);

            for (int i = 0; i < blockSize && offset + i < total; ++i)
                output.setSample (0, offset + i, buffer.getSample (0, i));
        }

        return output;
    };

    const auto aged = renderWithAge (1.0f);

    const auto duringBurst = bandEnergyDb (aged, static_cast<int> (1.0 * testSampleRate));
    const auto duringSilence = bandEnergyDb (aged, static_cast<int> (4.2 * testSampleRate));

    INFO ("band energy during burst = " << duringBurst << " dB, during silence = " << duringSilence << " dB");
    CHECK (duringBurst - duringSilence >= 6.0);

    // age 0 -> structurally silent noise layer (output after the echo of
    // silence is exactly zero, so the noise floor sits below -120 dBFS).
    const auto fresh = renderWithAge (0.0f);
    float peakTail = 0.0f;
    for (int i = static_cast<int> (4.2 * testSampleRate); i < fresh.getNumSamples(); ++i)
        peakTail = std::max (peakTail, std::abs (fresh.getSample (0, i)));

    INFO ("age 0 tail peak = " << peakTail);
    CHECK (peakTail < 1.0e-6f); // < -120 dBFS
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
