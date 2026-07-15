#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "params/ParameterLayout.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// Layout-contract tests: every frozen ID exists, the choice parameters
// carry exactly the strings the brief specifies (in the shared-table order
// PluginProcessor maps back to Hz/ratio values), and the out-of-the-box
// defaults implement the "Direct chain only until you push a fader up"
// design.
TEST_CASE ("Every frozen parameter ID exists in the layout", "[parameters]")
{
    MiserereAudioProcessor processor;

    static constexpr const char* allIds[] = {
        ParamIDs::inTrim, ParamIDs::outTrim, ParamIDs::bypass,
        ParamIDs::busAHpfEnabled, ParamIDs::busAHpfFreq,
        ParamIDs::busAEqLowGain, ParamIDs::busAEqMidFreq, ParamIDs::busAEqMidGain,
        ParamIDs::busAEqMidQ, ParamIDs::busAEqHighGain,
        ParamIDs::busACompRatio, ParamIDs::busACompThreshold, ParamIDs::busACompAttack,
        ParamIDs::busACompRelease, ParamIDs::busACompMakeup,
        ParamIDs::busADeessEnabled, ParamIDs::busADeessFreq, ParamIDs::busADeessThreshold,
        ParamIDs::busASatDrive,
        ParamIDs::busALevel, ParamIDs::busAMute, ParamIDs::busASolo,
        ParamIDs::busBLowBoostFreq, ParamIDs::busBLowBoostGain,
        ParamIDs::busBHighBoostFreq, ParamIDs::busBHighBoostGain,
        ParamIDs::busBOptoReduction, ParamIDs::busBOptoMakeup, ParamIDs::busBAirGain,
        ParamIDs::busBLevel, ParamIDs::busBMute, ParamIDs::busBSolo,
        ParamIDs::busCAttack, ParamIDs::busCRelease, ParamIDs::busCDrive, ParamIDs::busCOutputTrim,
        ParamIDs::busCLevel, ParamIDs::busCMute, ParamIDs::busCSolo,
        ParamIDs::busDDelayMs, ParamIDs::busDFeedback, ParamIDs::busDHpFreq, ParamIDs::busDLpFreq,
        ParamIDs::busDMono,
        ParamIDs::busDLevel, ParamIDs::busDMute, ParamIDs::busDSolo,
    };

    for (const auto* id : allIds)
    {
        INFO ("parameter id = " << id);
        CHECK (processor.apvts.getParameter (id) != nullptr);
    }
}

TEST_CASE ("Choice parameters carry the brief's selections in shared-table order", "[parameters][choice]")
{
    MiserereAudioProcessor processor;

    const auto checkChoices = [&] (const char* id, const juce::StringArray& expected)
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (id));
        REQUIRE (param != nullptr);

        INFO ("parameter id = " << id);
        REQUIRE (param->choices.size() == expected.size());

        for (int i = 0; i < expected.size(); ++i)
            CHECK (param->choices[i] == expected[i]);
    };

    checkChoices (ParamIDs::busACompRatio, msrr::compRatioChoices);
    checkChoices (ParamIDs::busBLowBoostFreq, msrr::busBLowBoostFreqChoices);
    checkChoices (ParamIDs::busBHighBoostFreq, msrr::busBHighBoostFreqChoices);

    // The value tables must line up 1:1 with the string lists.
    CHECK (msrr::compRatioChoices.size() == static_cast<int> (msrr::compRatioValues.size()));
    CHECK (msrr::busBLowBoostFreqChoices.size() == static_cast<int> (msrr::busBLowBoostFreqHz.size()));
    CHECK (msrr::busBHighBoostFreqChoices.size() == static_cast<int> (msrr::busBHighBoostFreqHz.size()));

    CHECK (msrr::compRatioValues[0] == 4.0f);
    CHECK (msrr::compRatioValues[1] == 8.0f);
    CHECK (msrr::busBLowBoostFreqHz[0] == 60.0f);
    CHECK (msrr::busBLowBoostFreqHz[1] == 100.0f);
    CHECK (msrr::busBHighBoostFreqHz[0] == 8000.0f);
    CHECK (msrr::busBHighBoostFreqHz[3] == 16000.0f);
}

TEST_CASE ("Defaults: Direct bus at unity, parallel busses at the fader floor", "[parameters][defaults]")
{
    MiserereAudioProcessor processor;

    const auto realValue = [&] (const char* id)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param->convertFrom0to1 (param->getValue());
    };

    // margin: convertFrom0to1 quantisation leaves ~1e-6 dB of rounding.
    CHECK (realValue (ParamIDs::busALevel) == Catch::Approx (0.0f).margin (1.0e-4));
    CHECK (realValue (ParamIDs::busBLevel) == Catch::Approx (-60.0f));
    CHECK (realValue (ParamIDs::busCLevel) == Catch::Approx (-60.0f));
    CHECK (realValue (ParamIDs::busDLevel) == Catch::Approx (-60.0f));

    CHECK (realValue (ParamIDs::busDDelayMs) == Catch::Approx (110.0f)); // brief's default slap time

    // No mute/solo engaged out of the box.
    for (const auto* id : { ParamIDs::busAMute, ParamIDs::busBMute, ParamIDs::busCMute, ParamIDs::busDMute,
                             ParamIDs::busASolo, ParamIDs::busBSolo, ParamIDs::busCSolo, ParamIDs::busDSolo,
                             ParamIDs::bypass })
    {
        INFO ("parameter id = " << id);
        CHECK (processor.apvts.getParameter (id)->getValue() < 0.5f);
    }
}
