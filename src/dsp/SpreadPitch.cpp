#include "SpreadPitch.h"

namespace
{
    float centsToRatio (float cents) noexcept
    {
        return std::pow (2.0f, cents / 1200.0f);
    }
}

void SpreadPitch::Voice::prepare (const juce::dsp::ProcessSpec& monoSpec, float maxDelaySamples)
{
    delayLine.setMaximumDelayInSamples (static_cast<int> (std::ceil (maxDelaySamples)) + 4);
    delayLine.prepare (monoSpec);
}

void SpreadPitch::Voice::reset (float baseMs, double sr)
{
    baseDelayMs = baseMs;
    delayLine.reset();

    const auto baseSamples = static_cast<float> (baseMs * 0.001 * sr);
    const auto grainSamples = static_cast<float> (grainMs * 0.001 * sr);

    // The two taps start a half-grain apart, so one is always near the
    // middle of its crossfade window (full gain) while the other is near
    // an edge (fading in/out) - see class comment.
    tapDelaySamples[0] = baseSamples;
    tapDelaySamples[1] = baseSamples - grainSamples * 0.5f;
}

float SpreadPitch::Voice::processSample (float input, float grainSamples, double sr) noexcept
{
    const auto baseSamples = static_cast<float> (baseDelayMs * 0.001 * sr);
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
        const auto gain = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * posInGrain));

        const auto delayClamped = juce::jlimit (1.0f, maxDelay, tapDelaySamples[tap]);
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

    reset();
}

void SpreadPitch::setWidth (float amount01) noexcept
{
    width = juce::jlimit (0.0f, 1.0f, amount01);
    widthSmoothed.setTargetValue (width);
}

void SpreadPitch::reset()
{
    voiceUp.reset (baseDelayUpMs * timeScale, sampleRate);
    voiceDown.reset (baseDelayDownMs * timeScale, sampleRate);

    voiceUp.pitchRatio = centsToRatio (detuneCents);
    voiceDown.pitchRatio = centsToRatio (-detuneCents);
}

void SpreadPitch::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto numChannels = block.getNumChannels();
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0 || numChannels == 0)
        return;

    voiceUp.baseDelayMs = baseDelayUpMs * timeScale;
    voiceDown.baseDelayMs = baseDelayDownMs * timeScale;
    voiceUp.pitchRatio = centsToRatio (detuneCents);
    voiceDown.pitchRatio = centsToRatio (-detuneCents);

    const auto grainSamples = static_cast<float> (grainMs * 0.001 * sampleRate);

    auto* left = block.getChannelPointer (0);
    auto* right = numChannels > 1 ? block.getChannelPointer (1) : nullptr;

    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        const auto monoInput = right != nullptr ? 0.5f * (left[sample] + right[sample]) : left[sample];

        const auto up = voiceUp.processSample (monoInput, grainSamples, sampleRate);
        const auto down = voiceDown.processSample (monoInput, grainSamples, sampleRate);

        const auto widthNow = juce::jlimit (0.0f, 1.0f, widthSmoothed.getNextValue());
        const auto centre = 0.5f * (up + down);

        left[sample] = juce::jmap (widthNow, centre, up);

        if (right != nullptr)
            right[sample] = juce::jmap (widthNow, centre, down);
    }
}
