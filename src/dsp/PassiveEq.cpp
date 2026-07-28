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

float PassiveEq::dialToPotFraction (float dial0to10) noexcept
{
    // Log/audio-taper approximation for the EQP's LF boost/atten pots
    // (research-pultec-eqp1a.md section 3.1: "low boost/atten log/audio
    // taper - bake the taper into the normalized->resistance mapping").
    const auto dial = juce::jlimit (0.0f, 10.0f, dial0to10) / 10.0f;
    return std::pow (dial, potTaperExponent);
}

double PassiveEq::lfBoostCapForSelector (float lfFreqHz) noexcept
{
    // C_lo2 per LF selector (SMC-2024 component table): 2.2u / 1.1u /
    // 560n / 330n for 20/30/60/100 Hz. Nearest-selection lookup so the
    // smoothed frequency value between switch steps still lands on a
    // hardware cap value.
    if (lfFreqHz < 25.0f)
        return 2.2e-6;
    if (lfFreqHz < 45.0f)
        return 1.1e-6;
    if (lfFreqHz < 80.0f)
        return 560.0e-9;

    return 330.0e-9;
}

std::array<float, 6> PassiveEq::computeLfNetworkCoefficients (double rBoostOhm, double rCutOhm, double boostCapFarad) const noexcept
{
    // Exact 2nd-order transfer function of the LF sub-ladder (HF branches at
    // their resistive DC equivalents - see PassiveEq.h). With
    //   a = R_cut * C_lo1,  b = R_boost * C_lo2,
    //   Z_H = R_cut/(1+sa)   (cut element, series arm)
    //   Z_J = R_boost/(1+sb) (boost element, return leg, ADDED to output)
    //
    //   N2(s)  = (R_cut + R2 + R3) + (R2 + R3) * a * s        (branch2 * (1+sa))
    //   D_p(s) = Rsh*(1+sa) + N2(s)
    //   Num(s) = R3*Rsh*(1+sa)(1+sb) + R_boost*D_p(s)
    //   Den(s) = Rs1*D_p(s)*(1+sb) + Rsh*N2(s)*(1+sb) + R_boost*D_p(s)
    //   H(s)   = makeup * Num(s)/Den(s)
    //
    // where the makeup normalises the flat (both pots 0) setting to exactly
    // unity (the hardware's 15.4882x amp gain vs the passive loss).
    const auto cutCapFarad = boostCapFarad / lfCutCapRatio;
    const auto a = rCutOhm * cutCapFarad;
    const auto b = rBoostOhm * boostCapFarad;

    const auto r23 = dividerTopOhm + dividerBottomOhm;
    const auto rsh = hfAttenShuntOhm;
    const auto rs1 = hfSeriesOhm;
    const auto r3 = dividerBottomOhm;

    const auto e0 = rCutOhm + r23;      // N2 constant term
    const auto e1 = r23 * a;            // N2 s term
    const auto d0 = rsh + e0;           // D_p constant term
    const auto d1 = (rsh + r23) * a;    // D_p s term

    // Numerator polynomial.
    const auto n0 = r3 * rsh + rBoostOhm * d0;
    const auto n1 = r3 * rsh * (a + b) + rBoostOhm * d1;
    const auto n2 = r3 * rsh * a * b;

    // Denominator polynomial.
    const auto den0 = rs1 * d0 + rsh * e0 + rBoostOhm * d0;
    const auto den1 = rs1 * (d1 + d0 * b) + rsh * (e1 + e0 * b) + rBoostOhm * d1;
    const auto den2 = rs1 * d1 * b + rsh * e1 * b;

    // Flat makeup: both pots 0 -> H = (r3*rsh) / (rs1*(rsh+r23) + rsh*r23),
    // frequency-independent.
    const auto makeup = (rs1 * (rsh + r23) + rsh * r23) / (r3 * rsh);

    // Bilinear transform (exact at these LF corners per the SMC accuracy
    // table; no prewarp needed).
    const auto K = 2.0 * sampleRate;
    const auto K2 = K * K;

    // ONE POT AT EXACTLY 0 IS A FIRST-ORDER NETWORK, and must be transformed
    // as one. Both quadratic terms carry the factor a*b (n2 = r3*rsh*a*b;
    // den2 = a*b*(rs1*(rsh+r23) + rsh*r23)), so a single reactive element
    // remains as soon as either pot sits at its stop. Feeding that degenerate
    // quadratic through the 2nd-order transform below is algebraically valid
    // but numerically not: numerator and denominator then BOTH factor as
    // (z+1)*(first order), i.e. a pole and a zero exactly at Nyquist that are
    // supposed to cancel. Rounded to float they no longer do, and what
    // survives is a marginally-stable z = -1 mode - it rings at Nyquist
    // forever once excited instead of decaying (this is the SANDWICH bus's
    // ~3e-7 resting residue; both shipped defaults drive exactly one LF pot
    // per PassiveEq instance, so both hit it). Dividing the (z+1) out
    // analytically - which is all the branch below does - gives the identical
    // response with no pole at Nyquist to leak.
    if (a == 0.0 || b == 0.0)
        return { static_cast<float> (makeup * (n1 * K + n0)),
                 static_cast<float> (makeup * (n0 - n1 * K)),
                 0.0f,
                 static_cast<float> (den1 * K + den0),
                 static_cast<float> (den0 - den1 * K),
                 0.0f };

    const auto b0 = makeup * (n2 * K2 + n1 * K + n0);
    const auto b1 = makeup * (2.0 * (n0 - n2 * K2));
    const auto b2 = makeup * (n2 * K2 - n1 * K + n0);
    const auto a0 = den2 * K2 + den1 * K + den0;
    const auto a1 = 2.0 * (den0 - den2 * K2);
    const auto a2 = den2 * K2 - den1 * K + den0;

    return { static_cast<float> (b0), static_cast<float> (b1), static_cast<float> (b2),
             static_cast<float> (a0), static_cast<float> (a1), static_cast<float> (a2) };
}

void PassiveEq::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    lfNetwork.prepare (spec);
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
    lfNetwork.reset();
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
    const auto lfBoostDial = juce::jlimit (0.0f, 10.0f, lfBoostSmoothed.skip (static_cast<int> (numSamples)));
    const auto lfCutDial = juce::jlimit (0.0f, 10.0f, lfCutSmoothed.skip (static_cast<int> (numSamples)));
    const auto hfBellFreqHz = clampBelowNyquist (hfBellFreqSmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    const auto hfBellBoostDial = juce::jlimit (0.0f, 10.0f, hfBellBoostSmoothed.skip (static_cast<int> (numSamples)));
    const auto hfBellBandwidth01 = juce::jlimit (0.0f, 10.0f, hfBellBandwidthSmoothed.skip (static_cast<int> (numSamples))) / 10.0f;
    const auto hfShelfFreqHz = clampBelowNyquist (hfShelfFreqSmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    const auto hfShelfAttenDb = dialToDb (hfShelfAttenSmoothed.skip (static_cast<int> (numSamples)), hfShelfAttenMaxDb);

    juce::dsp::ProcessContextReplacing<float> context (block);

    // LF ladder network: one section carrying both the boost and the cut
    // element (see class comment - their interaction IS the "low end
    // trick"). Structurally skipped (bit-exact) while both dials are 0.
    if (lfBoostDial > neutralDialEpsilon || lfCutDial > neutralDialEpsilon)
    {
        const auto boostCap = lfBoostCapForSelector (lfFreqHz);
        const auto rBoost = lfBoostPotMaxOhm * static_cast<double> (dialToPotFraction (lfBoostDial));
        const auto rCut = lfCutPotMaxOhm * static_cast<double> (dialToPotFraction (lfCutDial));

        msrr::applyBiquadCoefficients (*lfNetwork.state, computeLfNetworkCoefficients (rBoost, rCut, boostCap));
        lfNetwork.process (context);
    }

    // HF bell: matched peak with hardware-coupled gain/Q (see class
    // comment).
    {
        const auto seriesOhm = hfBellSeriesMinOhm + hfBellBandwidth01 * (hfBellSeriesMaxOhm - hfBellSeriesMinOhm);
        const auto bellQ = hfBellZ0Ohm / (seriesOhm + hfBellLoadingOhm);
        const auto bellGainDb = dialToDb (hfBellBoostDial, hfBellMaxDb)
                                * (1.0f - hfBellBroadGainLossFraction * hfBellBandwidth01);

        if (bellGainDb > neutralGainEpsilonDb)
        {
            msrr::applyBiquadCoefficients (*hfBell.state,
                msrr::makeMatchedPeak (sampleRate, hfBellFreqHz, bellQ, bellGainDb));
            hfBell.process (context);
        }
    }

    if (hfShelfAttenDb > neutralGainEpsilonDb)
    {
        msrr::applyBiquadCoefficients (*hfShelf.state,
            msrr::makeMatchedHighShelf (sampleRate, hfShelfFreqHz, hfShelfQ, -hfShelfAttenDb));
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
