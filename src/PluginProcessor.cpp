#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIds.h"
#include "params/ParameterLayout.h"

namespace
{
    // Maps an AudioParameterChoice's raw (float) index into a concrete
    // value table, clamped defensively.
    template <size_t N>
    float choiceToValue (const std::atomic<float>* rawIndex, const std::array<float, N>& table) noexcept
    {
        const auto index = juce::jlimit (0, static_cast<int> (N) - 1,
                                          static_cast<int> (rawIndex->load (std::memory_order_relaxed)));
        return table[static_cast<size_t> (index)];
    }
}

//==============================================================================
MiserereAudioProcessor::MiserereAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    inTrimDb = apvts.getRawParameterValue (ParamIDs::inTrim);
    outTrimDb = apvts.getRawParameterValue (ParamIDs::outTrim);
    bypassFlag = apvts.getRawParameterValue (ParamIDs::bypass);
    bypassParameter = apvts.getParameter (ParamIDs::bypass);

    busAHpfEnabled = apvts.getRawParameterValue (ParamIDs::busAHpfEnabled);
    busAHpfFreq = apvts.getRawParameterValue (ParamIDs::busAHpfFreq);
    busAEqLowGain = apvts.getRawParameterValue (ParamIDs::busAEqLowGain);
    busAEqMidFreq = apvts.getRawParameterValue (ParamIDs::busAEqMidFreq);
    busAEqMidGain = apvts.getRawParameterValue (ParamIDs::busAEqMidGain);
    busAEqMidQ = apvts.getRawParameterValue (ParamIDs::busAEqMidQ);
    busAEqHighGain = apvts.getRawParameterValue (ParamIDs::busAEqHighGain);
    busACompRatio = apvts.getRawParameterValue (ParamIDs::busACompRatio);
    busACompThreshold = apvts.getRawParameterValue (ParamIDs::busACompThreshold);
    busACompAttack = apvts.getRawParameterValue (ParamIDs::busACompAttack);
    busACompRelease = apvts.getRawParameterValue (ParamIDs::busACompRelease);
    busACompMakeup = apvts.getRawParameterValue (ParamIDs::busACompMakeup);
    busADeessEnabled = apvts.getRawParameterValue (ParamIDs::busADeessEnabled);
    busADeessFreq = apvts.getRawParameterValue (ParamIDs::busADeessFreq);
    busADeessThreshold = apvts.getRawParameterValue (ParamIDs::busADeessThreshold);
    busASatDrive = apvts.getRawParameterValue (ParamIDs::busASatDrive);

    busBLowBoostFreq = apvts.getRawParameterValue (ParamIDs::busBLowBoostFreq);
    busBLowBoostGain = apvts.getRawParameterValue (ParamIDs::busBLowBoostGain);
    busBHighBoostFreq = apvts.getRawParameterValue (ParamIDs::busBHighBoostFreq);
    busBHighBoostGain = apvts.getRawParameterValue (ParamIDs::busBHighBoostGain);
    busBOptoReduction = apvts.getRawParameterValue (ParamIDs::busBOptoReduction);
    busBOptoMakeup = apvts.getRawParameterValue (ParamIDs::busBOptoMakeup);
    busBAirGain = apvts.getRawParameterValue (ParamIDs::busBAirGain);

    busCAttack = apvts.getRawParameterValue (ParamIDs::busCAttack);
    busCRelease = apvts.getRawParameterValue (ParamIDs::busCRelease);
    busCDrive = apvts.getRawParameterValue (ParamIDs::busCDrive);
    busCOutputTrim = apvts.getRawParameterValue (ParamIDs::busCOutputTrim);

    busDDelayMs = apvts.getRawParameterValue (ParamIDs::busDDelayMs);
    busDFeedback = apvts.getRawParameterValue (ParamIDs::busDFeedback);
    busDHpFreq = apvts.getRawParameterValue (ParamIDs::busDHpFreq);
    busDLpFreq = apvts.getRawParameterValue (ParamIDs::busDLpFreq);
    busDMono = apvts.getRawParameterValue (ParamIDs::busDMono);

    static constexpr const char* levelIds[] = { ParamIDs::busALevel, ParamIDs::busBLevel, ParamIDs::busCLevel, ParamIDs::busDLevel };
    static constexpr const char* muteIds[] = { ParamIDs::busAMute, ParamIDs::busBMute, ParamIDs::busCMute, ParamIDs::busDMute };
    static constexpr const char* soloIds[] = { ParamIDs::busASolo, ParamIDs::busBSolo, ParamIDs::busCSolo, ParamIDs::busDSolo };

    for (int bus = 0; bus < MiserereEngine::numBusses; ++bus)
    {
        busLevelDb[static_cast<size_t> (bus)] = apvts.getRawParameterValue (levelIds[bus]);
        busMuteFlag[static_cast<size_t> (bus)] = apvts.getRawParameterValue (muteIds[bus]);
        busSoloFlag[static_cast<size_t> (bus)] = apvts.getRawParameterValue (soloIds[bus]);

        // Solo exclusivity listener (see parameterChanged()).
        apvts.addParameterListener (soloIds[bus], this);
    }

    jassert (inTrimDb != nullptr);
    jassert (outTrimDb != nullptr);
    jassert (bypassFlag != nullptr);
    jassert (bypassParameter != nullptr);
    jassert (busAHpfEnabled != nullptr && busAHpfFreq != nullptr);
    jassert (busAEqLowGain != nullptr && busAEqMidFreq != nullptr && busAEqMidGain != nullptr);
    jassert (busAEqMidQ != nullptr && busAEqHighGain != nullptr);
    jassert (busACompRatio != nullptr && busACompThreshold != nullptr && busACompAttack != nullptr);
    jassert (busACompRelease != nullptr && busACompMakeup != nullptr);
    jassert (busADeessEnabled != nullptr && busADeessFreq != nullptr && busADeessThreshold != nullptr);
    jassert (busASatDrive != nullptr);
    jassert (busBLowBoostFreq != nullptr && busBLowBoostGain != nullptr);
    jassert (busBHighBoostFreq != nullptr && busBHighBoostGain != nullptr);
    jassert (busBOptoReduction != nullptr && busBOptoMakeup != nullptr && busBAirGain != nullptr);
    jassert (busCAttack != nullptr && busCRelease != nullptr && busCDrive != nullptr && busCOutputTrim != nullptr);
    jassert (busDDelayMs != nullptr && busDFeedback != nullptr && busDHpFreq != nullptr);
    jassert (busDLpFreq != nullptr && busDMono != nullptr);

    for (int bus = 0; bus < MiserereEngine::numBusses; ++bus)
    {
        jassert (busLevelDb[static_cast<size_t> (bus)] != nullptr);
        jassert (busMuteFlag[static_cast<size_t> (bus)] != nullptr);
        jassert (busSoloFlag[static_cast<size_t> (bus)] != nullptr);
    }
}

MiserereAudioProcessor::~MiserereAudioProcessor()
{
    static constexpr const char* soloIds[] = { ParamIDs::busASolo, ParamIDs::busBSolo, ParamIDs::busCSolo, ParamIDs::busDSolo };

    for (const auto* soloId : soloIds)
        apvts.removeParameterListener (soloId, this);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MiserereAudioProcessor::createParameterLayout()
{
    return msrr::createParameterLayout();
}

//==============================================================================
void MiserereAudioProcessor::parameterChanged (const juce::String& parameterId, float newValue)
{
    // Solo exclusivity: when a solo engages, release every other bus's
    // solo. The guard stops the setValueNotifyingHost() cascade from
    // re-entering this handler for the solos being cleared.
    //
    // Known limitation (documented in docs/architecture.md): if a host
    // automates two solos on in the same gesture, this listener resolves
    // them in callback order - fine for the intended "click Solo on a
    // different bus" workflow, and the engine stays well-defined for any
    // combination regardless.
    if (newValue < 0.5f || soloExclusivityGuard.exchange (true))
        return;

    static constexpr const char* soloIds[] = { ParamIDs::busASolo, ParamIDs::busBSolo, ParamIDs::busCSolo, ParamIDs::busDSolo };

    for (const auto* soloId : soloIds)
    {
        if (parameterId == soloId)
            continue;

        if (auto* param = apvts.getParameter (soloId))
            if (param->getValue() >= 0.5f)
                param->setValueNotifyingHost (0.0f);
    }

    soloExclusivityGuard.store (false);
}

//==============================================================================
const juce::String MiserereAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool MiserereAudioProcessor::acceptsMidi() const
{
    return false;
}

bool MiserereAudioProcessor::producesMidi() const
{
    return false;
}

bool MiserereAudioProcessor::isMidiEffect() const
{
    return false;
}

double MiserereAudioProcessor::getTailLengthSeconds() const
{
    // Bus D's slap can keep ringing after the input stops: worst case is
    // the maximum 180 ms delay with maximum (30%) feedback - after five
    // round trips (0.3^5 < 0.25%) the tail is far below audibility, so a
    // conservative 1 second covers it comfortably.
    return 1.0;
}

int MiserereAudioProcessor::getNumPrograms()
{
    return 1;
}

int MiserereAudioProcessor::getCurrentProgram()
{
    return 0;
}

void MiserereAudioProcessor::setCurrentProgram (int)
{
}

const juce::String MiserereAudioProcessor::getProgramName (int)
{
    return {};
}

void MiserereAudioProcessor::changeProgramName (int, const juce::String&)
{
}

//==============================================================================
void MiserereAudioProcessor::updateEngineParameters() noexcept
{
    const auto load = [] (const std::atomic<float>* value) { return value->load (std::memory_order_relaxed); };
    const auto loadBool = [&load] (const std::atomic<float>* value) { return load (value) >= 0.5f; };

    engine.setInTrimDb (load (inTrimDb));
    engine.setOutTrimDb (load (outTrimDb));

    engine.setHpfEnabled (loadBool (busAHpfEnabled));
    engine.setHpfFreqHz (load (busAHpfFreq));
    engine.setEqLowGainDb (load (busAEqLowGain));
    engine.setEqMidFreqHz (load (busAEqMidFreq));
    engine.setEqMidGainDb (load (busAEqMidGain));
    engine.setEqMidQ (load (busAEqMidQ));
    engine.setEqHighGainDb (load (busAEqHighGain));
    engine.setCompRatio (choiceToValue (busACompRatio, msrr::compRatioValues));
    engine.setCompThresholdDb (load (busACompThreshold));
    engine.setCompAttackMs (load (busACompAttack));
    engine.setCompReleaseMs (load (busACompRelease));
    engine.setCompMakeupDb (load (busACompMakeup));
    engine.setDeessEnabled (loadBool (busADeessEnabled));
    engine.setDeessFreqHz (load (busADeessFreq));
    engine.setDeessThresholdDb (load (busADeessThreshold));
    engine.setSatDriveDb (load (busASatDrive));

    engine.setPassiveLowBoost (choiceToValue (busBLowBoostFreq, msrr::busBLowBoostFreqHz), load (busBLowBoostGain));
    engine.setPassiveHighBoost (choiceToValue (busBHighBoostFreq, msrr::busBHighBoostFreqHz), load (busBHighBoostGain));
    engine.setOptoPeakReduction (load (busBOptoReduction) * 0.01f);
    engine.setOptoMakeupDb (load (busBOptoMakeup));
    engine.setPassiveAirGainDb (load (busBAirGain));

    engine.setSmashAttackMs (load (busCAttack));
    engine.setSmashReleaseMs (load (busCRelease));
    engine.setSmashDriveDb (load (busCDrive));
    engine.setSmashOutputTrimDb (load (busCOutputTrim));

    engine.setSlapDelayMs (load (busDDelayMs));
    engine.setSlapFeedback (load (busDFeedback) * 0.01f);
    engine.setSlapLoopHighPassHz (load (busDHpFreq));
    engine.setSlapLoopLowPassHz (load (busDLpFreq));
    engine.setSlapMonoEnabled (loadBool (busDMono));

    for (int bus = 0; bus < MiserereEngine::numBusses; ++bus)
    {
        engine.setBusLevelDb (bus, load (busLevelDb[static_cast<size_t> (bus)]));
        engine.setBusMute (bus, loadBool (busMuteFlag[static_cast<size_t> (bus)]));
        engine.setBusSolo (bus, loadBool (busSoloFlag[static_cast<size_t> (bus)]));
    }
}

//==============================================================================
void MiserereAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32> (getTotalNumOutputChannels());

    // Seed the engine from the current APVTS state before prepare() primes
    // smoothers/coefficients, so the very first block after prepareToPlay()
    // already reflects the host/session's actual parameter values rather
    // than the engine's built-in defaults.
    updateEngineParameters();

    engine.prepare (spec);

    preparedBlockSize = samplesPerBlock;

    // Nothing in any bus adds reported host latency: Busses A-C are
    // minimum-phase/causal by the suite's phase discipline, and Bus D's
    // delay is the effect itself, not a compensation delay (see
    // docs/adr/0003-parallel-bus-topology.md).
    setLatencySamples (engine.getLatencySamples());
}

void MiserereAudioProcessor::releaseResources()
{
}

void MiserereAudioProcessor::reset()
{
    engine.reset();
}

bool MiserereAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();

    const auto mainOut = layouts.getMainOutputChannelSet();
    const auto mainIn = layouts.getMainInputChannelSet();

    if (mainOut != mono && mainOut != stereo)
        return false;

    if (mainOut != mainIn)
        return false;

    return true;
}

void MiserereAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Buses are constrained to in == out (mono or stereo), so this is
    // normally a no-op, but it's cheap insurance against stray channels.
    for (auto channel = totalNumInputChannels; channel < totalNumOutputChannels; ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    if (bypassFlag->load (std::memory_order_relaxed) >= 0.5f)
        return;

    // Parameters are read once per host block (not per chunk): the engine's
    // smoothers spread any change across samples regardless.
    updateEngineParameters();

    juce::dsp::AudioBlock<float> fullBlock (buffer);

    // Oversized-block guard (a REAL clamp, not a jassert): hosts are
    // expected never to exceed the block size promised to prepareToPlay(),
    // but if one does, the buffer is processed in chunks of at most
    // preparedBlockSize so the engine's prepare()-sized bus buffers are
    // never indexed out of bounds - and no audio is dropped.
    const auto chunkLimit = preparedBlockSize > 0
                                 ? static_cast<size_t> (preparedBlockSize)
                                 : juce::jmax (static_cast<size_t> (1), fullBlock.getNumSamples());

    for (size_t offset = 0; offset < fullBlock.getNumSamples(); offset += chunkLimit)
    {
        const auto chunkLength = juce::jmin (chunkLimit, fullBlock.getNumSamples() - offset);
        auto chunk = fullBlock.getSubBlock (offset, chunkLength);
        engine.process (chunk);
    }
}

//==============================================================================
bool MiserereAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* MiserereAudioProcessor::createEditor()
{
    return new MiserereAudioProcessorEditor (*this);
}

//==============================================================================
juce::AudioProcessorParameter* MiserereAudioProcessor::getBypassParameter() const
{
    return bypassParameter;
}

//==============================================================================
void MiserereAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();
    const std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void MiserereAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MiserereAudioProcessor();
}
