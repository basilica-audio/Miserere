#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>

// Small shared helpers used across the Tests target. Kept dependency-free
// (just juce_audio_basics) so it can be included from any test file.
namespace TestHelpers
{
    // Fills every channel of the buffer with a sine wave of the given
    // frequency. `startSampleIndex` offsets the phase calculation, so
    // calling this for consecutive blocks with startSampleIndex incremented
    // by each block's length produces a phase-continuous sine across block
    // boundaries (needed whenever a test processes multiple blocks through a
    // stateful IIR/envelope-follower processor - a phase discontinuity at
    // block boundaries would inject spurious broadband energy and pollute
    // level measurements).
    inline void fillWithSine (juce::AudioBuffer<float>& buffer,
                              double sampleRate,
                              double frequencyHz,
                              float amplitude = 0.5f,
                              juce::int64 startSampleIndex = 0)
    {
        const auto numChannels = buffer.getNumChannels();
        const auto numSamples = buffer.getNumSamples();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* data = buffer.getWritePointer (channel);

            for (int sample = 0; sample < numSamples; ++sample)
            {
                const auto phase = juce::MathConstants<double>::twoPi * frequencyHz
                                    * static_cast<double> (startSampleIndex + sample) / sampleRate;
                data[sample] = amplitude * static_cast<float> (std::sin (phase));
            }
        }
    }

    // Root-mean-square level across all channels/samples in the buffer.
    inline double rms (const juce::AudioBuffer<float>& buffer)
    {
        double sumOfSquares = 0.0;
        juce::int64 numValues = 0;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto value = static_cast<double> (data[sample]);
                sumOfSquares += value * value;
                ++numValues;
            }
        }

        return numValues > 0 ? std::sqrt (sumOfSquares / static_cast<double> (numValues)) : 0.0;
    }

    // RMS of the buffer's tail only (from `fromSample` on) - skips filter/
    // envelope turn-on transients so steady-state levels are measured.
    inline double tailRms (const juce::AudioBuffer<float>& buffer, int fromSample)
    {
        double sumOfSquares = 0.0;
        juce::int64 numValues = 0;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = fromSample; sample < buffer.getNumSamples(); ++sample)
            {
                const auto value = static_cast<double> (data[sample]);
                sumOfSquares += value * value;
                ++numValues;
            }
        }

        return numValues > 0 ? std::sqrt (sumOfSquares / static_cast<double> (numValues)) : 0.0;
    }

    // Largest absolute sample value across all channels/samples.
    inline float peakAbsolute (const juce::AudioBuffer<float>& buffer)
    {
        float peak = 0.0f;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                peak = std::max (peak, std::abs (data[sample]));
        }

        return peak;
    }

    // Returns true if every sample in the buffer is finite (no NaN/Inf).
    inline bool allSamplesFinite (const juce::AudioBuffer<float>& buffer)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                if (! std::isfinite (data[sample]))
                    return false;
        }

        return true;
    }

    // Largest absolute difference between two equally-sized buffers, in dB
    // relative to full scale (0 dBFS == 1.0). Returns -infinity-ish (-200)
    // for a bit-exact match.
    inline double maxDifferenceDbfs (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        float maxDiff = 0.0f;

        const auto numChannels = std::min (a.getNumChannels(), b.getNumChannels());
        const auto numSamples = std::min (a.getNumSamples(), b.getNumSamples());

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto* dataA = a.getReadPointer (channel);
            const auto* dataB = b.getReadPointer (channel);

            for (int sample = 0; sample < numSamples; ++sample)
                maxDiff = std::max (maxDiff, std::abs (dataA[sample] - dataB[sample]));
        }

        return maxDiff > 0.0f ? 20.0 * std::log10 (static_cast<double> (maxDiff)) : -200.0;
    }

    // Magnitude of a single frequency bin (nearest FFT bin to `targetHz`) in
    // a Hann-windowed magnitude spectrum of `numSamples` samples starting at
    // `startSample` in `channel` - the same "quadratic peak interpolation
    // not needed, just read the nearest bin" approach SpreadPitchTests.cpp
    // uses for its FFT probe, generalised so harmonic-content tests
    // (fundamental + Nth-harmonic bin ratios -> THD-style measurements) can
    // share one implementation instead of re-deriving FFT plumbing per test
    // file.
    inline double fftBinMagnitude (const juce::AudioBuffer<float>& buffer,
                                    int channel,
                                    int startSample,
                                    int numSamples,
                                    double sampleRate,
                                    double targetHz)
    {
        int order = 0;
        while ((1 << order) < numSamples)
            ++order;

        const auto fftSize = 1 << order;
        juce::dsp::FFT fft (order);

        std::vector<float> fftBuffer (static_cast<size_t> (fftSize) * 2, 0.0f);
        const auto* data = buffer.getReadPointer (channel) + startSample;

        for (int i = 0; i < fftSize && i < numSamples; ++i)
            fftBuffer[static_cast<size_t> (i)] = data[i];

        juce::dsp::WindowingFunction<float> window (static_cast<size_t> (fftSize), juce::dsp::WindowingFunction<float>::hann);
        window.multiplyWithWindowingTable (fftBuffer.data(), static_cast<size_t> (fftSize));

        fft.performFrequencyOnlyForwardTransform (fftBuffer.data());

        const auto bin = juce::jlimit (0, fftSize / 2 - 1, static_cast<int> (std::lround (targetHz * fftSize / sampleRate)));
        return static_cast<double> (fftBuffer[static_cast<size_t> (bin)]);
    }

    // Total-harmonic-distortion-style estimate: sqrt(sum of harmonic-bin
    // magnitudes squared, harmonics 2..maxHarmonic) / fundamental-bin
    // magnitude, expressed as a ratio (multiply by 100 for a percentage).
    // Not a full THD+N measurement (no noise floor integration, discrete
    // harmonic bins only) but sufficient to regression-freeze "how much
    // harmonic content did this stage add, relative to the fundamental".
    inline double estimateThdRatio (const juce::AudioBuffer<float>& buffer,
                                     int channel,
                                     int startSample,
                                     int numSamples,
                                     double sampleRate,
                                     double fundamentalHz,
                                     int maxHarmonic = 6)
    {
        const auto fundamentalMag = fftBinMagnitude (buffer, channel, startSample, numSamples, sampleRate, fundamentalHz);

        if (fundamentalMag <= 1.0e-9)
            return 0.0;

        double sumSquares = 0.0;

        for (int harmonic = 2; harmonic <= maxHarmonic; ++harmonic)
        {
            const auto mag = fftBinMagnitude (buffer, channel, startSample, numSamples, sampleRate, fundamentalHz * harmonic);
            sumSquares += mag * mag;
        }

        return std::sqrt (sumSquares) / fundamentalMag;
    }
}
