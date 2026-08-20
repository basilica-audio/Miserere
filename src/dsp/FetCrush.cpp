#include "FetCrush.h"

FetCrush::BiasTuple FetCrush::biasTupleFor (Ratio r, Style s) noexcept
{
    // Discrete per-ratio bias tuples (research-fet-comp-1176.md sections
    // 1.1/3.2): each button shifts BOTH the rectifier bias (threshold) and
    // the loop steepness - higher ratio => higher threshold, steeper loop,
    // narrower emergent knee. ALL is a genuine fifth tuple, never an
    // interpolation: slight linear-region gain, under-damped loop
    // (loopGain x1.3 vs 20:1), doubled epsilon "hair". Gentle is a single
    // damped tuple that ignores the ratio selector (its existing semantic).
    if (s == Style::gentle)
        return { -32.0f, 0.35f, 0.5f, 0.5f, 0.0f, 10.0f, 1.0f };

    BiasTuple tuple { -30.0f, 0.8f, 0.8f, 1.0f, 0.0f, 10.0f, 1.0f };

    switch (r)
    {
        case Ratio::r4:   tuple = { -30.0f, 0.8f, 0.8f, 1.0f, 0.0f, 10.0f, 1.0f }; break;
        case Ratio::r8:   tuple = { -28.0f, 1.4f, 1.0f, 1.2f, 0.0f, 10.0f, 1.0f }; break;
        case Ratio::r12:  tuple = { -26.0f, 1.9f, 1.1f, 1.5f, 0.0f, 10.0f, 1.0f }; break;
        case Ratio::r20:  tuple = { -24.0f, 2.4f, 1.2f, 1.7f, 0.0f, 10.0f, 1.0f }; break;
        // ALL: loopGain = 1.2*1.3, eps x2, rectifier rail overdriven + all
        // ratio resistors in parallel (chargeScale) -> the under-damped
        // slam/overshoot/plateau bias state.
        case Ratio::rAll: tuple = { -24.0f, 2.4f, 1.56f, 3.4f, 0.7f, 60.0f, 8.0f }; break;
    }

    // Vintage (issue #20): the same per-ratio tuple family with a hot early-
    // revision bias state - see the Style enum's docs. The ratio selector
    // stays fully active (unlike Gentle).
    if (s == Style::vintage)
    {
        tuple.thresholdDb -= 2.0f;  // hotter rectifier bias: GR starts earlier
        tuple.loopGain *= 1.12f;    // hotter loop: deeper GR, slightly looser
        tuple.epsScale *= 2.2f;     // more residual-mismatch "hair"
        tuple.chargeScale *= 1.3f;  // under-damped charge path
    }

    return tuple;
}

void FetCrush::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    const auto numChannels = static_cast<size_t> (spec.numChannels);
    capVoltage.assign (numChannels, 0.0);
    feedbackSample.assign (numChannels, 0.0f);
    lfSaturationState.assign (numChannels, 0.0f);
    lfSaturationState2.assign (numChannels, 0.0f);
    lfColourStages.assign (numChannels, {});

    tupleFadeLengthSamples = juce::jmax (1, static_cast<int> (tupleCrossfadeSeconds * sampleRate));

    // Snap the tuple (no crossfade across a prepare).
    currentTuple = targetTuple = biasTupleFor (ratio, style);
    tupleFadeSamplesLeft = 0;

    reset();
}

void FetCrush::reset()
{
    std::fill (capVoltage.begin(), capVoltage.end(), 0.0);
    std::fill (feedbackSample.begin(), feedbackSample.end(), 0.0f);
    std::fill (lfSaturationState.begin(), lfSaturationState.end(), 0.0f);
    std::fill (lfSaturationState2.begin(), lfSaturationState2.end(), 0.0f);

    for (auto& stage : lfColourStages)
        stage.reset();

    currentGainReductionDb.store (0.0f, std::memory_order_relaxed);
}

void FetCrush::setRatio (Ratio newRatio) noexcept
{
    if (ratio == newRatio)
        return;

    ratio = newRatio;
    targetTuple = biasTupleFor (ratio, style);
    tupleFadeSamplesLeft = tupleFadeLengthSamples; // 10 ms tuple crossfade (research 3.2)
}

void FetCrush::setStyle (Style newStyle) noexcept
{
    if (style == newStyle)
        return;

    style = newStyle;
    targetTuple = biasTupleFor (ratio, style);
    tupleFadeSamplesLeft = tupleFadeLengthSamples;
}

void FetCrush::setAttackStep (float step1to7) noexcept
{
    const auto step = juce::jlimit (1.0f, 7.0f, step1to7);
    attackUs = juce::jmap (step, 1.0f, 7.0f, attackMaxUs, attackMinUs);
}

void FetCrush::setReleaseStep (float step1to7) noexcept
{
    const auto step = juce::jlimit (1.0f, 7.0f, step1to7);
    releaseMs = juce::jmap (step, 1.0f, 7.0f, releaseMaxMs, releaseMinMs);
}

void FetCrush::process (juce::dsp::AudioBlock<float>& block,
                        const juce::dsp::AudioBlock<const float>* externalKey) noexcept
{
    const auto numChannels = block.getNumChannels();
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0 || numChannels == 0)
        return;

    // External sidechain (issue #23): the key REPLACES the feedback loop
    // drive, turning this into a feed-forward keyed compressor - see the
    // class comment. Fewer key channels than audio channels is legal (the
    // last key channel is reused).
    const auto keyChannels = externalKey != nullptr ? externalKey->getNumChannels() : 0;
    const auto useKey = keyChannels > 0 && externalKey->getNumSamples() >= numSamples;

    // Exponential (per-branch analytic) Euler coefficients for the
    // single-cap two-path RC (research section 3.2). The calibration
    // factors map the panel-spec dial bracketing onto MEASURED 63%/37%
    // times (the feedback loop lengthens raw RC responses); the attack
    // path carries the shared-leg release term (see
    // releaseAttackCouplingKappa in FetCrush.h).
    const auto attackTau = static_cast<double> (attackUs) * 1.0e-6 * static_cast<double> (attackRcCalibration)
                           + static_cast<double> (releaseMs) * 1.0e-3 * static_cast<double> (releaseAttackCouplingKappa);
    const auto releaseTau = static_cast<double> (releaseMs) * 1.0e-3 * static_cast<double> (releaseRcCalibration);
    const auto aA = 1.0 - std::exp (-1.0 / (attackTau * sampleRate));
    const auto aR = 1.0 - std::exp (-1.0 / (releaseTau * sampleRate));

    const auto lfAlpha = static_cast<float> (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * lfSaturationCutoffHz / sampleRate));

    const auto numChannelsToProcess = juce::jmin (numChannels, capVoltage.size());

    // Non-finite abuse re-seed (brief 6.12): clamp/clear poisoned loop
    // state at block rate; the engine's output sanitiser covers the sum
    // within the poisoned block.
    for (size_t channel = 0; channel < numChannelsToProcess; ++channel)
    {
        if (! std::isfinite (feedbackSample[channel]) || ! std::isfinite (static_cast<float> (capVoltage[channel])))
        {
            feedbackSample[channel] = 0.0f;
            capVoltage[channel] = 0.0;
        }

        if (! std::isfinite (lfSaturationState[channel]) || ! std::isfinite (lfSaturationState2[channel]))
        {
            lfSaturationState[channel] = 0.0f;
            lfSaturationState2[channel] = 0.0f;
            lfColourStages[channel].reset();
        }
    }

    for (auto& stage : lfColourStages)
        stage.prepareBlock (lfColourDrive, 1.0f / lfColourDrive); // k = 1 -> pure residual delta

    float peakGainReductionDb = 0.0f;

    // Derived loop constants - recomputed only while the 10 ms tuple
    // crossfade is running (they involve pow/exp).
    auto thresholdLinear = std::pow (10.0, static_cast<double> (currentTuple.thresholdDb) / 20.0);
    auto vth = static_cast<double> (currentTuple.vthScale);
    auto kSc = vth / thresholdLinear;
    auto loopGain = static_cast<double> (currentTuple.loopGain);
    auto eps = epsilonBase * static_cast<double> (currentTuple.epsScale);
    auto linGain = static_cast<double> (juce::Decibels::decibelsToGain (currentTuple.linGainDb));
    auto rectRail = static_cast<double> (currentTuple.rectRailVolts);
    auto aACharge = std::min (1.0, aA * static_cast<double> (currentTuple.chargeScale));

    const auto numSidechains = linked ? std::min<size_t> (1, numChannelsToProcess) : numChannelsToProcess;

    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        // Advance the 10 ms tuple crossfade (research 3.2: crossfade the
        // tuple, let the loop swallow the transient).
        if (tupleFadeSamplesLeft > 0)
        {
            const auto t = 1.0f / static_cast<float> (tupleFadeSamplesLeft);
            currentTuple.thresholdDb += t * (targetTuple.thresholdDb - currentTuple.thresholdDb);
            currentTuple.vthScale += t * (targetTuple.vthScale - currentTuple.vthScale);
            currentTuple.loopGain += t * (targetTuple.loopGain - currentTuple.loopGain);
            currentTuple.epsScale += t * (targetTuple.epsScale - currentTuple.epsScale);
            currentTuple.linGainDb += t * (targetTuple.linGainDb - currentTuple.linGainDb);
            currentTuple.rectRailVolts += t * (targetTuple.rectRailVolts - currentTuple.rectRailVolts);
            currentTuple.chargeScale += t * (targetTuple.chargeScale - currentTuple.chargeScale);
            --tupleFadeSamplesLeft;

            thresholdLinear = std::pow (10.0, static_cast<double> (currentTuple.thresholdDb) / 20.0);
            vth = static_cast<double> (currentTuple.vthScale);
            kSc = vth / thresholdLinear;
            loopGain = static_cast<double> (currentTuple.loopGain);
            eps = epsilonBase * static_cast<double> (currentTuple.epsScale);
            linGain = static_cast<double> (juce::Decibels::decibelsToGain (currentTuple.linGainDb));
            rectRail = static_cast<double> (currentTuple.rectRailVolts);
            aACharge = std::min (1.0, aA * static_cast<double> (currentTuple.chargeScale));
        }

        // --- Sidechain pass: one shared cap when linked (driven by the
        // max-abs of the pair), one per channel otherwise. Two fixed-point
        // iterations of the loop per sample (research 3.2): evaluate the
        // cell from the previous output estimate, recompute the rectifier
        // from the CURRENT estimate, re-evaluate.
        double sharedVCell = 0.0;
        double sharedBaseGain = 1.0;

        for (size_t sc = 0; sc < numSidechains; ++sc)
        {
            double yEstimateAbs = 0.0;
            double drivenForSidechain = 0.0;

            if (linked)
            {
                for (size_t channel = 0; channel < numChannelsToProcess; ++channel)
                {
                    yEstimateAbs = std::max (yEstimateAbs, std::abs (static_cast<double> (feedbackSample[channel])));
                    drivenForSidechain = std::max (drivenForSidechain,
                        std::abs (static_cast<double> (block.getChannelPointer (channel)[sample]) * static_cast<double> (inputDriveLinear)));
                }
            }
            else
            {
                yEstimateAbs = std::abs (static_cast<double> (feedbackSample[sc]));
                drivenForSidechain = static_cast<double> (block.getChannelPointer (sc)[sample]) * static_cast<double> (inputDriveLinear);
            }

            // Keyed: the rectifier drive comes from the key (through the
            // same input drive) instead of from the loop. The fixed-point
            // iteration below then has nothing to converge - it recomputes
            // the identical vC twice, harmlessly - because the detector no
            // longer depends on the cell's own output.
            if (useKey)
            {
                yEstimateAbs = 0.0;

                if (linked)
                {
                    for (size_t channel = 0; channel < numChannelsToProcess; ++channel)
                    {
                        const auto keySample = externalKey->getChannelPointer (juce::jmin (channel, keyChannels - 1))[sample];

                        if (std::isfinite (keySample))
                            yEstimateAbs = std::max (yEstimateAbs, std::abs (static_cast<double> (keySample)));
                    }
                }
                else
                {
                    const auto keySample = externalKey->getChannelPointer (juce::jmin (sc, keyChannels - 1))[sample];
                    yEstimateAbs = std::isfinite (keySample) ? std::abs (static_cast<double> (keySample)) : 0.0;
                }

                yEstimateAbs *= static_cast<double> (inputDriveLinear);
            }

            double vC = capVoltage[sc];
            double vCell = 0.0;
            double baseGain = 1.0;

            for (int iteration = 0; iteration < 2; ++iteration)
            {
                // Rectifier with driver rail (per-tuple: the ABI bias state
                // overdrives the rail - see BiasTuple::rectRailVolts).
                const auto vRect = std::min (std::max (0.0, kSc * yEstimateAbs - vth), rectRail);

                // Diode-gated charge path + ALWAYS-ON release leak: R_rel
                // sits permanently across the cap in the hardware - the
                // leak participates during attack, which is half of the
                // attack/release coupling (the other half is the shared-leg
                // kappa term in the attack tau).
                vC = capVoltage[sc];
                if (vRect > vC)
                    vC += aACharge * (vRect - vC);
                vC -= aR * vC;
                vC = juce::jlimit (0.0, pinchOffVolts * 0.999, vC);

                vCell = juce::jlimit (0.0, pinchOffVolts * 0.999, loopGain * vC);

                // FET divider in conductance form: gds ~ vCell (see class
                // comment), G = 1/(1 + Rs*gds) - exact unity at idle.
                const auto gds = vCell / (rOnOhm * pinchOffVolts);
                baseGain = 1.0 / (1.0 + seriesOhm * gds);

                // Residual square-law mismatch for the output estimate. The
                // vCell/|Vp| factor scales the mismatch with control-voltage
                // excursion from the trimmed bias point - the measured
                // "hair grows with GR depth" behaviour (research 1.2.4).
                const auto vds = baseGain * drivenForSidechain;
                const auto gainWithHair = baseGain * (1.0 - eps * vds * (vCell / pinchOffVolts) / (2.0 * (vCell + pinchOffVolts)));

                if (! useKey)
                    yEstimateAbs = std::abs (gainWithHair * drivenForSidechain * linGain * static_cast<double> (outputTrimLinear));
            }

            capVoltage[sc] = vC;
            sharedVCell = vCell;
            sharedBaseGain = baseGain;
        }

        const auto reductionDb = static_cast<float> (20.0 * std::log10 (1.0 + seriesOhm * sharedVCell / (rOnOhm * pinchOffVolts)));
        const auto colourAmount = juce::jlimit (0.0f, 1.0f, reductionDb / harmonicReferenceGrDb);

        // --- Audio pass: apply the (per-sidechain) divider gain with the
        // per-channel epsilon "hair" (signal-dependent), then the GR-gated
        // LF transformer colour delta.
        for (size_t channel = 0; channel < numChannelsToProcess; ++channel)
        {
            auto* data = block.getChannelPointer (channel);
            const auto driven = static_cast<double> (data[sample]) * static_cast<double> (inputDriveLinear);

            // For unlinked stereo the per-channel sidechain already ran
            // above (sc == channel); recover its cell values.
            double vCell = sharedVCell;
            double baseGain = sharedBaseGain;

            if (! linked)
            {
                vCell = juce::jlimit (0.0, pinchOffVolts * 0.999, loopGain * capVoltage[channel]);
                const auto gds = vCell / (rOnOhm * pinchOffVolts);
                baseGain = 1.0 / (1.0 + seriesOhm * gds);
            }

            const auto vds = baseGain * driven;
            const auto gain = baseGain * (1.0 - eps * vds * (vCell / pinchOffVolts) / (2.0 * (vCell + pinchOffVolts)));

            const auto y = gain * driven * linGain * static_cast<double> (outputTrimLinear);

            auto attenuated = static_cast<float> (y);

            // GR-gated LF transformer colour: one-pole 150 Hz extract into
            // the fixed tanh curve; the delta IS the ADAA residual scaled
            // by the GR gate (brief F1 parallel-delta rule).
            const auto channelReductionDb = linked
                ? reductionDb
                : static_cast<float> (20.0 * std::log10 (1.0 + seriesOhm * vCell / (rOnOhm * pinchOffVolts)));
            const auto channelColour = linked
                ? colourAmount
                : juce::jlimit (0.0f, 1.0f, channelReductionDb / harmonicReferenceGrDb);

            // Second-order LF extract (two cascaded one-poles at 150 Hz):
            // steep enough that the transformer delta stays genuinely
            // LF-selective (the residual curve is cubic in its input, so
            // midband leak-through falls ~18 dB/oct in practice).
            auto& lfState = lfSaturationState[channel];
            auto& lfState2 = lfSaturationState2[channel];
            lfState += lfAlpha * (attenuated - lfState);
            lfState2 += lfAlpha * (lfState - lfState2);
            attenuated += channelColour * lfColourStages[channel].processResidual (lfState2);

            data[sample] = attenuated;
            feedbackSample[channel] = static_cast<float> (y);

            peakGainReductionDb = juce::jmax (peakGainReductionDb, channelReductionDb);
        }
    }

    currentGainReductionDb.store (peakGainReductionDb, std::memory_order_relaxed);
}
