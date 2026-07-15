#include "PassiveEq.h"

namespace
{
    float clampBelowNyquist (float frequencyHz, double sampleRate) noexcept
    {
        const auto nyquist = static_cast<float> (sampleRate) * 0.5f;
        return juce::jlimit (10.0f, nyquist * 0.9f, frequencyHz);
    }
}

void PassiveEq::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    lowBoost.prepare (spec);
    highBoost.prepare (spec);
    airShelf.prepare (spec);

    lowFreqSmoothed.reset (sampleRate, smoothingTimeSeconds);
    lowFreqSmoothed.setCurrentAndTargetValue (lastLowFreqHz);
    lowGainSmoothed.reset (sampleRate, smoothingTimeSeconds);
    lowGainSmoothed.setCurrentAndTargetValue (lastLowGainDb);
    highFreqSmoothed.reset (sampleRate, smoothingTimeSeconds);
    highFreqSmoothed.setCurrentAndTargetValue (lastHighFreqHz);
    highGainSmoothed.reset (sampleRate, smoothingTimeSeconds);
    highGainSmoothed.setCurrentAndTargetValue (lastHighGainDb);
    airGainSmoothed.reset (sampleRate, smoothingTimeSeconds);
    airGainSmoothed.setCurrentAndTargetValue (lastAirGainDb);

    reset();

    // Prime all three bands' coefficients immediately so the very first
    // process() call reflects the current parameter values.
    msrr::applyBiquadCoefficients (*lowBoost.state,
        juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (
            sampleRate, clampBelowNyquist (lastLowFreqHz, sampleRate), passiveQ, juce::Decibels::decibelsToGain (juce::jmax (0.0f, lastLowGainDb))));
    msrr::applyBiquadCoefficients (*highBoost.state,
        juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (
            sampleRate, clampBelowNyquist (lastHighFreqHz, sampleRate), passiveQ, juce::Decibels::decibelsToGain (juce::jmax (0.0f, lastHighGainDb))));
    msrr::applyBiquadCoefficients (*airShelf.state,
        juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (
            sampleRate, clampBelowNyquist (airFreqHz, sampleRate), airQ, juce::Decibels::decibelsToGain (juce::jmax (0.0f, lastAirGainDb))));
}

void PassiveEq::reset()
{
    lowBoost.reset();
    highBoost.reset();
    airShelf.reset();
}

void PassiveEq::setLowBoost (float freqHz, float gainDb) noexcept
{
    lastLowFreqHz = freqHz;
    lastLowGainDb = gainDb;
    lowFreqSmoothed.setTargetValue (freqHz);
    lowGainSmoothed.setTargetValue (gainDb);
}

void PassiveEq::setHighBoost (float freqHz, float gainDb) noexcept
{
    lastHighFreqHz = freqHz;
    lastHighGainDb = gainDb;
    highFreqSmoothed.setTargetValue (freqHz);
    highGainSmoothed.setTargetValue (gainDb);
}

void PassiveEq::setAirGainDb (float newGainDb) noexcept
{
    lastAirGainDb = newGainDb;
    airGainSmoothed.setTargetValue (newGainDb);
}

void PassiveEq::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0)
        return;

    const auto lowFreqHz = clampBelowNyquist (lowFreqSmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    const auto lowGainDb = juce::jmax (0.0f, lowGainSmoothed.skip (static_cast<int> (numSamples)));
    const auto highFreqHz = clampBelowNyquist (highFreqSmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    const auto highGainDb = juce::jmax (0.0f, highGainSmoothed.skip (static_cast<int> (numSamples)));
    const auto airGainDb = juce::jmax (0.0f, airGainSmoothed.skip (static_cast<int> (numSamples)));

    juce::dsp::ProcessContextReplacing<float> context (block);

    // Bands inside the ~0 dB dead zone are skipped entirely for a
    // bit-exact, compiler-independent neutral path - see
    // ConsoleEq::process() for the fp-contract + APVTS-denormalisation
    // rationale. This is also what makes an unused slot of a shared
    // PassiveEq instance (the engine leaves the "air" band of the in-EQ and
    // the low/high bands of the out-EQ at 0 dB) genuinely free.
    if (lowGainDb > neutralGainEpsilonDb)
    {
        msrr::applyBiquadCoefficients (*lowBoost.state,
            juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (sampleRate, lowFreqHz, passiveQ, juce::Decibels::decibelsToGain (lowGainDb)));
        lowBoost.process (context);
    }

    if (highGainDb > neutralGainEpsilonDb)
    {
        msrr::applyBiquadCoefficients (*highBoost.state,
            juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sampleRate, highFreqHz, passiveQ, juce::Decibels::decibelsToGain (highGainDb)));
        highBoost.process (context);
    }

    if (airGainDb > neutralGainEpsilonDb)
    {
        msrr::applyBiquadCoefficients (*airShelf.state,
            juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sampleRate, clampBelowNyquist (airFreqHz, sampleRate), airQ, juce::Decibels::decibelsToGain (airGainDb)));
        airShelf.process (context);
    }
}
