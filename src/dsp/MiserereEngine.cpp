#include "MiserereEngine.h"

MiserereEngine::MiserereEngine()
{
    // juce::dsp::Gain default-constructs its SmoothedValue at 0 (silence!) -
    // prime the trims to unity so an engine that is prepared/processed
    // before any setter call (e.g. in isolation in tests) passes audio.
    inTrimGain.setGainLinear (1.0f);
    outTrimGain.setGainLinear (1.0f);
}

void MiserereEngine::setParallelTrimDb (float newTrimDb) noexcept
{
    lastParallelTrimDb = newTrimDb;
    parallelTrimSmoothed.setTargetValue (juce::Decibels::decibelsToGain (newTrimDb));
}

void MiserereEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    inTrimGain.setRampDurationSeconds (smoothingTimeSeconds);
    inTrimGain.prepare (spec);
    outTrimGain.setRampDurationSeconds (smoothingTimeSeconds);
    outTrimGain.prepare (spec);

    parallelTrimSmoothed.reset (sampleRate, smoothingTimeSeconds);
    parallelTrimSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (lastParallelTrimDb));

    deesserPre.prepare (spec);
    directFet.prepare (spec);
    consoleEq.prepare (spec);
    tapeSat.prepare (spec);
    deesserPost.prepare (spec);

    crush.prepare (spec);
    sandwichPreEq.prepare (spec);
    opto.prepare (spec);
    sandwichPostEq.prepare (spec);
    spread.prepare (spec);
    slap.prepare (spec);

    const auto numChannels = static_cast<int> (spec.numChannels);
    const auto numSamples = static_cast<int> (spec.maximumBlockSize);

    directBuffer.setSize (numChannels, numSamples);
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

    deesserPre.reset();
    directFet.reset();
    consoleEq.reset();
    tapeSat.reset();
    deesserPost.reset();

    crush.reset();
    sandwichPreEq.reset();
    opto.reset();
    sandwichPostEq.reset();
    spread.reset(); // includes both micro-pitch delay lines
    slap.reset();   // includes the slap delay line
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

void MiserereEngine::setBusAudition (int busIndex, bool auditioned) noexcept
{
    if (busIndex >= 0 && busIndex < numBusses)
        busAuditioned[static_cast<size_t> (busIndex)] = auditioned;
}

void MiserereEngine::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto requestedSamples = block.getNumSamples();

    if (requestedSamples == 0)
        return;

    // Defensive last-resort clamp to the buffer capacity established in
    // prepare() (PluginProcessor chunks oversized host blocks before they
    // reach here, so in practice this never trims - see processBlock()).
    const auto numSamples = juce::jmin (requestedSamples, static_cast<size_t> (directBuffer.getNumSamples()));
    const auto numChannels = juce::jmin (block.getNumChannels(), static_cast<size_t> (directBuffer.getNumChannels()));

    if (numSamples == 0 || numChannels == 0)
        return;

    auto workingBlock = block.getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels);

    juce::dsp::ProcessContextReplacing<float> inputContext (workingBlock);
    inTrimGain.process (inputContext);

    //==========================================================================
    // Direct path (serial; every section optional, ALL OFF by default -
    // the core "bit-transparent wire" invariant).
    auto directBlock = juce::dsp::AudioBlock<float> (directBuffer).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels);
    directBlock.copyFrom (workingBlock);

    deesserPre.process (directBlock);

    if (directFetEnabled)
        directFet.process (directBlock);

    consoleEq.process (directBlock);
    tapeSat.process (directBlock);
    deesserPost.process (directBlock);

    //==========================================================================
    // Fan out: all four parallel busses receive an identical unity-tap copy
    // of the direct-path output (post-fader unity sends - the brief's
    // core correction over v1).
    std::array<juce::dsp::AudioBlock<float>, numBusses> busBlocks {
        juce::dsp::AudioBlock<float> (busBuffers[0]).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels),
        juce::dsp::AudioBlock<float> (busBuffers[1]).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels),
        juce::dsp::AudioBlock<float> (busBuffers[2]).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels),
        juce::dsp::AudioBlock<float> (busBuffers[3]).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels)
    };

    for (auto& busBlock : busBlocks)
        busBlock.copyFrom (directBlock);

    // (1) CRUSH.
    crush.process (busBlocks[0]);

    // (2) SANDWICH: Passive EQ -> Opto Leveler -> Passive EQ.
    sandwichPreEq.process (busBlocks[1]);
    opto.process (busBlocks[1]);
    sandwichPostEq.process (busBlocks[1]);

    // (3) SPREAD.
    spread.process (busBlocks[2]);

    // (4) SLAP (wet-only output).
    slap.process (busBlocks[3]);

    //==========================================================================
    // Mute/Audition resolution (console semantics): Mute always wins; if
    // any bus is auditioned, ONLY the auditioned (unmuted) bus(ses) reach
    // the sum, and the direct path itself is excluded too (Audition
    // isolates exactly what it names). All bus DSP above ran
    // unconditionally so envelopes/filters/delay lines stay continuous and
    // unmuting never pops.
    const bool anyAuditioned = busAuditioned[0] || busAuditioned[1] || busAuditioned[2] || busAuditioned[3];
    const auto directRouteGain = anyAuditioned ? 0.0f : 1.0f;

    std::array<float, numBusses> routeGain {};
    for (size_t bus = 0; bus < static_cast<size_t> (numBusses); ++bus)
        routeGain[bus] = (! busMuted[bus] && (! anyAuditioned || busAuditioned[bus])) ? 1.0f : 0.0f;

    // Sum the direct path and the four busses back into the working block
    // (the host's own buffer memory), applying the per-sample-smoothed
    // fader gains (each further scaled by the Parallel macro trim - the
    // "VCA ride back" gesture), and sanitise any non-finite sample to 0.
    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        const auto parallelTrimNow = parallelTrimSmoothed.getNextValue();

        std::array<float, numBusses> faderGain {};
        for (size_t bus = 0; bus < static_cast<size_t> (numBusses); ++bus)
            faderGain[bus] = busGainSmoothed[bus].getNextValue() * routeGain[bus] * parallelTrimNow;

        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            auto* out = workingBlock.getChannelPointer (channel);
            const auto directSample = directBlock.getChannelPointer (channel)[sample];

            auto sum = directSample * directRouteGain;
            for (size_t bus = 0; bus < static_cast<size_t> (numBusses); ++bus)
                sum += busBlocks[bus].getChannelPointer (channel)[sample] * faderGain[bus];

            out[sample] = std::isfinite (sum) ? sum : 0.0f;
        }
    }

    juce::dsp::ProcessContextReplacing<float> outputContext (workingBlock);
    outTrimGain.process (outputContext);
}
