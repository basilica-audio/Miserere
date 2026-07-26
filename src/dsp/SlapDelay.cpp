#include "SlapDelay.h"

namespace
{
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
    // capacity for the maximum 160 ms at the current sample rate plus
    // wobble-modulation and Hermite interpolation headroom.
    bufferLength = static_cast<int> (std::ceil (sampleRate * maxDelayMs / 1000.0)) + 8;

    const auto numChannels = juce::jmax (static_cast<size_t> (1), static_cast<size_t> (spec.numChannels));
    delayBuffers.assign (numChannels, std::vector<float> (static_cast<size_t> (bufferLength), 0.0f));
    writeIndex = 0;

    recordSaturators.assign (numChannels, {});

    repeatLowPass.clear();
    repeatLowPass.resize (numChannels); // Filter<float> is move-only, so resize() rather than assign()
    headBump.clear();
    headBump.resize (numChannels);
    spacingLoss.clear();
    spacingLoss.resize (numChannels);

    for (size_t channel = 0; channel < numChannels; ++channel)
    {
        repeatLowPass[channel].coefficients = lowPassCoefficients;
        headBump[channel].coefficients = headBumpCoefficients;
        spacingLoss[channel].coefficients = spacingLossCoefficients;
    }

    msrr::applyBiquadCoefficients (*lowPassCoefficients,
        juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (
            sampleRate, clampBelowNyquist (juce::jmap (lastTone01, toneLowPassBrightHz, toneLowPassDarkHz), sampleRate), loopFilterQ));

    msrr::applyBiquadCoefficients (*headBumpCoefficients,
        juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (
            sampleRate, clampBelowNyquist (headBumpFreqHz, sampleRate), headBumpQ, juce::Decibels::decibelsToGain (headBumpGainDb)));

    msrr::applyFirstOrderCoefficients (*spacingLossCoefficients,
        juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderLowPass (
            sampleRate, clampBelowNyquist (spacingLossFreshHz, sampleRate)));

    flutter.prepare (sampleRate);

    delayMsSmoothed.reset (sampleRate, delaySmoothingSeconds);
    delayMsSmoothed.setCurrentAndTargetValue (lastDelayMs);
    toneSmoothed.reset (sampleRate, smoothingTimeSeconds);
    toneSmoothed.setCurrentAndTargetValue (lastTone01);
    wobbleSmoothed.reset (sampleRate, smoothingTimeSeconds);
    wobbleSmoothed.setCurrentAndTargetValue (lastWobble01);
    ageSmoothed.reset (sampleRate, smoothingTimeSeconds);
    ageSmoothed.setCurrentAndTargetValue (lastAge01);

    reset();
}

void SlapDelay::reset()
{
    for (auto& buffer : delayBuffers)
        std::fill (buffer.begin(), buffer.end(), 0.0f);

    writeIndex = 0;

    for (auto& stage : recordSaturators)
        stage.reset();

    for (auto& filter : repeatLowPass)
        filter.reset();
    for (auto& filter : headBump)
        filter.reset();
    for (auto& filter : spacingLoss)
        filter.reset();

    flutter.reset();

    noiseRng = 0x00C0FFEEu;
    hissShapingState = 0.0f;
    asperityEnvelope = 0.0f;
}

void SlapDelay::setDelayMs (float newDelayMs) noexcept
{
    lastDelayMs = juce::jlimit (minDelayMs, maxDelayMs, newDelayMs);
    delayMsSmoothed.setTargetValue (lastDelayMs);
}

void SlapDelay::setToneProportion (float newAmount01) noexcept
{
    lastTone01 = juce::jlimit (0.0f, 1.0f, newAmount01);
    toneSmoothed.setTargetValue (lastTone01);
}

void SlapDelay::setWobbleProportion (float newAmount01) noexcept
{
    lastWobble01 = juce::jlimit (0.0f, 1.0f, newAmount01);
    wobbleSmoothed.setTargetValue (lastWobble01);
}

void SlapDelay::setAgeProportion (float newAmount01) noexcept
{
    lastAge01 = juce::jlimit (0.0f, 1.0f, newAmount01);
    ageSmoothed.setTargetValue (lastAge01);
}

float SlapDelay::readInterpolated (size_t channel, double delaySamples) const noexcept
{
    // 4-point cubic Hermite (Catmull-Rom) fractional read around the write
    // cursor; exact pass-through at integer delays (frac == 0 returns p1).
    const auto& buffer = delayBuffers[channel];
    const auto length = bufferLength;

    const auto clamped = juce::jlimit (2.0, static_cast<double> (length - 3), delaySamples);
    const auto integerDelay = static_cast<int> (clamped);
    const auto frac = static_cast<float> (clamped - integerDelay);

    // Sample at (writeIndex - 1) is the most recent write.
    auto indexFor = [length] (int offset) noexcept
    {
        auto wrapped = offset % length;
        if (wrapped < 0)
            wrapped += length;
        return static_cast<size_t> (wrapped);
    };

    // The write for the CURRENT sample happens before the read (record ->
    // play head order), so the just-written sample x[n] sits at writeIndex
    // and x[n - D] at writeIndex - D.
    const auto base = writeIndex - integerDelay;
    const auto p0 = buffer[indexFor (base + 1)];
    const auto p1 = buffer[indexFor (base)];
    const auto p2 = buffer[indexFor (base - 1)];
    const auto p3 = buffer[indexFor (base - 2)];

    const auto c1 = 0.5f * (p2 - p0);
    const auto c2 = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
    const auto c3 = 0.5f * (p3 - p0) + 1.5f * (p1 - p2);

    return ((c3 * frac + c2) * frac + c1) * frac + p1;
}

void SlapDelay::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto numChannels = block.getNumChannels();
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0 || numChannels == 0 || bufferLength <= 0)
        return;

    // Parameters advance once per block (block-rate coefficient updates,
    // the standard suite compromise); the wobble modulation itself is
    // per-sample (control-rate generated, linearly upsampled).
    const auto delayMs = juce::jlimit (minDelayMs, maxDelayMs, delayMsSmoothed.skip (static_cast<int> (numSamples)));
    const auto tone01 = juce::jlimit (0.0f, 1.0f, toneSmoothed.skip (static_cast<int> (numSamples)));
    const auto wobble01 = juce::jlimit (0.0f, 1.0f, wobbleSmoothed.skip (static_cast<int> (numSamples)));
    const auto age01 = juce::jlimit (0.0f, 1.0f, ageSmoothed.skip (static_cast<int> (numSamples)));

    const auto lowPassHz = clampBelowNyquist (juce::jmap (tone01, toneLowPassBrightHz, toneLowPassDarkHz), sampleRate);
    msrr::applyBiquadCoefficients (*lowPassCoefficients,
        juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (sampleRate, lowPassHz, loopFilterQ));

    const auto ageActive = age01 > 1.0e-4f;

    if (ageActive)
        msrr::applyFirstOrderCoefficients (*spacingLossCoefficients,
            juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderLowPass (
                sampleRate, clampBelowNyquist (juce::jmap (age01, spacingLossFreshHz, spacingLossWornHz), sampleRate)));

    // Record-side saturator: asymmetric ADAA tanh (brief F1/F5), drive
    // coupled to tone as before, TapeSaturator-style level compensation.
    const auto satDriveLinear = juce::jmap (tone01, toneSatDriveMin, toneSatDriveMax);
    const auto satCompensation = TapeSaturator::compensationForDrive (satDriveLinear);

    const auto channelsToProcess = juce::jmin (numChannels, static_cast<size_t> (2), delayBuffers.size());

    for (size_t channel = 0; channel < channelsToProcess; ++channel)
        recordSaturators[channel].prepareBlock (satDriveLinear, recordSatBias, satCompensation);

    const auto delayTargetSamples = static_cast<double> (delayMs) * 0.001 * sampleRate;
    flutter.setDepth (wobble01, static_cast<double> (delayMs) * 0.001);

    const auto hissGain = age01 * juce::Decibels::decibelsToGain (hissLevelDb);
    const auto hissAlpha = static_cast<float> (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * hissShapingHz / sampleRate));
    const auto envAlpha = static_cast<float> (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * asperityEnvelopeHz / sampleRate));

    auto* left = block.getChannelPointer (0);
    auto* right = numChannels > 1 ? block.getChannelPointer (1) : nullptr;

    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        const auto monoInput = right != nullptr ? 0.5f * (left[sample] + right[sample]) : left[sample];

        // Wow/flutter: tau(t) = tau_target / (1 + m(t)). m == 0 exactly at
        // wobble 0 (structurally off).
        const auto m = flutter.getNext();
        const auto delayNow = delayTargetSamples / (1.0 + m);

        // One shared noise value per sample (one machine) - generated only
        // while age is active (age = 0 advances no RNG).
        float noiseSample = 0.0f;

        if (ageActive)
        {
            noiseRng ^= noiseRng << 13;
            noiseRng ^= noiseRng >> 17;
            noiseRng ^= noiseRng << 5;
            const auto white = static_cast<float> (noiseRng) * (2.0f / 4294967295.0f) - 1.0f;
            hissShapingState += hissAlpha * (white - hissShapingState); // pink-ish tilt
            noiseSample = hissShapingState;
        }

        for (size_t channel = 0; channel < channelsToProcess; ++channel)
        {
            auto* data = channel == 0 ? left : right;
            if (data == nullptr)
                break;

            const auto input = stereoEnabled ? data[sample] : monoInput;

            // Record side: saturate, then write to tape.
            delayBuffers[channel][static_cast<size_t> (writeIndex)] = recordSaturators[channel].processSample (input);

            // Play side: fractional Hermite read -> loss voicing.
            auto wet = readInterpolated (channel, delayNow);
            wet = repeatLowPass[channel].processSample (wet);
            wet = headBump[channel].processSample (wet);

            if (ageActive)
            {
                wet = spacingLoss[channel].processSample (wet);

                if (channel == 0)
                    asperityEnvelope += envAlpha * (std::abs (wet) - asperityEnvelope);

                wet += hissGain * noiseSample
                       * (asperityFloor + asperityDepth * juce::jmin (1.0f, asperityEnvelope * 4.0f));
            }

            data[sample] = wet;
        }

        if (++writeIndex >= bufferLength)
            writeIndex = 0;
    }
}
