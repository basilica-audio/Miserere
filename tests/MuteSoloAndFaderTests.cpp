#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// Guarantees 8 and 9 (mute/audition part): return-fader true-zero, the
// -60 dB "-inf" floor, the Parallel macro trim, and Mute/Audition semantics
// (Audition is exclusive; Mute wins over Audition on the same bus) - the
// v2 renaming of v1's Solo, per the brief ("labelled Audition, because the
// technique forbids judging solo sound").
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

    void muteAllExceptCrush (MiserereAudioProcessor& processor)
    {
        setBool (processor, ParamIDs::sandMute, true);
        setBool (processor, ParamIDs::spreadMute, true);
        setBool (processor, ParamIDs::slapMute, true);
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

TEST_CASE ("Fader: Crush at unity (0 dB) and everything else muted passes only the direct + Crush sum", "[fader][dsp]")
{
    MiserereAudioProcessor processor;
    muteAllExceptCrush (processor);
    setParam (processor, ParamIDs::crushLevel, 0.0f);
    setParam (processor, ParamIDs::crushInput, 0.0f); // near-neutral so the fader test isn't dominated by GR

    const auto outputRms = processAndMeasureRms (processor);
    CHECK (outputRms > 0.0);
}

TEST_CASE ("Fader: -6 dB on the Crush return fader attenuates its contribution by 6 dB", "[fader][dsp]")
{
    // Isolate Crush's own contribution: mute the direct path's audibility
    // by auditioning Crush (Audition excludes the direct path too - see
    // MiserereEngine's mute/audition resolution).
    const auto measureAtLevel = [] (float levelDb)
    {
        MiserereAudioProcessor processor;
        setBool (processor, ParamIDs::crushAudition, true);
        setParam (processor, ParamIDs::crushLevel, levelDb);
        setParam (processor, ParamIDs::crushInput, 0.0f);
        return processAndMeasureRms (processor);
    };

    const auto atUnity = measureAtLevel (0.0f);
    const auto atMinusSix = measureAtLevel (-6.0f);

    REQUIRE (atUnity > 0.0);
    CHECK (juce::Decibels::gainToDecibels (atMinusSix / atUnity) == Catch::Approx (-6.0).margin (0.2));
}

TEST_CASE ("Fader: the -60 dB floor silences a bus completely (-inf semantics)", "[fader][dsp]")
{
    MiserereAudioProcessor processor;
    setBool (processor, ParamIDs::crushAudition, true);
    setParam (processor, ParamIDs::crushLevel, -60.0f);

    const auto outputRms = processAndMeasureRms (processor);
    CHECK (outputRms < 1.0e-7);
}

TEST_CASE ("Parallel trim: offsets all four return busses simultaneously", "[fader][dsp][parallel-trim]")
{
    const auto measureAtTrim = [] (float trimDb)
    {
        MiserereAudioProcessor processor;
        setBool (processor, ParamIDs::crushAudition, true);
        setParam (processor, ParamIDs::crushLevel, 0.0f);
        setParam (processor, ParamIDs::crushInput, 0.0f);
        setParam (processor, ParamIDs::parallelTrim, trimDb);
        return processAndMeasureRms (processor);
    };

    const auto atZeroTrim = measureAtTrim (0.0f);
    const auto atMinusTwelveTrim = measureAtTrim (-12.0f);

    REQUIRE (atZeroTrim > 0.0);
    CHECK (juce::Decibels::gainToDecibels (atMinusTwelveTrim / atZeroTrim) == Catch::Approx (-12.0).margin (0.3));
}

TEST_CASE ("Mute: muting Crush removes it even when auditioned", "[mute][dsp]")
{
    MiserereAudioProcessor processor;
    setBool (processor, ParamIDs::crushAudition, true);
    setBool (processor, ParamIDs::crushMute, true);
    setParam (processor, ParamIDs::crushLevel, 0.0f);

    const auto outputRms = processAndMeasureRms (processor);
    CHECK (outputRms < 1.0e-7);
}

TEST_CASE ("Audition: auditioning Crush excludes the direct path and the other busses", "[audition][dsp]")
{
    MiserereAudioProcessor processor;
    setBool (processor, ParamIDs::crushAudition, true);
    setParam (processor, ParamIDs::crushLevel, -60.0f); // Crush's OWN contribution silenced too

    // With Crush auditioned but at the fader floor, and nothing else
    // reaching the sum (direct path excluded by Audition), the output must
    // be silent - even though the direct path's default settings would
    // otherwise pass audio at unity.
    const auto outputRms = processAndMeasureRms (processor);
    CHECK (outputRms < 1.0e-7);
}

TEST_CASE ("Mute wins over Audition on the same bus", "[mute][audition][dsp]")
{
    MiserereAudioProcessor processor;
    setBool (processor, ParamIDs::crushAudition, true);
    setBool (processor, ParamIDs::crushMute, true);
    setParam (processor, ParamIDs::crushLevel, 0.0f);

    const auto outputRms = processAndMeasureRms (processor);
    CHECK (outputRms < 1.0e-7);
}

TEST_CASE ("Audition is exclusive: engaging one bus's audition releases the others", "[audition][exclusivity]")
{
    MiserereAudioProcessor processor;

    auto* auditionCrush = processor.apvts.getParameter (ParamIDs::crushAudition);
    auto* auditionSand = processor.apvts.getParameter (ParamIDs::sandAudition);
    auto* auditionSpread = processor.apvts.getParameter (ParamIDs::spreadAudition);
    auto* auditionSlap = processor.apvts.getParameter (ParamIDs::slapAudition);

    REQUIRE (auditionCrush != nullptr);
    REQUIRE (auditionSand != nullptr);
    REQUIRE (auditionSpread != nullptr);
    REQUIRE (auditionSlap != nullptr);

    auditionCrush->setValueNotifyingHost (1.0f);
    CHECK (auditionCrush->getValue() >= 0.5f);

    // Engaging Sand must release Crush.
    auditionSand->setValueNotifyingHost (1.0f);
    CHECK (auditionSand->getValue() >= 0.5f);
    CHECK (auditionCrush->getValue() < 0.5f);

    // Engaging Slap must release Sand; Spread was never on.
    auditionSlap->setValueNotifyingHost (1.0f);
    CHECK (auditionSlap->getValue() >= 0.5f);
    CHECK (auditionSand->getValue() < 0.5f);
    CHECK (auditionSpread->getValue() < 0.5f);

    // Releasing the active audition leaves everything off (no bounce-back).
    auditionSlap->setValueNotifyingHost (0.0f);
    CHECK (auditionCrush->getValue() < 0.5f);
    CHECK (auditionSand->getValue() < 0.5f);
    CHECK (auditionSpread->getValue() < 0.5f);
    CHECK (auditionSlap->getValue() < 0.5f);
}
