#include "SlapDelay.h"

namespace
{
    constexpr float loopFilterQ = 0.70710678f; // Butterworth

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
    // capacity for the maximum 180 ms at the current sample rate plus
    // interpolation headroom.
    const auto maxDelaySamples = static_cast<int> (std::ceil (sampleRate * maxDelayMs / 1000.0)) + 4;
    delayLine.setMaximumDelayInSamples (maxDelaySamples);
    delayLine.prepare (spec);

    const auto numChannels = static_cast<size_t> (spec.numChannels);
    loopHighPass.clear();
    loopHighPass.resize (numChannels); // Filter<float> is move-only, so resize() rather than assign()
    loopLowPass.clear();
    loopLowPass.resize (numChannels);

    for (auto& filter : loopHighPass)
        filter.coefficients = highPassCoefficients;
    for (auto& filter : loopLowPass)
        filter.coefficients = lowPassCoefficients;

    msrr::applyBiquadCoefficients (*highPassCoefficients,
        juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (sampleRate, clampBelowNyquist (lastHighPassHz, sampleRate), loopFilterQ));
    msrr::applyBiquadCoefficients (*lowPassCoefficients,
        juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (sampleRate, clampBelowNyquist (lastLowPassHz, sampleRate), loopFilterQ));

    delayMsSmoothed.reset (sampleRate, delaySmoothingSeconds);
    delayMsSmoothed.setCurrentAndTargetValue (lastDelayMs);
    feedbackSmoothed.reset (sampleRate, smoothingTimeSeconds);
    feedbackSmoothed.setCurrentAndTargetValue (lastFeedback01);
    highPassSmoothed.reset (sampleRate, smoothingTimeSeconds);
    highPassSmoothed.setCurrentAndTargetValue (lastHighPassHz);
    lowPassSmoothed.reset (sampleRate, smoothingTimeSeconds);
    lowPassSmoothed.setCurrentAndTargetValue (lastLowPassHz);

    loopSatCompensation = TapeSaturator::compensationForDrive (loopDriveLinear);

    reset();
}

void SlapDelay::reset()
{
    delayLine.reset();

    for (auto& filter : loopHighPass)
        filter.reset();
    for (auto& filter : loopLowPass)
        filter.reset();
}

void SlapDelay::setDelayMs (float newDelayMs) noexcept
{
    lastDelayMs = juce::jlimit (minDelayMs, maxDelayMs, newDelayMs);
    delayMsSmoothed.setTargetValue (lastDelayMs);
}

void SlapDelay::setFeedbackProportion (float newFeedback01) noexcept
{
    lastFeedback01 = juce::jlimit (0.0f, 0.3f, newFeedback01);
    feedbackSmoothed.setTargetValue (lastFeedback01);
}

void SlapDelay::setLoopHighPassHz (float newFrequencyHz) noexcept
{
    lastHighPassHz = newFrequencyHz;
    highPassSmoothed.setTargetValue (newFrequencyHz);
}

void SlapDelay::setLoopLowPassHz (float newFrequencyHz) noexcept
{
    lastLowPassHz = newFrequencyHz;
    lowPassSmoothed.setTargetValue (newFrequencyHz);
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
    const auto feedback = juce::jlimit (0.0f, 0.3f, feedbackSmoothed.skip (static_cast<int> (numSamples)));
    const auto highPassHz = clampBelowNyquist (highPassSmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    const auto lowPassHz = clampBelowNyquist (lowPassSmoothed.skip (static_cast<int> (numSamples)), sampleRate);

    msrr::applyBiquadCoefficients (*highPassCoefficients,
        juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (sampleRate, highPassHz, loopFilterQ));
    msrr::applyBiquadCoefficients (*lowPassCoefficients,
        juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (sampleRate, lowPassHz, loopFilterQ));

    const auto delaySamples = juce::jlimit (
        1.0f,
        static_cast<float> (delayLine.getMaximumDelayInSamples()),
        delayMs * 0.001f * static_cast<float> (sampleRate));

    auto* left = block.getChannelPointer (0);
    auto* right = numChannels > 1 ? block.getChannelPointer (1) : nullptr;

    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        // Mono switch: the loop is fed the mono sum and both channels carry
        // the identical echo (the per-channel delay lines then hold the same
        // content, so the outputs match exactly).
        const auto monoInput = right != nullptr ? 0.5f * (left[sample] + right[sample]) : left[sample];

        // The plugin's bus layout is constrained to mono/stereo (see
        // PluginProcessor::isBusesLayoutSupported); the jmin is a defensive
        // clamp for any wider block handed to the raw engine.
        const auto channelsToProcess = juce::jmin (numChannels, static_cast<size_t> (2), loopHighPass.size());

        for (size_t channel = 0; channel < channelsToProcess; ++channel)
        {
            auto* data = channel == 0 ? left : right;
            if (data == nullptr)
                break;

            const auto input = monoEnabled ? monoInput : data[sample];

            // Pop-before-push still reads exactly `delaySamples` back for
            // any delay >= 1 sample (JUCE 8.0.14 juce_DelayLine.cpp: read
            // index = readPos + delayInt with both position counters
            // decrementing once per sample), which lets the feedback term
            // derive from this sample's own wet output.
            const auto wet = delayLine.popSample (static_cast<int> (channel), delaySamples);

            // Feedback path: HP -> LP -> tape-soft saturation -> feedback
            // gain. tanh is strictly bounded, so the loop cannot diverge
            // even at maximum feedback.
            auto fbSignal = loopHighPass[channel].processSample (wet);
            fbSignal = loopLowPass[channel].processSample (fbSignal);
            fbSignal = TapeSaturator::processSample (fbSignal, loopDriveLinear, loopSatCompensation);

            delayLine.pushSample (static_cast<int> (channel), input + fbSignal * feedback);

            data[sample] = wet;
        }
    }
}
