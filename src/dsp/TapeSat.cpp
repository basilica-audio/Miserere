#include "TapeSat.h"

void TapeSat::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    preEmphasis.prepare (spec);
    deEmphasis.prepare (spec);

    // Fixed emphasis pair - computed once here, never on the audio thread.
    // Clamped below Nyquist for very low sample rates (defensive; 3 kHz is
    // safe at every realistic rate).
    const auto freqHz = juce::jmin (emphasisFreqHz, static_cast<float> (sampleRate) * 0.45f);

    msrr::applyBiquadCoefficients (*preEmphasis.state,
        juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (
            sampleRate, freqHz, emphasisQ, juce::Decibels::decibelsToGain (emphasisGainDb)));

    msrr::applyBiquadCoefficients (*deEmphasis.state,
        juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (
            sampleRate, freqHz, emphasisQ, juce::Decibels::decibelsToGain (-emphasisGainDb)));

    driveDbSmoothed.reset (sampleRate, smoothingTimeSeconds);
    driveDbSmoothed.setCurrentAndTargetValue (lastDriveDb);

    reset();
}

void TapeSat::reset()
{
    preEmphasis.reset();
    deEmphasis.reset();
}

void TapeSat::setDriveDb (float newDriveDb) noexcept
{
    lastDriveDb = newDriveDb;
    driveDbSmoothed.setTargetValue (newDriveDb);
}

void TapeSat::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto numChannels = block.getNumChannels();
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0 || numChannels == 0)
        return;

    const auto driveDb = juce::jmax (0.0f, driveDbSmoothed.skip (static_cast<int> (numSamples)));

    // Bit-exact bypass at drive == 0 (see class comment). The smoother above
    // still advances so re-engaging drive mid-stream ramps from the right
    // place.
    if (driveDb <= 0.0f)
        return;

    juce::dsp::ProcessContextReplacing<float> context (block);
    preEmphasis.process (context);

    const auto driveGainLinear = juce::Decibels::decibelsToGain (driveDb);
    const auto compensation = TapeSaturator::compensationForDrive (driveGainLinear);

    for (size_t channel = 0; channel < numChannels; ++channel)
    {
        auto* data = block.getChannelPointer (channel);

        for (size_t sample = 0; sample < numSamples; ++sample)
            data[sample] = TapeSaturator::processSample (data[sample], driveGainLinear, compensation);
    }

    deEmphasis.process (context);
}
