#include "SpreadPitch.h"

namespace
{
    float centsToRatio (float cents) noexcept
    {
        return std::exp2 (cents / 1200.0f);
    }
}

void SpreadPitch::Voice::prepare (const juce::dsp::ProcessSpec& monoSpec, float maxDelaySamples)
{
    delayLine.setMaximumDelayInSamples (static_cast<int> (std::ceil (maxDelaySamples)) + 4);
    delayLine.prepare (monoSpec);
}

void SpreadPitch::Voice::reset (float baseSamples, float grainSamples)
{
    delayLine.reset();

    // The two taps start a half-grain apart, so one is always near the
    // middle of its crossfade window (full gain) while the other is near
    // an edge (fading in/out) - see class comment.
    tapDelaySamples[0] = baseSamples;
    tapDelaySamples[1] = baseSamples - grainSamples * 0.5f;
}

float SpreadPitch::Voice::processSample (float input, float baseSamples, float grainSamples) noexcept
{
    const auto halfGrain = grainSamples * 0.5f;
    const auto maxDelay = static_cast<float> (delayLine.getMaximumDelayInSamples());

    float outSample = 0.0f;

    for (size_t tap = 0; tap < 2; ++tap)
    {
        // Read speed != write speed (1 sample/sample) is what produces the
        // pitch shift: the delay shrinks (pitch up, ratio > 1) or grows
        // (pitch down, ratio < 1) by (ratio - 1) samples every sample.
        tapDelaySamples[tap] -= (pitchRatio - 1.0f);

        if (tapDelaySamples[tap] < baseSamples - halfGrain)
            tapDelaySamples[tap] += grainSamples;
        else if (tapDelaySamples[tap] > baseSamples + halfGrain)
            tapDelaySamples[tap] -= grainSamples;

        const auto posInGrain = juce::jlimit (0.0f, 1.0f, (tapDelaySamples[tap] - (baseSamples - halfGrain)) / grainSamples);

        // Equal-power (sin) crossfade (brief F6): with the taps half a
        // grain apart, gain1^2 + gain2^2 == 1 - constant summed power for
        // the decorrelated taps, minimal envelope ripple.
        const auto gain = std::sin (juce::MathConstants<float>::pi * posInGrain);

        const auto delayClamped = juce::jlimit (2.0f, maxDelay, tapDelaySamples[tap]);
        const auto updateReadPointer = tap == 1; // advance the shared write/read cursor exactly once per input sample
        outSample += delayLine.popSample (0, delayClamped, updateReadPointer) * gain;
    }

    delayLine.pushSample (0, input);
    return outSample;
}

void SpreadPitch::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    juce::dsp::ProcessSpec monoSpec = spec;
    monoSpec.numChannels = 1;

    const auto maxBaseMs = juce::jmax (baseDelayUpMs, baseDelayDownMs) * maxTimeScale;
    const auto maxDelaySamples = static_cast<float> ((maxBaseMs + grainMs + capacityHeadroomMs) * 0.001 * sampleRate);

    voiceUp.prepare (monoSpec, maxDelaySamples);
    voiceDown.prepare (monoSpec, maxDelaySamples);

    widthSmoothed.reset (sampleRate, smoothingTimeSeconds);
    widthSmoothed.setCurrentAndTargetValue (width);
    detuneSmoothed.reset (sampleRate, smoothingTimeSeconds);
    detuneSmoothed.setCurrentAndTargetValue (detuneCents);
    timeScaleSmoothed.reset (sampleRate, smoothingTimeSeconds);
    timeScaleSmoothed.setCurrentAndTargetValue (timeScale);

    reset();
}

void SpreadPitch::setDetuneCents (float cents) noexcept
{
    detuneCents = juce::jlimit (0.0f, 15.0f, cents);
    detuneSmoothed.setTargetValue (detuneCents);
}

void SpreadPitch::setTimeScale (float scale) noexcept
{
    timeScale = juce::jlimit (0.5f, 2.0f, scale);
    timeScaleSmoothed.setTargetValue (timeScale);
}

void SpreadPitch::setWidth (float amount01) noexcept
{
    width = juce::jlimit (0.0f, 1.0f, amount01);
    widthSmoothed.setTargetValue (width);
}

void SpreadPitch::reset()
{
    const auto grainSamples = static_cast<float> (grainMs * 0.001 * sampleRate);

    detuneSmoothed.setCurrentAndTargetValue (detuneCents);
    timeScaleSmoothed.setCurrentAndTargetValue (timeScale);
    widthSmoothed.setCurrentAndTargetValue (width);

    voiceUp.reset (static_cast<float> (baseDelayUpMs * timeScale * 0.001 * sampleRate), grainSamples);
    voiceDown.reset (static_cast<float> (baseDelayDownMs * timeScale * 0.001 * sampleRate), grainSamples);

    voiceUp.pitchRatio = centsToRatio (detuneCents);
    voiceDown.pitchRatio = centsToRatio (-detuneCents);
}

void SpreadPitch::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto numChannels = block.getNumChannels();
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0 || numChannels == 0)
        return;

    const auto grainSamples = static_cast<float> (grainMs * 0.001 * sampleRate);
    const auto samplesPerMsUp = static_cast<float> (baseDelayUpMs * 0.001 * sampleRate);
    const auto samplesPerMsDown = static_cast<float> (baseDelayDownMs * 0.001 * sampleRate);

    auto* left = block.getChannelPointer (0);
    auto* right = numChannels > 1 ? block.getChannelPointer (1) : nullptr;

    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        // Per-sample smoothed detune/timeScale (brief F6 - no block steps).
        const auto detuneNow = detuneSmoothed.getNextValue();
        const auto timeScaleNow = timeScaleSmoothed.getNextValue();

        voiceUp.pitchRatio = centsToRatio (detuneNow);
        voiceDown.pitchRatio = centsToRatio (-detuneNow);

        const auto monoInput = right != nullptr ? 0.5f * (left[sample] + right[sample]) : left[sample];

        const auto up = voiceUp.processSample (monoInput, samplesPerMsUp * timeScaleNow, grainSamples);
        const auto down = voiceDown.processSample (monoInput, samplesPerMsDown * timeScaleNow, grainSamples);

        const auto widthNow = juce::jlimit (0.0f, 1.0f, widthSmoothed.getNextValue());
        const auto centre = 0.5f * (up + down);

        left[sample] = juce::jmap (widthNow, centre, up);

        if (right != nullptr)
            right[sample] = juce::jmap (widthNow, centre, down);
    }
}
