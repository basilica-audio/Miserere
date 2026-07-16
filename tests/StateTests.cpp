#include "PluginProcessor.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

// Guarantee 9 (state part): full state round-trip for float, bool and
// choice parameters, plus v1-tolerant import (unknown old IDs ignored, no
// crash) - the design brief's "state migration = tolerant import".
TEST_CASE ("State round-trip preserves non-default values of every float parameter", "[state]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    static constexpr const char* floatIds[] = {
        ParamIDs::inTrim, ParamIDs::outTrim, ParamIDs::parallelTrim,
        ParamIDs::directDeessPreFreq, ParamIDs::directDeessPreThreshold,
        ParamIDs::directFetThreshold, ParamIDs::directFetAttack, ParamIDs::directFetRelease, ParamIDs::directFetMakeup,
        ParamIDs::directEqLowGain, ParamIDs::directEqMidGain, ParamIDs::directEqHighGain, ParamIDs::directEqDrive,
        ParamIDs::directSatDrive,
        ParamIDs::directDeessPostFreq, ParamIDs::directDeessPostThreshold,
        ParamIDs::crushInput, ParamIDs::crushAttack, ParamIDs::crushRelease, ParamIDs::crushOutput, ParamIDs::crushLevel,
        ParamIDs::sandPreLfBoost, ParamIDs::sandPreLfCut, ParamIDs::sandPreHfBellBoost, ParamIDs::sandPreHfBellBandwidth, ParamIDs::sandPreHfShelfAtten,
        ParamIDs::sandPeakRed, ParamIDs::sandEmphasis,
        ParamIDs::sandPostLfBoost, ParamIDs::sandPostLfCut, ParamIDs::sandPostHfBellBoost, ParamIDs::sandPostHfBellBandwidth, ParamIDs::sandPostHfShelfAtten,
        ParamIDs::sandLevel,
        ParamIDs::spreadDetune, ParamIDs::spreadTime, ParamIDs::spreadWidth, ParamIDs::spreadLevel,
        ParamIDs::slapTime, ParamIDs::slapTone, ParamIDs::slapLevel,
    };

    std::vector<juce::RangedAudioParameter*> params;
    std::vector<float> savedNormalisedValues;

    for (const auto* id : floatIds)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        params.push_back (param);
    }

    for (size_t i = 0; i < params.size(); ++i)
    {
        auto normalisedValue = 0.2f + 0.6f * (static_cast<float> (i % 5) / 4.0f);

        if (std::abs (normalisedValue - params[i]->getDefaultValue()) < 0.05f)
            normalisedValue = std::fmod (normalisedValue + 0.37f, 1.0f);

        params[i]->setValueNotifyingHost (normalisedValue);
        savedNormalisedValues.push_back (params[i]->getValue());
    }

    juce::MemoryBlock savedState;
    processor.getStateInformation (savedState);
    REQUIRE (savedState.getSize() > 0);

    for (auto* param : params)
        param->setValueNotifyingHost (param->getDefaultValue());

    for (size_t i = 0; i < params.size(); ++i)
        REQUIRE (params[i]->getValue() != Catch::Approx (savedNormalisedValues[i]));

    processor.setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));

    for (size_t i = 0; i < params.size(); ++i)
    {
        INFO ("parameter index = " << i << " (" << floatIds[i] << ")");
        CHECK (params[i]->getValue() == Catch::Approx (savedNormalisedValues[i]).margin (1e-6));
    }
}

TEST_CASE ("State round-trip preserves every bool parameter", "[state]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    // crushAudition is deliberately excluded: flipping every audition to
    // true in a loop would fight the audition-exclusivity listener (only
    // the last one would stick) - audition round-trip is covered separately
    // below with a single active audition, which is the only state the
    // exclusivity contract allows anyway.
    static constexpr const char* boolIds[] = {
        ParamIDs::bypass, ParamIDs::link,
        ParamIDs::directDeessPreEnabled, ParamIDs::directFetEnabled, ParamIDs::directEqHpfEnabled, ParamIDs::directDeessPostEnabled,
        ParamIDs::sandLimit, ParamIDs::sandResidual,
        ParamIDs::slapStereo,
        ParamIDs::crushMute, ParamIDs::sandMute, ParamIDs::spreadMute, ParamIDs::slapMute,
    };

    std::vector<juce::RangedAudioParameter*> params;
    std::vector<float> savedValues;

    for (const auto* id : boolIds)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        const auto flipped = param->getDefaultValue() >= 0.5f ? 0.0f : 1.0f;
        param->setValueNotifyingHost (flipped);
        params.push_back (param);
        savedValues.push_back (param->getValue());
    }

    juce::MemoryBlock savedState;
    processor.getStateInformation (savedState);
    REQUIRE (savedState.getSize() > 0);

    for (auto* param : params)
        param->setValueNotifyingHost (param->getDefaultValue());

    processor.setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));

    for (size_t i = 0; i < params.size(); ++i)
    {
        INFO ("parameter index = " << i << " (" << boolIds[i] << ")");
        CHECK (params[i]->getValue() == Catch::Approx (savedValues[i]));
    }
}

TEST_CASE ("State round-trip preserves an active audition", "[state][audition]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    auto* auditionSlap = processor.apvts.getParameter (ParamIDs::slapAudition);
    REQUIRE (auditionSlap != nullptr);
    auditionSlap->setValueNotifyingHost (1.0f);

    juce::MemoryBlock savedState;
    processor.getStateInformation (savedState);

    auditionSlap->setValueNotifyingHost (0.0f);
    REQUIRE (auditionSlap->getValue() < 0.5f);

    processor.setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));

    CHECK (auditionSlap->getValue() >= 0.5f);
}

TEST_CASE ("State round-trip preserves every choice parameter", "[state][choice]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    static constexpr const char* choiceIds[] = {
        ParamIDs::crushRatio,       // default index 4 ("ALL") -> set 0 ("4:1")
        ParamIDs::crushStyle,       // default index 0 -> set 1 ("Gentle")
        ParamIDs::directEqHpfFreq,  // default index 1 -> set 3
        ParamIDs::sandPreLfFreq,    // default index 3 -> set 0
        ParamIDs::sandPreHfBellFreq,
    };
    static constexpr float nonDefaultNormalised[] = { 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };

    std::vector<juce::RangedAudioParameter*> params;
    std::vector<float> savedValues;

    for (size_t i = 0; i < std::size (choiceIds); ++i)
    {
        auto* param = processor.apvts.getParameter (choiceIds[i]);
        REQUIRE (param != nullptr);
        REQUIRE (param->getValue() != Catch::Approx (nonDefaultNormalised[i]));
        param->setValueNotifyingHost (nonDefaultNormalised[i]);
        params.push_back (param);
        savedValues.push_back (param->getValue());
    }

    juce::MemoryBlock savedState;
    processor.getStateInformation (savedState);

    for (auto* param : params)
        param->setValueNotifyingHost (param->getDefaultValue());

    processor.setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));

    for (size_t i = 0; i < params.size(); ++i)
    {
        INFO ("parameter index = " << i << " (" << choiceIds[i] << ")");
        CHECK (params[i]->getValue() == Catch::Approx (savedValues[i]).margin (1e-6));
    }
}

TEST_CASE ("State: a v1 session (unknown old busA_/busB_/busC_/busD_ IDs) loads without crashing", "[state][v1-import]")
{
    // A hand-built ValueTree shaped like a real APVTS save: the same root
    // tag APVTS expects, with each parameter as a child "PARAM" node
    // carrying "id"/"value" properties (see
    // AudioProcessorValueTreeState::valueType/valuePropertyID/idPropertyID
    // - JUCE 8.0.14, juce_AudioProcessorValueTreeState.h). Every ID here
    // except in_trim belongs to the retired v1 layout (see the old
    // ParameterIds.h, git history) and does not exist in the v2 layout -
    // setStateInformation must ignore the unknown ones silently rather than
    // crash (design-brief.md: "state migration = tolerant import").
    juce::ValueTree v1State ("PARAMETERS");

    const auto addParam = [&v1State] (const char* id, double denormalisedValue)
    {
        juce::ValueTree param ("PARAM");
        param.setProperty ("id", id, nullptr);
        param.setProperty ("value", denormalisedValue, nullptr);
        v1State.appendChild (param, nullptr);
    };

    addParam ("in_trim", 3.0); // this ID DOES still exist in v2 - proves survivors are still applied

    static constexpr const char* retiredV1Ids[] = {
        "busA_hpfEnabled", "busA_hpfFreq", "busA_compRatio", "busA_compThreshold",
        "busB_lowBoostFreq", "busB_optoReduction", "busB_optoMakeup", "busB_airGain",
        "busC_attack", "busC_release", "busC_drive", "busC_outputTrim",
        "busD_feedback", "busD_mono",
    };

    for (const auto* id : retiredV1Ids)
        addParam (id, 0.5);

    const std::unique_ptr<juce::XmlElement> xml (v1State.createXml());
    juce::MemoryBlock v1Binary;
    juce::AudioProcessor::copyXmlToBinary (*xml, v1Binary);

    MiserereAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    CHECK_NOTHROW (processor.setStateInformation (v1Binary.getData(), static_cast<int> (v1Binary.getSize())));

    // The processor must still be fully usable afterwards.
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    CHECK_NOTHROW (processor.processBlock (buffer, midi));

    // The one ID that DOES still exist in v2 (in_trim) round-trips normally
    // even inside an otherwise-foreign state tree.
    auto* inTrim = processor.apvts.getParameter (ParamIDs::inTrim);
    REQUIRE (inTrim != nullptr);
    CHECK (inTrim->convertFrom0to1 (inTrim->getValue()) == Catch::Approx (3.0f).margin (0.01f));
}
