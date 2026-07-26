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

    const auto numChannels = static_cast<size_t> (spec.numChannels);
    oddStages.assign (numChannels, {});
    ironStages.assign (numChannels, {});
    ironStates.assign (numChannels, {});

    // Iron integrator/differentiator pair (see class comment): the analog
    // prototype is H_int(s) = w_ref / (s + w_leak), a leaky flux integrator
    // normalised to unity gain at the 100 Hz reference so drive numbers stay
    // comparable across stages.
    //
    // DISCRETISATION (deviation from the brief's "trapezoidal pair", recorded
    // deliberately): the bilinear integrator's exact inverse is
    // ((2/T + w_leak) - (2/T - w_leak) z^-1) / (w_ref (1 + z^-1)) - it carries
    // a pole at z = -1, i.e. an undamped resonance at Nyquist. Damping it
    // (z = -0.98) both breaks the exactness of the pairing and still leaves
    // ~34 dB of gain on whatever arithmetic noise reaches it, which is what
    // put the §6.6 near-zero-drive null at -63 dBFS. The impulse-invariant
    // one-pole below has an inverse that is a two-tap FIR - pole-free, no
    // Nyquist resonance, and an EXACT inverse in the linear region, which is
    // precisely the property the brief's F8 null assertion is about.
    {
        const auto wLeak = juce::MathConstants<double>::twoPi * static_cast<double> (ironIntegratorPoleHz);
        const auto thetaRef = juce::MathConstants<double>::twoPi * static_cast<double> (ironReferenceHz) / sampleRate;

        ironIntegratorPole = std::exp (-wLeak / sampleRate);

        // G = |1 - a e^{-j theta_ref}| makes |H_int| == 1 at the reference.
        const auto re = 1.0 - ironIntegratorPole * std::cos (thetaRef);
        const auto im = ironIntegratorPole * std::sin (thetaRef);
        ironIntegratorGain = std::sqrt (re * re + im * im);
        ironDifferentiatorGain = 1.0 / ironIntegratorGain;
    }

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
        msrr::makeMatchedHighShelf (sampleRate, highShelfFreqHz, highShelfQ, lastHighGainDb));
}

void ConsoleEq::reset()
{
    hpfFirstOrder.reset();
    hpfSecondOrder.reset();
    lowShelf.reset();
    midPeak.reset();
    highShelf.reset();

    for (auto& stage : oddStages)
        stage.reset();

    for (auto& stage : ironStages)
        stage.reset();

    for (auto& state : ironStates)
        state = {};
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
        // Matched (decramped) 12 kHz shelf - see class comment (brief F2).
        msrr::applyBiquadCoefficients (*highShelf.state,
            msrr::makeMatchedHighShelf (sampleRate, highShelfFreqHz, highShelfQ, highGainDb));
        highShelf.process (context);
    }

    // Drive: 0 dB (parameter minimum) is a bit-exact structural bypass.
    // Odd 3rd-leaning term = shared TapeSaturator curve via residual-form
    // ADAA (brief F1); even/LF term = flux-domain iron chain (brief F8) as
    // a parallel ADAA-residual delta - see class comment.
    if (driveDb > 0.0f)
    {
        const auto driveGainLinear = juce::Decibels::decibelsToGain (driveDb);
        const auto compensation = TapeSaturator::compensationForDrive (driveGainLinear);

        // Iron drive FACTOR (not a gain): dF = g - 1 -> 0 as drive -> 0 dB,
        // so the normalised iron curve
        //   s = (tanh(dF*u + b) - tanh(b)) / (dF * sech^2(b))
        // collapses to the exact identity s = u and the iron delta
        // (differentiated ADAA residual) vanishes - the drive -> 0 null is
        // structural (brief F8 / test 6.6).
        const auto ironDriveFactor = juce::jmax (1.0e-4f, driveGainLinear - 1.0f);
        const auto sechSqB = 1.0f - std::tanh (ironDcBias) * std::tanh (ironDcBias);
        const auto ironScale = 1.0f / (ironDriveFactor * sechSqB);

        const auto channelsToProcess = juce::jmin (numChannels, oddStages.size());

        for (size_t channel = 0; channel < channelsToProcess; ++channel)
        {
            auto& odd = oddStages[channel];
            auto& iron = ironStages[channel];
            auto& state = ironStates[channel];

            odd.prepareBlock (driveGainLinear, compensation);
            iron.prepareBlock (ironDriveFactor, ironDcBias, ironScale);

            auto* data = block.getChannelPointer (channel);

            for (size_t sample = 0; sample < numSamples; ++sample)
            {
                const auto x = data[sample];

                // Flux estimate (leaky one-pole integrator, double - LF
                // accumulation over long programme material).
                state.integrator = ironIntegratorPole * state.integrator
                                   + ironIntegratorGain * static_cast<double> (x);

                // Iron residual (ADAA, parallel-delta rule) - fed in double,
                // because rounding the flux state through float here would
                // reappear as divided-difference noise (see AdaaSaturator.h).
                const auto fluxResidual = iron.processResidual (state.integrator);

                // ... differentiated back to the voltage domain by the
                // integrator's exact two-tap inverse (see prepare()).
                const auto diffOut = (fluxResidual - ironIntegratorPole * state.diffPrevIn) * ironDifferentiatorGain;
                state.diffPrevIn = fluxResidual;

                data[sample] = odd.processSample (x) + ironAmount * static_cast<float> (diffOut);
            }
        }
    }
}
