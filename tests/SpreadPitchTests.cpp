#include "dsp/SpreadPitch.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
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
    // Envelope peak-to-trough on a sustained tone, with settle long enough
    // for the detector + separation slew to converge (~1.4 s worst case)
    // and analysis covering at least one full crossfade sweep cycle at
    // 15 cents detune (~3.2 s).
    double smartSpliceRippleDb (double frequencyHz, bool smartSplice)
    {
        constexpr int settleSamples = 120000; // 2.5 s
        constexpr int analysisSamples = 168000; // 3.5 s
        constexpr int total = settleSamples + analysisSamples;

        SpreadPitch spread;
        spread.setSmartSplice (smartSplice);
        spread.setDetuneCents (15.0f);
        spread.setTimeScale (1.0f);
        spread.setWidth (1.0f);
        spread.prepare (makeMonoInputSpec (total));

        juce::AudioBuffer<float> buffer (2, total);
        TestHelpers::fillWithSine (buffer, testSampleRate, frequencyHz, 0.5f);
        juce::dsp::AudioBlock<float> block (buffer);
        spread.process (block);

        const auto* data = buffer.getReadPointer (0);
        float state = 0.0f;
        const auto alpha = static_cast<float> (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 80.0 / testSampleRate));

        double envMin = 1.0e9, envMax = 0.0;

        for (int i = 0; i < total; ++i)
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
}

TEST_CASE ("Spread: period-adaptive splice bounds sustained-tone ripple at every probe, incl. the worst comb tooth", "[dsp][spread][quality][smartsplice]")
{
    // Probe set: three anti-phase teeth of the 33.3 Hz separation comb
    // ((k+0.5)/30 ms - the deep-null frequencies), one in-phase tooth and
    // the golden's 220 Hz. Today's pinned behaviour is note-dependent comb
    // luck; the smart splice must flatten ALL of them below a uniform bar.
    const double probes[] = { 6.5 / 0.030, 220.0, 7.5 / 0.030, 300.0, 9.5 / 0.030 };

    std::vector<double> offDb, onDb;

    for (const auto f : probes)
    {
        offDb.push_back (smartSpliceRippleDb (f, false));
        onDb.push_back (smartSpliceRippleDb (f, true));
        INFO ("f = " << f << " Hz: OFF " << offDb.back() << " dB, ON " << onDb.back() << " dB");

        // Uniform bar: no note-dependence survives (the OFF path swings
        // from ~4 dB to >18 dB across these same probes; ON measures
        // 1.5-2.1 dB on this build - the bar leaves platform margin).
        CHECK (onDb.back() <= 3.5);
    }

    const auto maxOff = *std::max_element (offDb.begin(), offDb.end());
    const auto maxOn = *std::max_element (onDb.begin(), onDb.end());

    INFO ("worst probe: OFF " << maxOff << " dB, ON " << maxOn << " dB");

    // The anti-phase teeth must show the pinned pathology (this is what
    // makes the A/B meaningful) and the smart splice must collapse it.
    CHECK (maxOff >= 15.0);
    CHECK (maxOn <= maxOff - 10.0);
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
