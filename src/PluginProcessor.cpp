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

    int choiceIndex (const std::atomic<float>* rawIndex) noexcept
    {
        return static_cast<int> (rawIndex->load (std::memory_order_relaxed));
    }

    FetCrush::Ratio crushRatioFromIndex (int index) noexcept
    {
        switch (index)
        {
            case 0: return FetCrush::Ratio::r4;
            case 1: return FetCrush::Ratio::r8;
            case 2: return FetCrush::Ratio::r12;
            case 3: return FetCrush::Ratio::r20;
            default: return FetCrush::Ratio::rAll;
        }
    }

    FetCrush::Style crushStyleFromIndex (int index) noexcept
    {
        return index == 0 ? FetCrush::Style::allButtons : FetCrush::Style::gentle;
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
    linkFlag = apvts.getRawParameterValue (ParamIDs::link);
    parallelTrimDb = apvts.getRawParameterValue (ParamIDs::parallelTrim);

    directDeessPreEnabled = apvts.getRawParameterValue (ParamIDs::directDeessPreEnabled);
    directDeessPreFreq = apvts.getRawParameterValue (ParamIDs::directDeessPreFreq);
    directDeessPreThreshold = apvts.getRawParameterValue (ParamIDs::directDeessPreThreshold);
    directFetEnabled = apvts.getRawParameterValue (ParamIDs::directFetEnabled);
    directFetThreshold = apvts.getRawParameterValue (ParamIDs::directFetThreshold);
    directFetAttack = apvts.getRawParameterValue (ParamIDs::directFetAttack);
    directFetRelease = apvts.getRawParameterValue (ParamIDs::directFetRelease);
    directFetMakeup = apvts.getRawParameterValue (ParamIDs::directFetMakeup);
    directEqHpfEnabled = apvts.getRawParameterValue (ParamIDs::directEqHpfEnabled);
    directEqHpfFreq = apvts.getRawParameterValue (ParamIDs::directEqHpfFreq);
    directEqLowFreq = apvts.getRawParameterValue (ParamIDs::directEqLowFreq);
    directEqLowGain = apvts.getRawParameterValue (ParamIDs::directEqLowGain);
    directEqMidFreq = apvts.getRawParameterValue (ParamIDs::directEqMidFreq);
    directEqMidGain = apvts.getRawParameterValue (ParamIDs::directEqMidGain);
    directEqHighGain = apvts.getRawParameterValue (ParamIDs::directEqHighGain);
    directEqDrive = apvts.getRawParameterValue (ParamIDs::directEqDrive);
    directSatDrive = apvts.getRawParameterValue (ParamIDs::directSatDrive);
    directDeessPostEnabled = apvts.getRawParameterValue (ParamIDs::directDeessPostEnabled);
    directDeessPostFreq = apvts.getRawParameterValue (ParamIDs::directDeessPostFreq);
    directDeessPostThreshold = apvts.getRawParameterValue (ParamIDs::directDeessPostThreshold);

    crushInput = apvts.getRawParameterValue (ParamIDs::crushInput);
    crushRatio = apvts.getRawParameterValue (ParamIDs::crushRatio);
    crushStyle = apvts.getRawParameterValue (ParamIDs::crushStyle);
    crushAttack = apvts.getRawParameterValue (ParamIDs::crushAttack);
    crushRelease = apvts.getRawParameterValue (ParamIDs::crushRelease);
    crushOutput = apvts.getRawParameterValue (ParamIDs::crushOutput);

    sandPreLfFreq = apvts.getRawParameterValue (ParamIDs::sandPreLfFreq);
    sandPreLfBoost = apvts.getRawParameterValue (ParamIDs::sandPreLfBoost);
    sandPreLfCut = apvts.getRawParameterValue (ParamIDs::sandPreLfCut);
    sandPreHfBellFreq = apvts.getRawParameterValue (ParamIDs::sandPreHfBellFreq);
    sandPreHfBellBoost = apvts.getRawParameterValue (ParamIDs::sandPreHfBellBoost);
    sandPreHfBellBandwidth = apvts.getRawParameterValue (ParamIDs::sandPreHfBellBandwidth);
    sandPreHfShelfFreq = apvts.getRawParameterValue (ParamIDs::sandPreHfShelfFreq);
    sandPreHfShelfAtten = apvts.getRawParameterValue (ParamIDs::sandPreHfShelfAtten);
    sandPeakRed = apvts.getRawParameterValue (ParamIDs::sandPeakRed);
    sandLimit = apvts.getRawParameterValue (ParamIDs::sandLimit);
    sandEmphasis = apvts.getRawParameterValue (ParamIDs::sandEmphasis);
    sandResidual = apvts.getRawParameterValue (ParamIDs::sandResidual);
    sandPostLfFreq = apvts.getRawParameterValue (ParamIDs::sandPostLfFreq);
    sandPostLfBoost = apvts.getRawParameterValue (ParamIDs::sandPostLfBoost);
    sandPostLfCut = apvts.getRawParameterValue (ParamIDs::sandPostLfCut);
    sandPostHfBellFreq = apvts.getRawParameterValue (ParamIDs::sandPostHfBellFreq);
    sandPostHfBellBoost = apvts.getRawParameterValue (ParamIDs::sandPostHfBellBoost);
    sandPostHfBellBandwidth = apvts.getRawParameterValue (ParamIDs::sandPostHfBellBandwidth);
    sandPostHfShelfFreq = apvts.getRawParameterValue (ParamIDs::sandPostHfShelfFreq);
    sandPostHfShelfAtten = apvts.getRawParameterValue (ParamIDs::sandPostHfShelfAtten);

    spreadDetune = apvts.getRawParameterValue (ParamIDs::spreadDetune);
    spreadTime = apvts.getRawParameterValue (ParamIDs::spreadTime);
    spreadWidth = apvts.getRawParameterValue (ParamIDs::spreadWidth);

    slapTime = apvts.getRawParameterValue (ParamIDs::slapTime);
    slapStereo = apvts.getRawParameterValue (ParamIDs::slapStereo);
    slapTone = apvts.getRawParameterValue (ParamIDs::slapTone);

    static constexpr const char* levelIds[] = { ParamIDs::crushLevel, ParamIDs::sandLevel, ParamIDs::spreadLevel, ParamIDs::slapLevel };
    static constexpr const char* muteIds[] = { ParamIDs::crushMute, ParamIDs::sandMute, ParamIDs::spreadMute, ParamIDs::slapMute };
    static constexpr const char* auditionIds[] = { ParamIDs::crushAudition, ParamIDs::sandAudition, ParamIDs::spreadAudition, ParamIDs::slapAudition };

    for (int bus = 0; bus < MiserereEngine::numBusses; ++bus)
    {
        busLevelDb[static_cast<size_t> (bus)] = apvts.getRawParameterValue (levelIds[bus]);
        busMuteFlag[static_cast<size_t> (bus)] = apvts.getRawParameterValue (muteIds[bus]);
        busAuditionFlag[static_cast<size_t> (bus)] = apvts.getRawParameterValue (auditionIds[bus]);

        // Audition exclusivity listener (see parameterChanged()).
        apvts.addParameterListener (auditionIds[bus], this);
    }

    jassert (inTrimDb != nullptr && outTrimDb != nullptr && bypassFlag != nullptr && bypassParameter != nullptr);
    jassert (linkFlag != nullptr && parallelTrimDb != nullptr);
    jassert (directDeessPreEnabled != nullptr && directDeessPreFreq != nullptr && directDeessPreThreshold != nullptr);
    jassert (directFetEnabled != nullptr && directFetThreshold != nullptr && directFetAttack != nullptr);
    jassert (directFetRelease != nullptr && directFetMakeup != nullptr);
    jassert (directEqHpfEnabled != nullptr && directEqHpfFreq != nullptr && directEqLowFreq != nullptr);
    jassert (directEqLowGain != nullptr && directEqMidFreq != nullptr && directEqMidGain != nullptr);
    jassert (directEqHighGain != nullptr && directEqDrive != nullptr && directSatDrive != nullptr);
    jassert (directDeessPostEnabled != nullptr && directDeessPostFreq != nullptr && directDeessPostThreshold != nullptr);
    jassert (crushInput != nullptr && crushRatio != nullptr && crushStyle != nullptr);
    jassert (crushAttack != nullptr && crushRelease != nullptr && crushOutput != nullptr);
    jassert (sandPreLfFreq != nullptr && sandPreLfBoost != nullptr && sandPreLfCut != nullptr);
    jassert (sandPreHfBellFreq != nullptr && sandPreHfBellBoost != nullptr && sandPreHfBellBandwidth != nullptr);
    jassert (sandPreHfShelfFreq != nullptr && sandPreHfShelfAtten != nullptr);
    jassert (sandPeakRed != nullptr && sandLimit != nullptr && sandEmphasis != nullptr && sandResidual != nullptr);
    jassert (sandPostLfFreq != nullptr && sandPostLfBoost != nullptr && sandPostLfCut != nullptr);
    jassert (sandPostHfBellFreq != nullptr && sandPostHfBellBoost != nullptr && sandPostHfBellBandwidth != nullptr);
    jassert (sandPostHfShelfFreq != nullptr && sandPostHfShelfAtten != nullptr);
    jassert (spreadDetune != nullptr && spreadTime != nullptr && spreadWidth != nullptr);
    jassert (slapTime != nullptr && slapStereo != nullptr && slapTone != nullptr);

    for (int bus = 0; bus < MiserereEngine::numBusses; ++bus)
    {
        jassert (busLevelDb[static_cast<size_t> (bus)] != nullptr);
        jassert (busMuteFlag[static_cast<size_t> (bus)] != nullptr);
        jassert (busAuditionFlag[static_cast<size_t> (bus)] != nullptr);
    }
}

MiserereAudioProcessor::~MiserereAudioProcessor()
{
    static constexpr const char* auditionIds[] = { ParamIDs::crushAudition, ParamIDs::sandAudition, ParamIDs::spreadAudition, ParamIDs::slapAudition };

    for (const auto* auditionId : auditionIds)
        apvts.removeParameterListener (auditionId, this);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MiserereAudioProcessor::createParameterLayout()
{
    return msrr::createParameterLayout();
}

//==============================================================================
void MiserereAudioProcessor::parameterChanged (const juce::String& parameterId, float newValue)
{
    // Audition exclusivity: when an audition engages, release every other
    // bus's audition. The guard stops the setValueNotifyingHost() cascade
    // from re-entering this handler for the auditions being cleared.
    //
    // Known limitation (documented in docs/architecture.md): if a host
    // automates two auditions on in the same gesture, this listener
    // resolves them in callback order - fine for the intended "click
    // Audition on a different bus" workflow, and the engine stays
    // well-defined for any combination regardless.
    if (newValue < 0.5f || auditionExclusivityGuard.exchange (true))
        return;

    static constexpr const char* auditionIds[] = { ParamIDs::crushAudition, ParamIDs::sandAudition, ParamIDs::spreadAudition, ParamIDs::slapAudition };

    for (const auto* auditionId : auditionIds)
    {
        if (parameterId == auditionId)
            continue;

        if (auto* param = apvts.getParameter (auditionId))
            if (param->getValue() >= 0.5f)
                param->setValueNotifyingHost (0.0f);
    }

    auditionExclusivityGuard.store (false);
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
    // Bus (4) SLAP can keep ringing after the input stops: the worst case is
    // the maximum 160 ms delay plus its repeat's own decay - a conservative
    // 1 second covers it comfortably.
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
    engine.setLinked (loadBool (linkFlag));
    engine.setParallelTrimDb (load (parallelTrimDb));

    engine.setDeessPreEnabled (loadBool (directDeessPreEnabled));
    engine.setDeessPreFreqHz (load (directDeessPreFreq));
    engine.setDeessPreThresholdDb (load (directDeessPreThreshold));

    engine.setDirectFetEnabled (loadBool (directFetEnabled));
    engine.setDirectFetThresholdDb (load (directFetThreshold));
    engine.setDirectFetAttackMs (load (directFetAttack));
    engine.setDirectFetReleaseMs (load (directFetRelease));
    engine.setDirectFetMakeupDb (load (directFetMakeup));

    engine.setEqHpfEnabled (loadBool (directEqHpfEnabled));
    engine.setEqHpfFreqHz (choiceToValue (directEqHpfFreq, msrr::eqHpfFreqHz));
    engine.setEqLowFreqHz (choiceToValue (directEqLowFreq, msrr::eqLowFreqHz));
    engine.setEqLowGainDb (load (directEqLowGain));
    engine.setEqMidFreqHz (choiceToValue (directEqMidFreq, msrr::eqMidFreqHz));
    engine.setEqMidGainDb (load (directEqMidGain));
    engine.setEqHighGainDb (load (directEqHighGain));
    engine.setEqDriveDb (load (directEqDrive));

    engine.setSatDriveDb (load (directSatDrive));

    engine.setDeessPostEnabled (loadBool (directDeessPostEnabled));
    engine.setDeessPostFreqHz (load (directDeessPostFreq));
    engine.setDeessPostThresholdDb (load (directDeessPostThreshold));

    engine.setCrushInputDriveDb (load (crushInput));
    engine.setCrushRatio (crushRatioFromIndex (choiceIndex (crushRatio)));
    engine.setCrushStyle (crushStyleFromIndex (choiceIndex (crushStyle)));
    engine.setCrushAttackStep (load (crushAttack));
    engine.setCrushReleaseStep (load (crushRelease));
    engine.setCrushOutputTrimDb (load (crushOutput));

    engine.setSandPreLfFreqHz (choiceToValue (sandPreLfFreq, msrr::sandLfFreqHz));
    engine.setSandPreLfBoostDial (load (sandPreLfBoost));
    engine.setSandPreLfCutDial (load (sandPreLfCut));
    engine.setSandPreHfBellFreqHz (choiceToValue (sandPreHfBellFreq, msrr::sandHfBellFreqHz));
    engine.setSandPreHfBellBoostDial (load (sandPreHfBellBoost));
    engine.setSandPreHfBellBandwidthDial (load (sandPreHfBellBandwidth));
    engine.setSandPreHfShelfFreqHz (choiceToValue (sandPreHfShelfFreq, msrr::sandHfShelfFreqHz));
    engine.setSandPreHfShelfAttenDial (load (sandPreHfShelfAtten));

    engine.setSandPeakReductionProportion (load (sandPeakRed) * 0.01f);
    engine.setSandLimitEnabled (loadBool (sandLimit));
    engine.setSandEmphasisProportion (load (sandEmphasis) * 0.01f);
    engine.setSandResidualEnabled (loadBool (sandResidual));

    engine.setSandPostLfFreqHz (choiceToValue (sandPostLfFreq, msrr::sandLfFreqHz));
    engine.setSandPostLfBoostDial (load (sandPostLfBoost));
    engine.setSandPostLfCutDial (load (sandPostLfCut));
    engine.setSandPostHfBellFreqHz (choiceToValue (sandPostHfBellFreq, msrr::sandHfBellFreqHz));
    engine.setSandPostHfBellBoostDial (load (sandPostHfBellBoost));
    engine.setSandPostHfBellBandwidthDial (load (sandPostHfBellBandwidth));
    engine.setSandPostHfShelfFreqHz (choiceToValue (sandPostHfShelfFreq, msrr::sandHfShelfFreqHz));
    engine.setSandPostHfShelfAttenDial (load (sandPostHfShelfAtten));

    engine.setSpreadDetuneCents (load (spreadDetune));
    engine.setSpreadTimeScale (load (spreadTime));
    engine.setSpreadWidth (load (spreadWidth) * 0.01f);

    engine.setSlapDelayMs (load (slapTime));
    engine.setSlapStereoEnabled (loadBool (slapStereo));
    engine.setSlapToneProportion (load (slapTone) * 0.01f);

    for (int bus = 0; bus < MiserereEngine::numBusses; ++bus)
    {
        engine.setBusLevelDb (bus, load (busLevelDb[static_cast<size_t> (bus)]));
        engine.setBusMute (bus, loadBool (busMuteFlag[static_cast<size_t> (bus)]));
        engine.setBusAudition (bus, loadBool (busAuditionFlag[static_cast<size_t> (bus)]));
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

    // Nothing in any bus adds reported host latency: busses (1)/(2) are
    // minimum-phase/causal by the suite's phase discipline, and busses
    // (3)/(4)'s delays are the effects themselves, not compensation delays
    // (see docs/adr/0003).
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
    // preparedBlockSize so the engine's prepare()-sized buffers are never
    // indexed out of bounds - and no audio is dropped.
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
    // Tolerant import: a v1 (or any unrecognised) session's XML simply has
    // its unknown parameter IDs ignored by ValueTree::fromXml/APVTS's
    // internal restore - no explicit migration is attempted (pre-1.0
    // breaking parameter changes are acceptable per docs/design-brief.md's
    // "Versioning" section). The only requirement is "no crash" - see
    // tests/StateTests.cpp's v1-import test.
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
