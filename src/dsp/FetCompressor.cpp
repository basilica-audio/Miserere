#include "FetCompressor.h"

FetCompressor::CharacterTuple FetCompressor::characterTupleFor (Character c) noexcept
{
    // Per-character voicing tuples (issue #20). FET is all-zeros/unity by
    // construction so the default character reproduces the pre-#20
    // arithmetic exactly (see the class comment's bit-identity note).
    switch (c)
    {
        case Character::fet:    return { 0.0f, 1.0f, 1.0f, 0.0f };
        case Character::vca:    return { 6.0f, 0.6f, 1.0f, 0.0f };
        case Character::tubeMu: return { 12.0f, 2.5f, 1.6f, 0.25f };
    }

    return { 0.0f, 1.0f, 1.0f, 0.0f };
}

void FetCompressor::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    const auto numChannels = static_cast<size_t> (spec.numChannels);
    envelopeState.assign (numChannels, 0.0f);
    harmonicDcState.assign (numChannels, 0.0f);

    thresholdSmoothed.reset (sampleRate, smoothingTimeSeconds);
    thresholdSmoothed.setCurrentAndTargetValue (lastThresholdDb);

    // Snap the character tuple across a prepare (no ramp from a stale
    // value into the first block).
    const auto tuple = characterTupleFor (character);
    kneeSmoothed.reset (sampleRate, smoothingTimeSeconds);
    kneeSmoothed.setCurrentAndTargetValue (tuple.kneeDb);
    harmonicSmoothed.reset (sampleRate, smoothingTimeSeconds);
    harmonicSmoothed.setCurrentAndTargetValue (tuple.harmonicAmount);

    reset();
}

void FetCompressor::reset()
{
    std::fill (envelopeState.begin(), envelopeState.end(), 0.0f);
    std::fill (harmonicDcState.begin(), harmonicDcState.end(), 0.0f);
    currentGainReductionDb.store (0.0f, std::memory_order_relaxed);
}

void FetCompressor::setCharacter (Character newCharacter) noexcept
{
    if (character == newCharacter)
        return;

    character = newCharacter;

    const auto tuple = characterTupleFor (character);
    kneeSmoothed.setTargetValue (tuple.kneeDb);
    harmonicSmoothed.setTargetValue (tuple.harmonicAmount);
}

void FetCompressor::setThresholdDb (float newThresholdDb) noexcept
{
    lastThresholdDb = newThresholdDb;
    thresholdSmoothed.setTargetValue (newThresholdDb);
}

void FetCompressor::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto numChannels = block.getNumChannels();
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0 || numChannels == 0)
        return;

    const auto thresholdDb = thresholdSmoothed.skip (static_cast<int> (numSamples));
    const auto kneeDb = kneeSmoothed.skip (static_cast<int> (numSamples));
    const auto harmonicAmount = harmonicSmoothed.skip (static_cast<int> (numSamples));
    const auto ratioFactor = 1.0f - (1.0f / ratio);

    // Attack/release scales step at block rate, exactly like the dials
    // themselves (the envelope's own smoothing absorbs coefficient steps).
    const auto tuple = characterTupleFor (character);
    const auto effectiveAttackMs = juce::jmax (0.01f, attackMs * tuple.attackScale);
    const auto effectiveReleaseMs = juce::jmax (1.0f, releaseMs * tuple.releaseScale);

    const auto attackCoeff = std::exp (-1.0 / (static_cast<double> (effectiveAttackMs) * 0.001 * sampleRate));
    const auto releaseCoeff = std::exp (-1.0 / (static_cast<double> (effectiveReleaseMs) * 0.001 * sampleRate));

    const auto dcAlpha = static_cast<float> (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi
                                                              * harmonicDcCutoffHz / sampleRate));

    const auto halfKnee = 0.5f * kneeDb;
    const auto useSoftKnee = kneeDb > 0.01f;
    const auto useHarmonic = harmonicAmount > 1.0e-6f;

    float peakGainReductionDb = 0.0f;

    for (size_t channel = 0; channel < numChannels && channel < envelopeState.size(); ++channel)
    {
        auto* data = block.getChannelPointer (channel);
        auto& envelope = envelopeState[channel];
        auto& dcState = harmonicDcState[channel];

        // Non-finite abuse re-seed (block rate): a poisoned DC estimate
        // would otherwise stick a NaN into every later colour delta.
        if (! std::isfinite (dcState))
            dcState = 0.0f;

        for (size_t sample = 0; sample < numSamples; ++sample)
        {
            const auto inputSample = data[sample];

            const auto rectified = inputSample * inputSample;
            const auto coeff = rectified > envelope ? attackCoeff : releaseCoeff;
            envelope = static_cast<float> (coeff * envelope + (1.0 - coeff) * rectified);

            // envelope <= threshold (in dB terms) yields an exact 0 dB
            // reduction via this clamp (not an asymptotic approximation) -
            // see the class comment. With a soft knee the same exactness
            // holds below threshold - kneeDb/2 (the VCA transparency claim).
            const auto envelopeDb = juce::Decibels::gainToDecibels (std::sqrt (juce::jmax (envelope, 1.0e-12f)), -120.0f);

            float reductionDb;

            if (! useSoftKnee)
            {
                // Legacy hard-knee branch - bit-identical to pre-#20.
                const auto overshootDb = juce::jmax (0.0f, envelopeDb - thresholdDb);
                reductionDb = overshootDb * ratioFactor;
            }
            else
            {
                const auto over = envelopeDb - thresholdDb;

                if (over <= -halfKnee)
                    reductionDb = 0.0f;
                else if (over >= halfKnee)
                    reductionDb = over * ratioFactor;
                else
                {
                    const auto kneeTerm = over + halfKnee;
                    reductionDb = ratioFactor * kneeTerm * kneeTerm / (2.0f * kneeDb);
                }
            }

            const auto gainFactor = juce::Decibels::decibelsToGain (-reductionDb);

            if (! useHarmonic)
            {
                // Legacy clean path - bit-identical to pre-#20.
                data[sample] = inputSample * gainFactor * makeupGainLinear;
            }
            else
            {
                // GR-gated 2nd-harmonic colour (Tube Mu): even-order term
                // on the compressed signal, DC-rejected by a slow one-pole
                // estimate of its own mean.
                const auto compressed = inputSample * gainFactor;
                const auto squared = compressed * compressed;
                dcState += dcAlpha * (squared - dcState);

                const auto grGate = juce::jmin (1.0f, reductionDb / harmonicReferenceGrDb);
                const auto colour = harmonicAmount * grGate * (squared - dcState);

                data[sample] = (compressed + colour) * makeupGainLinear;
            }

            peakGainReductionDb = juce::jmax (peakGainReductionDb, reductionDb);
        }
    }

    currentGainReductionDb.store (peakGainReductionDb, std::memory_order_relaxed);
}
