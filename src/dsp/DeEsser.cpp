#include "DeEsser.h"

namespace
{
    float clampBelowNyquist (float frequencyHz, double sampleRate) noexcept
    {
        const auto nyquist = static_cast<float> (sampleRate) * 0.5f;
        return juce::jlimit (200.0f, nyquist * 0.9f, frequencyHz);
    }
}

void DeEsser::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    const auto numChannels = static_cast<size_t> (spec.numChannels);
    detectorFilters.clear();
    detectorFilters.resize (numChannels); // Filter<float> is move-only, so resize() rather than assign()
    envelopeState.assign (numChannels, 0.0f);

    for (auto& filter : detectorFilters)
        filter.coefficients = detectorCoefficients;

    msrr::applyBiquadCoefficients (*detectorCoefficients,
        juce::dsp::IIR::ArrayCoefficients<float>::makeBandPass (sampleRate, clampBelowNyquist (lastFrequencyHz, sampleRate), detectorQ));

    frequencySmoothed.reset (sampleRate, smoothingTimeSeconds);
    frequencySmoothed.setCurrentAndTargetValue (lastFrequencyHz);
    thresholdSmoothed.reset (sampleRate, smoothingTimeSeconds);
    thresholdSmoothed.setCurrentAndTargetValue (lastThresholdDb);

    reset();
}

void DeEsser::reset()
{
    for (auto& filter : detectorFilters)
        filter.reset();

    std::fill (envelopeState.begin(), envelopeState.end(), 0.0f);
    currentGainReductionDb = 0.0f;
}

void DeEsser::setFrequencyHz (float newFrequencyHz) noexcept
{
    lastFrequencyHz = newFrequencyHz;
    frequencySmoothed.setTargetValue (newFrequencyHz);
}

void DeEsser::setThresholdDb (float newThresholdDb) noexcept
{
    lastThresholdDb = newThresholdDb;
    thresholdSmoothed.setTargetValue (newThresholdDb);
}

void DeEsser::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto numChannels = block.getNumChannels();
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0 || ! enabled)
        return;

    const auto frequencyHz = clampBelowNyquist (frequencySmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    const auto thresholdDb = thresholdSmoothed.skip (static_cast<int> (numSamples));

    msrr::applyBiquadCoefficients (*detectorCoefficients,
        juce::dsp::IIR::ArrayCoefficients<float>::makeBandPass (sampleRate, frequencyHz, detectorQ));

    const auto attackCoeff = std::exp (-1.0 / (attackTimeSeconds * sampleRate));
    const auto releaseCoeff = std::exp (-1.0 / (releaseTimeSeconds * sampleRate));

    float peakGainReductionDb = 0.0f;

    for (size_t channel = 0; channel < numChannels && channel < detectorFilters.size(); ++channel)
    {
        auto* data = block.getChannelPointer (channel);
        auto& envelope = envelopeState[channel];
        auto& filter = detectorFilters[channel];

        for (size_t sample = 0; sample < numSamples; ++sample)
        {
            const auto inputSample = data[sample];
            const auto bandpassed = filter.processSample (inputSample);

            const auto rectified = bandpassed * bandpassed;
            const auto coeff = rectified > envelope ? attackCoeff : releaseCoeff;
            envelope = static_cast<float> (coeff * envelope + (1.0 - coeff) * rectified);

            const auto envelopeDb = juce::Decibels::gainToDecibels (std::sqrt (juce::jmax (envelope, 1.0e-12f)), -120.0f);
            const auto overshootDb = juce::jmax (0.0f, envelopeDb - thresholdDb);
            const auto reductionDb = juce::jlimit (0.0f, maxReductionDb, overshootDb);
            const auto gainFactor = juce::Decibels::decibelsToGain (-reductionDb);

            data[sample] = inputSample + bandpassed * (gainFactor - 1.0f);
            peakGainReductionDb = juce::jmax (peakGainReductionDb, reductionDb);
        }
    }

    currentGainReductionDb = peakGainReductionDb;
}
