#include "OptoLeveler.h"

float OptoLeveler::staticCurveOutputDb (float levelDb, bool limitEnabled) noexcept
{
    if (levelDb <= breakawayDb)
        return levelDb;

    const auto kneeRatio = limitEnabled ? limitKneeRatio : normalKneeRatio;
    const auto kneeCeilingDb = breakawayDb + kneeRegionDb;

    if (levelDb <= kneeCeilingDb)
        return breakawayDb + (levelDb - breakawayDb) / kneeRatio;

    const auto outputAtCeiling = breakawayDb + kneeRegionDb / kneeRatio;
    return outputAtCeiling + (levelDb - kneeCeilingDb) / ceilingRatio;
}

void OptoLeveler::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    const auto numChannels = static_cast<size_t> (spec.numChannels);
    fastPathState.assign (numChannels, 0.0f);
    midPathState.assign (numChannels, 0.0f);
    slowPathState.assign (numChannels, 0.0f);

    emphasisFilters.clear();
    emphasisFilters.resize (numChannels); // Filter<float> is move-only, so resize() rather than assign()

    for (auto& filter : emphasisFilters)
        filter.coefficients = emphasisCoefficients;

    driveSmoothed.reset (sampleRate, driveSmoothingTimeSeconds);
    driveSmoothed.setCurrentAndTargetValue (lastAmount01);

    postAttenuatorCompensation = TapeSaturator::compensationForDrive (postAttenuatorDriveLinear);

    reset();

    msrr::applyBiquadCoefficients (*emphasisCoefficients,
        juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (
            sampleRate, emphasisFreqHz, emphasisShelfQ, juce::Decibels::decibelsToGain (-emphasisAmount * emphasisMaxCutDb)));
}

void OptoLeveler::reset()
{
    std::fill (fastPathState.begin(), fastPathState.end(), 0.0f);
    std::fill (midPathState.begin(), midPathState.end(), 0.0f);
    std::fill (slowPathState.begin(), slowPathState.end(), 0.0f);

    for (auto& filter : emphasisFilters)
        filter.reset();

    currentGainReductionDb = 0.0f;
}

void OptoLeveler::setPeakReductionProportion (float newAmount01) noexcept
{
    lastAmount01 = juce::jlimit (0.0f, 1.0f, newAmount01);
    driveSmoothed.setTargetValue (lastAmount01);
}

void OptoLeveler::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto numChannels = block.getNumChannels();
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0 || numChannels == 0)
        return;

    // Peak Reduction is an input drive into the fixed static curve (0-18 dB
    // at 100%), not a threshold - see class comment.
    constexpr float maxDriveDb = 18.0f;
    const auto driveDb = juce::jlimit (0.0f, 1.0f, driveSmoothed.skip (static_cast<int> (numSamples))) * maxDriveDb;
    const auto driveGainLinear = juce::Decibels::decibelsToGain (driveDb);

    msrr::applyBiquadCoefficients (*emphasisCoefficients,
        juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (
            sampleRate, emphasisFreqHz, emphasisShelfQ, juce::Decibels::decibelsToGain (-emphasisAmount * emphasisMaxCutDb)));

    // Each path's own fixed attack/release coefficients - see class
    // comment (the mid/slow paths' slow attack, not a history accumulator,
    // is what makes their contribution genuinely depend on how long the
    // signal has been loud).
    const auto fastAttackCoeff = std::exp (-1.0 / (fastAttackSeconds * sampleRate));
    const auto fastReleaseCoeff = std::exp (-1.0 / (fastReleaseSeconds * sampleRate));
    const auto midAttackCoeff = std::exp (-1.0 / (midAttackSeconds * sampleRate));
    const auto midReleaseCoeff = std::exp (-1.0 / (midReleaseSeconds * sampleRate));
    const auto slowAttackCoeff = std::exp (-1.0 / (slowAttackSeconds * sampleRate));
    const auto slowReleaseCoeff = std::exp (-1.0 / (slowReleaseSeconds * sampleRate));

    const auto numChannelsToProcess = juce::jmin (numChannels, fastPathState.size());

    float peakGainReductionDb = 0.0f;

    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        // Linked detection: both channels' filters/envelopes are fed the
        // signed sample of whichever channel currently has the larger
        // magnitude, so a hard-panned burst produces identical gain
        // reduction on both channels (guarantee 10). Unlinked (default):
        // each channel is fed its own sample.
        float sharedDrivenSample = 0.0f;

        if (linked)
        {
            auto largestAbs = -1.0f;

            for (size_t channel = 0; channel < numChannelsToProcess; ++channel)
            {
                const auto driven = block.getChannelPointer (channel)[sample] * driveGainLinear;

                if (std::abs (driven) > largestAbs)
                {
                    largestAbs = std::abs (driven);
                    sharedDrivenSample = driven;
                }
            }
        }

        for (size_t channel = 0; channel < numChannelsToProcess; ++channel)
        {
            auto* data = block.getChannelPointer (channel);
            auto& fastPath = fastPathState[channel];
            auto& midPath = midPathState[channel];
            auto& slowPath = slowPathState[channel];
            auto& filter = emphasisFilters[channel];

            const auto drivenSample = data[sample] * driveGainLinear;
            const auto detectorInput = linked ? sharedDrivenSample : drivenSample;
            const auto emphasised = filter.processSample (detectorInput);
            const auto rectified = emphasised * emphasised;

            fastPath = static_cast<float> ((rectified > fastPath ? fastAttackCoeff : fastReleaseCoeff) * fastPath
                                            + (1.0 - (rectified > fastPath ? fastAttackCoeff : fastReleaseCoeff)) * rectified);
            midPath = static_cast<float> ((rectified > midPath ? midAttackCoeff : midReleaseCoeff) * midPath
                                           + (1.0 - (rectified > midPath ? midAttackCoeff : midReleaseCoeff)) * rectified);
            slowPath = static_cast<float> ((rectified > slowPath ? slowAttackCoeff : slowReleaseCoeff) * slowPath
                                            + (1.0 - (rectified > slowPath ? slowAttackCoeff : slowReleaseCoeff)) * rectified);

            // The two-stage (fast-then-slow), history-dependent release
            // falls straight out of taking the max of three paths whose
            // mid/slow attacks are themselves slow - see class comment.
            const auto combined = juce::jmax (fastPath, juce::jmax (midPath, slowPath));
            const auto levelDb = juce::Decibels::gainToDecibels (std::sqrt (juce::jmax (combined, 1.0e-12f)), -120.0f);
            const auto outputDb = staticCurveOutputDb (levelDb, limitEnabled);
            const auto reductionDb = juce::jmax (0.0f, levelDb - outputDb);

            const auto gainFactor = juce::Decibels::decibelsToGain (-reductionDb);
            const auto attenuated = drivenSample * gainFactor;

            // Gentle, always-on, level-dependent tube/transformer-style
            // colouration after the (clean) attenuator - see class comment.
            data[sample] = TapeSaturator::processSample (attenuated, postAttenuatorDriveLinear, postAttenuatorCompensation);

            peakGainReductionDb = juce::jmax (peakGainReductionDb, reductionDb);
        }
    }

    currentGainReductionDb = peakGainReductionDb;
}
