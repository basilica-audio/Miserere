#include "OutputLimiter.h"

#include <cmath>

void OutputLimiter::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    ceilingDbSmoothed.reset (sampleRate, ceilingSmoothingSeconds);
    ceilingDbSmoothed.setCurrentAndTargetValue (lastCeilingDb);

    updateReleaseCoefficient();

    reset();
}

void OutputLimiter::reset() noexcept
{
    currentGain = 1.0;
    ceilingDbSmoothed.setCurrentAndTargetValue (lastCeilingDb);
    currentGainReductionDb.store (0.0f, std::memory_order_relaxed);
}

void OutputLimiter::setCeilingDb (float newCeilingDb) noexcept
{
    lastCeilingDb = newCeilingDb;
    ceilingDbSmoothed.setTargetValue (newCeilingDb);
}

void OutputLimiter::setReleaseMs (float newReleaseMs) noexcept
{
    const auto clamped = juce::jlimit (1.0f, 5000.0f, newReleaseMs);

    if (juce::approximatelyEqual (clamped, releaseMs))
        return;

    releaseMs = clamped;
    updateReleaseCoefficient();
}

void OutputLimiter::updateReleaseCoefficient() noexcept
{
    // 63 %-recovery one-pole, the convention used by every other release
    // control in this plugin.
    const auto tauSamples = juce::jmax (1.0, (static_cast<double> (releaseMs) * 0.001) * sampleRate);
    releaseCoefficient = static_cast<float> (1.0 - std::exp (-1.0 / tauSamples));
}

float OutputLimiter::staticGainFor (float peak, float ceilingDb) noexcept
{
    // Below the knee the stage is structurally transparent - EXACT 1.0f,
    // and no transcendental is evaluated at all.
    const auto kneeStartDb = ceilingDb - kneeDb * 0.5f;

    if (! (peak > 0.0f) || ! std::isfinite (peak))
        return 1.0f;

    const auto xDb = juce::Decibels::gainToDecibels (peak, -200.0f);

    if (xDb <= kneeStartDb)
        return 1.0f;

    // u^2/(2*knee) is the soft-knee interpolation; the outer min() makes
    // the ratio infinite above the knee (see the header's derivation - the
    // quadratic's own maximum over the knee is exactly ceilingDb, so the
    // two branches meet with matching value AND slope).
    const auto u = juce::jlimit (0.0f, kneeDb, xDb - kneeStartDb);
    const auto yDb = juce::jmin (xDb - (u * u) / (2.0f * kneeDb), ceilingDb);

    const auto gain = juce::Decibels::decibelsToGain (yDb - xDb);

    // Belt and braces against the dB round trip's own float error: ABOVE
    // the ceiling the exact linear solution is known in closed form, so
    // clamp to it. Inside the knee the quotient is > 1 and this is inert.
    // What remains is at most one ulp from the division itself - which is
    // what "exact sample-peak ceiling" means in float arithmetic.
    const auto ceilingLinear = juce::Decibels::decibelsToGain (ceilingDb);

    return peak > ceilingLinear ? juce::jmin (gain, ceilingLinear / peak) : gain;
}

void OutputLimiter::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto numSamples = block.getNumSamples();
    const auto numChannels = block.getNumChannels();

    if (numSamples == 0 || numChannels == 0)
        return;

    if (! enabled)
    {
        // Bit-exact bypass: not one sample is read or written, and the loop
        // is parked at a settled unity so engaging the limiter mid-stream
        // starts from a wire rather than from a stale gain.
        currentGain = 1.0;
        ceilingDbSmoothed.setCurrentAndTargetValue (lastCeilingDb);
        currentGainReductionDb.store (0.0f, std::memory_order_relaxed);
        return;
    }

    auto minGain = 1.0f;

    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        const auto ceilingNowDb = ceilingDbSmoothed.getNextValue();

        // Permanently LINKED detection (see the header): one gain for the
        // whole frame, so a peak can never pan the image.
        auto peak = 0.0f;

        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            const auto value = block.getChannelPointer (channel)[sample];

            if (std::isfinite (value))
                peak = juce::jmax (peak, std::abs (value));
        }

        const auto staticGain = static_cast<double> (staticGainFor (peak, ceilingNowDb));

        if (staticGain < currentGain)
        {
            currentGain = staticGain; // instantaneous attack - no lookahead, no ramp
        }
        else
        {
            // First-order approach FROM BELOW: monotone, never overshoots
            // its target, so recovery alone can never breach the ceiling.
            currentGain += static_cast<double> (releaseCoefficient) * (staticGain - currentGain);

            // A settled limiter is a wire. Only taken while there is
            // nothing to limit (staticGain >= 1), so this can never round a
            // limiting gain up over the ceiling.
            if (staticGain >= 1.0 && currentGain > 1.0 - static_cast<double> (unitySnapEpsilon))
                currentGain = 1.0;
        }

        const auto gain = static_cast<float> (currentGain);

        for (size_t channel = 0; channel < numChannels; ++channel)
            block.getChannelPointer (channel)[sample] *= gain;

        minGain = juce::jmin (minGain, gain);
    }

    currentGainReductionDb.store (minGain < 1.0f ? -juce::Decibels::gainToDecibels (minGain, -100.0f) : 0.0f,
                                  std::memory_order_relaxed);
}
