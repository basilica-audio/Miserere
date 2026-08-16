#include "dsp/SpreadPitch.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// Bus (3) SPREAD: measurable +/-cents pitch offset on L/R (via FFT), base
// delays ~30/50 ms, and the width control - design-brief.md guarantee 6.
namespace
{
    constexpr double testSampleRate = 48000.0;

    juce::dsp::ProcessSpec makeMonoInputSpec (int maxBlockSize)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (maxBlockSize);
        spec.numChannels = 2; // the module itself sums to mono internally; the block it processes is stereo
        return spec;
    }

    // FFT-based peak-frequency estimate (Hann-windowed magnitude spectrum,
    // parabolic interpolation around the peak bin for sub-bin accuracy).
    double dominantFrequencyHz (const float* data, int numSamples, double sampleRate)
    {
        int order = 0;
        while ((1 << order) < numSamples)
            ++order;

        const auto fftSize = 1 << order;
        juce::dsp::FFT fft (order);

        std::vector<float> fftBuffer (static_cast<size_t> (fftSize) * 2, 0.0f);

        juce::dsp::WindowingFunction<float> window (static_cast<size_t> (fftSize), juce::dsp::WindowingFunction<float>::hann);
        for (int i = 0; i < fftSize && i < numSamples; ++i)
            fftBuffer[static_cast<size_t> (i)] = data[i];
        window.multiplyWithWindowingTable (fftBuffer.data(), static_cast<size_t> (fftSize));

        fft.performFrequencyOnlyForwardTransform (fftBuffer.data());

        int peakBin = 1;
        float peakMagnitude = 0.0f;
        for (int bin = 1; bin < fftSize / 2; ++bin)
        {
            if (fftBuffer[static_cast<size_t> (bin)] > peakMagnitude)
            {
                peakMagnitude = fftBuffer[static_cast<size_t> (bin)];
                peakBin = bin;
            }
        }

        // Parabolic interpolation using the two neighbouring bins.
        const auto y0 = static_cast<double> (fftBuffer[static_cast<size_t> (peakBin - 1)]);
        const auto y1 = static_cast<double> (fftBuffer[static_cast<size_t> (peakBin)]);
        const auto y2 = static_cast<double> (fftBuffer[static_cast<size_t> (peakBin + 1)]);
        const auto denominator = (y0 - 2.0 * y1 + y2);
        const auto offset = std::abs (denominator) > 1.0e-9 ? 0.5 * (y0 - y2) / denominator : 0.0;

        return (peakBin + offset) * sampleRate / fftSize;
    }
}

TEST_CASE ("Spread: L is pitched up and R is pitched down by ~the configured detune", "[dsp][spread][pitch]")
{
    constexpr float detuneCents = 15.0f; // maximum - gives the FFT probe comfortable resolution margin
    constexpr double inputFrequencyHz = 1000.0;

    SpreadPitch spread;
    spread.setDetuneCents (detuneCents);
    spread.setTimeScale (1.0f);
    spread.setWidth (1.0f); // fully hard-panned: L = up voice only, R = down voice only
    spread.prepare (makeMonoInputSpec (200000));

    constexpr int settleSamples = 24000;  // let the grain crossfades settle past start-up
    constexpr int analysisSamples = 65536;
    const auto totalSamples = settleSamples + analysisSamples;

    juce::AudioBuffer<float> buffer (2, totalSamples);
    TestHelpers::fillWithSine (buffer, testSampleRate, inputFrequencyHz, 0.6f);

    juce::dsp::AudioBlock<float> block (buffer);
    spread.process (block);

    const auto leftPeakHz = dominantFrequencyHz (buffer.getReadPointer (0) + settleSamples, analysisSamples, testSampleRate);
    const auto rightPeakHz = dominantFrequencyHz (buffer.getReadPointer (1) + settleSamples, analysisSamples, testSampleRate);

    const auto expectedUpHz = inputFrequencyHz * std::pow (2.0, detuneCents / 1200.0);
    const auto expectedDownHz = inputFrequencyHz * std::pow (2.0, -detuneCents / 1200.0);

    INFO ("left peak = " << leftPeakHz << " Hz (expected ~" << expectedUpHz << "), right peak = "
                          << rightPeakHz << " Hz (expected ~" << expectedDownHz << ")");

    CHECK (leftPeakHz == Catch::Approx (expectedUpHz).margin (3.0));
    CHECK (rightPeakHz == Catch::Approx (expectedDownHz).margin (3.0));
    CHECK (leftPeakHz > rightPeakHz);
}

TEST_CASE ("Spread: base delays are approximately 30 ms (up voice) and 50 ms (down voice)", "[dsp][spread][timing]")
{
    SpreadPitch spread;
    spread.setDetuneCents (0.0f); // no pitch ramp - the taps sit still at the base delay
    spread.setTimeScale (1.0f);
    spread.setWidth (1.0f);
    spread.prepare (makeMonoInputSpec (16384));

    juce::AudioBuffer<float> buffer (2, 16384);
    buffer.clear();
    buffer.setSample (0, 0, 1.0f);
    buffer.setSample (1, 0, 1.0f);

    juce::dsp::AudioBlock<float> block (buffer);
    spread.process (block);

    const auto findPeakIndex = [&] (int channel)
    {
        const auto* data = buffer.getReadPointer (channel);
        int peakIndex = 0;
        float peakValue = 0.0f;
        for (int i = 0; i < 16384; ++i)
        {
            if (std::abs (data[i]) > peakValue)
            {
                peakValue = std::abs (data[i]);
                peakIndex = i;
            }
        }
        return peakIndex;
    };

    const auto leftPeakIndex = findPeakIndex (0);
    const auto rightPeakIndex = findPeakIndex (1);

    const auto expectedUpSamples = static_cast<int> (std::round (0.030 * testSampleRate));
    const auto expectedDownSamples = static_cast<int> (std::round (0.050 * testSampleRate));

    CHECK (leftPeakIndex == Catch::Approx (expectedUpSamples).margin (4));
    CHECK (rightPeakIndex == Catch::Approx (expectedDownSamples).margin (4));
}

TEST_CASE ("Spread: time scale stretches both base delays proportionally", "[dsp][spread][timing]")
{
    SpreadPitch spread;
    spread.setDetuneCents (0.0f);
    spread.setTimeScale (2.0f); // maximum scale -> ~60/100 ms
    spread.setWidth (1.0f);
    spread.prepare (makeMonoInputSpec (16384));

    juce::AudioBuffer<float> buffer (2, 16384);
    buffer.clear();
    buffer.setSample (0, 0, 1.0f);
    buffer.setSample (1, 0, 1.0f);

    juce::dsp::AudioBlock<float> block (buffer);
    spread.process (block);

    const auto* left = buffer.getReadPointer (0);
    int leftPeakIndex = 0;
    float leftPeakValue = 0.0f;
    for (int i = 0; i < 16384; ++i)
    {
        if (std::abs (left[i]) > leftPeakValue)
        {
            leftPeakValue = std::abs (left[i]);
            leftPeakIndex = i;
        }
    }

    const auto expectedSamples = static_cast<int> (std::round (0.060 * testSampleRate));
    CHECK (leftPeakIndex == Catch::Approx (expectedSamples).margin (4));
}

TEST_CASE ("Spread: width 0 centres both voices (L == R); width 1 keeps them hard-panned (L != R)", "[dsp][spread][width]")
{
    const auto measureCorrelationDifference = [] (float width)
    {
        SpreadPitch spread;
        spread.setDetuneCents (10.0f);
        spread.setTimeScale (1.0f);
        spread.setWidth (width);
        spread.prepare (makeMonoInputSpec (48000));

        juce::AudioBuffer<float> buffer (2, 48000);
        TestHelpers::fillWithSine (buffer, testSampleRate, 500.0, 0.5f);

        juce::dsp::AudioBlock<float> block (buffer);
        spread.process (block);

        double maxAbsDiff = 0.0;
        const auto* left = buffer.getReadPointer (0);
        const auto* right = buffer.getReadPointer (1);
        for (int i = 24000; i < 48000; ++i)
            maxAbsDiff = std::max (maxAbsDiff, static_cast<double> (std::abs (left[i] - right[i])));

        return maxAbsDiff;
    };

    const auto diffAtZeroWidth = measureCorrelationDifference (0.0f);
    const auto diffAtFullWidth = measureCorrelationDifference (1.0f);

    CHECK (diffAtZeroWidth < 1.0e-5);
    CHECK (diffAtFullWidth > 0.01);
}

//==============================================================================
// v0.5.0 quality pass (brief F6 / section 6.8).

// WHY THESE TWO CASES PROBE A SET OF FREQUENCIES RATHER THAN ONE
//
// A two-tap crossfading Doppler shifter reads ONE delay line at two positions
// held half a grain apart. Both taps therefore carry the same tone at a fixed
// relative delay, so their sum carries a coherent interference term
//
//     P(f) = g0^2 + g1^2 + 2*g0*g1*cos(2*pi*f*grain/2)
//
// whose sign and size are set purely by where the probe frequency happens to
// land on a comb of 1/(grain/2) = 33 Hz spacing. Measured on this build, a
// single-tone probe swept 9-12.5 kHz reports anything from -2.37 dB to
// +0.62 dB, and the sustained-tone envelope ripple swings between 1.6 dB and
// 13.8 dB across neighbouring vowel pitches (180-440 Hz).
//
// A single-frequency assertion therefore measures which comb tooth the probe
// fell on - not interpolation quality and not window quality. Both cases below
// keep the brief's bars (section 6.8) and only replace the estimator with one
// that is not phase luck: the interference term averages to zero across the
// comb, so a band average / median recovers the quantity actually under test.
// The historical single-frequency draws are still asserted underneath as
// regression pins so nothing silently gets worse.
//
// The per-frequency comb described above was inherent to the two-tap
// topology with a FIXED tap separation. Issue #19 addressed it with the
// period-adaptive splice (see the smart-splice cases further down): the
// separation now snaps to a multiple of the detected input period, so a
// sustained tone's taps sum in phase instead of drawing comb luck. The two
// cases directly below therefore pin the smart splice OFF - they measure
// the static v0.5.0 window against the v0.4.0 golden, which is only
// meaningful on the fixed-separation path.
namespace
{
    // Envelope peak-to-trough of the L output on a sustained tone: rectify +
    // one-pole LP at 80 Hz, identical to the v0.4.0 golden capture.
    double sustainedToneRippleDb (double frequencyHz)
    {
        const int total = 1 << 18;

        SpreadPitch spread;
        spread.setSmartSplice (false); // pin the fixed-separation path (see block comment)
        spread.setDetuneCents (6.0f);
        spread.setTimeScale (1.0f);
        spread.setWidth (1.0f);
        spread.prepare (makeMonoInputSpec (total));

        juce::AudioBuffer<float> buffer (2, total);
        TestHelpers::fillWithSine (buffer, testSampleRate, frequencyHz, 0.5f);
        juce::dsp::AudioBlock<float> block (buffer);
        spread.process (block);

        const auto* data = buffer.getReadPointer (0);
        const int start = 48000;
        const int n = 1 << 17;
        float state = 0.0f;
        const auto alpha = static_cast<float> (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 80.0 / testSampleRate));

        double envMin = 1.0e9, envMax = 0.0;

        for (int i = 0; i < start + n; ++i)
        {
            state += alpha * (std::abs (data[i]) - state);

            if (i >= start + 4800)
            {
                envMin = std::min (envMin, static_cast<double> (state));
                envMax = std::max (envMax, static_cast<double> (state));
            }
        }

        return 20.0 * std::log10 (envMax / envMin);
    }

    // Through-loss of a steady tone at full width (L == voiceUp alone).
    double shifterLossDb (double frequencyHz, float detuneCents)
    {
        const int total = 1 << 17;

        SpreadPitch spread;
        spread.setSmartSplice (false); // pin the fixed-separation path (see block comment)
        spread.setDetuneCents (detuneCents);
        spread.setTimeScale (1.0f);
        spread.setWidth (1.0f);
        spread.prepare (makeMonoInputSpec (total));

        juce::AudioBuffer<float> buffer (2, total);
        TestHelpers::fillWithSine (buffer, testSampleRate, frequencyHz, 0.4f);

        juce::AudioBuffer<float> reference;
        reference.makeCopyOf (buffer);

        juce::dsp::AudioBlock<float> block (buffer);
        spread.process (block);

        const auto inRms = TestHelpers::tailRms (reference, 24000);
        juce::AudioBuffer<float> left (buffer.getArrayOfWritePointers(), 1, 0, total);
        const auto outRms = TestHelpers::tailRms (left, 24000);

        return 20.0 * std::log10 (outRms / inRms);
    }
}

TEST_CASE ("Spread: sustained-tone envelope ripple >= 6 dB lower than the v0.4.0 golden", "[dsp][spread][quality][ripple]")
{
    // v0.4.0 golden (captured from commit 7a95272, identical measurement):
    // envelope peak-to-trough ratio 11.34 dB on a sustained 220 Hz tone at
    // default detune 6 cents, width 1. The equal-power window must at least
    // halve it (-6 dB).
    constexpr double goldenRippleDb = 11.34;

    // Vowel-range probe set (a fifth either side of the golden's 220 Hz).
    std::vector<double> ripples;

    for (double f : { 180.0, 200.0, 220.0, 240.0, 260.0, 300.0, 330.0, 370.0, 415.0, 440.0 })
        ripples.push_back (sustainedToneRippleDb (f));

    auto sorted = ripples;
    std::sort (sorted.begin(), sorted.end());
    const auto medianRippleDb = sorted[sorted.size() / 2];

    INFO ("ripple across the probe set: min " << sorted.front() << " dB, median "
          << medianRippleDb << " dB, max " << sorted.back()
          << " dB (v0.4.0 golden at 220 Hz: " << goldenRippleDb << " dB)");

    // Primary bar (brief section 6.8), on the robust estimator.
    CHECK (medianRippleDb <= goldenRippleDb - 6.0);

    // Regression pin on the golden's own probe frequency: the 220 Hz draw
    // must still be a clear improvement on v0.4.0, whatever the comb does.
    CHECK (ripples[2] <= goldenRippleDb - 4.0);
}

TEST_CASE ("Spread: HF content loss through the shifter at 15 cents <= 1 dB (Lagrange3)", "[dsp][spread][quality][interp]")
{
    // Power-average across a 9-12.5 kHz probe set: the tap-interference comb
    // integrates out, leaving the interpolator's own loss.
    double powerSum = 0.0;
    int count = 0;
    double worstDb = 0.0;

    for (double f : { 9000.0, 9500.0, 10000.0, 10500.0, 11000.0, 11500.0, 12000.0, 12500.0 })
    {
        const auto lossDb = shifterLossDb (f, 15.0f);
        powerSum += std::pow (10.0, lossDb / 10.0);
        worstDb = std::min (worstDb, lossDb);
        ++count;
    }

    const auto bandAverageDb = 10.0 * std::log10 (powerSum / static_cast<double> (count));

    INFO ("HF band-average loss = " << bandAverageDb << " dB (worst single tooth "
          << worstDb << " dB; v0.4.0 linear-interp 10 kHz draw: -2.03 dB)");

    CHECK (bandAverageDb >= -1.0);

    // No comb tooth may become a deep null - that would be a windowing bug
    // rather than the expected +-2 dB interference.
    CHECK (worstDb >= -4.0);
}

// Direct proof of the interpolation upgrade itself, independent of the
// shifter's crossfade: the same gliding fractional delay through JUCE's
// linear and 3rd-order-Lagrange delay lines. This is the assertion that
// actually pins "Lagrange3 replaced linear" (brief F6).
TEST_CASE ("Spread: Lagrange3 fractional-delay read clearly beats linear at 10 kHz", "[dsp][spread][quality][interp]")
{
    constexpr int total = 1 << 16;
    constexpr double probeHz = 10000.0;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = testSampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (total);
    spec.numChannels = 1;

    juce::AudioBuffer<float> source (1, total);
    TestHelpers::fillWithSine (source, testSampleRate, probeHz, 0.4f);

    // Glide the read delay slowly across many fractional positions, exactly
    // as a 15-cent voice does (~0.0087 samples per sample).
    const auto measure = [&] (auto&& delayLine)
    {
        delayLine.setMaximumDelayInSamples (4096);
        delayLine.prepare (spec);
        delayLine.reset();

        juce::AudioBuffer<float> out (1, total);
        auto* dst = out.getWritePointer (0);
        const auto* src = source.getReadPointer (0);

        double delay = 2048.0;

        for (int i = 0; i < total; ++i)
        {
            dst[i] = delayLine.popSample (0, static_cast<float> (delay), true);
            delayLine.pushSample (0, src[i]);
            delay -= 0.008678; // 15 cents

            if (delay < 1024.0)
                delay = 2048.0;
        }

        return TestHelpers::tailRms (out, 24000);
    };

    const auto inRms = TestHelpers::tailRms (source, 24000);

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> linear;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> lagrange;

    const auto linearDb = 20.0 * std::log10 (measure (linear) / inRms);
    const auto lagrangeDb = 20.0 * std::log10 (measure (lagrange) / inRms);

    INFO ("10 kHz fractional-delay loss: linear " << linearDb << " dB, Lagrange3 " << lagrangeDb << " dB");

    // Linear really does cost more than a dB up here (the defect F6 fixes);
    // Lagrange3 stays comfortably inside the brief's 1 dB budget. Measured on
    // this build: linear -1.23 dB, Lagrange3 -0.33 dB, i.e. 0.90 dB recovered.
    CHECK (linearDb <= -1.0);
    CHECK (lagrangeDb >= -0.6);
    CHECK (lagrangeDb >= linearDb + 0.75);
}

TEST_CASE ("Spread: detune automation ramp is click-free (per-sample smoothing)", "[dsp][spread][quality][automation]")
{
    // Ramp detune 0 -> 15 cents over ~100 ms (updated per 256-sample
    // block, smoothed per sample inside) while processing a 1 kHz sine;
    // the largest sample-to-sample output step must stay within 0.5 dB of
    // a static render's largest step (block-stepped detune produced hard
    // discontinuities here).
    const auto maxStep = [] (bool ramp)
    {
        SpreadPitch spread;
        spread.setDetuneCents (ramp ? 0.0f : 15.0f);
        spread.setTimeScale (1.0f);
        spread.setWidth (1.0f);
        spread.prepare (makeMonoInputSpec (256));

        constexpr int blockSize = 256;
        constexpr int numBlocks = 64; // ~340 ms
        float previous = 0.0f;
        float largest = 0.0f;

        for (int b = 0; b < numBlocks; ++b)
        {
            if (ramp)
            {
                const auto progress = juce::jlimit (0.0f, 1.0f, static_cast<float> (b * blockSize) / (0.1f * static_cast<float> (testSampleRate)));
                spread.setDetuneCents (15.0f * progress);
            }

            juce::AudioBuffer<float> buffer (2, blockSize);
            TestHelpers::fillWithSine (buffer, testSampleRate, 1000.0, 0.5f, b * blockSize);
            juce::dsp::AudioBlock<float> block (buffer);
            spread.process (block);

            const auto* data = buffer.getReadPointer (0);
            for (int i = 0; i < blockSize; ++i)
            {
                largest = juce::jmax (largest, std::abs (data[i] - previous));
                previous = data[i];
            }
        }

        return largest;
    };

    const auto staticStep = maxStep (false);
    const auto rampedStep = maxStep (true);

    INFO ("max per-sample step: static = " << staticStep << ", ramped = " << rampedStep);
    CHECK (rampedStep <= staticStep * std::pow (10.0f, 0.5f / 20.0f));
}

//==============================================================================
// Period-adaptive splice (issue #19).
//
// WHY THE A/B: the ripple mechanism derived above is env^2 = 1 +
// sin(2*pi*p)*cos(2*pi*f*sep/fs) - its depth is set purely by where the
// note's frequency lands against the tap separation. The smart splice snaps
// the separation to a multiple of the detected input period (cos -> +1 at
// the fundamental and EVERY harmonic) and blends the window law toward the
// amplitude-complementary sin^2 pair, whose coherent in-phase sum is
// constant. setSmartSplice() exposes both paths so these cases measure the
// same build against its own pinned v0.5.0 behaviour.
namespace
{
    // Envelope peak-to-trough of an already-rendered stereo buffer's L
    // channel: rectify + one-pole LP at 80 Hz, analysed past the settle.
    double envelopeRippleDb (const juce::AudioBuffer<float>& buffer, int settleSamples, double sampleRate)
    {
        const auto* data = buffer.getReadPointer (0);
        float state = 0.0f;
        const auto alpha = static_cast<float> (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 80.0 / sampleRate));

        double envMin = 1.0e9, envMax = 0.0;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            state += alpha * (std::abs (data[i]) - state);

            if (i >= settleSamples)
            {
                envMin = std::min (envMin, static_cast<double> (state));
                envMax = std::max (envMax, static_cast<double> (state));
            }
        }

        return 20.0 * std::log10 (envMax / std::max (envMin, 1.0e-12));
    }

    // Ripple on a sustained tone, with settle long enough for the detector +
    // separation slew to converge (~1.4 s worst case) and analysis covering
    // at least one full crossfade sweep cycle at 15 cents detune (~3.2 s).
    // Rate is a parameter: the detector's whole geometry (decimation, lag
    // range, ring size) derives from it in prepare().
    double smartSpliceRippleDb (double frequencyHz, bool smartSplice, double sampleRate = testSampleRate)
    {
        const auto settleSamples = static_cast<int> (2.5 * sampleRate);
        const auto analysisSamples = static_cast<int> (3.5 * sampleRate);
        const auto total = settleSamples + analysisSamples;

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (total);
        spec.numChannels = 2;

        SpreadPitch spread;
        spread.setSmartSplice (smartSplice);
        spread.setDetuneCents (15.0f);
        spread.setTimeScale (1.0f);
        spread.setWidth (1.0f);
        spread.prepare (spec);

        juce::AudioBuffer<float> buffer (2, total);
        TestHelpers::fillWithSine (buffer, sampleRate, frequencyHz, 0.5f);
        juce::dsp::AudioBlock<float> block (buffer);
        spread.process (block);

        return envelopeRippleDb (buffer, settleSamples, sampleRate);
    }
}

TEST_CASE ("Spread: period-adaptive splice bounds sustained-tone ripple at every probe, incl. the worst comb tooth", "[dsp][spread][quality][smartsplice]")
{
    // Probe set: two anti-phase teeth of the 33.3 Hz separation comb
    // ((k+0.5)/30 ms - the deep-null frequencies), one in-phase tooth and
    // the golden's 220 Hz. Today's pinned behaviour is note-dependent comb
    // luck; the smart splice must flatten ALL of them below a uniform bar.
    const double probes[] = { 6.5 / 0.030, 220.0, 300.0, 9.5 / 0.030 };

    std::vector<double> offDb, onDb;

    for (const auto f : probes)
    {
        offDb.push_back (smartSpliceRippleDb (f, false));
        onDb.push_back (smartSpliceRippleDb (f, true));
        INFO ("f = " << f << " Hz: OFF " << offDb.back() << " dB, ON " << onDb.back() << " dB");

        // Uniform bar: no note-dependence survives (the OFF path swings
        // from ~4 dB to >18 dB across these same probes; ON measures
        // 1.5-2.1 dB on this build). The bar sits BELOW the 3.01 dB that a
        // separation-only fix would give (in-phase taps through the plain
        // equal-power window sum to a sqrt(2) mid-crossfade bump), so it
        // also pins that the sin^2 window blend actually engages.
        CHECK (onDb.back() <= 2.6);
    }

    const auto maxOff = *std::max_element (offDb.begin(), offDb.end());
    const auto maxOn = *std::max_element (onDb.begin(), onDb.end());

    INFO ("worst probe: OFF " << maxOff << " dB, ON " << maxOn << " dB");

    // The anti-phase teeth must show the pinned pathology (this is what
    // makes the A/B meaningful) and the smart splice must collapse it.
    CHECK (maxOff >= 15.0);
    CHECK (maxOn <= maxOff - 10.0);
}

TEST_CASE ("Spread: smart splice holds at 44.1 kHz (rate-derived detector geometry)", "[dsp][spread][quality][smartsplice]")
{
    // The detector's decimation factor, lag range, correlation window and
    // ring size all derive from the sample rate in prepare(); the comb
    // teeth sit at the same FREQUENCIES (the separation is a time), so the
    // worst 48 kHz probe is the worst 44.1 kHz probe too.
    const double worstToothHz = 6.5 / 0.030;

    const auto offDb = smartSpliceRippleDb (worstToothHz, false, 44100.0);
    const auto onDb = smartSpliceRippleDb (worstToothHz, true, 44100.0);

    INFO ("44.1 kHz worst tooth: OFF " << offDb << " dB, ON " << onDb << " dB");
    CHECK (offDb >= 12.0);
    CHECK (onDb <= 2.6);
}

TEST_CASE ("Spread: detector survives a NaN/Inf burst and re-acquires without reset()", "[dsp][spread][robustness][smartsplice]")
{
    // The engine only sanitizes the bus SUM, so garbage reaches the bus
    // modules themselves. The delay lines self-flush in ~180 ms, but the
    // detector's recursive one-poles would latch a NaN forever without
    // their input guard - and a latched detector silently reverts the bus
    // to the fixed-separation comb for the rest of the session. Feed a
    // burst, then a worst-tooth tone, and require the ripple bar WITHOUT
    // any reset() call in between.
    constexpr int burstSamples = 512;
    const double worstToothHz = 6.5 / 0.030;
    constexpr int settleSamples = 144000; // 3 s: delay-line flush + re-acquisition + slew
    constexpr int analysisSamples = 168000;

    SpreadPitch spread;
    spread.setDetuneCents (15.0f);
    spread.setTimeScale (1.0f);
    spread.setWidth (1.0f);
    spread.prepare (makeMonoInputSpec (settleSamples + analysisSamples));

    juce::AudioBuffer<float> burst (2, burstSamples);
    for (int channel = 0; channel < 2; ++channel)
    {
        auto* data = burst.getWritePointer (channel);

        for (int i = 0; i < burstSamples; ++i)
            data[i] = (i % 3 == 0) ? std::numeric_limits<float>::quiet_NaN()
                    : (i % 7 == 0) ? std::numeric_limits<float>::infinity()
                                   : 0.5f;
    }

    juce::dsp::AudioBlock<float> burstBlock (burst);
    spread.process (burstBlock);

    juce::AudioBuffer<float> tone (2, settleSamples + analysisSamples);
    TestHelpers::fillWithSine (tone, testSampleRate, worstToothHz, 0.5f);
    juce::dsp::AudioBlock<float> toneBlock (tone);
    spread.process (toneBlock);

    // Output must be clean again once the delay lines have flushed. The
    // ripple is measured on this trimmed view too: the follower is itself
    // a recursive filter, so running it across the poisoned head would
    // latch NaN and return a meaningless -inf.
    juce::AudioBuffer<float> tail (tone.getArrayOfWritePointers(), 2, 48000, settleSamples + analysisSamples - 48000);
    CHECK (TestHelpers::allSamplesFinite (tail));

    // ...and the smart splice must have RE-ENGAGED: a latched detector
    // would leave the worst tooth at its >15 dB comb ripple.
    const auto rippleDb = envelopeRippleDb (tail, settleSamples - 48000, testSampleRate);
    INFO ("post-burst worst-tooth ripple = " << rippleDb << " dB");
    CHECK (rippleDb >= 0.0); // a real measurement, not a NaN-latched follower
    CHECK (rippleDb <= 2.6);
}

TEST_CASE ("Spread: smart splice stays click-free through a pitch step", "[dsp][spread][quality][smartsplice][automation]")
{
    // A note change while fully engaged fires everything at once: the
    // snapped target jumps, confidence dips, the blend collapses via its
    // alignment gate and the quieter tap slews to the new separation. None
    // of that may click. Phase-continuous input (the step happens in
    // frequency, not in the waveform), so every output discontinuity is the
    // module's own. Bound the stepped render's largest sample-to-sample
    // jump against static renders at both pitches - same technique as the
    // detune-automation case.
    constexpr int half = 120000; // 2.5 s per segment
    constexpr double fA = 220.0;
    constexpr double fB = 246.94; // B3 - lands on a different snapped separation

    const auto maxStep = [] (double firstHz, double secondHz)
    {
        SpreadPitch spread;
        spread.setDetuneCents (15.0f);
        spread.setTimeScale (1.0f);
        spread.setWidth (1.0f);
        spread.prepare (makeMonoInputSpec (2 * half));

        juce::AudioBuffer<float> buffer (2, 2 * half);
        double phase = 0.0;

        for (int i = 0; i < 2 * half; ++i)
        {
            phase += juce::MathConstants<double>::twoPi * (i < half ? firstHz : secondHz) / testSampleRate;
            const auto value = static_cast<float> (0.5 * std::sin (phase));
            buffer.setSample (0, i, value);
            buffer.setSample (1, i, value);
        }

        juce::dsp::AudioBlock<float> block (buffer);
        spread.process (block);

        const auto* data = buffer.getReadPointer (0);
        float previous = 0.0f;
        float largest = 0.0f;

        for (int i = 0; i < 2 * half; ++i)
        {
            largest = juce::jmax (largest, std::abs (data[i] - previous));
            previous = data[i];
        }

        return largest;
    };

    // The higher pitch has larger natural per-sample steps, so the bar is
    // the worst of the two STATIC renders - the stepped render may only add
    // the 0.5 dB margin on top of what the input itself does.
    const auto staticWorst = juce::jmax (maxStep (fA, fA), maxStep (fB, fB));
    const auto stepped = maxStep (fA, fB);

    INFO ("max per-sample step: static worst = " << staticWorst << ", stepped 220->246.94 Hz = " << stepped);
    CHECK (stepped <= staticWorst * std::pow (10.0f, 0.5f / 20.0f));
}

TEST_CASE ("Spread: smart splice never engages on non-periodic treble-band material", "[dsp][spread][quality][smartsplice]")
{
    // Consonant/breath proxy: deterministic white noise double-differenced
    // (~12 dB/oct highpass hinged at Nyquist) so essentially no energy
    // reaches the detector's ~1.2 kHz lowpass. Both the NACF confidence and
    // the spectral plausibility gate must hold the smart path on the
    // nominal separation - output identical to the pinned v0.5.0 path.
    const auto render = [] (bool smartSplice)
    {
        constexpr int total = 96000;

        SpreadPitch spread;
        spread.setSmartSplice (smartSplice);
        spread.setDetuneCents (10.0f);
        spread.setTimeScale (1.0f);
        spread.setWidth (1.0f);
        spread.prepare (makeMonoInputSpec (total));

        juce::AudioBuffer<float> buffer (2, total);
        juce::Random rng (42);
        float w1 = 0.0f, w2 = 0.0f;

        for (int i = 0; i < total; ++i)
        {
            const auto w = rng.nextFloat() * 2.0f - 1.0f;
            const auto sample = 0.25f * (w - 2.0f * w1 + w2);
            w2 = w1;
            w1 = w;
            buffer.setSample (0, i, sample);
            buffer.setSample (1, i, sample);
        }

        juce::dsp::AudioBlock<float> block (buffer);
        spread.process (block);
        return buffer;
    };

    const auto off = render (false);
    const auto on = render (true);

    double maxAbsDiff = 0.0;

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto* a = off.getReadPointer (ch);
        const auto* b = on.getReadPointer (ch);

        for (int i = 0; i < off.getNumSamples(); ++i)
            maxAbsDiff = std::max (maxAbsDiff, static_cast<double> (std::abs (a[i] - b[i])));
    }

    INFO ("max |ON - OFF| on treble-band noise = " << maxAbsDiff);
    CHECK (maxAbsDiff < 1.0e-6);
}

TEST_CASE ("Spread: reset() restores the smart-splice state machine exactly", "[dsp][spread][reset][smartsplice]")
{
    // Drive the detector hard (2 s of a snapping tone), reset, then process
    // a fresh probe: the output must match a factory-fresh instance sample
    // for sample - reset() clearing ALL new state is a suite-wide guardrail
    // (separation, detector ring, sweep machine, smoothers).
    constexpr int primeSamples = 96000;
    constexpr int probeSamples = 48000;

    SpreadPitch used;
    used.setDetuneCents (6.0f);
    used.setTimeScale (1.0f);
    used.setWidth (1.0f);
    used.prepare (makeMonoInputSpec (primeSamples));

    juce::AudioBuffer<float> prime (2, primeSamples);
    TestHelpers::fillWithSine (prime, testSampleRate, 220.0, 0.5f);
    juce::dsp::AudioBlock<float> primeBlock (prime);
    used.process (primeBlock);

    used.reset();

    SpreadPitch fresh;
    fresh.setDetuneCents (6.0f);
    fresh.setTimeScale (1.0f);
    fresh.setWidth (1.0f);
    fresh.prepare (makeMonoInputSpec (primeSamples));

    juce::AudioBuffer<float> probeA (2, probeSamples);
    TestHelpers::fillWithSine (probeA, testSampleRate, 220.0, 0.5f);
    juce::AudioBuffer<float> probeB;
    probeB.makeCopyOf (probeA);

    juce::dsp::AudioBlock<float> blockA (probeA);
    juce::dsp::AudioBlock<float> blockB (probeB);
    used.process (blockA);
    fresh.process (blockB);

    double maxAbsDiff = 0.0;

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto* a = probeA.getReadPointer (ch);
        const auto* b = probeB.getReadPointer (ch);

        for (int i = 0; i < probeSamples; ++i)
            maxAbsDiff = std::max (maxAbsDiff, static_cast<double> (std::abs (a[i] - b[i])));
    }

    INFO ("max |after-reset - fresh| = " << maxAbsDiff);
    CHECK (maxAbsDiff < 1.0e-7);
}

//==============================================================================
// Causal window guard (issue #28).
//
// At timeScale < 1 a voice's base delay shrinks below the nominal 30 ms
// separation (up voice: 15 ms base at timeScale 0.5; down voice: 25 ms), so
// the window [base - sep, base + sep] used to straddle zero delay: a tap
// gliding through the non-causal stretch sat pinned at the 2-sample read
// clamp with window gain up to -3 dB, outputting an unshifted dry copy of
// the input for ~1.7 s per traversal. The fix caps the separation at
// base - 2 inside the voice (plus a silencing fade at the read floor and a
// boosted slew while a live window is still non-causal after automation).
namespace
{
    // Amplitude of the EXACT probe frequency in one channel: Hann-weighted
    // complex projection (a single-bin DFT at a non-integer bin). The
    // shifted taps sit a detune away (~8.7 Hz at 15 cents / 997 Hz) with
    // sidebands spaced at the slow traversal rate, and a multi-second Hann
    // window suppresses them by > 60 dB at the probe frequency - anything
    // that remains there is genuinely unshifted bleed from a pinned tap.
    double toneAmplitudeAt (const juce::AudioBuffer<float>& buffer,
                            int channel,
                            int startSample,
                            int numSamples,
                            double frequencyHz,
                            double sampleRate)
    {
        const auto* data = buffer.getReadPointer (channel);
        double re = 0.0, im = 0.0, windowSum = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto w = 0.5 * (1.0 - std::cos (juce::MathConstants<double>::twoPi * i / (numSamples - 1)));
            const auto phi = juce::MathConstants<double>::twoPi * frequencyHz
                              * static_cast<double> (startSample + i) / sampleRate;

            re += w * static_cast<double> (data[startSample + i]) * std::cos (phi);
            im += w * static_cast<double> (data[startSample + i]) * std::sin (phi);
            windowSum += w;
        }

        return 2.0 * std::sqrt (re * re + im * im) / windowSum;
    }
}

TEST_CASE ("Spread: timeScale 0.5 keeps both voices causal - no unshifted dry bleed (issue #28)", "[dsp][spread][causal]")
{
    constexpr double inputHz = 997.0;
    constexpr float amp = 0.5f;

    const auto settle = static_cast<int> (1.5 * testSampleRate);
    const auto analysis = static_cast<int> (3.5 * testSampleRate);

    // Both smart-splice paths: the capped separation is a geometric
    // property of the voice, not of the detector.
    for (const bool smart : { false, true })
    {
        SpreadPitch spread;
        spread.setSmartSplice (smart);
        spread.setDetuneCents (15.0f);
        spread.setTimeScale (0.5f);
        spread.setWidth (1.0f);
        spread.prepare (makeMonoInputSpec (settle + analysis));

        juce::AudioBuffer<float> buffer (2, settle + analysis);
        TestHelpers::fillWithSine (buffer, testSampleRate, inputHz, amp);
        juce::dsp::AudioBlock<float> block (buffer);
        spread.process (block);

        CHECK (TestHelpers::allSamplesFinite (buffer));

        for (int channel = 0; channel < 2; ++channel)
        {
            const auto bleed = toneAmplitudeAt (buffer, channel, settle, analysis, inputHz, testSampleRate);
            const auto bleedDb = 20.0 * std::log10 (bleed / amp + 1.0e-12);

            // The voice must still be ALIVE and shifting - a fix that just
            // silenced a tap would pass the bleed bar trivially. Two live
            // equal-power taps put the voice near the input's own RMS.
            juce::AudioBuffer<float> channelView (buffer.getArrayOfWritePointers() + channel, 1, 0, settle + analysis);
            const auto levelDb = 20.0 * std::log10 (TestHelpers::tailRms (channelView, settle) / (amp / juce::MathConstants<double>::sqrt2));

            INFO ("smartSplice " << (smart ? "ON" : "OFF") << ", channel " << channel
                  << ": dry bleed " << bleedDb << " dB re input, level " << levelDb << " dB re input RMS");

            // Issue #28 acceptance: no unshifted dry bleed above -40 dB.
            // (Unfixed, the pinned taps measure around -7 dB here.)
            CHECK (bleedDb <= -40.0);
            CHECK (levelDb >= -6.0);
        }
    }
}

TEST_CASE ("Spread: timeScale automation across the causal boundary is click-free and recovers (issue #28)", "[dsp][spread][causal][automation]")
{
    // timeScale 0.5 -> 2 -> 0.5 during a held tone (the issue's acceptance
    // sweep). The downward move is the hard direction: the live window must
    // shrink from 30 ms to under the new 15 ms base - until it has, the
    // causal-floor fade must silence the pinned tap (instead of dry bleed)
    // and the boosted slew must converge the window in well under a second
    // (instead of waiting up to ~8 s for a wrap). Automation happens at
    // block rate through the public setter, smoothed inside.
    constexpr double inputHz = 997.0;
    constexpr float amp = 0.5f;
    constexpr int blockSize = 512;

    const auto segment1 = static_cast<int> (1.5 * testSampleRate); // dwell at 0.5
    const auto segment2 = static_cast<int> (2.0 * testSampleRate); // dwell at 2.0
    const auto segment3 = static_cast<int> (3.0 * testSampleRate); // dwell at 0.5 again
    const auto total = segment1 + segment2 + segment3;

    const auto render = [&] (bool automate)
    {
        SpreadPitch spread;
        spread.setDetuneCents (15.0f);
        spread.setTimeScale (0.5f);
        spread.setWidth (1.0f);
        spread.prepare (makeMonoInputSpec (blockSize));

        juce::AudioBuffer<float> out (2, total);

        for (int start = 0; start < total; start += blockSize)
        {
            if (automate)
                spread.setTimeScale (start < segment1 || start >= segment1 + segment2 ? 0.5f : 2.0f);

            const auto length = std::min (blockSize, total - start);
            juce::AudioBuffer<float> blockBuffer (2, length);
            TestHelpers::fillWithSine (blockBuffer, testSampleRate, inputHz, amp, start);
            juce::dsp::AudioBlock<float> block (blockBuffer);
            spread.process (block);

            for (int channel = 0; channel < 2; ++channel)
                out.copyFrom (channel, start, blockBuffer, channel, 0, length);
        }

        return out;
    };

    const auto renderStatic = [&] (float scale)
    {
        SpreadPitch spread;
        spread.setDetuneCents (15.0f);
        spread.setTimeScale (scale);
        spread.setWidth (1.0f);
        spread.prepare (makeMonoInputSpec (total));

        juce::AudioBuffer<float> buffer (2, total);
        TestHelpers::fillWithSine (buffer, testSampleRate, inputHz, amp);
        juce::dsp::AudioBlock<float> block (buffer);
        spread.process (block);
        return buffer;
    };

    const auto maxStep = [&] (const juce::AudioBuffer<float>& buffer)
    {
        float largest = 0.0f;

        for (int channel = 0; channel < 2; ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);
            float previous = 0.0f;

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                largest = juce::jmax (largest, std::abs (data[i] - previous));
                previous = data[i];
            }
        }

        return largest;
    };

    const auto automated = render (true);
    CHECK (TestHelpers::allSamplesFinite (automated));

    // Click bar: the automated render's largest per-sample step must stay
    // within 0.5 dB of the worst STATIC render (same tone, both dwelled
    // scales) - every re-seat lands at a window edge (gain ~0) and both the
    // fade and the boosted slew are continuous, so automation may not add
    // discontinuities of its own.
    const auto staticWorst = juce::jmax (maxStep (renderStatic (0.5f)), maxStep (renderStatic (2.0f)));
    const auto steppedWorst = maxStep (automated);

    INFO ("max per-sample step: static worst = " << staticWorst << ", automated = " << steppedWorst);
    CHECK (steppedWorst <= staticWorst * std::pow (10.0f, 0.5f / 20.0f));

    // Recovery bar: within a second of the downward move the live windows
    // must be causal again - measure dry bleed over the final two seconds
    // of the last 0.5 dwell (the boosted slew converges in ~0.75 s; the old
    // wrap-luck path would still be bleeding here).
    const auto recoveryStart = segment1 + segment2 + static_cast<int> (1.0 * testSampleRate);
    const auto recoveryLength = total - recoveryStart;

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto bleed = toneAmplitudeAt (automated, channel, recoveryStart, recoveryLength, inputHz, testSampleRate);
        const auto bleedDb = 20.0 * std::log10 (bleed / amp + 1.0e-12);

        INFO ("post-automation dry bleed, channel " << channel << ": " << bleedDb << " dB re input");
        CHECK (bleedDb <= -40.0);
    }
}

TEST_CASE ("Spread: reset() clears both micro-pitch delay lines", "[dsp][spread][reset]")
{
    SpreadPitch spread;
    spread.setDetuneCents (6.0f);
    spread.prepare (makeMonoInputSpec (4096));

    juce::AudioBuffer<float> buffer (2, 4096);
    TestHelpers::fillWithSine (buffer, testSampleRate, 500.0, 0.5f);
    juce::dsp::AudioBlock<float> block (buffer);
    spread.process (block);

    spread.reset();

    juce::AudioBuffer<float> silence (2, 4096);
    silence.clear();
    juce::dsp::AudioBlock<float> silentBlock (silence);
    spread.process (silentBlock);

    CHECK (TestHelpers::peakAbsolute (silence) < 1.0e-6f);
}
