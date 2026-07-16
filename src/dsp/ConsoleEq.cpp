#include "ConsoleEq.h"

namespace
{
    float clampBelowNyquist (float frequencyHz, double sampleRate) noexcept
    {
        const auto nyquist = static_cast<float> (sampleRate) * 0.5f;
        return juce::jlimit (10.0f, nyquist * 0.9f, frequencyHz);
    }
}

void ConsoleEq::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    hpfFirstOrder.prepare (spec);
    hpfSecondOrder.prepare (spec);
    lowShelf.prepare (spec);
    midPeak.prepare (spec);
    highShelf.prepare (spec);

    hpfFreqSmoothed.reset (sampleRate, smoothingTimeSeconds);
    hpfFreqSmoothed.setCurrentAndTargetValue (lastHpfFreqHz);
    lowFreqSmoothed.reset (sampleRate, smoothingTimeSeconds);
    lowFreqSmoothed.setCurrentAndTargetValue (lastLowFreqHz);
    lowGainSmoothed.reset (sampleRate, smoothingTimeSeconds);
    lowGainSmoothed.setCurrentAndTargetValue (lastLowGainDb);
    midFreqSmoothed.reset (sampleRate, smoothingTimeSeconds);
    midFreqSmoothed.setCurrentAndTargetValue (lastMidFreqHz);
    midGainSmoothed.reset (sampleRate, smoothingTimeSeconds);
    midGainSmoothed.setCurrentAndTargetValue (lastMidGainDb);
    highGainSmoothed.reset (sampleRate, smoothingTimeSeconds);
    highGainSmoothed.setCurrentAndTargetValue (lastHighGainDb);
    driveDbSmoothed.reset (sampleRate, smoothingTimeSeconds);
    driveDbSmoothed.setCurrentAndTargetValue (lastDriveDb);

    reset();

    const auto hpfHz = clampBelowNyquist (lastHpfFreqHz, sampleRate);
    msrr::applyFirstOrderCoefficients (*hpfFirstOrder.state,
        juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderHighPass (sampleRate, hpfHz));
    msrr::applyBiquadCoefficients (*hpfSecondOrder.state,
        juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (sampleRate, hpfHz, hpfSecondOrderQ));

    msrr::applyBiquadCoefficients (*lowShelf.state,
        juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (
            sampleRate, clampBelowNyquist (lastLowFreqHz, sampleRate), lowShelfQ, juce::Decibels::decibelsToGain (lastLowGainDb)));
    msrr::applyBiquadCoefficients (*midPeak.state,
        juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (
            sampleRate, clampBelowNyquist (lastMidFreqHz, sampleRate), midQ, juce::Decibels::decibelsToGain (lastMidGainDb)));
    msrr::applyBiquadCoefficients (*highShelf.state,
        juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (
            sampleRate, highShelfFreqHz, highShelfQ, juce::Decibels::decibelsToGain (lastHighGainDb)));
}

void ConsoleEq::reset()
{
    hpfFirstOrder.reset();
    hpfSecondOrder.reset();
    lowShelf.reset();
    midPeak.reset();
    highShelf.reset();
}

void ConsoleEq::setHpfFreqHz (float newFrequencyHz) noexcept
{
    lastHpfFreqHz = newFrequencyHz;
    hpfFreqSmoothed.setTargetValue (newFrequencyHz);
}

void ConsoleEq::setLowFreqHz (float newFrequencyHz) noexcept
{
    lastLowFreqHz = newFrequencyHz;
    lowFreqSmoothed.setTargetValue (newFrequencyHz);
}

void ConsoleEq::setLowGainDb (float newGainDb) noexcept
{
    lastLowGainDb = newGainDb;
    lowGainSmoothed.setTargetValue (newGainDb);
}

void ConsoleEq::setMidFreqHz (float newFrequencyHz) noexcept
{
    lastMidFreqHz = newFrequencyHz;
    midFreqSmoothed.setTargetValue (newFrequencyHz);
}

void ConsoleEq::setMidGainDb (float newGainDb) noexcept
{
    lastMidGainDb = newGainDb;
    midGainSmoothed.setTargetValue (newGainDb);
}

void ConsoleEq::setHighGainDb (float newGainDb) noexcept
{
    lastHighGainDb = newGainDb;
    highGainSmoothed.setTargetValue (newGainDb);
}

void ConsoleEq::setDriveDb (float newDriveDb) noexcept
{
    lastDriveDb = newDriveDb;
    driveDbSmoothed.setTargetValue (newDriveDb);
}

void ConsoleEq::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto numChannels = block.getNumChannels();
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0 || numChannels == 0)
        return;

    const auto hpfHz = clampBelowNyquist (hpfFreqSmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    const auto lowFreqHz = clampBelowNyquist (lowFreqSmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    const auto lowGainDb = lowGainSmoothed.skip (static_cast<int> (numSamples));
    const auto midFreqHz = clampBelowNyquist (midFreqSmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    const auto midGainDb = midGainSmoothed.skip (static_cast<int> (numSamples));
    const auto highGainDb = highGainSmoothed.skip (static_cast<int> (numSamples));
    const auto driveDb = juce::jmax (0.0f, driveDbSmoothed.skip (static_cast<int> (numSamples)));

    juce::dsp::ProcessContextReplacing<float> context (block);

    // Bit-exact bypass while disabled - see the class comment (a highpass
    // has no frequency setting that is an exact identity).
    if (hpfEnabled)
    {
        msrr::applyFirstOrderCoefficients (*hpfFirstOrder.state,
            juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderHighPass (sampleRate, hpfHz));
        msrr::applyBiquadCoefficients (*hpfSecondOrder.state,
            juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (sampleRate, hpfHz, hpfSecondOrderQ));

        hpfFirstOrder.process (context);
        hpfSecondOrder.process (context);
    }

    // Each shelf/bell is skipped entirely while its gain sits inside a tiny
    // dead zone around 0 dB - see ConsoleEq's v1 ancestor / RealtimeCoefficients.h
    // for the fp-contract + APVTS-denormalisation rationale this preserves.
    if (std::abs (lowGainDb) > neutralGainEpsilonDb)
    {
        msrr::applyBiquadCoefficients (*lowShelf.state,
            juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (sampleRate, lowFreqHz, lowShelfQ, juce::Decibels::decibelsToGain (lowGainDb)));
        lowShelf.process (context);
    }

    if (std::abs (midGainDb) > neutralGainEpsilonDb)
    {
        msrr::applyBiquadCoefficients (*midPeak.state,
            juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (sampleRate, midFreqHz, midQ, juce::Decibels::decibelsToGain (midGainDb)));
        midPeak.process (context);
    }

    if (std::abs (highGainDb) > neutralGainEpsilonDb)
    {
        msrr::applyBiquadCoefficients (*highShelf.state,
            juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sampleRate, highShelfFreqHz, highShelfQ, juce::Decibels::decibelsToGain (highGainDb)));
        highShelf.process (context);
    }

    // Drive: 0 dB (parameter minimum) is a bit-exact bypass (see TapeSat.h
    // for the identical rationale - this reuses the same TapeSaturator
    // curve for the odd/3rd-leaning part, plus a small fixed even-harmonic
    // add-on for the "near-equal 2nd+3rd" transformer-style character).
    if (driveDb > 0.0f)
    {
        const auto driveGainLinear = juce::Decibels::decibelsToGain (driveDb);
        const auto compensation = TapeSaturator::compensationForDrive (driveGainLinear);

        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            auto* data = block.getChannelPointer (channel);

            for (size_t sample = 0; sample < numSamples; ++sample)
            {
                const auto x = data[sample];
                const auto driven = x * driveGainLinear;
                const auto odd = TapeSaturator::processSample (x, driveGainLinear, compensation);
                const auto even = evenHarmonicAmount * driven * driven * (x >= 0.0f ? 1.0f : -1.0f);
                data[sample] = odd + even * compensation;
            }
        }
    }
}
