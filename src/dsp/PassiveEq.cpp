#include "PassiveEq.h"

namespace
{
    float clampBelowNyquist (float frequencyHz, double sampleRate) noexcept
    {
        const auto nyquist = static_cast<float> (sampleRate) * 0.5f;
        return juce::jlimit (10.0f, nyquist * 0.9f, frequencyHz);
    }
}

float PassiveEq::dialToDb (float dial0to10, float maxDb) noexcept
{
    const auto dial = juce::jlimit (0.0f, 10.0f, dial0to10) / 10.0f;
    return maxDb * std::pow (dial, dialTaperExponent);
}

float PassiveEq::bandwidthDialToQ (float dial0to10) noexcept
{
    // 0 = sharp (high Q), 10 = broad (low Q) - see class comment.
    const auto dial = juce::jlimit (0.0f, 10.0f, dial0to10) / 10.0f;
    return juce::jmap (dial, 0.0f, 1.0f, 3.0f, 0.3f);
}

void PassiveEq::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    lfBoost.prepare (spec);
    lfCut.prepare (spec);
    hfBell.prepare (spec);
    hfShelf.prepare (spec);
    residualShelf.prepare (spec);

    lfFreqSmoothed.reset (sampleRate, smoothingTimeSeconds);
    lfFreqSmoothed.setCurrentAndTargetValue (lastLfFreqHz);
    lfBoostSmoothed.reset (sampleRate, smoothingTimeSeconds);
    lfBoostSmoothed.setCurrentAndTargetValue (lastLfBoostDial);
    lfCutSmoothed.reset (sampleRate, smoothingTimeSeconds);
    lfCutSmoothed.setCurrentAndTargetValue (lastLfCutDial);
    hfBellFreqSmoothed.reset (sampleRate, smoothingTimeSeconds);
    hfBellFreqSmoothed.setCurrentAndTargetValue (lastHfBellFreqHz);
    hfBellBoostSmoothed.reset (sampleRate, smoothingTimeSeconds);
    hfBellBoostSmoothed.setCurrentAndTargetValue (lastHfBellBoostDial);
    hfBellBandwidthSmoothed.reset (sampleRate, smoothingTimeSeconds);
    hfBellBandwidthSmoothed.setCurrentAndTargetValue (lastHfBellBandwidthDial);
    hfShelfFreqSmoothed.reset (sampleRate, smoothingTimeSeconds);
    hfShelfFreqSmoothed.setCurrentAndTargetValue (lastHfShelfFreqHz);
    hfShelfAttenSmoothed.reset (sampleRate, smoothingTimeSeconds);
    hfShelfAttenSmoothed.setCurrentAndTargetValue (lastHfShelfAttenDial);

    reset();
}

void PassiveEq::reset()
{
    lfBoost.reset();
    lfCut.reset();
    hfBell.reset();
    hfShelf.reset();
    residualShelf.reset();
}

void PassiveEq::setLfFreqHz (float freqHz) noexcept
{
    lastLfFreqHz = freqHz;
    lfFreqSmoothed.setTargetValue (freqHz);
}

void PassiveEq::setLfBoostDial (float dial0to10) noexcept
{
    lastLfBoostDial = dial0to10;
    lfBoostSmoothed.setTargetValue (dial0to10);
}

void PassiveEq::setLfCutDial (float dial0to10) noexcept
{
    lastLfCutDial = dial0to10;
    lfCutSmoothed.setTargetValue (dial0to10);
}

void PassiveEq::setHfBellFreqHz (float freqHz) noexcept
{
    lastHfBellFreqHz = freqHz;
    hfBellFreqSmoothed.setTargetValue (freqHz);
}

void PassiveEq::setHfBellBoostDial (float dial0to10) noexcept
{
    lastHfBellBoostDial = dial0to10;
    hfBellBoostSmoothed.setTargetValue (dial0to10);
}

void PassiveEq::setHfBellBandwidthDial (float dial0to10) noexcept
{
    lastHfBellBandwidthDial = dial0to10;
    hfBellBandwidthSmoothed.setTargetValue (dial0to10);
}

void PassiveEq::setHfShelfFreqHz (float freqHz) noexcept
{
    lastHfShelfFreqHz = freqHz;
    hfShelfFreqSmoothed.setTargetValue (freqHz);
}

void PassiveEq::setHfShelfAttenDial (float dial0to10) noexcept
{
    lastHfShelfAttenDial = dial0to10;
    hfShelfAttenSmoothed.setTargetValue (dial0to10);
}

void PassiveEq::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0)
        return;

    const auto lfFreqHz = clampBelowNyquist (lfFreqSmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    const auto lfBoostDb = dialToDb (lfBoostSmoothed.skip (static_cast<int> (numSamples)), lfBoostMaxDb);
    const auto lfCutDb = dialToDb (lfCutSmoothed.skip (static_cast<int> (numSamples)), lfCutMaxDb);
    const auto hfBellFreqHz = clampBelowNyquist (hfBellFreqSmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    const auto hfBellBoostDb = dialToDb (hfBellBoostSmoothed.skip (static_cast<int> (numSamples)), hfBellMaxDb);
    const auto hfBellQ = bandwidthDialToQ (hfBellBandwidthSmoothed.skip (static_cast<int> (numSamples)));
    const auto hfShelfFreqHz = clampBelowNyquist (hfShelfFreqSmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    const auto hfShelfAttenDb = dialToDb (hfShelfAttenSmoothed.skip (static_cast<int> (numSamples)), hfShelfAttenMaxDb);

    juce::dsp::ProcessContextReplacing<float> context (block);

    // Each band is skipped entirely while neutral - see ConsoleEq.cpp for
    // the fp-contract + APVTS-denormalisation rationale this preserves.
    if (lfBoostDb > neutralGainEpsilonDb)
    {
        msrr::applyBiquadCoefficients (*lfBoost.state,
            juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (
                sampleRate, clampBelowNyquist (lfFreqHz * lfBoostCornerMultiplier, sampleRate), lfBoostQ, juce::Decibels::decibelsToGain (lfBoostDb)));
        lfBoost.process (context);
    }

    if (lfCutDb > neutralGainEpsilonDb)
    {
        msrr::applyBiquadCoefficients (*lfCut.state,
            juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (
                sampleRate, clampBelowNyquist (lfFreqHz * lfCutCentreMultiplier, sampleRate), lfCutQ, juce::Decibels::decibelsToGain (-lfCutDb)));
        lfCut.process (context);
    }

    if (hfBellBoostDb > neutralGainEpsilonDb)
    {
        msrr::applyBiquadCoefficients (*hfBell.state,
            juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (sampleRate, hfBellFreqHz, hfBellQ, juce::Decibels::decibelsToGain (hfBellBoostDb)));
        hfBell.process (context);
    }

    if (hfShelfAttenDb > neutralGainEpsilonDb)
    {
        msrr::applyBiquadCoefficients (*hfShelf.state,
            juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sampleRate, hfShelfFreqHz, hfShelfQ, juce::Decibels::decibelsToGain (-hfShelfAttenDb)));
        hfShelf.process (context);
    }

    // Vintage residual: a small, always-on tilt (a defeatable few tenths of
    // a dB high shelf) that varies with the LF selector - see class
    // comment. Structurally skipped (bit-exact) when disabled.
    if (residualEnabled)
    {
        const auto residualDb = juce::jmap (juce::jlimit (20.0f, 100.0f, lfFreqHz), 20.0f, 100.0f, residualMaxDb, -residualMaxDb);

        msrr::applyBiquadCoefficients (*residualShelf.state,
            juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sampleRate, 5000.0f, 0.5f, juce::Decibels::decibelsToGain (residualDb)));
        residualShelf.process (context);
    }
}
