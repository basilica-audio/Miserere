#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIds.h"
#include "params/ParameterLayout.h"

#include <BinaryData.h>

#include <cmath>

namespace
{
    // Issue #41: fixed, sample-rate-independent crossfade time for the
    // wet/dry bypass blend (see bypassWetMix's docs in PluginProcessor.h).
    // 20 ms is long enough that the blend itself never reads as a click and
    // short enough that engaging/disengaging bypass still feels
    // instantaneous to a player - the same figure the sibling plugins use
    // for their own transition ramps.
    constexpr double bypassCrossfadeDurationSeconds = 0.02;

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
        switch (index)
        {
            case 1: return FetCrush::Style::gentle;
            case 2: return FetCrush::Style::vintage;
            default: return FetCrush::Style::allButtons;
        }
    }

    FetCompressor::Character directFetCharacterFromIndex (int index) noexcept
    {
        switch (index)
        {
            case 1: return FetCompressor::Character::vca;
            case 2: return FetCompressor::Character::tubeMu;
            default: return FetCompressor::Character::fet;
        }
    }

    OptoLeveler::Colour sandColourFromIndex (int index) noexcept
    {
        switch (index)
        {
            case 1: return OptoLeveler::Colour::quick;
            case 2: return OptoLeveler::Colour::deep;
            default: return OptoLeveler::Colour::classic;
        }
    }

    // The small, Miserere-specific config surface PresetManager needs (see
    // src/presets/PresetManager.h's class docs) - everything else about the
    // preset system is fully generic and portable to sibling plugins (see
    // nave's docs/preset-system-notes.md, the M2 pilot's replication recipe).
    basilica::presets::PresetManagerConfig makePresetManagerConfig()
    {
        // JucePlugin_CFBundleIdentifier expands to a raw (unquoted) token
        // sequence, not a string literal - JUCE_STRINGIFY() is the
        // documented way to turn it into one. This is always
        // "com.yvesvogl.miserere" here (BUNDLE_ID in CMakeLists.txt),
        // matching the "plugin" field baked into every
        // presets/factory/*.json file.
        basilica::presets::PresetManagerConfig config;
        config.pluginId = JUCE_STRINGIFY (JucePlugin_CFBundleIdentifier);
        config.pluginName = JucePlugin_Name;
        config.manufacturerName = "Yves Vogl";
        config.pluginVersion = JucePlugin_VersionString;
        // userPresetsDirectoryOverrideForTests intentionally left
        // default-constructed (empty) - production instances always use the
        // real platform-standard preset location (see PresetManager.h):
        // ~/Library/Audio/Presets/Yves Vogl/Miserere/ on macOS.
        return config;
    }

    // BinaryData symbol names are derived from the presets/factory/*.json
    // file names passed to juce_add_binary_data() in CMakeLists.txt (dots
    // become underscores) - this list must stay in sync with that SOURCES
    // list. Order here only affects factory-preset iteration order before
    // getAllPresets() re-sorts alphabetically, so it isn't otherwise
    // significant.
    std::vector<basilica::presets::FactoryPresetAsset> makeFactoryPresetAssets()
    {
        return {
            { BinaryData::default_json, BinaryData::default_jsonSize },
            { BinaryData::classicParallelBlend_json, BinaryData::classicParallelBlend_jsonSize },
            { BinaryData::crushForward_json, BinaryData::crushForward_jsonSize },
            { BinaryData::silkSandwich_json, BinaryData::silkSandwich_jsonSize },
            { BinaryData::wideAndWet_json, BinaryData::wideAndWet_jsonSize },
            { BinaryData::directChannelOnly_json, BinaryData::directChannelOnly_jsonSize },
            { BinaryData::gentleBus_json, BinaryData::gentleBus_jsonSize },
            { BinaryData::roughMixGlue_json, BinaryData::roughMixGlue_jsonSize },
            { BinaryData::whisperThicken_json, BinaryData::whisperThicken_jsonSize },
            { BinaryData::aggressiveRockVocal_json, BinaryData::aggressiveRockVocal_jsonSize },
            { BinaryData::tapeSlap75_json, BinaryData::tapeSlap75_jsonSize },
            { BinaryData::wornSlap_json, BinaryData::wornSlap_jsonSize },
            { BinaryData::bvMode_json, BinaryData::bvMode_jsonSize },
        };
    }
}

//==============================================================================
MiserereAudioProcessor::MiserereAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          // External sidechain (issue #23), DISABLED by
                          // default: the default layout stays byte-identical
                          // to the pre-v0.7.0 one, so existing sessions and
                          // auval/pluginval's default-layout runs see no
                          // change at all. Hosts that want the key enable
                          // the bus explicitly.
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout()),
      presetManager (apvts, makePresetManagerConfig(), makeFactoryPresetAssets())
{
    inTrimDb = apvts.getRawParameterValue (ParamIDs::inTrim);
    outTrimDb = apvts.getRawParameterValue (ParamIDs::outTrim);
    bypassFlag = apvts.getRawParameterValue (ParamIDs::bypass);
    bypassParameter = apvts.getParameter (ParamIDs::bypass);
    linkFlag = apvts.getRawParameterValue (ParamIDs::link);
    parallelTrimDb = apvts.getRawParameterValue (ParamIDs::parallelTrim);
    limiterEnabled = apvts.getRawParameterValue (ParamIDs::limiterEnabled);
    limiterCeilingDb = apvts.getRawParameterValue (ParamIDs::limiterCeiling);
    limiterReleaseMs = apvts.getRawParameterValue (ParamIDs::limiterRelease);

    directDeessPreEnabled = apvts.getRawParameterValue (ParamIDs::directDeessPreEnabled);
    directDeessPreFreq = apvts.getRawParameterValue (ParamIDs::directDeessPreFreq);
    directDeessPreThreshold = apvts.getRawParameterValue (ParamIDs::directDeessPreThreshold);
    directFetEnabled = apvts.getRawParameterValue (ParamIDs::directFetEnabled);
    directFetKeyExt = apvts.getRawParameterValue (ParamIDs::directFetKeyExt);
    directFetColour = apvts.getRawParameterValue (ParamIDs::directFetColour);
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
    crushKeyExt = apvts.getRawParameterValue (ParamIDs::crushKeyExt);

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
    sandColour = apvts.getRawParameterValue (ParamIDs::sandColour);
    sandEmphasis = apvts.getRawParameterValue (ParamIDs::sandEmphasis);
    sandResidual = apvts.getRawParameterValue (ParamIDs::sandResidual);
    sandKeyExt = apvts.getRawParameterValue (ParamIDs::sandKeyExt);
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
    slapWobble = apvts.getRawParameterValue (ParamIDs::slapWobble);
    slapAge = apvts.getRawParameterValue (ParamIDs::slapAge);

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
    jassert (limiterEnabled != nullptr && limiterCeilingDb != nullptr && limiterReleaseMs != nullptr);
    jassert (directFetKeyExt != nullptr && crushKeyExt != nullptr && sandKeyExt != nullptr);
    jassert (directDeessPreEnabled != nullptr && directDeessPreFreq != nullptr && directDeessPreThreshold != nullptr);
    jassert (directFetEnabled != nullptr && directFetColour != nullptr && directFetThreshold != nullptr && directFetAttack != nullptr);
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
    jassert (sandPeakRed != nullptr && sandLimit != nullptr && sandColour != nullptr && sandEmphasis != nullptr && sandResidual != nullptr);
    jassert (sandPostLfFreq != nullptr && sandPostLfBoost != nullptr && sandPostLfCut != nullptr);
    jassert (sandPostHfBellFreq != nullptr && sandPostHfBellBoost != nullptr && sandPostHfBellBandwidth != nullptr);
    jassert (sandPostHfShelfFreq != nullptr && sandPostHfShelfAtten != nullptr);
    jassert (spreadDetune != nullptr && spreadTime != nullptr && spreadWidth != nullptr);
    jassert (slapTime != nullptr && slapStereo != nullptr && slapTone != nullptr);
    jassert (slapWobble != nullptr && slapAge != nullptr);

    for (int bus = 0; bus < MiserereEngine::numBusses; ++bus)
    {
        jassert (busLevelDb[static_cast<size_t> (bus)] != nullptr);
        jassert (busMuteFlag[static_cast<size_t> (bus)] != nullptr);
        jassert (busAuditionFlag[static_cast<size_t> (bus)] != nullptr);
    }

    // M2 default resolution: user "Default" preset > factory "Default"
    // preset > the ParameterLayout defaults apvts was just constructed
    // with above (see PresetManager::applyStartupDefault()'s docs). The
    // factory "Default" preset (presets/factory/default.json) mirrors the
    // ParameterLayout defaults exactly, so this is a no-op on a machine
    // with no user presets yet.
    presetManager.applyStartupDefault();
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

    engine.setLimiterEnabled (loadBool (limiterEnabled));
    engine.setLimiterCeilingDb (load (limiterCeilingDb));
    engine.setLimiterReleaseMs (load (limiterReleaseMs));

    engine.setDeessPreEnabled (loadBool (directDeessPreEnabled));
    engine.setDeessPreFreqHz (load (directDeessPreFreq));
    engine.setDeessPreThresholdDb (load (directDeessPreThreshold));

    engine.setDirectFetEnabled (loadBool (directFetEnabled));
    engine.setDirectFetKeyExternal (loadBool (directFetKeyExt));
    engine.setCrushKeyExternal (loadBool (crushKeyExt));
    engine.setSandKeyExternal (loadBool (sandKeyExt));
    engine.setDirectFetCharacter (directFetCharacterFromIndex (choiceIndex (directFetColour)));
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
    engine.setSandColour (sandColourFromIndex (choiceIndex (sandColour)));
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
    engine.setSlapWobbleProportion (load (slapWobble) * 0.01f);
    engine.setSlapAgeProportion (load (slapAge) * 0.01f);

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

    // Issue #41: the bypass dry path. Everything it needs is allocated here,
    // never in processBlock().
    bypassDryDelay.setMaximumDelayInSamples (maxLatencyCompensationSamples);
    bypassDryDelay.prepare (spec);
    bypassDryDelay.setDelay (static_cast<float> (juce::jlimit (0, maxLatencyCompensationSamples,
                                                               getLatencySamples())));
    bypassDryDelay.reset();

    // Scratch for the untouched input, sized to the promised block size (at
    // least one sample, so a degenerate prepareToPlay(rate, 0) still leaves
    // a valid buffer behind) so processBlock() never resizes on the audio
    // thread.
    bypassDryBuffer.setSize (static_cast<int> (spec.numChannels), juce::jmax (1, samplesPerBlock));
    bypassDryBuffer.clear();

    // Primed to the CURRENT bypass state rather than always starting from
    // "wet", so a session saved while bypassed - or a host that reads the
    // bypass parameter before the first processBlock() - does not open with
    // an audible ramp in from the wrong side.
    bypassWetMix.reset (sampleRate, bypassCrossfadeDurationSeconds);
    bypassWetMix.setCurrentAndTargetValue (bypassFlag->load (std::memory_order_relaxed) >= 0.5f ? 0.0f : 1.0f);
}

void MiserereAudioProcessor::releaseResources()
{
}

void MiserereAudioProcessor::reset()
{
    engine.reset();

    // Issue #41: flush the dry delay line's history (a transport rewind must
    // not replay pre-rewind audio into the bypass blend) and snap the
    // crossfade to wherever it was heading, cancelling any ramp in flight -
    // the same "stop cleanly, leave no stale motion behind" contract
    // MiserereEngine::reset() gives every module above.
    bypassDryDelay.reset();
    bypassDryBuffer.clear();
    bypassWetMix.setCurrentAndTargetValue (bypassWetMix.getTargetValue());
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

    // External sidechain (issue #23): entirely optional. Disabled (the
    // default), mono or stereo are all admissible, in any combination with
    // a mono or stereo main path - a mono key into a stereo main bus is a
    // normal host routing and the engine reuses the key's last channel for
    // the remaining audio channels. Anything wider is refused rather than
    // silently half-used.
    if (layouts.inputBuses.size() > 1)
    {
        const auto sidechain = layouts.getChannelSet (true, 1);

        if (! sidechain.isDisabled() && sidechain != mono && sidechain != stereo)
            return false;
    }

    return true;
}

void MiserereAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Since v0.7.0 (issue #23) `buffer` may carry a sidechain bus after the
    // main channels, so the audio path must be taken as the MAIN bus's own
    // view rather than as the whole buffer - processing the raw buffer would
    // run the engine over the key channels as if they were audio. With the
    // sidechain disabled (the default) this view is the whole buffer, and
    // behaviour is byte-identical to before.
    auto mainBuffer = getBusBuffer (buffer, false, 0);

    const auto mainNumInputChannels = getMainBusNumInputChannels();
    const auto mainNumOutputChannels = getMainBusNumOutputChannels();

    // The main buses are constrained to in == out (mono or stereo), so this
    // is normally a no-op, but it's cheap insurance against stray channels.
    for (auto channel = mainNumInputChannels; channel < mainNumOutputChannels; ++channel)
        mainBuffer.clear (channel, 0, mainBuffer.getNumSamples());

    // Issue #41: bypass is a crossfade (bypassWetMix, advanced one step per
    // sample in applyBypassCrossfade()) between the engine below - which
    // keeps running unconditionally, bypassed or not, so none of its state
    // (filter memory, envelope followers, both delay-line busses, the
    // limiter's release) ever freezes - and a delayed copy of the untouched
    // input. There is deliberately no early return here any more: freezing
    // the engine while bypassed is exactly what made coming back OUT of
    // bypass click, stale state resuming into a live signal, and the switch
    // itself stepped by whatever the wet and dry signals happened to differ
    // by at that instant.
    bypassWetMix.setTargetValue (bypassFlag->load (std::memory_order_relaxed) >= 0.5f ? 0.0f : 1.0f);

    // Parameters are read once per host block (not per chunk): the engine's
    // smoothers spread any change across samples regardless.
    updateEngineParameters();

    // The external key for this block, if the host has enabled the bus. Read
    // only - never written - so it can reference the host's own memory with
    // no copy. An absent or disabled bus leaves the block empty and every
    // keyed detector falls back to internal detection.
    juce::dsp::AudioBlock<const float> keyBlock;

    if (const auto* sidechainBus = getBus (true, 1))
    {
        if (sidechainBus->isEnabled())
        {
            const auto keyBuffer = getBusBuffer (buffer, true, 1);

            if (keyBuffer.getNumChannels() > 0)
                keyBlock = juce::dsp::AudioBlock<const float> (keyBuffer.getArrayOfReadPointers(),
                                                                static_cast<size_t> (keyBuffer.getNumChannels()),
                                                                static_cast<size_t> (keyBuffer.getNumSamples()));
        }
    }

    juce::dsp::AudioBlock<float> fullBlock (mainBuffer);

    // Oversized-block guard (a REAL clamp, not a jassert): hosts are
    // expected never to exceed the block size promised to prepareToPlay(),
    // but if one does, the buffer is processed in chunks of at most
    // preparedBlockSize so the engine's prepare()-sized buffers are never
    // indexed out of bounds - and no audio is dropped.
    //
    // Issue #41: the chunk is additionally capped to bypassDryBuffer's own
    // prepared capacity, since the dry snapshot for a chunk has to fit in it
    // and it is never resized on this thread.
    const auto dryScratchCapacity = static_cast<size_t> (juce::jmax (1, bypassDryBuffer.getNumSamples()));
    const auto chunkLimit = preparedBlockSize > 0
                                 ? juce::jmin (static_cast<size_t> (preparedBlockSize), dryScratchCapacity)
                                 : juce::jmin (juce::jmax (static_cast<size_t> (1), fullBlock.getNumSamples()),
                                               dryScratchCapacity);

    for (size_t offset = 0; offset < fullBlock.getNumSamples(); offset += chunkLimit)
    {
        const auto chunkLength = juce::jmin (chunkLimit, fullBlock.getNumSamples() - offset);
        auto chunk = fullBlock.getSubBlock (offset, chunkLength);
        const auto chunkChannels = juce::jmin (chunk.getNumChannels(),
                                               static_cast<size_t> (bypassDryBuffer.getNumChannels()));

        // Issue #41: snapshot the untouched input before engine.process()
        // mutates `chunk` (i.e. this region of the host's own buffer) in
        // place with the wet signal.
        //
        // The snapshot is sanitised on the way in, exactly the way
        // MiserereEngine's own final sum sanitises (docs/architecture.md's
        // NaN/Inf policy: the output-side guarantee is unconditional). The
        // dry path goes around the engine by definition, so it would
        // otherwise be the one route by which a non-finite input sample
        // reaches the output - and worse, a non-finite dry sample multiplied
        // by a zero blend gain is NaN, so an unsanitised dry copy would
        // poison the output even when bypass is fully disengaged. Cleaning
        // here rather than after the blend also keeps bypassDryDelay's
        // history finite, so a single bad input sample cannot echo back out
        // of the delay line later.
        auto dryChunk = juce::dsp::AudioBlock<float> (bypassDryBuffer)
                             .getSubBlock (0, chunkLength)
                             .getSubsetChannelBlock (0, chunkChannels);

        for (size_t channel = 0; channel < chunkChannels; ++channel)
        {
            const auto* source = chunk.getChannelPointer (channel);
            auto* destination = dryChunk.getChannelPointer (channel);

            for (size_t sample = 0; sample < chunkLength; ++sample)
                destination[sample] = std::isfinite (source[sample]) ? source[sample] : 0.0f;
        }

        // The key is chunked in lockstep with the audio so a keyed detector
        // sees exactly the samples that belong to the chunk it is judging.
        if (keyBlock.getNumChannels() > 0 && offset + chunkLength <= keyBlock.getNumSamples())
        {
            const auto keyChunk = keyBlock.getSubBlock (offset, chunkLength);
            engine.process (chunk, &keyChunk);
        }
        else
        {
            engine.process (chunk);
        }

        // Always run, never only while bypassed: the dry path has to stay
        // time-aligned with the engine's latency at every instant, or the
        // delay line's history would be stale the moment bypass toggles (see
        // bypassDryDelay's docs in PluginProcessor.h).
        bypassDryDelay.process (juce::dsp::ProcessContextReplacing<float> (dryChunk));

        auto wetChunk = chunk.getSubsetChannelBlock (0, chunkChannels);
        applyBypassCrossfade (juce::dsp::AudioBlock<const float> (dryChunk), wetChunk);
    }
}

void MiserereAudioProcessor::applyBypassCrossfade (const juce::dsp::AudioBlock<const float>& dry,
                                                   juce::dsp::AudioBlock<float>& wet) noexcept
{
    // Linear, not equal-power: the wet and dry signals here are two
    // renderings of the same programme material and are strongly correlated
    // (with a bit-transparent default direct path they are frequently
    // near-identical), so an equal-power law would produce a measurable
    // mid-fade level bump rather than a transparent blend.
    const auto numChannels = wet.getNumChannels();
    const auto numSamples = wet.getNumSamples();

    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        const auto wetGain = bypassWetMix.getNextValue();
        const auto dryGain = 1.0f - wetGain;

        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            auto* wetData = wet.getChannelPointer (channel);
            wetData[sample] = wetGain * wetData[sample] + dryGain * dry.getChannelPointer (channel)[sample];
        }
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
    auto state = apvts.copyState();

    // State schema stamp: version 4 = the v0.7.0 layout that introduced the
    // output limiter (limiter_enabled/limiter_ceiling/limiter_release,
    // issue #24); version 3 was the v0.6.0 layout that introduced
    // direct_fet_colour/sand_colour and appended crush_style's third choice
    // (issue #20); version 2 introduced slap_wobble/slap_age (v0.5.0). A
    // missing property on load means version 1 (v0.2.0-v0.4.0 layouts) -
    // see setStateInformation(), whose absent-parameter reset is generic
    // and needs no per-version branching (a v0.6.0 session simply carries
    // no limiter values and therefore loads with the limiter OFF).
    state.setProperty ("stateVersion", 4, nullptr);

    const std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void MiserereAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Tolerant import: a v1 (or any unrecognised) session's XML simply has
    // its unknown parameter IDs ignored by ValueTree::fromXml/APVTS's
    // internal restore - no explicit migration is attempted (pre-1.0
    // breaking parameter changes are acceptable per docs/design-brief.md's
    // "Versioning" section) - see tests/StateTests.cpp's v1-import test.
    const std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState == nullptr || ! xmlState->hasTagName (apvts.state.getType()))
        return;

    const auto incoming = juce::ValueTree::fromXml (*xmlState);

    // Explicit reset of ABSENT parameters (binding, v0.5.0 brief section
    // 4): JUCE 8.0.14's replaceState() has NO default-fill - for any
    // parameter missing a value-bearing child in the incoming tree,
    // updateParameterConnectionsToChildTrees() creates the child WITHOUT a
    // value and flushParameterValuesToValueTree() then writes the
    // parameter's CURRENT value into it
    // (juce_AudioProcessorValueTreeState.cpp:396-441). Loading a v0.4.0
    // session into a live instance whose slap_wobble/slap_age are non-zero
    // would therefore silently keep the stale values. Reset every
    // parameter that the incoming tree does not carry back to its default
    // BEFORE replaceState() - the same reset-then-apply pattern
    // PresetManager::applyParsedPreset() already uses.
    {
        const auto hasValueForId = [&incoming] (const juce::String& paramId)
        {
            for (const auto& child : incoming)
                if (child.hasType ("PARAM")
                    && child.getProperty ("id").toString() == paramId
                    && child.hasProperty ("value"))
                    return true;

            return false;
        };

        for (auto* parameter : getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                if (! hasValueForId (ranged->paramID))
                    ranged->setValueNotifyingHost (ranged->getDefaultValue());
    }

    apvts.replaceState (incoming);
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MiserereAudioProcessor();
}
