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

//==============================================================================
// Brief section 6.12 - the guard must also cover the v0.5.0 paths that only
// come alive at non-neutral settings: the F5 flutter generator and age noise
// layer, the F3/F4 feedback engines' per-sample ODE iteration, and the F2
// matched-coefficient recomputation (which runs at block rate and is exactly
// where a stray Coefficients::make* would hide).
TEST_CASE ("v0.5.0 paths allocate nothing: flutter, age noise, feedback engines, matched coefficients",
           "[robustness][realtime][allocation][sota]")
{
    MiserereAudioProcessor processor;
    bringUpAllBusses (processor);

    // F5: wow/flutter + age noise both structurally engaged.
    setParam (processor, ParamIDs::slapWobble, 100.0f);
    setParam (processor, ParamIDs::slapAge, 100.0f);
    setParam (processor, ParamIDs::slapTone, 100.0f);

    // F3/F4: both feedback engines working hard.
    setParam (processor, ParamIDs::crushInput, 36.0f);
    setParam (processor, ParamIDs::sandPeakRed, 90.0f);
    setParam (processor, ParamIDs::sandEmphasis, 100.0f);

    // F2/F8: matched shelves and the flux-domain iron term off their
    // neutral bypasses, so coefficients really are recomputed each block.
    setParam (processor, ParamIDs::directEqHighGain, 12.0f);
    setParam (processor, ParamIDs::directEqDrive, 18.0f);
    setParam (processor, ParamIDs::directSatDrive, 12.0f);
    setParam (processor, ParamIDs::sandPreHfBellBoost, 8.0f);
    setParam (processor, ParamIDs::sandPreLfBoost, 8.0f);
    setParam (processor, ParamIDs::sandPreLfCut, 8.0f);

    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    for (int warmup = 0; warmup < 4; ++warmup)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 220.0, 0.6f, warmup * 512);
        processor.processBlock (buffer, midi);
    }

    AllocationGuard::reset();

    for (int block = 0; block < 32; ++block)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 220.0, 0.6f, block * 512);
        processor.processBlock (buffer, midi);
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }

    CHECK (AllocationGuard::allocationCount() == 0);
}
