#include "MiserereEngine.h"

MiserereEngine::MiserereEngine()
{
    // Bus C's fixed all-buttons voicing (brief): ~20:1 into a fixed
    // threshold, mid-forward sidechain, program-dependent release. Drive is
    // the user's "how hard" control - there is no threshold parameter.
    smash.setRatio (smashRatio);
    smash.setThresholdDb (smashThresholdDb);
    smash.setSidechainTiltEnabled (true);
    smash.setProgramDependentReleaseEnabled (true);

    // juce::dsp::Gain default-constructs its SmoothedValue at 0 (silence!) -
    // prime both trims to unity so an engine that is prepared/processed
    // before any setter call (e.g. in isolation in tests) passes audio.
    inTrimGain.setGainLinear (1.0f);
    outTrimGain.setGainLinear (1.0f);
}

void MiserereEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    inTrimGain.setRampDurationSeconds (smoothingTimeSeconds);
    inTrimGain.prepare (spec);
    outTrimGain.setRampDurationSeconds (smoothingTimeSeconds);
    outTrimGain.prepare (spec);

    hpf.prepare (spec);
    consoleEq.prepare (spec);
    fetComp.prepare (spec);
    deEsser.prepare (spec);
    tapeSat.prepare (spec);

    passiveEqIn.prepare (spec);
    opto.prepare (spec);
    passiveAirOut.prepare (spec);

    smash.prepare (spec);

    slap.prepare (spec);

    const auto numChannels = static_cast<int> (spec.numChannels);
    const auto numSamples = static_cast<int> (spec.maximumBlockSize);

    for (auto& buffer : busBuffers)
        buffer.setSize (numChannels, numSamples);

    for (int bus = 0; bus < numBusses; ++bus)
    {
        busGainSmoothed[static_cast<size_t> (bus)].reset (sampleRate, smoothingTimeSeconds);

        const auto levelDb = lastBusLevelDb[static_cast<size_t> (bus)];
        const auto gain = levelDb <= faderFloorDb + 0.05f ? 0.0f : juce::Decibels::decibelsToGain (levelDb);
        busGainSmoothed[static_cast<size_t> (bus)].setCurrentAndTargetValue (gain);
    }

    reset();
}

void MiserereEngine::reset()
{
    inTrimGain.reset();
    outTrimGain.reset();

    hpf.reset();
    consoleEq.reset();
    fetComp.reset();
    deEsser.reset();
    tapeSat.reset();

    passiveEqIn.reset();
    opto.reset();
    passiveAirOut.reset();

    smash.reset();

    slap.reset(); // includes the full delay line - see SlapDelay::reset()
}

void MiserereEngine::setBusLevelDb (int busIndex, float levelDb) noexcept
{
    if (busIndex < 0 || busIndex >= numBusses)
        return;

    lastBusLevelDb[static_cast<size_t> (busIndex)] = levelDb;

    // At or below the fader floor the bus contributes exactly 0 ("-inf"
    // semantics for the bottom of the fader range).
    const auto gain = levelDb <= faderFloorDb + 0.05f ? 0.0f : juce::Decibels::decibelsToGain (levelDb);
    busGainSmoothed[static_cast<size_t> (busIndex)].setTargetValue (gain);
}

void MiserereEngine::setBusMute (int busIndex, bool muted) noexcept
{
    if (busIndex >= 0 && busIndex < numBusses)
        busMuted[static_cast<size_t> (busIndex)] = muted;
}

void MiserereEngine::setBusSolo (int busIndex, bool soloed) noexcept
{
    if (busIndex >= 0 && busIndex < numBusses)
        busSoloed[static_cast<size_t> (busIndex)] = soloed;
}

void MiserereEngine::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto requestedSamples = block.getNumSamples();

    if (requestedSamples == 0)
        return;

    // Defensive last-resort clamp to the bus-buffer capacity established in
    // prepare() (PluginProcessor chunks oversized host blocks before they
    // reach here, so in practice this never trims - see processBlock()).
    const auto numSamples = juce::jmin (requestedSamples, static_cast<size_t> (busBuffers[0].getNumSamples()));
    const auto numChannels = juce::jmin (block.getNumChannels(), static_cast<size_t> (busBuffers[0].getNumChannels()));

    if (numSamples == 0 || numChannels == 0)
        return;

    auto workingBlock = block.getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels);

    juce::dsp::ProcessContextReplacing<float> inputContext (workingBlock);
    inTrimGain.process (inputContext);

    // Fan out: all four busses receive the identical trimmed input.
    std::array<juce::dsp::AudioBlock<float>, numBusses> busBlocks {
        juce::dsp::AudioBlock<float> (busBuffers[0]).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels),
        juce::dsp::AudioBlock<float> (busBuffers[1]).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels),
        juce::dsp::AudioBlock<float> (busBuffers[2]).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels),
        juce::dsp::AudioBlock<float> (busBuffers[3]).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels)
    };

    for (auto& busBlock : busBlocks)
        busBlock.copyFrom (workingBlock);

    // Bus A - Direct chain.
    hpf.process (busBlocks[0]);
    consoleEq.process (busBlocks[0]);
    fetComp.process (busBlocks[0]);
    deEsser.process (busBlocks[0]);
    tapeSat.process (busBlocks[0]);

    // Bus B - Opto sandwich.
    passiveEqIn.process (busBlocks[1]);
    opto.process (busBlocks[1]);
    passiveAirOut.process (busBlocks[1]);

    // Bus C - Smash.
    smash.process (busBlocks[2]);

    // Bus D - Slap (wet-only output).
    slap.process (busBlocks[3]);

    // Mute/Solo resolution (console semantics): Mute always wins; if any
    // bus is soloed, only soloed (and unmuted) busses reach the sum. All
    // bus DSP above ran unconditionally so envelopes/filters/delay stay
    // continuous and unmuting never pops.
    const bool anySoloed = busSoloed[0] || busSoloed[1] || busSoloed[2] || busSoloed[3];

    std::array<float, numBusses> routeGain {};
    for (size_t bus = 0; bus < static_cast<size_t> (numBusses); ++bus)
        routeGain[bus] = (! busMuted[bus] && (! anySoloed || busSoloed[bus])) ? 1.0f : 0.0f;

    // Sum the busses back into the working block (the host's own buffer
    // memory), applying the per-sample smoothed fader gains, and sanitise
    // any non-finite sample to 0 (see class comment).
    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        std::array<float, numBusses> faderGain {};
        for (size_t bus = 0; bus < static_cast<size_t> (numBusses); ++bus)
            faderGain[bus] = busGainSmoothed[bus].getNextValue() * routeGain[bus];

        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            auto* out = workingBlock.getChannelPointer (channel);

            auto sum = 0.0f;
            for (size_t bus = 0; bus < static_cast<size_t> (numBusses); ++bus)
                sum += busBlocks[bus].getChannelPointer (channel)[sample] * faderGain[bus];

            out[sample] = std::isfinite (sum) ? sum : 0.0f;
        }
    }

    juce::dsp::ProcessContextReplacing<float> outputContext (workingBlock);
    outTrimGain.process (outputContext);
}
