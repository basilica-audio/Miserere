#include "SpreadPitch.h"

namespace
{
    float centsToRatio (float cents) noexcept
    {
        return std::exp2 (cents / 1200.0f);
    }
}

void SpreadPitch::Voice::prepare (const juce::dsp::ProcessSpec& monoSpec, float maxDelaySamples)
{
    delayLine.setMaximumDelayInSamples (static_cast<int> (std::ceil (maxDelaySamples)) + 4);
    delayLine.prepare (monoSpec);
}

void SpreadPitch::Voice::reset (float baseSamples, float halfGrainSamples)
{
    delayLine.reset();

    // The two taps start a half-grain apart, so one is always near the
    // middle of its crossfade window (full gain) while the other is near
    // an edge (fading in/out) - see class comment. The separation carries
    // the same causal cap as processSample (issue #28): a session restored
    // at timeScale < 1 must seed a causal window, not slew its way into one.
    sepSamples = juce::jmin (halfGrainSamples, baseSamples - minCausalDelaySamples);
    tapDelaySamples[0] = baseSamples;
    tapDelaySamples[1] = baseSamples - sepSamples;
}

float SpreadPitch::Voice::processSample (float input, float baseSamples, float sepTarget, float windowBlend) noexcept
{
    const auto maxDelay = static_cast<float> (delayLine.getMaximumDelayInSamples());

    // Causal window cap (issue #28): at timeScale < 1 the base delay can be
    // smaller than the requested separation, which would push the lower
    // window edge below the interpolator's read floor - a tap would then
    // ride the read clamp with non-zero gain, outputting an unshifted dry
    // copy of the input. Capping the TARGET keeps every converged window
    // inside [minCausalDelay, base + sep]; the slew below and the wrap
    // re-seats converge the live separation onto the cap.
    sepTarget = juce::jmin (sepTarget, baseSamples - minCausalDelaySamples);

    // Continuous separation correction (issue #19): a wrap only comes around
    // every couple of seconds at small detunes, far too rare to align a sung
    // note - so the live separation also slews toward the target between
    // wraps. The step is capped (~3.5 cents of transient pitch offset) and
    // applied to whichever tap currently carries LESS window gain (the one
    // farther from the window centre), where it is masked. While the live
    // window is still non-causal (only after a fast downward time-scale
    // move), the slew runs boosted: seconds of single-tap output shrink to
    // well under one, and the tap being slewed is the outer one - usually
    // the very tap the causal-floor fade below is silencing.
    const auto sepError = sepTarget - sepSamples;
    if (sepError != 0.0f)
    {
        const auto slewLimit = baseSamples - sepSamples < minCausalDelaySamples ? causalCatchupSlewPerSample
                                                                                : maxSepSlewPerSample;
        const auto step = juce::jlimit (-slewLimit, slewLimit, sepError);
        const auto quieter = std::abs (tapDelaySamples[0] - baseSamples) > std::abs (tapDelaySamples[1] - baseSamples) ? size_t { 0 } : size_t { 1 };

        // Widening (step > 0) pushes the quieter tap away from the other
        // tap; narrowing pulls it in. Direction depends on which side of
        // the other tap it sits.
        if (tapDelaySamples[quieter] > tapDelaySamples[1 - quieter])
            tapDelaySamples[quieter] += step;
        else
            tapDelaySamples[quieter] -= step;

        sepSamples += step;
    }

    float outSample = 0.0f;

    for (size_t tap = 0; tap < 2; ++tap)
    {
        // Read speed != write speed (1 sample/sample) is what produces the
        // pitch shift: the delay shrinks (pitch up, ratio > 1) or grows
        // (pitch down, ratio < 1) by (ratio - 1) samples every sample.
        tapDelaySamples[tap] -= (pitchRatio - 1.0f);

        // Wrap = re-seat relative to the LIVE other tap (issue #19), not a
        // jump by a fixed grain width: when this tap exits an edge the
        // other tap sits exactly at the window centre (it trails by one
        // separation), so seating this one at other +- sepTarget lands
        // exactly on the opposite edge (gain 0, click-free) and snaps the
        // separation to the target in one step.
        const auto other = tapDelaySamples[1 - tap];

        if (tapDelaySamples[tap] < baseSamples - sepSamples)
        {
            tapDelaySamples[tap] = other + sepTarget;
            sepSamples = sepTarget;
        }
        else if (tapDelaySamples[tap] > baseSamples + sepSamples)
        {
            tapDelaySamples[tap] = other - sepTarget;
            sepSamples = sepTarget;
        }

        const auto windowWidth = 2.0f * sepSamples;
        const auto posInGrain = juce::jlimit (0.0f, 1.0f, (tapDelaySamples[tap] - (baseSamples - sepSamples)) / windowWidth);

        // Window law (brief F6 + issue #19): equal-power sin keeps summed
        // POWER constant for decorrelated taps; once the period detector
        // has the taps summing in phase, the blend moves toward sin^2,
        // whose gains are amplitude-complementary (sin^2 + cos^2 == 1) -
        // a coherent in-phase sum then has constant AMPLITUDE, i.e. a flat
        // envelope. windowBlend == 0 reproduces the v0.5.0 law exactly.
        const auto s = std::sin (juce::MathConstants<float>::pi * posInGrain);
        auto gain = s * (1.0f - windowBlend + windowBlend * s);

        // Causal-floor fade (issue #28): belt-and-braces for the transient
        // where the live separation still exceeds the causal cap - a tap
        // whose ideal position is at or below the read clamp is silenced
        // instead of contributing an unshifted, clamp-delayed dry copy. In
        // any converged (capped) window the lower edge sits at or above the
        // floor, so this at most reshapes the outermost 64 samples of the
        // window, where the sin gain is already near zero.
        gain *= juce::jlimit (0.0f, 1.0f, (tapDelaySamples[tap] - minCausalDelaySamples) * (1.0f / causalFadeSamples));

        const auto delayClamped = juce::jlimit (minCausalDelaySamples, maxDelay, tapDelaySamples[tap]);
        const auto updateReadPointer = tap == 1; // advance the shared write/read cursor exactly once per input sample
        outSample += delayLine.popSample (0, delayClamped, updateReadPointer) * gain;
    }

    delayLine.pushSample (0, input);
    return outSample;
}

//==============================================================================

void SpreadPitch::PeriodDetector::prepare (double fullRate, float nominalTauSeconds, float floorHz)
{
    decFactor = juce::jmax (1, juce::roundToInt (fullRate / detectorTargetRateHz));
    const auto decRate = fullRate / decFactor;

    lagMax = juce::roundToInt (nominalTauSeconds * static_cast<float> (decRate));
    lagMin = juce::jmax (2, lagMax - juce::roundToInt (static_cast<float> (decRate) / floorHz));
    numLags = juce::jmin (static_cast<int> (sweepR.size()), lagMax - lagMin + 1);

    winLen = juce::roundToInt (0.016 * decRate); // >= one full period of the 80 Hz floor

    // The ring must hold the deepest read of a sweep (anchor - lagMax -
    // winLen) while the sweep writes numLags fresh samples past the anchor.
    const auto needed = lagMax + winLen + numLags + 8;
    history.assign (static_cast<size_t> (juce::nextPowerOfTwo (needed)), 0.0f);
    histMask = static_cast<int> (history.size()) - 1;

    // Two cascaded one-poles at ~1.2 kHz: the NACF only needs fundamentals,
    // and the lowpassed signal also feeds the spectral plausibility gate.
    lpCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 1200.0f / static_cast<float> (fullRate));
    powerCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 20.0f / static_cast<float> (fullRate));

    warmupCount = static_cast<std::uint64_t> (lagMax + winLen + 1);

    reset();
}

void SpreadPitch::PeriodDetector::reset() noexcept
{
    std::fill (history.begin(), history.end(), 0.0f);
    sweepR.fill (0.0f);
    writeCount = 0;
    decPhase = 0;
    decAccum = 0.0f;
    lp1 = lp2 = 0.0f;
    lowPower = fullPower = 0.0f;
    sweepLag = 0;
    sweepAnchor = 0;
    refEnergy = 0.0f;
    lastLagFullRate = static_cast<float> (lagMax * decFactor);
    lastConfidence = 0.0f;
}

bool SpreadPitch::PeriodDetector::pushSample (float x) noexcept
{
    // A NaN/Inf would latch in the one-pole states forever (the delay lines
    // self-flush, recursive filters do not) and silently disable the smart
    // splice until reset(). The engine only sanitizes the bus SUM, so the
    // detector guards its own input.
    if (! std::isfinite (x))
        x = 0.0f;

    lp1 += lpCoeff * (x - lp1);
    lp2 += lpCoeff * (lp1 - lp2);

    lowPower += powerCoeff * (lp2 * lp2 - lowPower);
    fullPower += powerCoeff * (x * x - fullPower);

    decAccum += lp2;

    if (++decPhase < decFactor)
        return false;

    decPhase = 0;
    const auto decSample = decAccum / static_cast<float> (decFactor);
    decAccum = 0.0f;

    history[static_cast<size_t> (writeCount & static_cast<std::uint64_t> (histMask))] = decSample;
    ++writeCount;

    if (writeCount < warmupCount)
        return false;

    const auto at = [this] (std::uint64_t index) noexcept
    {
        return history[static_cast<size_t> (index & static_cast<std::uint64_t> (histMask))];
    };

    if (sweepLag == 0)
    {
        // Starting a fresh sweep: freeze the reference window at the
        // current write position so every lag of this sweep is measured
        // against the same data (one shared reference energy).
        sweepAnchor = writeCount;
        refEnergy = 0.0f;

        for (int i = 0; i < winLen; ++i)
        {
            const auto v = at (sweepAnchor - 1 - static_cast<std::uint64_t> (i));
            refEnergy += v * v;
        }
    }

    // Progressive scan: exactly ONE lag's normalized correlation per
    // decimated sample - flat cost, no per-block spikes.
    const auto lag = static_cast<std::uint64_t> (lagMin + sweepLag);
    float dot = 0.0f;
    float lagEnergy = 0.0f;

    for (int i = 0; i < winLen; ++i)
    {
        const auto a = at (sweepAnchor - 1 - static_cast<std::uint64_t> (i));
        const auto b = at (sweepAnchor - 1 - lag - static_cast<std::uint64_t> (i));
        dot += a * b;
        lagEnergy += b * b;
    }

    const auto denom = std::sqrt (refEnergy * lagEnergy);
    sweepR[static_cast<size_t> (sweepLag)] = denom > 1.0e-12f ? dot / denom : 0.0f;

    if (++sweepLag < numLags)
        return false;

    sweepLag = 0; // the next decimated sample starts a fresh sweep

    float rMax = 0.0f;
    for (int i = 0; i < numLags; ++i)
        rMax = juce::jmax (rMax, sweepR[static_cast<size_t> (i)]);

    lastConfidence = 0.0f;

    if (rMax > 0.0f)
    {
        // Prefer the LARGEST lag whose local peak is within 5% of the
        // global max: on a periodic input every period multiple peaks
        // equally, and the largest keeps the separation closest to its
        // nominal 30 ms (least character drift).
        for (int i = numLags - 2; i >= 1; --i)
        {
            const auto y1 = sweepR[static_cast<size_t> (i)];

            if (y1 >= 0.95f * rMax
                && y1 >= sweepR[static_cast<size_t> (i - 1)]
                && y1 >= sweepR[static_cast<size_t> (i + 1)])
            {
                // Parabolic sub-lag refinement (same technique as the FFT
                // peak probe in tests): the NACF of a periodic signal is
                // smooth around a peak.
                const auto y0 = sweepR[static_cast<size_t> (i - 1)];
                const auto y2 = sweepR[static_cast<size_t> (i + 1)];
                const auto den = y0 - 2.0f * y1 + y2;
                const auto offset = std::abs (den) > 1.0e-9f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (y0 - y2) / den) : 0.0f;

                lastLagFullRate = (static_cast<float> (lagMin + i) + offset) * static_cast<float> (decFactor);
                lastConfidence = juce::jlimit (0.0f, 1.0f, y1);
                break;
            }
        }
    }

    // Spectral plausibility gate: the normalized autocorrelation is
    // scale-invariant, so a treble-only tone aliased through the decimator
    // would fake full confidence at some meaningless lag. Only trust the
    // detector when real energy exists below its lowpass.
    if (lowPower < 0.02f * fullPower + 1.0e-12f)
        lastConfidence = 0.0f;

    return true;
}

//==============================================================================

void SpreadPitch::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    juce::dsp::ProcessSpec monoSpec = spec;
    monoSpec.numChannels = 1;

    const auto maxBaseMs = juce::jmax (baseDelayUpMs, baseDelayDownMs) * maxTimeScale;
    const auto maxDelaySamples = static_cast<float> ((maxBaseMs + grainMs + capacityHeadroomMs) * 0.001 * sampleRate);

    voiceUp.prepare (monoSpec, maxDelaySamples);
    voiceDown.prepare (monoSpec, maxDelaySamples);

    nominalSepSamples = static_cast<float> (grainMs * 0.0005 * sampleRate);
    detector.prepare (sampleRate, grainMs * 0.0005f, detectorFloorHz);

    // Rate-invariant tolerances (see header): one detector lag quantum is
    // decFactor full-rate samples, and the sin^2 alignment tolerance is a
    // time quantity (~170 us, == 8 samples at 48 kHz).
    sepDeadbandSamples = 0.35f * static_cast<float> (detector.decFactor);
    blendAlignTolInv = 1.0f / (2.0f * static_cast<float> (detector.decFactor));

    widthSmoothed.reset (sampleRate, smoothingTimeSeconds);
    widthSmoothed.setCurrentAndTargetValue (width);
    detuneSmoothed.reset (sampleRate, smoothingTimeSeconds);
    detuneSmoothed.setCurrentAndTargetValue (detuneCents);
    timeScaleSmoothed.reset (sampleRate, smoothingTimeSeconds);
    timeScaleSmoothed.setCurrentAndTargetValue (timeScale);
    sepTargetSmoothed.reset (sampleRate, targetSmoothingSeconds);
    sepTargetSmoothed.setCurrentAndTargetValue (nominalSepSamples);
    windowBlendSmoothed.reset (sampleRate, targetSmoothingSeconds);
    windowBlendSmoothed.setCurrentAndTargetValue (0.0f);

    reset();
}

void SpreadPitch::setDetuneCents (float cents) noexcept
{
    detuneCents = juce::jlimit (0.0f, 15.0f, cents);
    detuneSmoothed.setTargetValue (detuneCents);
}

void SpreadPitch::setTimeScale (float scale) noexcept
{
    timeScale = juce::jlimit (0.5f, 2.0f, scale);
    timeScaleSmoothed.setTargetValue (timeScale);
}

void SpreadPitch::setWidth (float amount01) noexcept
{
    width = juce::jlimit (0.0f, 1.0f, amount01);
    widthSmoothed.setTargetValue (width);
}

void SpreadPitch::setSmartSplice (bool enabled) noexcept
{
    smartSplice = enabled;

    if (! enabled)
    {
        detectorEngaged = false;
        sepTargetSmoothed.setCurrentAndTargetValue (nominalSepSamples);
        windowBlendSmoothed.setCurrentAndTargetValue (0.0f);
    }
}

void SpreadPitch::reset()
{
    detuneSmoothed.setCurrentAndTargetValue (detuneCents);
    timeScaleSmoothed.setCurrentAndTargetValue (timeScale);
    widthSmoothed.setCurrentAndTargetValue (width);
    sepTargetSmoothed.setCurrentAndTargetValue (nominalSepSamples);
    windowBlendSmoothed.setCurrentAndTargetValue (0.0f);

    detector.reset();
    detectorEngaged = false;

    voiceUp.reset (static_cast<float> (baseDelayUpMs * timeScale * 0.001 * sampleRate), nominalSepSamples);
    voiceDown.reset (static_cast<float> (baseDelayDownMs * timeScale * 0.001 * sampleRate), nominalSepSamples);

    voiceUp.pitchRatio = centsToRatio (detuneCents);
    voiceDown.pitchRatio = centsToRatio (-detuneCents);
}

void SpreadPitch::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto numChannels = block.getNumChannels();
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0 || numChannels == 0)
        return;

    const auto samplesPerMsUp = static_cast<float> (baseDelayUpMs * 0.001 * sampleRate);
    const auto samplesPerMsDown = static_cast<float> (baseDelayDownMs * 0.001 * sampleRate);

    auto* left = block.getChannelPointer (0);
    auto* right = numChannels > 1 ? block.getChannelPointer (1) : nullptr;

    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        // Per-sample smoothed detune/timeScale (brief F6 - no block steps).
        const auto detuneNow = detuneSmoothed.getNextValue();
        const auto timeScaleNow = timeScaleSmoothed.getNextValue();

        voiceUp.pitchRatio = centsToRatio (detuneNow);
        voiceDown.pitchRatio = centsToRatio (-detuneNow);

        const auto monoInput = right != nullptr ? 0.5f * (left[sample] + right[sample]) : left[sample];

        auto sepTargetNow = nominalSepSamples;
        auto blendUp = 0.0f;
        auto blendDown = 0.0f;

        if (smartSplice)
        {
            if (detector.pushSample (monoInput))
            {
                // Hysteresis: engage above 0.6, release below 0.5 -
                // borderline-periodic material (breathy vocal, tone+noise)
                // hovering at the gate must not flap the separation target
                // between nominal and snapped on successive sweeps.
                if (detectorEngaged ? detector.lastConfidence < confidenceGateRelease
                                    : detector.lastConfidence > confidenceGateLow)
                    detectorEngaged = ! detectorEngaged;

                if (detectorEngaged)
                {
                    const auto snapped = juce::jlimit (0.55f * nominalSepSamples, nominalSepSamples, detector.lastLagFullRate);

                    // Deadband: sweep-to-sweep estimate jitter must not
                    // keep the separation slewing forever on a held note.
                    if (std::abs (snapped - sepTargetSmoothed.getTargetValue()) > sepDeadbandSamples)
                        sepTargetSmoothed.setTargetValue (snapped);

                    windowBlendSmoothed.setTargetValue (
                        juce::jmap (juce::jlimit (confidenceGateLow, confidenceGateHigh, detector.lastConfidence),
                                    confidenceGateLow, confidenceGateHigh, 0.0f, 1.0f));
                }
                else
                {
                    sepTargetSmoothed.setTargetValue (nominalSepSamples);
                    windowBlendSmoothed.setTargetValue (0.0f);
                }
            }

            sepTargetNow = sepTargetSmoothed.getNextValue();
            const auto blendTarget = windowBlendSmoothed.getNextValue();

            // Only trust the sin^2 law once each voice's separation has
            // actually arrived - a misaligned amplitude-complementary pair
            // is strictly worse than the equal-power fallback.
            blendUp = blendTarget * juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (voiceUp.sepSamples - sepTargetNow) * blendAlignTolInv);
            blendDown = blendTarget * juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (voiceDown.sepSamples - sepTargetNow) * blendAlignTolInv);
        }

        const auto up = voiceUp.processSample (monoInput, samplesPerMsUp * timeScaleNow, sepTargetNow, blendUp);
        const auto down = voiceDown.processSample (monoInput, samplesPerMsDown * timeScaleNow, sepTargetNow, blendDown);

        const auto widthNow = juce::jlimit (0.0f, 1.0f, widthSmoothed.getNextValue());
        const auto centre = 0.5f * (up + down);

        left[sample] = juce::jmap (widthNow, centre, up);

        if (right != nullptr)
            right[sample] = juce::jmap (widthNow, centre, down);
    }
}
