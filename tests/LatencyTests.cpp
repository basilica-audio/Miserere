#include "PluginProcessor.h"
#include "dsp/MiserereEngine.h"

#include <catch2/catch_test_macros.hpp>

// M1 guarantee 10 (latency part): busses A-C are minimum-phase/causal with
// no lookahead, and Bus D's slap delay is the effect itself rather than a
// compensation artefact, so Miserere reports exactly 0 samples of latency
// at all times (see docs/adr/0003-parallel-bus-topology.md).
TEST_CASE ("getLatencySamples() reports zero latency, before and after prepareToPlay", "[latency]")
{
    MiserereAudioProcessor processor;

    CHECK (processor.getLatencySamples() == 0);

    processor.prepareToPlay (48000.0, 512);

    CHECK (processor.getLatencySamples() == 0);
    CHECK (MiserereEngine::getLatencySamples() == 0);
}

TEST_CASE ("Latency stays zero across sample-rate and block-size changes", "[latency]")
{
    MiserereAudioProcessor processor;

    processor.prepareToPlay (44100.0, 256);
    CHECK (processor.getLatencySamples() == 0);

    processor.prepareToPlay (96000.0, 1024);
    CHECK (processor.getLatencySamples() == 0);

    processor.prepareToPlay (192000.0, 32);
    CHECK (processor.getLatencySamples() == 0);
}
