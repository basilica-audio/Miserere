#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

// Issue #41: bypass must be (1) click-free - a cross-fade between the
// continuously-running wet chain and a dry copy, not a hard switch - and (2)
// latency-compensated - the dry path delayed by exactly getLatencySamples()
// so a bypassed instance nulls against a dry track under a host's plugin
// delay compensation. See PluginProcessor.h's bypassDryDelay/bypassWetMix
// member docs for the mechanism, and basilica-audio/Crypta#87 for the same
// defect and the same fix in the sibling plugin this one is ported from.
//
// Miserere reports ZERO latency at all times by design (docs/adr/0003, and
// tests/LatencyTests.cpp pins it), so the latency half of the issue is not a
// live defect here the way it was in Crypta - the dirac and null tests below
// therefore pass both before and after the fix. They are kept, and phrased
// against getLatencySamples() rather than against the literal 0, because the
// invariant being asserted is "the dry path is delayed by exactly the
// reported latency": the day any bus stops being zero-latency, these are what
// stop the bypass path from silently becoming a phase bug.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 256;
    constexpr int testBlocks = 40;

    // 110 Hz, 0.5 amplitude: low enough that the probe's own steady-state
    // slew is tiny compared with any switching click, and well inside every
    // module's working range.
    constexpr double probeFrequencyHz = 110.0;
    constexpr float probeAmplitude = 0.5f;

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

    // A fresh Miserere is bit-transparent by default (the core v2 invariant:
    // every direct-path section off, every return fader at its floor), so a
    // default instance's wet and dry signals are identical and bypass could
    // not click even if it wanted to. Every click measurement below therefore
    // has to bring the plugin somewhere real first - this is the same
    // configuration tests/RobustnessTests.cpp's bringUpAllBusses() uses.
    void bringUpAllBusses (MiserereAudioProcessor& processor)
    {
        setParam (processor, ParamIDs::crushLevel, 0.0f);
        setParam (processor, ParamIDs::sandLevel, 0.0f);
        setParam (processor, ParamIDs::spreadLevel, 0.0f);
        setParam (processor, ParamIDs::slapLevel, 0.0f);
        setParam (processor, ParamIDs::crushInput, 24.0f);
        setParam (processor, ParamIDs::sandPeakRed, 60.0f);
        setBool (processor, ParamIDs::directFetEnabled, true);
        setBool (processor, ParamIDs::directEqHpfEnabled, true);
        setBool (processor, ParamIDs::directDeessPreEnabled, true);
        setBool (processor, ParamIDs::directDeessPostEnabled, true);
    }

    // Renders `buffer` in place through `processor` in blocks of `blockSize`.
    void renderThrough (MiserereAudioProcessor& processor, juce::AudioBuffer<float>& buffer, int blockSize)
    {
        juce::AudioBuffer<float> block (buffer.getNumChannels(), blockSize);
        juce::MidiBuffer midi;

        for (int offset = 0; offset < buffer.getNumSamples(); offset += blockSize)
        {
            const auto length = juce::jmin (blockSize, buffer.getNumSamples() - offset);

            block.clear();

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                block.copyFrom (channel, 0, buffer, channel, offset, length);

            processor.processBlock (block, midi);

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.copyFrom (channel, offset, block, channel, 0, length);
        }
    }

    float maxSampleStep (const juce::AudioBuffer<float>& buffer, int fromSample, int toSampleExclusive)
    {
        float maximum = 0.0f;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = juce::jmax (1, fromSample); sample < toSampleExclusive; ++sample)
                maximum = juce::jmax (maximum, std::abs (data[sample] - data[sample - 1]));
        }

        return maximum;
    }

    // Renders `testBlocks` blocks of a continuous-phase sine, toggling bypass
    // to `bypassOnAtToggle` immediately before block index `toggleAtBlock`.
    // Returns the whole concatenated render, so the transition itself sits
    // inside the measured region.
    juce::AudioBuffer<float> renderBypassToggle (bool startBypassed, bool bypassOnAtToggle, int toggleAtBlock)
    {
        MiserereAudioProcessor processor;
        bringUpAllBusses (processor);
        setBool (processor, ParamIDs::bypass, startBypassed);
        processor.setPlayConfigDetails (2, 2, testSampleRate, testBlockSize);
        processor.prepareToPlay (testSampleRate, testBlockSize);

        juce::AudioBuffer<float> render (2, testBlocks * testBlockSize);
        render.clear();

        juce::AudioBuffer<float> block (2, testBlockSize);
        juce::MidiBuffer midi;

        for (int i = 0; i < testBlocks; ++i)
        {
            if (i == toggleAtBlock)
                setBool (processor, ParamIDs::bypass, bypassOnAtToggle);

            TestHelpers::fillWithSine (block, testSampleRate, probeFrequencyHz, probeAmplitude,
                                       static_cast<juce::int64> (i) * testBlockSize);
            processor.processBlock (block, midi);

            for (int channel = 0; channel < 2; ++channel)
                render.copyFrom (channel, i * testBlockSize, block, channel, 0, testBlockSize);
        }

        return render;
    }
}

TEST_CASE ("Bypass: engaging mid-render does not step further than either steady state", "[bypass][dsp]")
{
    constexpr int toggleAtBlock = 20;
    const auto render = renderBypassToggle (false, true, toggleAtBlock);

    // Steady state before the toggle (skipping the first blocks so the
    // engine's own turn-on transient has settled) and after it (skipping
    // enough blocks for the crossfade ramp to have fully run out).
    const auto steadyBeforeStep = maxSampleStep (render, 6 * testBlockSize, toggleAtBlock * testBlockSize);
    const auto steadyAfterStep = maxSampleStep (render, (toggleAtBlock + 6) * testBlockSize, testBlocks * testBlockSize);
    const auto transitionStep = maxSampleStep (render, toggleAtBlock * testBlockSize, (toggleAtBlock + 6) * testBlockSize);

    INFO ("steady-state (wet) max step  = " << steadyBeforeStep);
    INFO ("steady-state (dry) max step  = " << steadyAfterStep);
    INFO ("transition max step          = " << transitionStep);

    CHECK (TestHelpers::allSamplesFinite (render));

    // The scale-free "no click" bound: the transition must not step further
    // than a small multiple of either endpoint's own steady-state slew. This
    // compares the plugin against itself rather than against an absolute
    // limit, which is what makes it meaningful on a signal whose legitimate
    // waveform already contains fast edges.
    const auto ownSlewBound = 4.0f * juce::jmax (steadyBeforeStep, steadyAfterStep);
    CHECK (transitionStep <= ownSlewBound);
}

TEST_CASE ("Bypass: disengaging mid-render does not step further than either steady state", "[bypass][dsp]")
{
    constexpr int toggleAtBlock = 20;
    const auto render = renderBypassToggle (true, false, toggleAtBlock);

    const auto steadyBeforeStep = maxSampleStep (render, 6 * testBlockSize, toggleAtBlock * testBlockSize);
    const auto steadyAfterStep = maxSampleStep (render, (toggleAtBlock + 6) * testBlockSize, testBlocks * testBlockSize);
    const auto transitionStep = maxSampleStep (render, toggleAtBlock * testBlockSize, (toggleAtBlock + 6) * testBlockSize);

    INFO ("steady-state (dry) max step  = " << steadyBeforeStep);
    INFO ("steady-state (wet) max step  = " << steadyAfterStep);
    INFO ("transition max step          = " << transitionStep);

    CHECK (TestHelpers::allSamplesFinite (render));

    const auto ownSlewBound = 4.0f * juce::jmax (steadyBeforeStep, steadyAfterStep);
    CHECK (transitionStep <= ownSlewBound);
}

TEST_CASE ("Bypass: reported latency does not change when bypass is engaged", "[bypass][latency][dsp]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (testSampleRate, testBlockSize);

    const auto latencyBeforeBypass = processor.getLatencySamples();

    setBool (processor, ParamIDs::bypass, true);

    // Engaging bypass is a parameter change, not a prepareToPlay() call - it
    // must never re-derive or change the reported figure (a latency change
    // mid-stream is something hosts handle very poorly). Miserere's figure is
    // 0 at all times (docs/adr/0003), so this also pins that the crossfade
    // machinery did not quietly introduce reported latency of its own.
    CHECK (processor.getLatencySamples() == latencyBeforeBypass);
    CHECK (processor.getLatencySamples() == 0);
}

TEST_CASE ("Bypass: a dirac arrives exactly at the reported latency while bypassed", "[bypass][latency][dsp]")
{
    // If a bypassed instance does not delay the dry signal by exactly
    // getLatencySamples(), a host's plugin delay compensation pulls its
    // output early relative to every other (non-bypassed) track and a null
    // test against a dry copy does not null. Miserere reports 0, so the
    // expected peak index is 0 - asserted against getLatencySamples() rather
    // than the literal, so the assertion keeps its meaning if that ever
    // changes.
    MiserereAudioProcessor processor;
    setBool (processor, ParamIDs::bypass, true);
    processor.setPlayConfigDetails (1, 1, testSampleRate, testBlockSize);

    // prepareToPlay() primes the crossfade straight to its (bypassed) target
    // rather than leaving a ramp in flight, which would otherwise blend a
    // genuinely-bypassed dirac with a genuinely-wet one and smear the peak
    // this test is about to look for.
    processor.prepareToPlay (testSampleRate, testBlockSize);
    processor.reset();

    const auto reportedLatency = processor.getLatencySamples();

    juce::AudioBuffer<float> buffer (1, 8192);
    buffer.clear();
    buffer.setSample (0, 0, 1.0f);

    renderThrough (processor, buffer, testBlockSize);

    int peakIndex = 0;
    float peakValue = 0.0f;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto magnitude = std::abs (buffer.getSample (0, sample));

        if (magnitude > peakValue)
        {
            peakValue = magnitude;
            peakIndex = sample;
        }
    }

    INFO ("reported latency = " << reportedLatency << ", dirac peak at " << peakIndex);
    CHECK (peakIndex == reportedLatency);
    CHECK (peakValue == Catch::Approx (1.0f).margin (1e-3));
}

TEST_CASE ("Bypass: settled bypass nulls against a dry copy delayed by the reported latency",
           "[bypass][latency][dsp]")
{
    // A settled bypass must be the input, shifted by exactly the reported
    // latency - not the input shifted by something else, and not a blend that
    // never quite arrives. Every bus is brought up first so that "the wet
    // chain is still running underneath" is a real condition rather than a
    // no-op on a bit-transparent default instance.
    MiserereAudioProcessor processor;
    bringUpAllBusses (processor);
    setBool (processor, ParamIDs::bypass, true);
    processor.setPlayConfigDetails (1, 1, testSampleRate, testBlockSize);
    processor.prepareToPlay (testSampleRate, testBlockSize);
    processor.reset();

    const auto latency = processor.getLatencySamples();

    constexpr int numSamples = 8192;
    juce::AudioBuffer<float> dry (1, numSamples);
    TestHelpers::fillWithSine (dry, testSampleRate, probeFrequencyHz, probeAmplitude);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (dry);

    renderThrough (processor, processed, testBlockSize);

    double sumSquaredError = 0.0;
    double sumSquaredReference = 0.0;
    const auto checkFrom = numSamples / 2; // deep in the settled region

    for (int n = checkFrom; n < numSamples; ++n)
    {
        const auto dryDelayed = (n - latency >= 0) ? dry.getSample (0, n - latency) : 0.0f;
        const auto error = processed.getSample (0, n) - dryDelayed;

        sumSquaredError += static_cast<double> (error) * static_cast<double> (error);
        sumSquaredReference += static_cast<double> (dryDelayed) * static_cast<double> (dryDelayed);
    }

    REQUIRE (sumSquaredReference > 0.0);

    const auto nullDepthDb = 10.0 * std::log10 (juce::jmax (1.0e-30, sumSquaredError / sumSquaredReference));
    INFO ("null depth = " << nullDepthDb << " dB (lower is a better null)");

    // A synthetic float render with no dither at all, so a far tighter bound
    // than the -80 dB a real-world null test settles at is fair.
    CHECK (nullDepthDb < -100.0);
}

TEST_CASE ("Bypass: a non-finite input stays finite on the dry path, bypassed or not", "[bypass][nan][dsp]")
{
    // The dry path goes around MiserereEngine by definition, so it goes
    // around the engine's own final-sum NaN/Inf sanitiser too - it is the one
    // route by which a non-finite input sample could reach the output
    // (docs/architecture.md's NaN/Inf policy is unconditional about the
    // output side). Worse, a non-finite dry sample multiplied by a zero blend
    // gain is NaN, so an unsanitised dry copy would poison the output even
    // with bypass fully DISENGAGED - which is why processBlock() sanitises
    // the snapshot on the way in rather than after the blend.
    //
    // The bypassed half of this is a strict improvement over the pre-fix
    // behaviour, where bypass returned early and handed a NaN straight back
    // to the host untouched.
    for (const auto bypassed : { false, true })
    {
        INFO ("bypassed = " << (bypassed ? "true" : "false"));

        MiserereAudioProcessor processor;
        bringUpAllBusses (processor);
        setBool (processor, ParamIDs::bypass, bypassed);
        processor.setPlayConfigDetails (2, 2, testSampleRate, testBlockSize);
        processor.prepareToPlay (testSampleRate, testBlockSize);

        juce::AudioBuffer<float> buffer (2, testBlockSize);
        juce::MidiBuffer midi;

        for (int channel = 0; channel < 2; ++channel)
        {
            auto* data = buffer.getWritePointer (channel);

            for (int sample = 0; sample < testBlockSize; ++sample)
                data[sample] = sample % 3 == 0 ? std::numeric_limits<float>::quiet_NaN()
                             : sample % 3 == 1 ? std::numeric_limits<float>::infinity()
                                               : 0.5f;
        }

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));

        // And the delay line must not have kept a copy to echo back out
        // later: a clean block after a poisoned one has to come out clean.
        TestHelpers::fillWithSine (buffer, testSampleRate, probeFrequencyHz, probeAmplitude, testBlockSize);
        processor.processBlock (buffer, midi);
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Bypass: the wet chain keeps running while bypassed, so its state never goes stale",
           "[bypass][dsp]")
{
    // The early-return implementation froze every module's internal state
    // (filter memory, envelope followers, both delay-line busses) the instant
    // bypass engaged - MiserereEngine::process() was never called at all while
    // bypassed. Fed the SAME continuous input throughout, a run that spends a
    // while bypassed in the middle and a run that never bypasses would
    // therefore end up in DIFFERENT internal states once bypass disengages:
    // the never-bypassed run's state kept evolving with the live signal, the
    // bypassed run's sat frozen at whatever it was when bypass engaged. That
    // divergence is exactly what surfaces as a click on the way OUT of bypass,
    // and it is a defect the click bound above cannot see, because the click
    // it causes is proportional to how far the state drifted.
    //
    // With the wet chain always running, both runs process identical input at
    // every sample regardless of what the bypass flag or the output crossfade
    // are doing - the blend only touches the final output, it never feeds back
    // into anything - so once the crossfade has settled back to fully wet the
    // two runs must agree to numerical precision.
    constexpr int bypassBlocks = 30;
    constexpr int postBypassSettleBlocks = 8; // several multiples of the 20 ms ramp
    constexpr int totalBlocks = 6 /* pre-settle */ + bypassBlocks + postBypassSettleBlocks + 4;

    const auto renderWithMidBypass = [] (bool engageBypass)
    {
        MiserereAudioProcessor processor;
        bringUpAllBusses (processor);
        processor.setPlayConfigDetails (2, 2, testSampleRate, testBlockSize);
        processor.prepareToPlay (testSampleRate, testBlockSize);

        juce::AudioBuffer<float> block (2, testBlockSize);
        juce::MidiBuffer midi;
        juce::int64 sampleIndex = 0;

        for (int i = 0; i < totalBlocks; ++i)
        {
            if (engageBypass && i == 6)
                setBool (processor, ParamIDs::bypass, true);

            if (engageBypass && i == 6 + bypassBlocks)
                setBool (processor, ParamIDs::bypass, false);

            // A varying-amplitude probe (rather than a flat sine) so the
            // compressor/limiter/opto envelopes actually move during the
            // bypassed section instead of sitting at a fixed level - state
            // that stayed frozen diverges far more visibly against this than
            // against a constant-level signal.
            const auto envelope = 0.5f + 0.5f * std::sin (juce::MathConstants<float>::twoPi
                                                          * static_cast<float> (i) / 17.0f);
            TestHelpers::fillWithSine (block, testSampleRate, probeFrequencyHz,
                                       probeAmplitude * envelope, sampleIndex);
            processor.processBlock (block, midi);
            sampleIndex += testBlockSize;
        }

        return block; // the final block, well after the crossfade settled back to wet
    };

    const auto neverBypassed = renderWithMidBypass (false);
    const auto bypassedThenRestored = renderWithMidBypass (true);

    double sumSquaredDifference = 0.0;
    double sumSquaredReference = 0.0;

    for (int channel = 0; channel < 2; ++channel)
    {
        for (int sample = 0; sample < testBlockSize; ++sample)
        {
            const auto reference = neverBypassed.getSample (channel, sample);
            const auto restored = bypassedThenRestored.getSample (channel, sample);
            const auto difference = restored - reference;

            sumSquaredDifference += static_cast<double> (difference) * static_cast<double> (difference);
            sumSquaredReference += static_cast<double> (reference) * static_cast<double> (reference);
        }
    }

    REQUIRE (sumSquaredReference > 0.0);

    const auto differenceDb = 10.0 * std::log10 (juce::jmax (1.0e-30, sumSquaredDifference / sumSquaredReference));
    INFO ("state-continuity difference, settled well after un-bypass = " << differenceDb << " dB");

    CHECK (differenceDb < -100.0);
}
