#include "FetCompressor.h"

void FetCompressor::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    const auto numChannels = static_cast<size_t> (spec.numChannels);
    envelopeState.assign (numChannels, 0.0f);

    thresholdSmoothed.reset (sampleRate, smoothingTimeSeconds);
    thresholdSmoothed.setCurrentAndTargetValue (lastThresholdDb);

    reset();
}

void FetCompressor::reset()
{
    std::fill (envelopeState.begin(), envelopeState.end(), 0.0f);
    currentGainReductionDb = 0.0f;
}

void FetCompressor::setThresholdDb (float newThresholdDb) noexcept
{
    lastThresholdDb = newThresholdDb;
    thresholdSmoothed.setTargetValue (newThresholdDb);
}

void FetCompressor::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto numChannels = block.getNumChannels();
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0 || numChannels == 0)
        return;

    const auto thresholdDb = thresholdSmoothed.skip (static_cast<int> (numSamples));
    const auto ratioFactor = 1.0f - (1.0f / ratio);

    const auto attackCoeff = std::exp (-1.0 / (static_cast<double> (attackMs) * 0.001 * sampleRate));
    const auto releaseCoeff = std::exp (-1.0 / (static_cast<double> (releaseMs) * 0.001 * sampleRate));

    float peakGainReductionDb = 0.0f;

    for (size_t channel = 0; channel < numChannels && channel < envelopeState.size(); ++channel)
    {
        auto* data = block.getChannelPointer (channel);
        auto& envelope = envelopeState[channel];

        for (size_t sample = 0; sample < numSamples; ++sample)
        {
            const auto inputSample = data[sample];

            const auto rectified = inputSample * inputSample;
            const auto coeff = rectified > envelope ? attackCoeff : releaseCoeff;
            envelope = static_cast<float> (coeff * envelope + (1.0 - coeff) * rectified);

            // envelope <= threshold (in dB terms) yields an exact 0 dB
            // reduction via this clamp (not an asymptotic approximation) -
            // see the class comment.
            const auto envelopeDb = juce::Decibels::gainToDecibels (std::sqrt (juce::jmax (envelope, 1.0e-12f)), -120.0f);
            const auto overshootDb = juce::jmax (0.0f, envelopeDb - thresholdDb);
            const auto reductionDb = overshootDb * ratioFactor;
            const auto gainFactor = juce::Decibels::decibelsToGain (-reductionDb);

            data[sample] = inputSample * gainFactor * makeupGainLinear;
            peakGainReductionDb = juce::jmax (peakGainReductionDb, reductionDb);
        }
    }

    currentGainReductionDb = peakGainReductionDb;
}
