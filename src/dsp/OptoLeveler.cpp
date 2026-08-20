#include "OptoLeveler.h"

OptoLeveler::ColourTuple OptoLeveler::colourTupleFor (Colour c) noexcept
{
    // See the class comment (issue #20). Classic is all-unity by
    // construction so the default colour is bit-identical to v0.5.0.
    switch (c)
    {
        case Colour::classic: return { 1.0, 1.0, 1.0, postAttenuatorDriveLinear };
        case Colour::quick:   return { 6.0, 2.449489743, 0.25, 1.02f }; // sqrt(6) mobility compensation
        case Colour::deep:    return { 0.4, 0.632455532, 3.0, 1.45f };  // sqrt(0.4)
    }

    return { 1.0, 1.0, 1.0, postAttenuatorDriveLinear };
}

void OptoLeveler::setColour (Colour newColour) noexcept
{
    if (colour == newColour)
        return;

    colour = newColour;

    // The kinetics swap is applied (state-preservingly) at the top of the
    // next process() call; only the colour-stage drive - an actual gain
    // term - needs the explicit ramp.
    colourDriveSmoothed.setTargetValue (colourTupleFor (colour).colourDriveLinear);
}

namespace
{
    msrr::OptoCellParams makeColourCellParams (const OptoLeveler::ColourTuple& tuple) noexcept
    {
        msrr::OptoCellParams params; // the T4B-recalibrated base set
        params.nuN *= tuple.nuNScale;
        params.muN *= tuple.muNScale;
        params.etaP *= tuple.etaPScale;
        return params;
    }
}

void OptoLeveler::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    const auto numChannels = static_cast<size_t> (spec.numChannels);

    cells.assign (numChannels, {});

    // Snap the colour's cell kinetics across a prepare (setParams resets to
    // the new dark equilibrium, which reset() below re-establishes anyway).
    const auto colourTuple = colourTupleFor (colour);
    for (auto& cell : cells)
        cell.setParams (makeColourCellParams (colourTuple));

    colourDriveSmoothed.reset (sampleRate, driveSmoothingTimeSeconds);
    colourDriveSmoothed.setCurrentAndTargetValue (colourTuple.colourDriveLinear);
    feedbackSample.assign (numChannels, 0.0f);
    currentGain.assign (numChannels, 1.0f);

    emphasisFilters.clear();
    emphasisFilters.resize (numChannels); // Filter<float> is move-only, so resize() rather than assign()

    for (auto& filter : emphasisFilters)
        filter.coefficients = emphasisCoefficients;

    driveSmoothed.reset (sampleRate, driveSmoothingTimeSeconds);
    driveSmoothed.setCurrentAndTargetValue (lastAmount01);

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

    currentGainReductionDb.store (0.0f, std::memory_order_relaxed);
}

void OptoLeveler::setPeakReductionProportion (float newAmount01) noexcept
{
    lastAmount01 = juce::jlimit (0.0f, 1.0f, newAmount01);
    driveSmoothed.setTargetValue (lastAmount01);
}

void OptoLeveler::process (juce::dsp::AudioBlock<float>& block,
                           const juce::dsp::AudioBlock<const float>* externalKey) noexcept
{
    const auto numChannels = block.getNumChannels();
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0 || numChannels == 0)
        return;

    // External sidechain (issue #23): the key REPLACES the feedback panel
    // drive, turning this into a feed-forward keyed leveler - see the class
    // comment. Fewer key channels than audio channels is legal (the last
    // key channel is reused).
    const auto keyChannels = externalKey != nullptr ? externalKey->getNumChannels() : 0;
    const auto useKey = keyChannels > 0 && externalKey->getNumSamples() >= numSamples;

    // Peak Reduction -> sidechain gain 0..+40 dB (see class comment).
    const auto sidechainGainDb = juce::jlimit (0.0f, 1.0f, driveSmoothed.skip (static_cast<int> (numSamples))) * maxSidechainGainDb;
    const auto sidechainGain = static_cast<double> (juce::Decibels::decibelsToGain (sidechainGainDb));

    // R37 emphasis shelf (matched low shelf, sidechain only - brief F2).
    msrr::applyBiquadCoefficients (*emphasisCoefficients,
        msrr::makeMatchedLowShelf (sampleRate, emphasisFreqHz, emphasisShelfQ, -emphasisAmount * emphasisMaxCutDb));

    // Limit/Compress tuple: sidechain loop gain + EL drive curve set.
    const auto driverGain = limitEnabled ? limitDriverGain : compressDriverGain;
    const auto elKnee = limitEnabled ? limitElKneeB : elKneeB;

    // Colour tuple (issue #20): state-preserving kinetics update at block
    // rate (continuous charge states - no gain step), plus the smoothed
    // colour-stage drive with its per-block compensation. For the default
    // Classic colour these are exactly the v0.5.0 constants.
    const auto colourCellParams = makeColourCellParams (colourTupleFor (colour));
    for (auto& cell : cells)
        cell.updateParamsPreservingState (colourCellParams);

    const auto colourDrive = colourDriveSmoothed.skip (static_cast<int> (numSamples));
    const auto colourCompensation = TapeSaturator::compensationForDrive (colourDrive);

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

            if (useKey)
            {
                if (linked)
                {
                    for (size_t channel = 0; channel < numChannelsToProcess; ++channel)
                    {
                        const auto keySample = externalKey->getChannelPointer (juce::jmin (channel, keyChannels - 1))[sample];

                        if (std::isfinite (keySample) && std::abs (keySample) > std::abs (det))
                            det = keySample;
                    }
                }
                else
                {
                    const auto keySample = externalKey->getChannelPointer (juce::jmin (sc, keyChannels - 1))[sample];
                    det = std::isfinite (keySample) ? keySample : 0.0f;
                }
            }
            else if (linked)
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
            // rule - preserves SANDWICH sample alignment). Drive is the
            // per-colour (smoothed) value - Classic == the fixed 1.15.
            const auto y = TapeSaturator::processSample (attenuated, colourDrive, colourCompensation);

            data[sample] = y;
            feedbackSample[channel] = y;

            const auto reductionDb = juce::Decibels::gainToDecibels (juce::jmax (1.0e-6f, gain));
            peakGainReductionDb = juce::jmax (peakGainReductionDb, -reductionDb);
        }
    }

    currentGainReductionDb.store (peakGainReductionDb, std::memory_order_relaxed);
}
