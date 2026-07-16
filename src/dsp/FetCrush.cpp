#include "FetCrush.h"

namespace
{
    // Standard soft-knee gain-reduction formula (Giannoulis, Massberg &
    // Reiss, "Digital Dynamic Range Compressor Design", JAES 2012):
    // returns the (non-negative) dB of reduction for a level `overDb` dB
    // above threshold, a given ratio and knee width.
    float softKneeReductionDb (float overDb, float ratio, float kneeDb) noexcept
    {
        const auto ratioFactor = 1.0f - (1.0f / ratio);

        if (2.0f * overDb < -kneeDb)
            return 0.0f;

        if (2.0f * std::abs (overDb) <= kneeDb)
        {
            const auto x = overDb + kneeDb * 0.5f;
            return ratioFactor * (x * x) / (2.0f * kneeDb);
        }

        return ratioFactor * overDb;
    }
}

FetCrush::RatioPoint FetCrush::ratioPointFor (Ratio r) noexcept
{
    switch (r)
    {
        case Ratio::r4:   return { 4.0f, -30.0f, 6.0f };
        case Ratio::r8:   return { 8.0f, -28.0f, 4.0f };
        case Ratio::r12:  return { 12.0f, -26.0f, 2.0f };
        case Ratio::r20:  return { 20.0f, -24.0f, 1.0f };
        case Ratio::rAll: return { 16.0f, -24.0f, 0.5f }; // "steep" first segment; see staticCurveReductionDb
    }

    return { 4.0f, -30.0f, 6.0f };
}

float FetCrush::staticCurveReductionDb (float levelDb, Ratio r, Style s) noexcept
{
    if (s == Style::gentle)
        return softKneeReductionDb (levelDb - (-32.0f), 2.0f, 8.0f); // fixed 2:1, softest knee (brief: "Gentle (2:1...)")

    const auto point = ratioPointFor (r);
    const auto overDb = levelDb - point.thresholdDb;

    if (r != Ratio::rAll)
        return softKneeReductionDb (overDb, point.ratio, point.kneeDb);

    // ALL mode: a steep ratio (16:1) just above the knee, giving back to a
    // softer ratio (10:1) above a fixed overshoot "kink" - a deliberately
    // non-monotonic gain-reduction slope (the plateau's "kink" the brief
    // specifies), continuous at the kink itself.
    constexpr float giveBackRatio = 10.0f;
    constexpr float kinkDb = 6.0f;

    if (overDb <= kinkDb)
        return softKneeReductionDb (overDb, point.ratio, point.kneeDb);

    const auto reductionAtKink = softKneeReductionDb (kinkDb, point.ratio, point.kneeDb);
    const auto giveBackFactor = 1.0f - (1.0f / giveBackRatio);
    return reductionAtKink + (overDb - kinkDb) * giveBackFactor;
}

void FetCrush::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    const auto numChannels = static_cast<size_t> (spec.numChannels);
    envelopeState.assign (numChannels, 0.0f);
    compressionDuration.assign (numChannels, 0.0f);

    reset();
}

void FetCrush::reset()
{
    std::fill (envelopeState.begin(), envelopeState.end(), 0.0f);
    std::fill (compressionDuration.begin(), compressionDuration.end(), 0.0f);
    currentGainReductionDb = 0.0f;
}

void FetCrush::setAttackStep (float step1to7) noexcept
{
    const auto step = juce::jlimit (1.0f, 7.0f, step1to7);
    attackUs = juce::jmap (step, 1.0f, 7.0f, attackMaxUs, attackMinUs);
}

void FetCrush::setReleaseStep (float step1to7) noexcept
{
    const auto step = juce::jlimit (1.0f, 7.0f, step1to7);
    releaseMs = juce::jmap (step, 1.0f, 7.0f, releaseMaxMs, releaseMinMs);
}

void FetCrush::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto numChannels = block.getNumChannels();
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0 || numChannels == 0)
        return;

    // ALL mode adds a short extra attack lag so the initial transient
    // overshoots through before the limiter clamps down hard (the
    // all-buttons "snap" - see class comment).
    const auto effectiveAttackUs = attackUs + (style == Style::allButtons && ratio == Ratio::rAll ? allButtonsAttackLagMs * 1000.0f : 0.0f);
    const auto attackCoeff = std::exp (-1.0 / (static_cast<double> (effectiveAttackUs) * 1.0e-6 * sampleRate));

    const auto fastReleaseCoeff = std::exp (-1.0 / (static_cast<double> (releaseMs) * 0.001 * sampleRate));
    const auto slowReleaseMs = releaseMs * slowReleaseMultiplier;
    const auto slowReleaseCoeff = std::exp (-1.0 / (static_cast<double> (slowReleaseMs) * 0.001 * sampleRate));

    const auto durationRiseCoeff = std::exp (-1.0 / (durationRiseTauSeconds * sampleRate));
    const auto durationFallCoeff = std::exp (-1.0 / (durationFallTauSeconds * sampleRate));

    float peakGainReductionDb = 0.0f;

    // Linked detection: both channels' envelopes track the combined (max
    // absolute) signal rather than their own - "dual mono" is the default
    // (unlinked); Link makes both channels react identically to a
    // hard-panned burst (guarantee 10).
    const auto numChannelsToProcess = juce::jmin (numChannels, envelopeState.size());

    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        float combinedDriven = 0.0f;

        if (linked)
        {
            for (size_t channel = 0; channel < numChannelsToProcess; ++channel)
                combinedDriven = juce::jmax (combinedDriven, std::abs (block.getChannelPointer (channel)[sample] * inputDriveLinear));
        }

        for (size_t channel = 0; channel < numChannelsToProcess; ++channel)
        {
            auto* data = block.getChannelPointer (channel);
            auto& envelope = envelopeState[channel];
            auto& duration = compressionDuration[channel];

            const auto drivenSample = data[sample] * inputDriveLinear;
            const auto detectorSample = linked ? combinedDriven : std::abs (drivenSample);

            const auto rectified = detectorSample * detectorSample;
            const auto durationNow = juce::jlimit (0.0f, 1.0f, duration);
            const auto releaseCoeff = static_cast<float> (fastReleaseCoeff + durationNow * (slowReleaseCoeff - fastReleaseCoeff));
            const auto coeff = rectified > envelope ? attackCoeff : releaseCoeff;
            envelope = static_cast<float> (coeff * envelope + (1.0 - coeff) * rectified);

            const auto envelopeDb = juce::Decibels::gainToDecibels (std::sqrt (juce::jmax (envelope, 1.0e-12f)), -120.0f);
            const auto reductionDb = juce::jmax (0.0f, staticCurveReductionDb (envelopeDb, ratio, style));

            // Compression-duration integrator: rises while actively
            // reducing gain, decays otherwise - governs the fast/slow
            // release blend above (dual-rate, program-dependent release).
            const auto durationTarget = reductionDb > durationGrEpsilonDb ? 1.0f : 0.0f;
            const auto durationCoeff = durationTarget > duration ? durationRiseCoeff : durationFallCoeff;
            duration = static_cast<float> (durationCoeff * duration + (1.0 - durationCoeff) * durationTarget);

            const auto gainFactor = juce::Decibels::decibelsToGain (-reductionDb);
            data[sample] = drivenSample * gainFactor * outputTrimLinear;

            peakGainReductionDb = juce::jmax (peakGainReductionDb, reductionDb);
        }
    }

    currentGainReductionDb = peakGainReductionDb;
}
