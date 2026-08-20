#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

// External sidechain tests (issue #23).
//
// The contract has two halves, and the second one is a HONESTY contract as
// much as a technical one:
//
//   1. The sidechain bus is optional and DISABLED by default, so the
//      default layout - the one auval and pluginval's default run see, and
//      the one every existing session carries - is byte-identical to the
//      pre-v0.7.0 plugin. Sidechain disabled/mono/stereo must all be
//      accepted alongside the existing mono/stereo main rule, and a mono key
//      into a stereo main path must work (a normal host routing).
//
//   2. CRUSH (`FetCrush`, whose rectifier is driven by y[n-1]) and SANDWICH
//      (`OptoLeveler`, whose EL panel is driven by its own compressed
//      output) are FEEDBACK topologies. An external key does not merely
//      re-source their detector - it REPLACES the loop drive and converts
//      them into feed-forward keyed compressors, with a measurably different
//      static curve. The tests below assert that difference exists rather
//      than pretending it does not; the manual and CHANGELOG say the same.
//      The Direct FET is feed-forward already, so keying it really is a pure
//      detector-source swap.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 512;

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

    juce::AudioProcessor::BusesLayout makeLayout (const juce::AudioChannelSet& main,
                                                  const juce::AudioChannelSet& sidechain)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (main);
        layout.inputBuses.add (sidechain);
        layout.outputBuses.add (main);
        return layout;
    }

    // Enables the sidechain bus and returns whether the host-side
    // negotiation succeeded.
    bool enableSidechain (MiserereAudioProcessor& processor, const juce::AudioChannelSet& sidechain)
    {
        return processor.setBusesLayout (makeLayout (juce::AudioChannelSet::stereo(), sidechain));
    }
}

TEST_CASE ("Sidechain bus: disabled by default, and the default layout is unchanged", "[sidechain][buses]")
{
    MiserereAudioProcessor processor;

    // Two input buses now exist, but the second one is OFF out of the box -
    // the default layout is exactly the pre-v0.7.0 stereo-in/stereo-out.
    REQUIRE (processor.getBusCount (true) == 2);
    CHECK (processor.getBusCount (false) == 1);

    const auto* sidechainBus = processor.getBus (true, 1);
    REQUIRE (sidechainBus != nullptr);
    CHECK (sidechainBus->getName() == "Sidechain");
    CHECK_FALSE (sidechainBus->isEnabled());

    CHECK (processor.getMainBusNumInputChannels() == 2);
    CHECK (processor.getMainBusNumOutputChannels() == 2);
    CHECK (processor.getTotalNumInputChannels() == 2);
}

TEST_CASE ("Sidechain bus: layouts accepted are exactly disabled / mono / stereo", "[sidechain][buses]")
{
    MiserereAudioProcessor processor;

    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();
    const auto disabled = juce::AudioChannelSet::disabled();

    // The pre-existing main-bus rule is untouched: mono->mono and
    // stereo->stereo only, with or without a key.
    for (const auto& main : { mono, stereo })
        for (const auto& key : { disabled, mono, stereo })
        {
            INFO ("main = " << main.getDescription() << ", sidechain = " << key.getDescription());
            CHECK (processor.checkBusesLayoutSupported (makeLayout (main, key)));
        }

    // Mismatched main in/out still refused.
    {
        juce::AudioProcessor::BusesLayout mismatched;
        mismatched.inputBuses.add (mono);
        mismatched.inputBuses.add (disabled);
        mismatched.outputBuses.add (stereo);
        CHECK_FALSE (processor.checkBusesLayoutSupported (mismatched));
    }

    // A wider key than stereo is refused rather than silently half-used.
    CHECK_FALSE (processor.checkBusesLayoutSupported (makeLayout (stereo, juce::AudioChannelSet::create5point1())));
}

TEST_CASE ("Sidechain: with the bus disabled every key switch is a no-op", "[sidechain][dsp]")
{
    MiserereAudioProcessor processor;

    setBool (processor, ParamIDs::directFetEnabled, true);
    setBool (processor, ParamIDs::directFetKeyExt, true);
    setBool (processor, ParamIDs::crushKeyExt, true);
    setBool (processor, ParamIDs::sandKeyExt, true);
    setParam (processor, ParamIDs::crushLevel, 0.0f);
    setParam (processor, ParamIDs::sandLevel, 0.0f);

    processor.prepareToPlay (testSampleRate, testBlockSize);

    // No sidechain bus enabled: the key switches must degrade silently to
    // internal detection, not to silence and not to a crash.
    juce::AudioBuffer<float> buffer (2, testBlockSize);
    juce::MidiBuffer midi;

    for (int block = 0; block < 8; ++block)
    {
        TestHelpers::fillWithSine (buffer, testSampleRate, 220.0, 0.5f, block * testBlockSize);
        processor.processBlock (buffer, midi);

        CHECK (TestHelpers::allSamplesFinite (buffer));
    }

    CHECK (TestHelpers::rms (buffer) > 0.05);
}

TEST_CASE ("Sidechain: a stereo key drives CRUSH and SANDWICH while the audio itself is quiet", "[sidechain][dsp]")
{
    MiserereAudioProcessor processor;

    REQUIRE (enableSidechain (processor, juce::AudioChannelSet::stereo()));
    REQUIRE (processor.getTotalNumInputChannels() == 4);

    setBool (processor, ParamIDs::crushKeyExt, true);
    setBool (processor, ParamIDs::sandKeyExt, true);
    setParam (processor, ParamIDs::crushInput, 30.0f);
    setParam (processor, ParamIDs::sandPeakRed, 80.0f);
    setParam (processor, ParamIDs::crushLevel, 0.0f);
    setParam (processor, ParamIDs::sandLevel, 0.0f);

    processor.prepareToPlay (testSampleRate, testBlockSize);

    // Main audio deliberately far too quiet to trip either detector on its
    // own; the key is hot. Any gain reduction seen here can ONLY have come
    // from the external key.
    juce::AudioBuffer<float> buffer (4, testBlockSize);
    juce::MidiBuffer midi;

    for (int block = 0; block < 24; ++block)
    {
        juce::AudioBuffer<float> audio (buffer.getArrayOfWritePointers(), 2, testBlockSize);
        juce::AudioBuffer<float> key (buffer.getArrayOfWritePointers() + 2, 2, testBlockSize);

        TestHelpers::fillWithSine (audio, testSampleRate, 220.0, 0.002f, block * testBlockSize);
        TestHelpers::fillWithSine (key, testSampleRate, 220.0, 0.9f, block * testBlockSize);

        processor.processBlock (buffer, midi);
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }

    CHECK (processor.getCrushGainReductionDb() > 6.0f);
    CHECK (processor.getSandGainReductionDb() > 1.0f);

    // The key channels themselves are an INPUT bus - the plugin must not
    // have written its output over them.
    const juce::AudioBuffer<float> key (const_cast<float**> (buffer.getArrayOfReadPointers()) + 2, 2, testBlockSize);
    CHECK (TestHelpers::peakAbsolute (key) == Catch::Approx (0.9f).margin (1.0e-3));
}

TEST_CASE ("Sidechain: a MONO key into a stereo main path keys both channels", "[sidechain][dsp]")
{
    MiserereAudioProcessor processor;

    REQUIRE (enableSidechain (processor, juce::AudioChannelSet::mono()));
    REQUIRE (processor.getTotalNumInputChannels() == 3);

    setBool (processor, ParamIDs::crushKeyExt, true);
    setParam (processor, ParamIDs::crushInput, 30.0f);
    setParam (processor, ParamIDs::crushLevel, 0.0f);
    setBool (processor, ParamIDs::sandMute, true);
    setBool (processor, ParamIDs::spreadMute, true);
    setBool (processor, ParamIDs::slapMute, true);

    processor.prepareToPlay (testSampleRate, testBlockSize);

    juce::AudioBuffer<float> buffer (3, testBlockSize);
    juce::MidiBuffer midi;

    for (int block = 0; block < 24; ++block)
    {
        juce::AudioBuffer<float> audio (buffer.getArrayOfWritePointers(), 2, testBlockSize);
        juce::AudioBuffer<float> key (buffer.getArrayOfWritePointers() + 2, 1, testBlockSize);

        TestHelpers::fillWithSine (audio, testSampleRate, 220.0, 0.002f, block * testBlockSize);
        TestHelpers::fillWithSine (key, testSampleRate, 220.0, 0.9f, block * testBlockSize);

        // getBusBuffer() is the API the processor itself uses to find the
        // key - assert it resolves to exactly the one channel we filled, so
        // this test fails loudly if the bus indexing ever slips.
        const auto resolvedKey = processor.getBusBuffer (buffer, true, 1);
        REQUIRE (resolvedKey.getNumChannels() == 1);
        CHECK (resolvedKey.getReadPointer (0) == buffer.getReadPointer (2));

        processor.processBlock (buffer, midi);
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }

    // One key channel, both audio channels keyed off it (the module reuses
    // the last key channel).
    CHECK (processor.getCrushGainReductionDb() > 6.0f);
}

TEST_CASE ("Sidechain: keying CRUSH converts a FEEDBACK compressor into a feed-forward one - and it is audibly different", "[sidechain][dsp][topology]")
{
    // The honest part of issue #23. `FetCrush` drives its rectifier from
    // y[n-1]; an external key REPLACES that drive. A feedback detector sees
    // the already-reduced output and therefore backs itself off, while a
    // feed-forward keyed detector sees the full-scale key and bites harder.
    // Feeding the module's OWN INPUT as the key is the fairest possible
    // comparison - same signal, same settings, only the topology differs -
    // and the curves still separate.
    const auto measureCrushGainReductionDb = [] (bool keyed, float driveDb = 24.0f, float amplitude = 0.35f)
    {
        MiserereAudioProcessor processor;

        REQUIRE (enableSidechain (processor, juce::AudioChannelSet::stereo()));

        setBool (processor, ParamIDs::crushKeyExt, keyed);
        setParam (processor, ParamIDs::crushInput, driveDb);
        setParam (processor, ParamIDs::crushLevel, 0.0f);

        processor.prepareToPlay (testSampleRate, testBlockSize);

        juce::AudioBuffer<float> buffer (4, testBlockSize);
        juce::MidiBuffer midi;

        for (int block = 0; block < 32; ++block)
        {
            juce::AudioBuffer<float> audio (buffer.getArrayOfWritePointers(), 2, testBlockSize);
            juce::AudioBuffer<float> key (buffer.getArrayOfWritePointers() + 2, 2, testBlockSize);

            TestHelpers::fillWithSine (audio, testSampleRate, 220.0, amplitude, block * testBlockSize);
            TestHelpers::fillWithSine (key, testSampleRate, 220.0, amplitude, block * testBlockSize);

            processor.processBlock (buffer, midi);
        }

        return processor.getCrushGainReductionDb();
    };

    // 0 dB input drive at -22 dBFS: both modes are working but neither is
    // anywhere near the module's ~30 dB rail, so the comparison is about
    // the curve rather than about two clipped detectors.
    const auto internalGrDb = measureCrushGainReductionDb (false, 0.0f, 0.08f);
    const auto keyedGrDb = measureCrushGainReductionDb (true, 0.0f, 0.08f);

    INFO ("CRUSH gain reduction: internal (feedback) = " << internalGrDb
          << " dB, keyed (feed-forward) = " << keyedGrDb << " dB");

    // Both are genuinely working, and neither is railed...
    CHECK (internalGrDb > 1.0f);
    CHECK (keyedGrDb > 1.0f);
    CHECK (keyedGrDb < 29.0f);

    // ...and the feed-forward version bites far deeper on the same signal at
    // the same settings (measured ~2.7 dB internal vs ~20.7 dB keyed): the
    // feedback detector sees the reduced output and backs itself off, the
    // keyed one sees the full-scale key and does not. This is the documented
    // consequence of replacing the loop drive, not a bug - which is why the
    // manual describes the keyed modes as a different compressor rather than
    // as the same one listening elsewhere.
    CHECK (keyedGrDb > internalGrDb + 6.0f);
}

TEST_CASE ("Sidechain: keying SANDWICH's opto leveler is the same feedback-to-feed-forward conversion", "[sidechain][dsp][topology]")
{
    const auto measureSandGainReductionDb = [] (bool keyed)
    {
        MiserereAudioProcessor processor;

        REQUIRE (enableSidechain (processor, juce::AudioChannelSet::stereo()));

        setBool (processor, ParamIDs::sandKeyExt, keyed);
        setParam (processor, ParamIDs::sandPeakRed, 70.0f);
        setParam (processor, ParamIDs::sandLevel, 0.0f);

        processor.prepareToPlay (testSampleRate, testBlockSize);

        juce::AudioBuffer<float> buffer (4, testBlockSize);
        juce::MidiBuffer midi;

        for (int block = 0; block < 48; ++block)
        {
            juce::AudioBuffer<float> audio (buffer.getArrayOfWritePointers(), 2, testBlockSize);
            juce::AudioBuffer<float> key (buffer.getArrayOfWritePointers() + 2, 2, testBlockSize);

            TestHelpers::fillWithSine (audio, testSampleRate, 220.0, 0.35f, block * testBlockSize);
            TestHelpers::fillWithSine (key, testSampleRate, 220.0, 0.35f, block * testBlockSize);

            processor.processBlock (buffer, midi);
        }

        return processor.getSandGainReductionDb();
    };

    const auto internalGrDb = measureSandGainReductionDb (false);
    const auto keyedGrDb = measureSandGainReductionDb (true);

    INFO ("SANDWICH gain reduction: internal (feedback) = " << internalGrDb
          << " dB, keyed (feed-forward) = " << keyedGrDb << " dB");

    CHECK (internalGrDb > 0.5f);
    CHECK (keyedGrDb > internalGrDb + 0.5f);
}

TEST_CASE ("Sidechain: keying the Direct FET is a pure detector-source swap (it is feed-forward already)", "[sidechain][dsp][topology]")
{
    MiserereAudioProcessor processor;

    REQUIRE (enableSidechain (processor, juce::AudioChannelSet::stereo()));

    setBool (processor, ParamIDs::directFetEnabled, true);
    setBool (processor, ParamIDs::directFetKeyExt, true);
    setParam (processor, ParamIDs::directFetThreshold, -30.0f);

    processor.prepareToPlay (testSampleRate, testBlockSize);

    juce::AudioBuffer<float> buffer (4, testBlockSize);
    juce::MidiBuffer midi;

    // Silent key, loud audio: a feed-forward detector looking at the key
    // sees nothing, so the compressor must stay open however hot the audio
    // is. (With internal detection this signal would compress hard.)
    for (int block = 0; block < 16; ++block)
    {
        juce::AudioBuffer<float> audio (buffer.getArrayOfWritePointers(), 2, testBlockSize);
        juce::AudioBuffer<float> key (buffer.getArrayOfWritePointers() + 2, 2, testBlockSize);

        TestHelpers::fillWithSine (audio, testSampleRate, 220.0, 0.8f, block * testBlockSize);
        key.clear();

        processor.processBlock (buffer, midi);
    }

    CHECK (processor.getDirectFetGainReductionDb() == Catch::Approx (0.0f).margin (0.01f));

    // ...and with the key hot instead, it closes.
    for (int block = 0; block < 16; ++block)
    {
        juce::AudioBuffer<float> audio (buffer.getArrayOfWritePointers(), 2, testBlockSize);
        juce::AudioBuffer<float> key (buffer.getArrayOfWritePointers() + 2, 2, testBlockSize);

        TestHelpers::fillWithSine (audio, testSampleRate, 220.0, 0.8f, block * testBlockSize);
        TestHelpers::fillWithSine (key, testSampleRate, 220.0, 0.8f, block * testBlockSize);

        processor.processBlock (buffer, midi);
    }

    CHECK (processor.getDirectFetGainReductionDb() > 3.0f);
}

TEST_CASE ("Sidechain: an enabled key bus does not disturb the default-wire invariant or the reported latency", "[sidechain][dsp]")
{
    MiserereAudioProcessor processor;

    REQUIRE (enableSidechain (processor, juce::AudioChannelSet::stereo()));

    // Every bus muted, direct path at its bit-transparent defaults, no key
    // switches engaged: the plugin must still be a wire, with a hot key
    // present and doing nothing.
    setBool (processor, ParamIDs::crushMute, true);
    setBool (processor, ParamIDs::sandMute, true);
    setBool (processor, ParamIDs::spreadMute, true);
    setBool (processor, ParamIDs::slapMute, true);

    processor.prepareToPlay (testSampleRate, testBlockSize);

    CHECK (processor.getLatencySamples() == 0);

    juce::AudioBuffer<float> buffer (4, testBlockSize);
    juce::MidiBuffer midi;

    // Settle the 3 ms route ramps first.
    for (int block = 0; block < 4; ++block)
    {
        juce::AudioBuffer<float> audio (buffer.getArrayOfWritePointers(), 2, testBlockSize);
        juce::AudioBuffer<float> key (buffer.getArrayOfWritePointers() + 2, 2, testBlockSize);

        TestHelpers::fillWithSine (audio, testSampleRate, 220.0, 0.5f, block * testBlockSize);
        TestHelpers::fillWithSine (key, testSampleRate, 3000.0, 0.9f, block * testBlockSize);

        processor.processBlock (buffer, midi);
    }

    juce::AudioBuffer<float> audio (buffer.getArrayOfWritePointers(), 2, testBlockSize);
    juce::AudioBuffer<float> key (buffer.getArrayOfWritePointers() + 2, 2, testBlockSize);

    TestHelpers::fillWithSine (audio, testSampleRate, 220.0, 0.5f, 4 * testBlockSize);
    TestHelpers::fillWithSine (key, testSampleRate, 3000.0, 0.9f, 4 * testBlockSize);

    const juce::AudioBuffer<float> reference (audio);

    processor.processBlock (buffer, midi);

    // The key is at a completely different frequency, so any leakage would
    // be obvious rather than masked.
    CHECK (TestHelpers::maxDifferenceDbfs (audio, reference) < -120.0);
}
