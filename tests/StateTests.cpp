#include "PluginProcessor.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

// M1 guarantee 10 (state part): full state round-trip for float, bool and
// choice parameters.
TEST_CASE ("State round-trip preserves non-default values of every float parameter", "[state]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    // Bool and choice parameters are covered by their own round-trip tests
    // below: AudioParameterBool quantises getValue() to exactly 0.0/1.0 and
    // AudioParameterChoice to its index grid, so this test's "distinct
    // fractional normalised value per parameter" technique doesn't apply.
    static constexpr const char* floatIds[] = {
        ParamIDs::inTrim, ParamIDs::outTrim,
        ParamIDs::busAHpfFreq,
        ParamIDs::busAEqLowGain, ParamIDs::busAEqMidFreq, ParamIDs::busAEqMidGain,
        ParamIDs::busAEqMidQ, ParamIDs::busAEqHighGain,
        ParamIDs::busACompThreshold, ParamIDs::busACompAttack, ParamIDs::busACompRelease, ParamIDs::busACompMakeup,
        ParamIDs::busADeessFreq, ParamIDs::busADeessThreshold,
        ParamIDs::busASatDrive, ParamIDs::busALevel,
        ParamIDs::busBLowBoostGain, ParamIDs::busBHighBoostGain,
        ParamIDs::busBOptoReduction, ParamIDs::busBOptoMakeup, ParamIDs::busBAirGain, ParamIDs::busBLevel,
        ParamIDs::busCAttack, ParamIDs::busCRelease, ParamIDs::busCDrive, ParamIDs::busCOutputTrim, ParamIDs::busCLevel,
        ParamIDs::busDDelayMs, ParamIDs::busDFeedback, ParamIDs::busDHpFreq, ParamIDs::busDLpFreq, ParamIDs::busDLevel,
    };

    std::vector<juce::RangedAudioParameter*> params;
    std::vector<float> savedNormalisedValues;

    for (const auto* id : floatIds)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        params.push_back (param);
    }

    // Push every parameter to a distinct, non-default normalised value so
    // the round-trip assertion below can't pass by coincidence.
    for (size_t i = 0; i < params.size(); ++i)
    {
        auto normalisedValue = 0.2f + 0.6f * (static_cast<float> (i % 5) / 4.0f);

        // Guard against a coincidental match with this parameter's own
        // default normalised value, which would let the "still non-default
        // after reset" sanity check below pass by accident.
        if (std::abs (normalisedValue - params[i]->getDefaultValue()) < 0.05f)
            normalisedValue = std::fmod (normalisedValue + 0.37f, 1.0f);

        params[i]->setValueNotifyingHost (normalisedValue);
        savedNormalisedValues.push_back (params[i]->getValue());
    }

    juce::MemoryBlock savedState;
    processor.getStateInformation (savedState);
    REQUIRE (savedState.getSize() > 0);

    // Reset every parameter back to its default before restoring, so the
    // round-trip assertion below can't pass by accident.
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

    // busASolo is deliberately excluded: flipping every solo to true in a
    // loop would fight the solo-exclusivity listener (only the last one
    // would stick) - solo round-trip is covered separately below with a
    // single active solo, which is the only state the exclusivity contract
    // allows anyway.
    static constexpr const char* boolIds[] = {
        ParamIDs::bypass,
        ParamIDs::busAHpfEnabled, ParamIDs::busADeessEnabled,
        ParamIDs::busAMute, ParamIDs::busBMute, ParamIDs::busCMute, ParamIDs::busDMute,
        ParamIDs::busDMono,
    };

    std::vector<juce::RangedAudioParameter*> params;
    std::vector<float> savedValues;

    for (const auto* id : boolIds)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        // Flip each parameter away from its default so the round-trip
        // can't pass by already sitting at the post-restore value.
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

TEST_CASE ("State round-trip preserves an active solo", "[state][solo]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    auto* soloC = processor.apvts.getParameter (ParamIDs::busCSolo);
    REQUIRE (soloC != nullptr);
    soloC->setValueNotifyingHost (1.0f);

    juce::MemoryBlock savedState;
    processor.getStateInformation (savedState);

    soloC->setValueNotifyingHost (0.0f);
    REQUIRE (soloC->getValue() < 0.5f);

    processor.setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));

    CHECK (soloC->getValue() >= 0.5f);
}

TEST_CASE ("State round-trip preserves every choice parameter", "[state][choice]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    static constexpr const char* choiceIds[] = {
        ParamIDs::busACompRatio,     // default index 0 -> set 1 ("8:1")
        ParamIDs::busBLowBoostFreq,  // default index 1 -> set 0 ("60 Hz")
        ParamIDs::busBHighBoostFreq, // default index 2 -> set 3 ("16 kHz")
    };
    static constexpr float nonDefaultNormalised[] = { 1.0f, 0.0f, 1.0f };

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
