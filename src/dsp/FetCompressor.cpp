#include "FetCompressor.h"

void FetCompressor::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    const auto numChannels = static_cast<size_t> (spec.numChannels);
    envelopeState.assign (numChannels, 0.0f);

    sidechainFilters.clear();
    sidechainFilters.resize (numChannels); // Filter<float> is move-only, so resize() rather than assign()

    // Fixed sidechain tilt coefficients - computed once here, never on the
    // audio thread (frequency/gain/Q are compile-time constants).
    msrr::applyBiquadCoefficients (*sidechainCoefficients,
        juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (
            sampleRate,
            juce::jmin (sidechainTiltFreqHz, static_cast<float> (sampleRate) * 0.45f),
            sidechainTiltQ,
            juce::Decibels::decibelsToGain (sidechainTiltGainDb)));

    for (auto& filter : sidechainFilters)
        filter.coefficients = sidechainCoefficients;

    thresholdSmoothed.reset (sampleRate, smoothingTimeSeconds);
    thresholdSmoothed.setCurrentAndTargetValue (lastThresholdDb);

    reset();
}

void FetCompressor::reset()
{
    std::fill (envelopeState.begin(), envelopeState.end(), 0.0f);

    for (auto& filter : sidechainFilters)
        filter.reset();

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

    // Program-dependent release shortening (all-buttons character): derive
    // this block's effective release from the previous block's GR - a
    // block-rate approximation that avoids a per-sample exp() while still
    // giving the "deeper GR recovers faster" behaviour the brief asks for.
    auto effectiveReleaseMs = releaseMs;
    if (programDependentRelease)
    {
        const auto factor = juce::jmax (minReleaseFactor, 1.0f / (1.0f + releaseShorteningPerDb * currentGainReductionDb));
        effectiveReleaseMs = releaseMs * factor;
    }

    const auto attackCoeff = std::exp (-1.0 / (static_cast<double> (attackMs) * 0.001 * sampleRate));
    const auto releaseCoeff = std::exp (-1.0 / (static_cast<double> (effectiveReleaseMs) * 0.001 * sampleRate));

    float peakGainReductionDb = 0.0f;

    for (size_t channel = 0; channel < numChannels && channel < envelopeState.size(); ++channel)
    {
        auto* data = block.getChannelPointer (channel);
        auto& envelope = envelopeState[channel];
        auto& sidechainFilter = sidechainFilters[channel];

        for (size_t sample = 0; sample < numSamples; ++sample)
        {
            const auto drivenSample = data[sample] * driveGainLinear;

            // Detector path: optionally tilted copy of the driven signal.
            // The tilt filter's state always advances while enabled;
            // when disabled the raw driven sample feeds the detector and
            // the filter is skipped entirely (Bus A never runs it).
            const auto detectorSample = sidechainTiltEnabled
                                             ? sidechainFilter.processSample (drivenSample)
                                             : drivenSample;

            const auto rectified = detectorSample * detectorSample;
            const auto coeff = rectified > envelope ? attackCoeff : releaseCoeff;
            envelope = static_cast<float> (coeff * envelope + (1.0 - coeff) * rectified);

            // envelope <= threshold (in dB terms) yields an exact 0 dB
            // reduction via this clamp (not an asymptotic approximation),
            // so a signal that never crosses threshold produces gainFactor
            // == 1.0f exactly - see the class comment on the null test.
            const auto envelopeDb = juce::Decibels::gainToDecibels (std::sqrt (juce::jmax (envelope, 1.0e-12f)), -120.0f);
            const auto overshootDb = juce::jmax (0.0f, envelopeDb - thresholdDb);
            const auto reductionDb = overshootDb * ratioFactor;
            const auto gainFactor = juce::Decibels::decibelsToGain (-reductionDb);

            data[sample] = drivenSample * gainFactor * makeupGainLinear * outputTrimLinear;
            peakGainReductionDb = juce::jmax (peakGainReductionDb, reductionDb);
        }
    }

    currentGainReductionDb = peakGainReductionDb;
}
