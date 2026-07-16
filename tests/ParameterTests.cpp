#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "params/ParameterLayout.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// Layout-contract tests: every frozen ID exists, the choice parameters
// carry the brief's selections in the shared-table order PluginProcessor
// maps back to Hz/enum values, and the out-of-the-box defaults implement
// the v2 design (direct path fully off, return busses at the brief's
// specified -9/-12/-18/-15 dB levels, unlinked detection).
TEST_CASE ("Every frozen parameter ID exists in the layout", "[parameters]")
{
    MiserereAudioProcessor processor;

    static constexpr const char* allIds[] = {
        ParamIDs::inTrim, ParamIDs::outTrim, ParamIDs::bypass, ParamIDs::link, ParamIDs::parallelTrim,

        ParamIDs::directDeessPreEnabled, ParamIDs::directDeessPreFreq, ParamIDs::directDeessPreThreshold,
        ParamIDs::directFetEnabled, ParamIDs::directFetThreshold, ParamIDs::directFetAttack,
        ParamIDs::directFetRelease, ParamIDs::directFetMakeup,
        ParamIDs::directEqHpfEnabled, ParamIDs::directEqHpfFreq, ParamIDs::directEqLowFreq, ParamIDs::directEqLowGain,
        ParamIDs::directEqMidFreq, ParamIDs::directEqMidGain, ParamIDs::directEqHighGain, ParamIDs::directEqDrive,
        ParamIDs::directSatDrive,
        ParamIDs::directDeessPostEnabled, ParamIDs::directDeessPostFreq, ParamIDs::directDeessPostThreshold,

        ParamIDs::crushInput, ParamIDs::crushRatio, ParamIDs::crushStyle, ParamIDs::crushAttack,
        ParamIDs::crushRelease, ParamIDs::crushOutput, ParamIDs::crushLevel, ParamIDs::crushMute, ParamIDs::crushAudition,

        ParamIDs::sandPreLfFreq, ParamIDs::sandPreLfBoost, ParamIDs::sandPreLfCut,
        ParamIDs::sandPreHfBellFreq, ParamIDs::sandPreHfBellBoost, ParamIDs::sandPreHfBellBandwidth,
        ParamIDs::sandPreHfShelfFreq, ParamIDs::sandPreHfShelfAtten,
        ParamIDs::sandPeakRed, ParamIDs::sandLimit, ParamIDs::sandEmphasis, ParamIDs::sandResidual,
        ParamIDs::sandPostLfFreq, ParamIDs::sandPostLfBoost, ParamIDs::sandPostLfCut,
        ParamIDs::sandPostHfBellFreq, ParamIDs::sandPostHfBellBoost, ParamIDs::sandPostHfBellBandwidth,
        ParamIDs::sandPostHfShelfFreq, ParamIDs::sandPostHfShelfAtten,
        ParamIDs::sandLevel, ParamIDs::sandMute, ParamIDs::sandAudition,

        ParamIDs::spreadDetune, ParamIDs::spreadTime, ParamIDs::spreadWidth,
        ParamIDs::spreadLevel, ParamIDs::spreadMute, ParamIDs::spreadAudition,

        ParamIDs::slapTime, ParamIDs::slapStereo, ParamIDs::slapTone,
        ParamIDs::slapLevel, ParamIDs::slapMute, ParamIDs::slapAudition,
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

    checkChoices (ParamIDs::crushRatio, msrr::crushRatioChoices);
    checkChoices (ParamIDs::crushStyle, msrr::crushStyleChoices);
    checkChoices (ParamIDs::directEqHpfFreq, msrr::eqHpfFreqChoices);
    checkChoices (ParamIDs::directEqLowFreq, msrr::eqLowFreqChoices);
    checkChoices (ParamIDs::directEqMidFreq, msrr::eqMidFreqChoices);
    checkChoices (ParamIDs::sandPreLfFreq, msrr::sandLfFreqChoices);
    checkChoices (ParamIDs::sandPreHfBellFreq, msrr::sandHfBellFreqChoices);
    checkChoices (ParamIDs::sandPreHfShelfFreq, msrr::sandHfShelfFreqChoices);

    CHECK (msrr::crushRatioChoices.size() == 5);
    CHECK (msrr::eqHpfFreqChoices.size() == static_cast<int> (msrr::eqHpfFreqHz.size()));
    CHECK (msrr::eqLowFreqChoices.size() == static_cast<int> (msrr::eqLowFreqHz.size()));
    CHECK (msrr::eqMidFreqChoices.size() == static_cast<int> (msrr::eqMidFreqHz.size()));
    CHECK (msrr::sandLfFreqChoices.size() == static_cast<int> (msrr::sandLfFreqHz.size()));
    CHECK (msrr::sandHfBellFreqChoices.size() == static_cast<int> (msrr::sandHfBellFreqHz.size()));
    CHECK (msrr::sandHfShelfFreqChoices.size() == static_cast<int> (msrr::sandHfShelfFreqHz.size()));

    CHECK (msrr::eqHpfFreqHz[0] == 50.0f);
    CHECK (msrr::eqHpfFreqHz[3] == 300.0f);
    CHECK (msrr::sandLfFreqHz[3] == 100.0f);
    CHECK (msrr::sandHfBellFreqHz[0] == 3000.0f);
}

TEST_CASE ("Defaults: direct path fully off, return busses at the brief's specified levels", "[parameters][defaults]")
{
    MiserereAudioProcessor processor;

    const auto realValue = [&] (const char* id)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param->convertFrom0to1 (param->getValue());
    };

    // Direct path: every optional section defaults OFF - the "bit-
    // transparent wire" invariant.
    for (const auto* id : { ParamIDs::directDeessPreEnabled, ParamIDs::directFetEnabled,
                             ParamIDs::directEqHpfEnabled, ParamIDs::directDeessPostEnabled })
    {
        INFO ("parameter id = " << id);
        CHECK (processor.apvts.getParameter (id)->getValue() < 0.5f);
    }

    for (const auto* id : { ParamIDs::directEqLowGain, ParamIDs::directEqMidGain, ParamIDs::directEqHighGain,
                             ParamIDs::directEqDrive, ParamIDs::directSatDrive })
    {
        INFO ("parameter id = " << id);
        CHECK (realValue (id) == Catch::Approx (0.0f).margin (1.0e-3));
    }

    // Return busses: the brief's specified defaults (-9/-12/-18/-15 dB).
    CHECK (realValue (ParamIDs::crushLevel) == Catch::Approx (-9.0f).margin (1.0e-3));
    CHECK (realValue (ParamIDs::sandLevel) == Catch::Approx (-12.0f).margin (1.0e-3));
    CHECK (realValue (ParamIDs::spreadLevel) == Catch::Approx (-18.0f).margin (1.0e-3));
    CHECK (realValue (ParamIDs::slapLevel) == Catch::Approx (-15.0f).margin (1.0e-3));

    CHECK (realValue (ParamIDs::slapTime) == Catch::Approx (110.0f)); // brief's default slap time
    CHECK (realValue (ParamIDs::spreadDetune) == Catch::Approx (6.0f)); // brief's default detune

    // Unlinked by default ("dual mono is key").
    CHECK (processor.apvts.getParameter (ParamIDs::link)->getValue() < 0.5f);

    // No mute/audition/bypass engaged out of the box.
    for (const auto* id : { ParamIDs::crushMute, ParamIDs::sandMute, ParamIDs::spreadMute, ParamIDs::slapMute,
                             ParamIDs::crushAudition, ParamIDs::sandAudition, ParamIDs::spreadAudition, ParamIDs::slapAudition,
                             ParamIDs::bypass })
    {
        INFO ("parameter id = " << id);
        CHECK (processor.apvts.getParameter (id)->getValue() < 0.5f);
    }
}
