#include "SlapDelay.h"

namespace
{
    float clampBelowNyquist (float frequencyHz, double sampleRate) noexcept
    {
        const auto nyquist = static_cast<float> (sampleRate) * 0.5f;
        return juce::jlimit (10.0f, nyquist * 0.9f, frequencyHz);
    }
}

void SlapDelay::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    // Full delay-line allocation happens here, never on the audio thread:
    // capacity for the maximum 160 ms at the current sample rate plus
    // interpolation headroom.
    const auto maxDelaySamples = static_cast<int> (std::ceil (sampleRate * maxDelayMs / 1000.0)) + 4;
    delayLine.setMaximumDelayInSamples (maxDelaySamples);
    delayLine.prepare (spec);

    const auto numChannels = static_cast<size_t> (spec.numChannels);
    repeatLowPass.clear();
    repeatLowPass.resize (numChannels); // Filter<float> is move-only, so resize() rather than assign()

    for (auto& filter : repeatLowPass)
        filter.coefficients = lowPassCoefficients;

    msrr::applyBiquadCoefficients (*lowPassCoefficients,
        juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (
            sampleRate, clampBelowNyquist (juce::jmap (lastTone01, toneLowPassBrightHz, toneLowPassDarkHz), sampleRate), loopFilterQ));

    delayMsSmoothed.reset (sampleRate, delaySmoothingSeconds);
    delayMsSmoothed.setCurrentAndTargetValue (lastDelayMs);
    toneSmoothed.reset (sampleRate, smoothingTimeSeconds);
    toneSmoothed.setCurrentAndTargetValue (lastTone01);

    reset();
}

void SlapDelay::reset()
{
    delayLine.reset();

    for (auto& filter : repeatLowPass)
        filter.reset();
}

void SlapDelay::setDelayMs (float newDelayMs) noexcept
{
    lastDelayMs = juce::jlimit (minDelayMs, maxDelayMs, newDelayMs);
    delayMsSmoothed.setTargetValue (lastDelayMs);
}

void SlapDelay::setToneProportion (float newAmount01) noexcept
{
    lastTone01 = juce::jlimit (0.0f, 1.0f, newAmount01);
    toneSmoothed.setTargetValue (lastTone01);
}

void SlapDelay::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto numChannels = block.getNumChannels();
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0 || numChannels == 0)
        return;

    // Parameters advance once per block (block-rate coefficient updates,
    // the standard suite compromise). Delay time itself is also applied at
    // block rate - its dedicated slow smoother spreads any user drag across
    // many blocks, so per-block steps stay well under a sample of jump.
    const auto delayMs = juce::jlimit (minDelayMs, maxDelayMs, delayMsSmoothed.skip (static_cast<int> (numSamples)));
    const auto tone01 = juce::jlimit (0.0f, 1.0f, toneSmoothed.skip (static_cast<int> (numSamples)));

    const auto lowPassHz = clampBelowNyquist (juce::jmap (tone01, toneLowPassBrightHz, toneLowPassDarkHz), sampleRate);
    msrr::applyBiquadCoefficients (*lowPassCoefficients,
        juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (sampleRate, lowPassHz, loopFilterQ));

    const auto satDriveLinear = juce::jmap (tone01, toneSatDriveMin, toneSatDriveMax);
    const auto satCompensation = TapeSaturator::compensationForDrive (satDriveLinear);

    const auto delaySamples = juce::jlimit (
        1.0f,
        static_cast<float> (delayLine.getMaximumDelayInSamples()),
        delayMs * 0.001f * static_cast<float> (sampleRate));

    auto* left = block.getChannelPointer (0);
    auto* right = numChannels > 1 ? block.getChannelPointer (1) : nullptr;

    // The plugin's bus layout is constrained to mono/stereo (see
    // PluginProcessor::isBusesLayoutSupported); the jmin is a defensive
    // clamp for any wider block handed to the raw engine.
    const auto channelsToProcess = juce::jmin (numChannels, static_cast<size_t> (2), repeatLowPass.size());

    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        const auto monoInput = right != nullptr ? 0.5f * (left[sample] + right[sample]) : left[sample];

        for (size_t channel = 0; channel < channelsToProcess; ++channel)
        {
            auto* data = channel == 0 ? left : right;
            if (data == nullptr)
                break;

            const auto input = stereoEnabled ? data[sample] : monoInput;

            // Pop-before-push (see the delay-line convention documented in
            // SpreadPitch.cpp/the v1 ancestor of this module).
            auto wet = delayLine.popSample (static_cast<int> (channel), delaySamples);

            // Feedback is fixed at 0 (design brief v2: "single repeat") -
            // the darkening lowpass and soft saturation are applied ONCE to
            // the tap itself (there is no loop to voice progressively), the
            // structural difference from the v1 feedback-loop design.
            wet = repeatLowPass[channel].processSample (wet);
            wet = TapeSaturator::processSample (wet, satDriveLinear, satCompensation);

            delayLine.pushSample (static_cast<int> (channel), input);

            data[sample] = wet;
        }
    }
}
