#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// M1 guarantees 1 and 2 (docs/design-brief.md): the neutral-Direct-bus null
// test and the parallel impulse-alignment test - the two properties the
// whole parallel topology stands on (see docs/adr/0003).
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

    // Brief, guarantee 1: "Bus A solo, all modules neutral (EQ flat, comp
    // threshold at max, de-esser off, drive 0), fader unity".
    void configureNeutralDirectBus (MiserereAudioProcessor& processor)
    {
        setParam (processor, ParamIDs::inTrim, 0.0f);
        setParam (processor, ParamIDs::outTrim, 0.0f);
        setBool (processor, ParamIDs::bypass, false);

        setBool (processor, ParamIDs::busAHpfEnabled, false);
        setParam (processor, ParamIDs::busAEqLowGain, 0.0f);
        setParam (processor, ParamIDs::busAEqMidGain, 0.0f);
        setParam (processor, ParamIDs::busAEqHighGain, 0.0f);
        setParam (processor, ParamIDs::busACompThreshold, 0.0f); // max = never trips
        setParam (processor, ParamIDs::busACompMakeup, 0.0f);
        setBool (processor, ParamIDs::busADeessEnabled, false);
        setParam (processor, ParamIDs::busASatDrive, 0.0f);

        setParam (processor, ParamIDs::busALevel, 0.0f); // fader unity
        setBool (processor, ParamIDs::busAMute, false);
        setBool (processor, ParamIDs::busASolo, true);
    }

    // Neutralises Busses B and C (identity when their fader is up), used by
    // the alignment tests below.
    void configureNeutralParallelBusses (MiserereAudioProcessor& processor)
    {
        setParam (processor, ParamIDs::busBLowBoostGain, 0.0f);
        setParam (processor, ParamIDs::busBHighBoostGain, 0.0f);
        setParam (processor, ParamIDs::busBOptoReduction, 0.0f);
        setParam (processor, ParamIDs::busBOptoMakeup, 0.0f);
        setParam (processor, ParamIDs::busBAirGain, 0.0f);
        setParam (processor, ParamIDs::busBLevel, 0.0f);

        setParam (processor, ParamIDs::busCDrive, 0.0f);
        setParam (processor, ParamIDs::busCOutputTrim, 0.0f);
        setParam (processor, ParamIDs::busCLevel, 0.0f);
    }
}

TEST_CASE ("Null: neutral Direct bus soloed at unity fader is bit-transparent (<= -120 dBFS diff)", "[null][dsp]")
{
    MiserereAudioProcessor processor;
    configureNeutralDirectBus (processor);
    processor.prepareToPlay (48000.0, 4096);

    juce::AudioBuffer<float> reference (2, 4096);
    TestHelpers::fillWithSine (reference, 48000.0, 440.0, 0.5f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::MidiBuffer midi;
    processor.processBlock (processed, midi);

    CHECK (TestHelpers::maxDifferenceDbfs (processed, reference) <= -120.0);
}

TEST_CASE ("Null: transparency holds across multiple consecutive blocks", "[null][dsp]")
{
    MiserereAudioProcessor processor;
    configureNeutralDirectBus (processor);
    processor.prepareToPlay (48000.0, 512);

    juce::MidiBuffer midi;

    for (int block = 0; block < 16; ++block)
    {
        juce::AudioBuffer<float> reference (2, 512);
        TestHelpers::fillWithSine (reference, 48000.0, 440.0, 0.5f, block * 512);

        juce::AudioBuffer<float> processed;
        processed.makeCopyOf (reference);

        processor.processBlock (processed, midi);

        INFO ("block = " << block);
        CHECK (TestHelpers::maxDifferenceDbfs (processed, reference) <= -120.0);
    }
}

TEST_CASE ("Null: in/out trims are the only level change on the neutral path (+6 then -6 dB nulls)", "[null][dsp][trim]")
{
    MiserereAudioProcessor processor;
    configureNeutralDirectBus (processor);
    setParam (processor, ParamIDs::inTrim, 6.0f);
    setParam (processor, ParamIDs::outTrim, -6.0f);
    processor.prepareToPlay (48000.0, 4096);

    juce::AudioBuffer<float> reference (2, 4096);
    TestHelpers::fillWithSine (reference, 48000.0, 440.0, 0.5f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::MidiBuffer midi;
    processor.processBlock (processed, midi);

    // +6 dB then -6 dB is unity only up to float rounding of the two gain
    // factors, so the bar here is -100 dB rather than the bit-exact -120.
    CHECK (TestHelpers::maxDifferenceDbfs (processed, reference) <= -100.0);
}

TEST_CASE ("Alignment: busses A/B/C sum an impulse to a single aligned impulse", "[alignment][dsp]")
{
    MiserereAudioProcessor processor;
    configureNeutralDirectBus (processor);
    setBool (processor, ParamIDs::busASolo, false); // all three busses in play
    configureNeutralParallelBusses (processor);
    setBool (processor, ParamIDs::busDMute, true);

    processor.prepareToPlay (48000.0, 4096);

    constexpr int impulseIndex = 100;
    constexpr float impulseAmplitude = 0.25f; // low enough that Bus C's fixed threshold never trips

    juce::AudioBuffer<float> buffer (2, 4096);
    buffer.clear();
    for (int channel = 0; channel < 2; ++channel)
        buffer.setSample (channel, impulseIndex, impulseAmplitude);

    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto* data = buffer.getReadPointer (channel);

        // The three busses each contribute the impulse at the same index.
        CHECK (data[impulseIndex] == Catch::Approx (3.0f * impulseAmplitude).margin (0.01));

        // No pre/post echoes above -100 dBFS anywhere else.
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            if (sample == impulseIndex)
                continue;

            INFO ("channel = " << channel << ", sample = " << sample);
            REQUIRE (std::abs (data[sample]) < 1.0e-5f);
        }
    }
}

TEST_CASE ("Alignment: impulse stays aligned across sample rates", "[alignment][dsp][samplerate]")
{
    for (const auto sampleRate : { 44100.0, 96000.0 })
    {
        INFO ("sample rate = " << sampleRate);

        MiserereAudioProcessor processor;
        configureNeutralDirectBus (processor);
        setBool (processor, ParamIDs::busASolo, false);
        configureNeutralParallelBusses (processor);
        setBool (processor, ParamIDs::busDMute, true);

        processor.prepareToPlay (sampleRate, 2048);

        constexpr int impulseIndex = 64;
        juce::AudioBuffer<float> buffer (2, 2048);
        buffer.clear();
        buffer.setSample (0, impulseIndex, 0.25f);
        buffer.setSample (1, impulseIndex, 0.25f);

        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);

        CHECK (buffer.getSample (0, impulseIndex) == Catch::Approx (0.75f).margin (0.01));

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (sample != impulseIndex)
                REQUIRE (std::abs (buffer.getSample (0, sample)) < 1.0e-5f);
    }
}
