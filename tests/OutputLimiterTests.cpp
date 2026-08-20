#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "dsp/OutputLimiter.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

// Output limiter tests (issue #24). The stage's contract, in order of how
// much it would hurt to break it:
//
//   1. OFF (the default) is a bit-exact bypass - the plugin's core
//      default-wire invariant runs straight through it.
//   2. No OUTPUT SAMPLE exceeds the ceiling, ever, at any release setting.
//   3. It adds no latency and uses no lookahead, which is exactly why (2)
//      says "sample" and not "true peak" - the inter-sample overshoot that
//      necessarily remains is MEASURED here (8x windowed-sinc
//      reconstruction) and regression-frozen, and docs/manual.md quotes the
//      figure rather than claiming a true-peak ceiling.
//   4. A settled limiter is a wire again, bit-exactly.
//   5. Detection is L/R-linked, so a peak on one channel cannot pan the
//      image.
namespace
{
    constexpr double testSampleRate = 44100.0;

    juce::dsp::ProcessSpec specFor (double sampleRate, int blockSize, int channels = 2)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (blockSize);
        spec.numChannels = static_cast<juce::uint32> (channels);
        return spec;
    }

    void processInPlace (OutputLimiter& limiter, juce::AudioBuffer<float>& buffer)
    {
        // Rebuilt per call the way processBlock() does - see CLAUDE.md's
        // AudioBuffer::clear()/cached-write-pointer note.
        juce::dsp::AudioBlock<float> block (buffer);
        limiter.process (block);
    }

    float ceilingLinear (float ceilingDb)
    {
        return juce::Decibels::decibelsToGain (ceilingDb);
    }

    // Reconstructs the continuous-time waveform the samples represent at 8x
    // the sample rate, using a Blackman-windowed sinc kernel (64-tap
    // half-width), and returns the largest absolute reconstructed value -
    // i.e. an inter-sample ("true") peak estimate. Deliberately implemented
    // here rather than with juce::dsp::Oversampling: the point of the
    // measurement is to characterise the limiter with a reference
    // reconstruction whose parameters are visible and fixed, independent of
    // any framework filter design that could change between JUCE releases.
    double interSamplePeak8x (const juce::AudioBuffer<float>& buffer)
    {
        constexpr int oversampling = 8;
        constexpr int halfWidth = 64;
        constexpr double pi = juce::MathConstants<double>::pi;

        // kernel[phase][d + halfWidth] for d = n - k, i.e. the tap weight
        // of input sample k when reconstructing at time n + phase/8.
        std::vector<std::vector<double>> kernel (oversampling,
                                                  std::vector<double> (2 * halfWidth, 0.0));

        for (int phase = 0; phase < oversampling; ++phase)
        {
            for (int d = -halfWidth; d < halfWidth; ++d)
            {
                const auto x = static_cast<double> (d) + static_cast<double> (phase) / oversampling;

                const auto sinc = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (pi * x) / (pi * x);

                // Symmetric Blackman window over |x| <= halfWidth: 1 at the
                // centre, exactly 0 at the ends.
                const auto w = 0.42
                             + 0.5 * std::cos (pi * x / halfWidth)
                             + 0.08 * std::cos (2.0 * pi * x / halfWidth);

                kernel[static_cast<size_t> (phase)][static_cast<size_t> (d + halfWidth)] = sinc * w;
            }
        }

        double peak = 0.0;
        const auto numSamples = buffer.getNumSamples();

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            // Stay clear of the buffer edges, where the kernel would be
            // truncated and could invent an overshoot of its own.
            for (int n = halfWidth; n < numSamples - halfWidth; ++n)
            {
                for (int phase = 0; phase < oversampling; ++phase)
                {
                    double sum = 0.0;

                    for (int d = -halfWidth; d < halfWidth; ++d)
                        sum += static_cast<double> (data[n - d])
                             * kernel[static_cast<size_t> (phase)][static_cast<size_t> (d + halfWidth)];

                    peak = std::max (peak, std::abs (sum));
                }
            }
        }

        return peak;
    }

    // A sine with an explicit phase offset, so the fs/4 worst case (samples
    // landing symmetrically either side of every crest) can be constructed
    // exactly.
    void fillWithPhasedSine (juce::AudioBuffer<float>& buffer, double sampleRate,
                             double frequencyHz, float amplitude, double phaseOffset)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* data = buffer.getWritePointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto phase = juce::MathConstants<double>::twoPi * frequencyHz
                                       * static_cast<double> (sample) / sampleRate
                                 + phaseOffset;
                data[sample] = amplitude * static_cast<float> (std::sin (phase));
            }
        }
    }
}

TEST_CASE ("Output limiter: disabled is a bit-exact bypass", "[limiter][dsp]")
{
    OutputLimiter limiter;
    limiter.prepare (specFor (testSampleRate, 512));
    limiter.setCeilingDb (-0.3f);
    limiter.setReleaseMs (60.0f);
    limiter.setEnabled (false);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, testSampleRate, 220.0, 4.0f); // +12 dBFS, far over any ceiling

    juce::AudioBuffer<float> reference (buffer);
    processInPlace (limiter, buffer);

    // Not "close to" - identical, bit for bit.
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            REQUIRE (buffer.getReadPointer (channel)[sample] == reference.getReadPointer (channel)[sample]);

    CHECK (limiter.getCurrentGainReductionDb() == Catch::Approx (0.0f));
}

TEST_CASE ("Output limiter: the static curve is unity below the knee and peaks at exactly the ceiling", "[limiter][dsp]")
{
    constexpr float ceilingDb = -0.3f;
    const auto kneeStartDb = ceilingDb - OutputLimiter::kneeDb * 0.5f;

    // Below the knee: EXACTLY 1.0f, not approximately - this is what makes
    // an idle limiter a wire.
    for (const auto levelDb : { -60.0f, -20.0f, -6.0f, kneeStartDb - 0.01f, kneeStartDb })
    {
        INFO ("input level = " << levelDb << " dBFS");
        CHECK (OutputLimiter::staticGainFor (juce::Decibels::decibelsToGain (levelDb), ceilingDb) == 1.0f);
    }

    // Silence is transparent (and evaluates no transcendental).
    CHECK (OutputLimiter::staticGainFor (0.0f, ceilingDb) == 1.0f);

    // Across the knee and above it the OUTPUT never exceeds the ceiling,
    // rises monotonically, and reaches the ceiling exactly at the top of
    // the knee (the quadratic's own maximum) - the infinite-ratio contract.
    auto previousOutputDb = -200.0f;

    for (int step = 0; step <= 400; ++step)
    {
        const auto inputDb = kneeStartDb + static_cast<float> (step) * 0.05f; // knee start .. +20 dB
        const auto peak = juce::Decibels::decibelsToGain (inputDb);
        const auto outputDb = juce::Decibels::gainToDecibels (peak * OutputLimiter::staticGainFor (peak, ceilingDb));

        INFO ("input = " << inputDb << " dBFS -> output = " << outputDb << " dBFS");
        CHECK (outputDb <= ceilingDb + 1.0e-4f);
        CHECK (outputDb >= previousOutputDb - 1.0e-4f);

        previousOutputDb = outputDb;
    }

    // At and above the top of the knee the curve is flat AT the ceiling.
    for (const auto inputDb : { ceilingDb + OutputLimiter::kneeDb * 0.5f, ceilingDb + 6.0f, ceilingDb + 24.0f })
    {
        const auto peak = juce::Decibels::decibelsToGain (inputDb);
        const auto outputDb = juce::Decibels::gainToDecibels (peak * OutputLimiter::staticGainFor (peak, ceilingDb));

        INFO ("input = " << inputDb << " dBFS");
        CHECK (outputDb == Catch::Approx (ceilingDb).margin (1.0e-4));
    }
}

TEST_CASE ("Output limiter: no output sample exceeds the ceiling", "[limiter][dsp]")
{
    for (const auto ceilingDb : { -0.3f, -1.0f, -6.0f, -12.0f, 0.0f })
    {
        for (const auto releaseMs : { 5.0f, 60.0f, 500.0f })
        {
            OutputLimiter limiter;
            limiter.prepare (specFor (testSampleRate, 256));
            limiter.setCeilingDb (ceilingDb);
            limiter.setReleaseMs (releaseMs);
            limiter.setEnabled (true);

            // Let the smoothed ceiling settle at its target before judging
            // absolute levels (a ramping ceiling is still honoured sample by
            // sample, but only against the ceiling in force AT that sample).
            juce::AudioBuffer<float> settle (2, 4096);
            settle.clear();
            processInPlace (limiter, settle);

            const auto limit = ceilingLinear (ceilingDb) * 1.000001f; // one float ulp of slack

            juce::Random random (0x51ade);
            juce::AudioBuffer<float> buffer (2, 256);

            for (int block = 0; block < 40; ++block)
            {
                // Alternating programme: a hot sine, a burst of noise, and
                // silence - so attack, release and the transitions between
                // them are all exercised against the same ceiling.
                if (block % 3 == 0)
                {
                    TestHelpers::fillWithSine (buffer, testSampleRate, 137.0, 6.0f, block * 256);
                }
                else if (block % 3 == 1)
                {
                    for (int channel = 0; channel < 2; ++channel)
                        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                            buffer.getWritePointer (channel)[sample] = 8.0f * (random.nextFloat() * 2.0f - 1.0f);
                }
                else
                {
                    buffer.clear();
                }

                processInPlace (limiter, buffer);

                INFO ("ceiling = " << ceilingDb << " dBFS, release = " << releaseMs
                      << " ms, block " << block);
                CHECK (TestHelpers::peakAbsolute (buffer) <= limit);
                CHECK (TestHelpers::allSamplesFinite (buffer));
            }
        }
    }
}

TEST_CASE ("Output limiter: attack is instantaneous - the first sample of a hot burst is already inside the ceiling", "[limiter][dsp]")
{
    constexpr float ceilingDb = -0.3f;

    OutputLimiter limiter;
    limiter.prepare (specFor (testSampleRate, 512));
    limiter.setCeilingDb (ceilingDb);
    limiter.setReleaseMs (500.0f); // slowest release: nothing can hide behind a fast recovery
    limiter.setEnabled (true);

    juce::AudioBuffer<float> silence (2, 512);
    silence.clear();
    processInPlace (limiter, silence);

    // A step straight from digital silence to +18 dBFS: with no lookahead,
    // the ONLY way sample 0 can be inside the ceiling is an instantaneous
    // attack.
    juce::AudioBuffer<float> burst (2, 512);

    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < burst.getNumSamples(); ++sample)
            burst.getWritePointer (channel)[sample] = 8.0f;

    processInPlace (limiter, burst);

    const auto limit = ceilingLinear (ceilingDb) * 1.000001f;

    CHECK (std::abs (burst.getReadPointer (0)[0]) <= limit);
    CHECK (TestHelpers::peakAbsolute (burst) <= limit);

    // ...and the metering reports the ~18 dB of reduction that implies.
    CHECK (limiter.getCurrentGainReductionDb() == Catch::Approx (18.0f + 0.3f).margin (0.2f));
}

TEST_CASE ("Output limiter: release approaches unity from below and never overshoots", "[limiter][dsp]")
{
    constexpr float ceilingDb = -0.3f;

    OutputLimiter limiter;
    limiter.prepare (specFor (testSampleRate, 64));
    limiter.setCeilingDb (ceilingDb);
    limiter.setReleaseMs (60.0f);
    limiter.setEnabled (true);

    // Drive it deep...
    juce::AudioBuffer<float> hot (2, 64);

    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < hot.getNumSamples(); ++sample)
            hot.getWritePointer (channel)[sample] = 10.0f;

    processInPlace (limiter, hot);
    const auto deepestGrDb = limiter.getCurrentGainReductionDb();
    CHECK (deepestGrDb > 15.0f);

    // ...then feed a constant, well-below-knee DC level and watch the gain
    // recover. Reading the gain back out of the audio (out/in) makes the
    // trajectory observable without exposing loop internals.
    constexpr float probeLevel = 0.1f;
    const auto limit = ceilingLinear (ceilingDb) * 1.000001f;

    auto previousGain = 0.0f;
    auto reachedUnity = false;
    auto monotonicityViolated = false;
    auto exceededUnity = false;
    auto exceededCeiling = false;
    auto samplesToUnity = 0;

    // ~2.2 s of probe signal: long enough for a 60 ms one-pole to close the
    // last 1e-7 to unity (~16 time constants). Violations are accumulated
    // rather than asserted per sample so the scan stays three assertions
    // instead of a hundred thousand.
    for (int block = 0; block < 1500 && ! reachedUnity; ++block)
    {
        juce::AudioBuffer<float> probe (2, 64);

        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < probe.getNumSamples(); ++sample)
                probe.getWritePointer (channel)[sample] = probeLevel;

        processInPlace (limiter, probe);

        for (int sample = 0; sample < probe.getNumSamples(); ++sample)
        {
            const auto gain = probe.getReadPointer (0)[sample] / probeLevel;

            // Monotone recovery, never above unity (a first-order approach
            // from below cannot overshoot), and the output never breaches
            // the ceiling during recovery either.
            monotonicityViolated = monotonicityViolated || gain < previousGain - 1.0e-6f;
            exceededUnity = exceededUnity || gain > 1.0f;
            exceededCeiling = exceededCeiling || std::abs (probe.getReadPointer (0)[sample]) > limit;

            previousGain = gain;

            if (! reachedUnity)
            {
                ++samplesToUnity;
                reachedUnity = juce::exactlyEqual (gain, 1.0f);
            }
        }
    }

    CHECK_FALSE (monotonicityViolated);
    CHECK_FALSE (exceededUnity);
    CHECK_FALSE (exceededCeiling);

    // And it genuinely lands on EXACT unity rather than crawling
    // asymptotically for ever (the unity snap) - within a couple of seconds
    // at the 60 ms release used here.
    INFO ("samples to exact unity = " << samplesToUnity);
    CHECK (reachedUnity);
    CHECK (samplesToUnity < static_cast<int> (2.0 * testSampleRate));
}

TEST_CASE ("Output limiter: a settled limiter is a bit-exact wire", "[limiter][dsp]")
{
    OutputLimiter limiter;
    limiter.prepare (specFor (testSampleRate, 512));
    limiter.setCeilingDb (-0.3f);
    limiter.setReleaseMs (5.0f);
    limiter.setEnabled (true);

    // Well below the knee from the very first sample: nothing to do.
    juce::AudioBuffer<float> settle (2, 4096);
    TestHelpers::fillWithSine (settle, testSampleRate, 440.0, 0.25f);
    processInPlace (limiter, settle);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, testSampleRate, 440.0, 0.25f, 4096);

    juce::AudioBuffer<float> reference (buffer);
    processInPlace (limiter, buffer);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            REQUIRE (buffer.getReadPointer (channel)[sample] == reference.getReadPointer (channel)[sample]);

    CHECK (limiter.getCurrentGainReductionDb() == Catch::Approx (0.0f));
}

TEST_CASE ("Output limiter: detection is L/R-linked, so peaks never pan the image", "[limiter][dsp]")
{
    OutputLimiter limiter;
    limiter.prepare (specFor (testSampleRate, 512));
    limiter.setCeilingDb (-0.3f);
    limiter.setReleaseMs (60.0f);
    limiter.setEnabled (true);

    // Hard-panned material: L is slammed, R sits quietly below the knee. An
    // UNLINKED limiter would leave R untouched and pull L down, i.e. move
    // the image on every peak.
    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, testSampleRate, 200.0, 1.0f);

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        buffer.getWritePointer (0)[sample] *= 6.0f;
        buffer.getWritePointer (1)[sample] *= 0.05f;
    }

    juce::AudioBuffer<float> reference (buffer);
    processInPlace (limiter, buffer);

    auto quietChannelWasAttenuated = false;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto inLeft = reference.getReadPointer (0)[sample];
        const auto inRight = reference.getReadPointer (1)[sample];

        if (std::abs (inLeft) < 1.0e-3f || std::abs (inRight) < 1.0e-3f)
            continue;

        const auto gainLeft = buffer.getReadPointer (0)[sample] / inLeft;
        const auto gainRight = buffer.getReadPointer (1)[sample] / inRight;

        INFO ("sample " << sample << " gainL " << gainLeft << " gainR " << gainRight);
        CHECK (gainRight == Catch::Approx (gainLeft).epsilon (1.0e-5));

        if (gainRight < 0.99f)
            quietChannelWasAttenuated = true;
    }

    // The linkage must be doing something here, not vacuously agreeing at
    // unity on both channels.
    CHECK (quietChannelWasAttenuated);
}

TEST_CASE ("Output limiter: measured inter-sample overshoot (8x windowed-sinc) - a SAMPLE-peak ceiling, not true-peak", "[limiter][dsp][truepeak]")
{
    constexpr float ceilingDb = -0.3f;

    // This test does not assert that inter-sample overshoot is absent - it
    // CANNOT be absent for a zero-latency, no-lookahead limiter, which is
    // what issue #24 mandates. It measures how large it is and freezes the
    // figure, so a future voicing change cannot quietly make it worse, and
    // so docs/manual.md can quote a number instead of a claim.
    const auto overshootDbFor = [&] (double frequencyHz, double phaseOffset, float amplitude, float releaseMs)
    {
        OutputLimiter limiter;
        limiter.prepare (specFor (testSampleRate, 4096));
        limiter.setCeilingDb (ceilingDb);
        limiter.setReleaseMs (releaseMs);
        limiter.setEnabled (true);

        juce::AudioBuffer<float> buffer (2, 4096);
        fillWithPhasedSine (buffer, testSampleRate, frequencyHz, amplitude, phaseOffset);

        processInPlace (limiter, buffer);

        // Sample-peak guarantee first: this is the part that IS exact.
        CHECK (TestHelpers::peakAbsolute (buffer) <= ceilingLinear (ceilingDb) * 1.000001f);

        const auto interSample = interSamplePeak8x (buffer);
        return 20.0 * std::log10 (interSample / static_cast<double> (ceilingLinear (ceilingDb)));
    };

    // (a) The theoretical worst case for a sine: fs/4, phase-shifted by
    // pi/4, so every sample lands symmetrically either side of a crest and
    // the reconstruction peaks sqrt(2) (= 3.01 dB) above the samples.
    const auto worstCaseDb = overshootDbFor (testSampleRate / 4.0, juce::MathConstants<double>::pi / 4.0, 6.0f, 60.0f);
    INFO ("fs/4 worst-case inter-sample overshoot = " << worstCaseDb << " dB");
    CHECK (worstCaseDb > 2.90); // measured 3.0103 dB (= 20*log10(sqrt(2)), the analytic bound)
    CHECK (worstCaseDb < 3.10);

    // (b) A high but musical partial (11 kHz at 44.1 kHz), driven hard.
    const auto highToneDb = overshootDbFor (11000.0, 0.31, 6.0f, 60.0f);
    INFO ("11 kHz inter-sample overshoot = " << highToneDb << " dB");
    CHECK (highToneDb > 0.50); // measured 0.712 dB
    CHECK (highToneDb < 1.00);

    // (c) A midrange tone, where reconstruction has plenty of samples per
    // period and the overshoot is dominated by the gain modulation itself
    // rather than by the waveform.
    const auto midToneDb = overshootDbFor (1000.0, 0.0, 6.0f, 5.0f);
    INFO ("1 kHz / 5 ms release inter-sample overshoot = " << midToneDb << " dB");
    CHECK (midToneDb >= 0.0);  // measured 0.059 dB
    CHECK (midToneDb < 0.20);
}

TEST_CASE ("Output limiter: survives silence, zero-length blocks, denormals, NaN and sample-rate changes", "[limiter][dsp][robustness]")
{
    OutputLimiter limiter;
    limiter.prepare (specFor (testSampleRate, 512));
    limiter.setCeilingDb (-0.3f);
    limiter.setReleaseMs (60.0f);
    limiter.setEnabled (true);

    // Zero-length block: a safe no-op.
    {
        juce::AudioBuffer<float> empty (2, 0);
        juce::dsp::AudioBlock<float> block (empty);
        limiter.process (block);
        SUCCEED();
    }

    // Denormal-scale input passes without the loop wandering off unity.
    {
        juce::AudioBuffer<float> tiny (2, 256);

        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < tiny.getNumSamples(); ++sample)
                tiny.getWritePointer (channel)[sample] = 1.0e-30f;

        processInPlace (limiter, tiny);

        CHECK (TestHelpers::allSamplesFinite (tiny));
        CHECK (tiny.getReadPointer (0)[255] == 1.0e-30f); // exactly a wire down here
    }

    // Non-finite input must not poison the loop: the limiter ignores such
    // samples for detection (the engine's own sum sanitises them upstream -
    // see MiserereEngine::process) and stays finite afterwards.
    {
        juce::AudioBuffer<float> poisoned (2, 256);
        TestHelpers::fillWithSine (poisoned, testSampleRate, 300.0, 0.2f);
        poisoned.getWritePointer (0)[10] = std::numeric_limits<float>::quiet_NaN();
        poisoned.getWritePointer (1)[20] = std::numeric_limits<float>::infinity();

        processInPlace (limiter, poisoned);

        juce::AudioBuffer<float> recovery (2, 512);
        TestHelpers::fillWithSine (recovery, testSampleRate, 300.0, 0.2f);
        processInPlace (limiter, recovery);

        CHECK (TestHelpers::allSamplesFinite (recovery));
        CHECK (TestHelpers::peakAbsolute (recovery) == Catch::Approx (0.2f).margin (1.0e-3));
    }

    // Sample-rate/block-size change re-prepares cleanly and the ceiling
    // still holds.
    {
        limiter.prepare (specFor (96000.0, 128));

        juce::AudioBuffer<float> hot (2, 128);
        TestHelpers::fillWithSine (hot, 96000.0, 500.0, 4.0f);
        processInPlace (limiter, hot);

        CHECK (TestHelpers::peakAbsolute (hot) <= ceilingLinear (-0.3f) * 1.000001f);
    }
}

TEST_CASE ("Plugin: the output limiter is the last stage, and off by default", "[limiter][engine]")
{
    MiserereAudioProcessor processor;
    processor.prepareToPlay (testSampleRate, 512);

    // Latency is unchanged by the limiter's existence - the whole point of
    // the no-lookahead mandate.
    CHECK (processor.getLatencySamples() == 0);

    const auto setParam = [&] (const char* id, float value)
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (processor.apvts.getParameter (id));
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    const auto runBlock = [&] (int blockIndex)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        TestHelpers::fillWithSine (buffer, testSampleRate, 220.0, 2.0f, blockIndex * 512);

        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);
        return buffer;
    };

    // Default (limiter OFF): the Out Trim is still the end of the chain, so
    // a hot signal leaves hot.
    {
        const auto out = runBlock (0);
        CHECK (TestHelpers::peakAbsolute (out) > 1.5f);
    }

    // Engaged, it clamps whatever the WHOLE plugin produced - including the
    // Out Trim's own boost, which is what proves the ordering: a limiter
    // placed before the out trim could not hold this ceiling.
    setParam (ParamIDs::limiterEnabled, 1.0f);
    setParam (ParamIDs::limiterCeiling, -1.0f);
    setParam (ParamIDs::limiterRelease, 60.0f);
    setParam (ParamIDs::outTrim, 12.0f);

    // Let the 50 ms ceiling smoother and the out-trim ramp reach their
    // targets before judging absolute levels - during the ramp the limiter
    // is still honouring the higher ceiling it is coming from.
    for (int block = 1; block < 9; ++block)
        runBlock (block);

    for (int block = 9; block < 17; ++block)
    {
        const auto out = runBlock (block);

        INFO ("block " << block);
        CHECK (TestHelpers::peakAbsolute (out) <= ceilingLinear (-1.0f) * 1.000001f);
        CHECK (TestHelpers::allSamplesFinite (out));
    }

    CHECK (processor.getLimiterGainReductionDb() > 6.0f);
}
