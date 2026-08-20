#include "AllocationGuard.h"
#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <new>

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

//==============================================================================
// Suite-wide hardening wave (2026-07-31): AllocationGuard's own mechanism was
// never self-verified anywhere in this file (or elsewhere in the suite) -
// every TEST_CASE below only ever checks `allocationCount() == 0`, which
// would read exactly the same whether the guard is genuinely observing zero
// allocations or has been silently broken (e.g. by a future edit to the
// replaced operator new in AllocationGuard.h). This closes that gap.
//
// The canary allocation deliberately goes through a direct call to
// ::operator new, not a `new`/`delete` expression. [expr.new] explicitly
// permits an implementation to omit the allocation of a new-expression
// whose storage is never observably used, and Clang/MSVC do exactly that at
// higher optimisation levels - the exact defect class already found and
// fixed in sibling plugins (Requiem's tests/EngineTests.cpp:481). A direct
// ::operator new call is a plain function call, so that elision permission
// does not apply, and the volatile write forces the returned storage to be
// observably used. The assertion itself reads allocationCount() only after
// the allocate/deallocate pair has fully completed, rather than from inside
// a Catch2 CHECK()/REQUIRE() invocation - sibling repos found that Catch2's
// own assertion macros are not guaranteed allocation-free, which would
// otherwise pollute the very count this test exists to verify.
TEST_CASE ("AllocationGuard: the guard itself fires on ordinary heap allocations "
           "and stays silent on pure stack/register arithmetic",
           "[robustness][realtime][allocation][self-test]")
{
    AllocationGuard::reset();

    std::size_t countAfterAllocation = 0;

    {
        AllocationGuard::Scope scope;

        auto* deliberate = static_cast<int*> (::operator new (sizeof (int)));
        *static_cast<volatile int*> (deliberate) = 7;
        ::operator delete (deliberate);

        countAfterAllocation = AllocationGuard::allocationCount();
    }

    CHECK (countAfterAllocation >= 1);

    AllocationGuard::reset();

    std::size_t countAfterArithmetic = 0;

    {
        AllocationGuard::Scope scope;
        volatile auto sum = 0.0f;

        for (int i = 0; i < 1000; ++i)
            sum = sum + static_cast<float> (i);

        countAfterArithmetic = AllocationGuard::allocationCount();
    }

    CHECK (countAfterArithmetic == 0);
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

//==============================================================================
// Issue #20 - the per-slot dynamics colours: swapping a colour mid-stream
// re-targets voicing tuples (FetCrush's 10 ms crossfade, FetCompressor's
// smoothed knee/harmonic ramps, OptoLeveler's state-preserving kinetics
// update + smoothed colour drive). None of that may touch the heap - the
// engine setters are the exact calls updateEngineParameters() makes from
// processBlock() when the host automates a colour choice.
TEST_CASE ("Colour swaps allocate nothing mid-stream (crush style, FET character, sand colour)",
           "[robustness][realtime][allocation][colour]")
{
    MiserereEngine engine;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 512;
    spec.numChannels = 2;
    engine.prepare (spec);

    engine.setBusLevelDb (0, 0.0f);
    engine.setBusLevelDb (1, 0.0f);
    engine.setCrushInputDriveDb (18.0f);
    engine.setSandPeakReductionProportion (0.6f);
    engine.setDirectFetEnabled (true);
    engine.setDirectFetThresholdDb (-30.0f);

    juce::AudioBuffer<float> buffer (2, 512);

    for (int warmup = 0; warmup < 4; ++warmup)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 440.0, 0.5f, warmup * 512);
        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);
    }

    static constexpr FetCrush::Style styles[] = { FetCrush::Style::vintage, FetCrush::Style::gentle,
                                                  FetCrush::Style::allButtons };
    static constexpr FetCompressor::Character characters[] = { FetCompressor::Character::vca,
                                                               FetCompressor::Character::tubeMu,
                                                               FetCompressor::Character::fet };
    static constexpr OptoLeveler::Colour colours[] = { OptoLeveler::Colour::quick, OptoLeveler::Colour::deep,
                                                       OptoLeveler::Colour::classic };

    AllocationGuard::reset();

    for (int block = 0; block < 24; ++block)
    {
        // A colour change every few blocks, mid-crossfade included.
        engine.setCrushStyle (styles[(block / 3) % 3]);
        engine.setDirectFetCharacter (characters[(block / 3) % 3]);
        engine.setSandColour (colours[(block / 3) % 3]);

        TestHelpers::fillWithSine (buffer, 48000.0, 440.0, 0.5f, block * 512);
        juce::dsp::AudioBlock<float> audioBlock (buffer);
        engine.process (audioBlock);
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }

    CHECK (AllocationGuard::allocationCount() == 0);
}
