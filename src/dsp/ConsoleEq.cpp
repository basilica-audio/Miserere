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

    lowShelf.prepare (spec);
    midPeak.prepare (spec);
    highShelf.prepare (spec);

    lowGainSmoothed.reset (sampleRate, smoothingTimeSeconds);
    lowGainSmoothed.setCurrentAndTargetValue (lastLowGainDb);
    midFreqSmoothed.reset (sampleRate, smoothingTimeSeconds);
    midFreqSmoothed.setCurrentAndTargetValue (lastMidFreqHz);
    midGainSmoothed.reset (sampleRate, smoothingTimeSeconds);
    midGainSmoothed.setCurrentAndTargetValue (lastMidGainDb);
    midQSmoothed.reset (sampleRate, smoothingTimeSeconds);
    midQSmoothed.setCurrentAndTargetValue (lastMidQ);
    highGainSmoothed.reset (sampleRate, smoothingTimeSeconds);
    highGainSmoothed.setCurrentAndTargetValue (lastHighGainDb);

    reset();

    // Prime all three bands' coefficients immediately so the very first
    // process() call reflects the current parameter values, not the
    // identity placeholder each Duplicator was constructed with.
    msrr::applyBiquadCoefficients (*lowShelf.state,
        juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (sampleRate, lowShelfFreqHz, shelfQ, juce::Decibels::decibelsToGain (lastLowGainDb)));
    msrr::applyBiquadCoefficients (*midPeak.state,
        juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (sampleRate, clampBelowNyquist (lastMidFreqHz, sampleRate), lastMidQ, juce::Decibels::decibelsToGain (lastMidGainDb)));
    msrr::applyBiquadCoefficients (*highShelf.state,
        juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sampleRate, highShelfFreqHz, shelfQ, juce::Decibels::decibelsToGain (lastHighGainDb)));
}

void ConsoleEq::reset()
{
    lowShelf.reset();
    midPeak.reset();
    highShelf.reset();
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

void ConsoleEq::setMidQ (float newQ) noexcept
{
    lastMidQ = newQ;
    midQSmoothed.setTargetValue (newQ);
}

void ConsoleEq::setHighGainDb (float newGainDb) noexcept
{
    lastHighGainDb = newGainDb;
    highGainSmoothed.setTargetValue (newGainDb);
}

void ConsoleEq::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0)
        return;

    const auto lowGainDb = lowGainSmoothed.skip (static_cast<int> (numSamples));
    const auto midFreqHz = clampBelowNyquist (midFreqSmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    const auto midGainDb = midGainSmoothed.skip (static_cast<int> (numSamples));
    const auto midQ = juce::jlimit (0.1f, 10.0f, midQSmoothed.skip (static_cast<int> (numSamples)));
    const auto highGainDb = highGainSmoothed.skip (static_cast<int> (numSamples));

    juce::dsp::ProcessContextReplacing<float> context (block);

    // Each band is skipped ENTIRELY while its (smoothed) gain sits inside a
    // tiny dead zone around 0 dB, for two reasons:
    // 1. Even a 0 dB RBJ shelf/peak (perfectly symmetric b == a
    //    coefficients) is not bit-exact through the filter on every
    //    compiler - clang's default fp-contract fuses (input*b1) -
    //    (output*a1) into an fma whose 1-ulp residual recirculates through
    //    the biquad's feedback path and surfaces around -96 dB on
    //    low-frequency bands.
    // 2. A "0 dB" APVTS value that round-trips through normalise/denormalise
    //    (setValueNotifyingHost + snapToLegalValue) comes back as ~-4e-7 dB,
    //    not exactly 0 - so an exact-zero comparison never fires in a real
    //    host.
    // The dead zone (a millidecibel, far below audibility and far above the
    // quantisation noise) makes the M1 null test's "EQ flat" guarantee
    // structural and compiler-independent - and saves the work. A band's
    // filter state freezes while skipped; the ~50 ms gain smoother means a
    // band only reaches the dead zone after ramping down to inaudibility,
    // so no click can result from re-engaging it.
    if (std::abs (lowGainDb) > neutralGainEpsilonDb)
    {
        msrr::applyBiquadCoefficients (*lowShelf.state,
            juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (sampleRate, lowShelfFreqHz, shelfQ, juce::Decibels::decibelsToGain (lowGainDb)));
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
            juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sampleRate, highShelfFreqHz, shelfQ, juce::Decibels::decibelsToGain (highGainDb)));
        highShelf.process (context);
    }
}
