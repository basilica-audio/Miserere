#include "OptoLeveler.h"

void OptoLeveler::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    const auto numChannels = static_cast<size_t> (spec.numChannels);
    detectorState.assign (numChannels, 0.0f);
    grSmoothedDb.assign (numChannels, 0.0f);
    historyState.assign (numChannels, 0.0f);

    amountSmoothed.reset (sampleRate, smoothingTimeSeconds);
    amountSmoothed.setCurrentAndTargetValue (lastAmount01);

    reset();
}

void OptoLeveler::reset()
{
    std::fill (detectorState.begin(), detectorState.end(), 0.0f);
    std::fill (grSmoothedDb.begin(), grSmoothedDb.end(), 0.0f);
    std::fill (historyState.begin(), historyState.end(), 0.0f);
    currentGainReductionDb = 0.0f;
}

void OptoLeveler::setPeakReductionProportion (float newAmount01) noexcept
{
    lastAmount01 = newAmount01;
    amountSmoothed.setTargetValue (newAmount01);
}

void OptoLeveler::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto numChannels = block.getNumChannels();
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0 || numChannels == 0)
        return;

    const auto amount01 = juce::jlimit (0.0f, 1.0f, amountSmoothed.skip (static_cast<int> (numSamples)));

    // Bit-exact bypass at Peak Reduction == 0: skip the whole gain path
    // (detector/history state still advances below so engaging reduction
    // mid-stream starts from a settled, continuous state).
    const bool bypassed = amount01 <= 0.0f;

    const auto thresholdDb = juce::jmap (amount01, 0.0f, 1.0f, 0.0f, thresholdMinDb);
    const auto ratio = juce::jmap (amount01, 0.0f, 1.0f, 1.0f, maxRatio);
    const auto ratioFactor = 1.0f - (1.0f / ratio);

    const auto detectorCoeff = std::exp (-1.0 / (detectorTimeSeconds * sampleRate));
    const auto attackCoeff = std::exp (-1.0 / (attackTimeSeconds * sampleRate));
    const auto historyCoeff = std::exp (-1.0 / (historyTimeSeconds * sampleRate));

    float peakGainReductionDb = 0.0f;

    for (size_t channel = 0; channel < numChannels && channel < detectorState.size(); ++channel)
    {
        auto* data = block.getChannelPointer (channel);
        auto& detector = detectorState[channel];
        auto& grDb = grSmoothedDb[channel];
        auto& history = historyState[channel];

        // Two-stage program-dependent release: interpolate the release time
        // constant from the fast (~60 ms) to the slow (~600 ms) stage based
        // on the channel's light-history at the start of this block, and
        // derive the one-pole coefficient once per block (an exp() per
        // sample would be needlessly expensive; the history moves on a
        // ~1.5 s time scale, far slower than any block).
        const auto historyNow = juce::jlimit (0.0f, 1.0f, history);
        const auto releaseSeconds = fastReleaseSeconds + static_cast<double> (historyNow) * (slowReleaseSeconds - fastReleaseSeconds);
        const auto releaseCoeff = std::exp (-1.0 / (releaseSeconds * sampleRate));

        for (size_t sample = 0; sample < numSamples; ++sample)
        {
            const auto inputSample = data[sample];

            // Level detector (RMS-ish squared one-pole).
            const auto rectified = inputSample * inputSample;
            detector = static_cast<float> (detectorCoeff * detector + (1.0 - detectorCoeff) * rectified);

            const auto levelDb = juce::Decibels::gainToDecibels (std::sqrt (juce::jmax (detector, 1.0e-12f)), -120.0f);
            const auto targetGrDb = juce::jmax (0.0f, levelDb - thresholdDb) * ratioFactor;

            // Gain-domain ballistics: fixed attack, history-dependent release.
            const auto coeff = targetGrDb > grDb ? attackCoeff : releaseCoeff;
            grDb = static_cast<float> (coeff * grDb + (1.0 - coeff) * targetGrDb);

            // Light-history: integrates applied GR toward [0, 1]; saturates
            // at historyFullScaleGrDb of sustained reduction.
            const auto historyTarget = juce::jlimit (0.0f, 1.0f, grDb / historyFullScaleGrDb);
            history = static_cast<float> (historyCoeff * history + (1.0 - historyCoeff) * historyTarget);

            if (bypassed)
                continue;

            const auto gainFactor = juce::Decibels::decibelsToGain (-grDb);
            data[sample] = inputSample * gainFactor * makeupGainLinear;
            peakGainReductionDb = juce::jmax (peakGainReductionDb, grDb);
        }
    }

    currentGainReductionDb = bypassed ? 0.0f : peakGainReductionDb;
}
