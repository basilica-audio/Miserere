#include "PluginProcessor.h"
#include "dsp/MiserereEngine.h"

#include <catch2/catch_test_macros.hpp>

// Guarantee 9 (latency part): busses (1)/(2) are minimum-phase/causal with
// no lookahead, and busses (3)/(4)'s delays are the effects themselves
// rather than compensation artefacts, so Miserere reports exactly 0 samples
// of latency at all times (see docs/adr/0003-parallel-bus-topology.md).
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
