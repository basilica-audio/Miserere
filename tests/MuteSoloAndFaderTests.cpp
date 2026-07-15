#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// M1 guarantee 10 (mute/solo part): solo is exclusive, mute wins over solo
// on the same bus - plus the per-bus fader contract (unity, attenuation,
// and the -60 dB "-inf" floor).
namespace
{
    void setParam (MiserereAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    void setBool (MiserereAudioProcessor& processor, const char* id, bool on)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (on ? 1.0f : 0.0f);
    }

    // Neutral Direct bus only (all other busses at their default -60 dB
    // fader floor), so output level changes reflect Bus A's fader/mute/solo
    // state alone.
    void configureNeutralDirectOnly (MiserereAudioProcessor& processor)
    {
        setBool (processor, ParamIDs::busAHpfEnabled, false);
        setParam (processor, ParamIDs::busAEqLowGain, 0.0f);
        setParam (processor, ParamIDs::busAEqMidGain, 0.0f);
        setParam (processor, ParamIDs::busAEqHighGain, 0.0f);
        setParam (processor, ParamIDs::busACompThreshold, 0.0f);
        setParam (processor, ParamIDs::busACompMakeup, 0.0f);
        setBool (processor, ParamIDs::busADeessEnabled, false);
        setParam (processor, ParamIDs::busASatDrive, 0.0f);
        setParam (processor, ParamIDs::busALevel, 0.0f);
    }

    double processAndMeasureRms (MiserereAudioProcessor& processor)
    {
        processor.prepareToPlay (48000.0, 4096);

        juce::AudioBuffer<float> buffer (2, 4096);
        TestHelpers::fillWithSine (buffer, 48000.0, 440.0, 0.5f);

        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);

        REQUIRE (TestHelpers::allSamplesFinite (buffer));
        return TestHelpers::tailRms (buffer, 1024);
    }
}

TEST_CASE ("Fader: Bus A at unity passes the neutral chain at unity level", "[fader][dsp]")
{
    MiserereAudioProcessor processor;
    configureNeutralDirectOnly (processor);

    const auto outputRms = processAndMeasureRms (processor);
    const auto expectedRms = 0.5 / juce::MathConstants<double>::sqrt2;

    CHECK (juce::Decibels::gainToDecibels (outputRms / expectedRms) == Catch::Approx (0.0).margin (0.1));
}

TEST_CASE ("Fader: -6 dB on the Bus A fader attenuates the output by 6 dB", "[fader][dsp]")
{
    MiserereAudioProcessor processor;
    configureNeutralDirectOnly (processor);
    setParam (processor, ParamIDs::busALevel, -6.0f);

    const auto outputRms = processAndMeasureRms (processor);
    const auto expectedRms = 0.5 / juce::MathConstants<double>::sqrt2;

    CHECK (juce::Decibels::gainToDecibels (outputRms / expectedRms) == Catch::Approx (-6.0).margin (0.1));
}

TEST_CASE ("Fader: the -60 dB floor silences the bus completely (-inf semantics)", "[fader][dsp]")
{
    MiserereAudioProcessor processor;
    configureNeutralDirectOnly (processor);
    setParam (processor, ParamIDs::busALevel, -60.0f);

    const auto outputRms = processAndMeasureRms (processor);

    CHECK (outputRms < 1.0e-7);
}

TEST_CASE ("Mute: muting Bus A silences the output when only Bus A is up", "[mute][dsp]")
{
    MiserereAudioProcessor processor;
    configureNeutralDirectOnly (processor);
    setBool (processor, ParamIDs::busAMute, true);

    const auto outputRms = processAndMeasureRms (processor);

    CHECK (outputRms < 1.0e-7);
}

TEST_CASE ("Solo: soloing Bus B excludes the unmuted Bus A from the sum", "[solo][dsp]")
{
    MiserereAudioProcessor processor;
    configureNeutralDirectOnly (processor);
    setBool (processor, ParamIDs::busBSolo, true); // Bus B's fader is still at the floor -> silence

    const auto outputRms = processAndMeasureRms (processor);

    CHECK (outputRms < 1.0e-7);
}

TEST_CASE ("Solo: soloing the neutral Bus A keeps it passing at unity", "[solo][dsp]")
{
    MiserereAudioProcessor processor;
    configureNeutralDirectOnly (processor);
    setBool (processor, ParamIDs::busASolo, true);

    const auto outputRms = processAndMeasureRms (processor);
    const auto expectedRms = 0.5 / juce::MathConstants<double>::sqrt2;

    CHECK (juce::Decibels::gainToDecibels (outputRms / expectedRms) == Catch::Approx (0.0).margin (0.1));
}

TEST_CASE ("Mute wins over Solo on the same bus", "[mute][solo][dsp]")
{
    MiserereAudioProcessor processor;
    configureNeutralDirectOnly (processor);
    setBool (processor, ParamIDs::busASolo, true);
    setBool (processor, ParamIDs::busAMute, true);

    const auto outputRms = processAndMeasureRms (processor);

    CHECK (outputRms < 1.0e-7);
}

TEST_CASE ("Solo is exclusive: engaging one bus's solo releases the others", "[solo][exclusivity]")
{
    MiserereAudioProcessor processor;

    auto* soloA = processor.apvts.getParameter (ParamIDs::busASolo);
    auto* soloB = processor.apvts.getParameter (ParamIDs::busBSolo);
    auto* soloC = processor.apvts.getParameter (ParamIDs::busCSolo);
    auto* soloD = processor.apvts.getParameter (ParamIDs::busDSolo);

    REQUIRE (soloA != nullptr);
    REQUIRE (soloB != nullptr);
    REQUIRE (soloC != nullptr);
    REQUIRE (soloD != nullptr);

    soloA->setValueNotifyingHost (1.0f);
    CHECK (soloA->getValue() >= 0.5f);

    // Engaging B must release A.
    soloB->setValueNotifyingHost (1.0f);
    CHECK (soloB->getValue() >= 0.5f);
    CHECK (soloA->getValue() < 0.5f);

    // Engaging D must release B; C was never on.
    soloD->setValueNotifyingHost (1.0f);
    CHECK (soloD->getValue() >= 0.5f);
    CHECK (soloB->getValue() < 0.5f);
    CHECK (soloC->getValue() < 0.5f);

    // Releasing the active solo leaves everything off (no bounce-back).
    soloD->setValueNotifyingHost (0.0f);
    CHECK (soloA->getValue() < 0.5f);
    CHECK (soloB->getValue() < 0.5f);
    CHECK (soloC->getValue() < 0.5f);
    CHECK (soloD->getValue() < 0.5f);
}
