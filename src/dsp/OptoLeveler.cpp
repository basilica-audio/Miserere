#include "OptoLeveler.h"

void OptoLeveler::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    const auto numChannels = static_cast<size_t> (spec.numChannels);

    cells.assign (numChannels, {});
    feedbackSample.assign (numChannels, 0.0f);
    currentGain.assign (numChannels, 1.0f);

    emphasisFilters.clear();
    emphasisFilters.resize (numChannels); // Filter<float> is move-only, so resize() rather than assign()

    for (auto& filter : emphasisFilters)
        filter.coefficients = emphasisCoefficients;

    driveSmoothed.reset (sampleRate, driveSmoothingTimeSeconds);
    driveSmoothed.setCurrentAndTargetValue (lastAmount01);

    postAttenuatorCompensation = TapeSaturator::compensationForDrive (postAttenuatorDriveLinear);

    // Dark divider gain: Rcell = Rdark.
    {
        const auto rDark = msrr::OptoCellParams().rDarkOhm;
        const auto rp = rDark * loadOhm / (rDark + loadOhm);
        darkGain = rp / (seriesOhm + rp);
    }

    reset();

    msrr::applyBiquadCoefficients (*emphasisCoefficients,
        msrr::makeMatchedLowShelf (sampleRate, emphasisFreqHz, emphasisShelfQ, -emphasisAmount * emphasisMaxCutDb));
}

void OptoLeveler::reset()
{
    for (auto& cell : cells)
        cell.reset();

    std::fill (feedbackSample.begin(), feedbackSample.end(), 0.0f);
    std::fill (currentGain.begin(), currentGain.end(), 1.0f);

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

    // Peak Reduction -> sidechain gain 0..+40 dB (see class comment).
    const auto sidechainGainDb = juce::jlimit (0.0f, 1.0f, driveSmoothed.skip (static_cast<int> (numSamples))) * maxSidechainGainDb;
    const auto sidechainGain = static_cast<double> (juce::Decibels::decibelsToGain (sidechainGainDb));

    // R37 emphasis shelf (matched low shelf, sidechain only - brief F2).
    msrr::applyBiquadCoefficients (*emphasisCoefficients,
        msrr::makeMatchedLowShelf (sampleRate, emphasisFreqHz, emphasisShelfQ, -emphasisAmount * emphasisMaxCutDb));

    // Limit/Compress tuple: sidechain loop gain + EL drive curve set.
    const auto driverGain = limitEnabled ? limitDriverGain : compressDriverGain;
    const auto elKnee = limitEnabled ? limitElKneeB : elKneeB;

    const auto T = 1.0 / sampleRate;
    const auto numChannelsToProcess = juce::jmin (numChannels, cells.size());
    const auto numSidechains = linked ? std::min<size_t> (1, numChannelsToProcess) : numChannelsToProcess;

    // Graceful re-seed after non-finite abuse (NaN/Inf feed, brief
    // section 6.12): carrier states back to dark equilibrium, sidechain
    // filter and feedback tap cleared. Block-rate check - within the
    // poisoned block the engine's output sanitiser covers the sum.
    for (size_t channel = 0; channel < numChannelsToProcess; ++channel)
    {
        if (! std::isfinite (feedbackSample[channel]))
        {
            feedbackSample[channel] = 0.0f;
            currentGain[channel] = 1.0f;
            emphasisFilters[juce::jmin (channel, emphasisFilters.size() - 1)].reset();
            cells[juce::jmin (channel, cells.size() - 1)].reset();
        }
    }

    for (auto& cell : cells)
        cell.sanitise();

    float peakGainReductionDb = 0.0f;

    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        // --- Sidechain update(s) from the PREVIOUS output sample(s)
        // (one-sample feedback tap). Linked: one shared sidechain fed the
        // max-abs of both channels (the EL law is even, so the magnitude
        // choice is exact); unlinked: one per channel.
        for (size_t sc = 0; sc < numSidechains; ++sc)
        {
            float det = 0.0f;

            if (linked)
            {
                for (size_t channel = 0; channel < numChannelsToProcess; ++channel)
                    if (std::abs (feedbackSample[channel]) > std::abs (det))
                        det = feedbackSample[channel];
            }
            else
            {
                det = feedbackSample[sc];
            }

            const auto emphasised = static_cast<double> (emphasisFilters[sc].processSample (det));

            // 6AQ5 driver soft clip.
            const auto driven = driverRailVolts * std::tanh (driverGain * sidechainGain * emphasised / driverRailVolts);

            // EL panel -> photo-generation (audio rate, no rectifier).
            const auto g = std::min (msrr::electroluminance (driven, elCoefficientB0, elKnee), generationCeiling);

            const auto rCell = cells[sc].processSample (g, T);

            const auto rp = rCell * loadOhm / (rCell + loadOhm);
            const auto a = rp / (seriesOhm + rp);

            currentGain[sc] = static_cast<float> (a / darkGain);
        }

        // --- Audio path: divider gain a[n-1] -> exempt colour tanh.
        for (size_t channel = 0; channel < numChannelsToProcess; ++channel)
        {
            auto* data = block.getChannelPointer (channel);

            const auto gain = currentGain[linked ? 0 : channel];
            const auto attenuated = data[sample] * gain;

            // ADAA-EXEMPT memoryless colour stage (brief F1 exemption
            // rule - preserves SANDWICH sample alignment).
            const auto y = TapeSaturator::processSample (attenuated, postAttenuatorDriveLinear, postAttenuatorCompensation);

            data[sample] = y;
            feedbackSample[channel] = y;

            const auto reductionDb = juce::Decibels::gainToDecibels (juce::jmax (1.0e-6f, gain));
            peakGainReductionDb = juce::jmax (peakGainReductionDb, -reductionDb);
        }
    }

    currentGainReductionDb = peakGainReductionDb;
}
