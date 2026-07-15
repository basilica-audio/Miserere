#include "Hpf.h"

namespace
{
    // Keeps a requested cutoff safely below Nyquist regardless of host
    // sample rate, so ArrayCoefficients::makeHighPass never receives an
    // out-of-range value (which would produce invalid/NaN coefficients).
    float clampBelowNyquist (float frequencyHz, double sampleRate) noexcept
    {
        const auto nyquist = static_cast<float> (sampleRate) * 0.5f;
        return juce::jlimit (10.0f, nyquist * 0.9f, frequencyHz);
    }
}

void Hpf::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    filter.prepare (spec);

    frequencySmoothed.reset (sampleRate, smoothingTimeSeconds);
    frequencySmoothed.setCurrentAndTargetValue (lastFrequencyHz);

    reset();

    const auto raw = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (
        sampleRate, clampBelowNyquist (lastFrequencyHz, sampleRate), q);
    msrr::applyBiquadCoefficients (*filter.state, raw);
}

void Hpf::reset()
{
    filter.reset();
}

void Hpf::setFrequencyHz (float newFrequencyHz) noexcept
{
    lastFrequencyHz = newFrequencyHz;
    frequencySmoothed.setTargetValue (newFrequencyHz);
}

void Hpf::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0)
        return;

    // Bit-exact bypass when disabled: skip entirely, including the
    // coefficient recompute below (the smoother still advances so re-
    // enabling doesn't jump).
    frequencySmoothed.skip (static_cast<int> (numSamples));

    if (! enabled)
        return;

    const auto frequencyHz = clampBelowNyquist (frequencySmoothed.getCurrentValue(), sampleRate);
    const auto raw = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (sampleRate, frequencyHz, q);
    msrr::applyBiquadCoefficients (*filter.state, raw);

    juce::dsp::ProcessContextReplacing<float> context (block);
    filter.process (context);
}
