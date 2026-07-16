#include "AllocationGuard.h"
#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

// Real-time-safety guarantee: processBlock() must not touch the heap once
// prepareToPlay() has completed (docs/architecture.md, CLAUDE.md). Exercises
// every module by bringing all four busses up (including the Slap/Spread
// delay lines and the Sandwich/Opto path) before measuring.
namespace
{
    void setParam (MiserereAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    void bringUpAllBusses (MiserereAudioProcessor& processor)
    {
        setParam (processor, ParamIDs::crushLevel, 0.0f);
        setParam (processor, ParamIDs::sandLevel, 0.0f);
        setParam (processor, ParamIDs::spreadLevel, 0.0f);
        setParam (processor, ParamIDs::slapLevel, 0.0f);
        setParam (processor, ParamIDs::sandPeakRed, 60.0f);
        setParam (processor, ParamIDs::crushInput, 12.0f);
    }
}

TEST_CASE ("processBlock() performs zero heap allocations once prepared", "[robustness][realtime][allocation]")
{
    MiserereAudioProcessor processor;
    bringUpAllBusses (processor);
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 440.0, 0.5f);
    juce::MidiBuffer midi;

    // Warm up: the first couple of blocks may still touch caches/branch
    // predictors but must not allocate either - measured across all of
    // them, not just a "warm" one, since real-time safety has no grace
    // period.
    for (int warmup = 0; warmup < 4; ++warmup)
        processor.processBlock (buffer, midi);

    AllocationGuard::reset();

    for (int block = 0; block < 16; ++block)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 440.0, 0.5f, block * 512);
        processor.processBlock (buffer, midi);
    }

    CHECK (AllocationGuard::allocationCount() == 0);
}

TEST_CASE ("MiserereEngine::process() performs zero heap allocations once prepared", "[robustness][realtime][allocation]")
{
    MiserereEngine engine;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 512;
    spec.numChannels = 2;
    engine.prepare (spec);

    engine.setBusLevelDb (0, 0.0f);
    engine.setBusLevelDb (1, 0.0f);
    engine.setBusLevelDb (2, 0.0f);
    engine.setBusLevelDb (3, 0.0f);
    engine.setCrushInputDriveDb (12.0f);
    engine.setSandPeakReductionProportion (0.6f);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 440.0, 0.5f);

    for (int warmup = 0; warmup < 4; ++warmup)
    {
        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);
    }

    AllocationGuard::reset();

    for (int i = 0; i < 16; ++i)
    {
        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);
    }

    CHECK (AllocationGuard::allocationCount() == 0);
}
