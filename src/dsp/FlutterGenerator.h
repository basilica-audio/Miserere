#pragma once

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <cstdint>

// Wow/flutter generator for the SLAP bus's tape-transport voicing (v0.5.0
// brief F5; research-tape-echo-slap.md sections 2.1/3.2): two quasi-periodic
// oscillators plus a leaky-random-walk drift, each with slow +-10% AM/PM
// wander (one-pole filtered noise, tau ~ 2 s) so nothing is locked-periodic:
//
//   - f1 = 0.9 Hz (pinch roller / wow), relative depth 0.55
//   - f2 = 5.2 Hz (capstan), relative depth 0.30, plus its 2nd harmonic at
//     -10 dB
//   - drift: leaky-integrated white noise, cutoff ~0.2 Hz
//
// Output m(t) modulates the tape speed: tau(t) = tau_target / (1 + m(t)).
// The absolute scale is calibrated so the PEAK relative frequency deviation
// of the read tap - which is tau * dm/dt for slowly-varying m - equals the
// configured wow&flutter percentage (0..0.5 % at wobble 0..1, IEC-style
// 3150 Hz measurement, tests/SlapDelayTests.cpp).
//
// Runs at control rate fs/16 with linear upsampling to audio rate. RNG is a
// per-instance deterministically seeded xorshift32 so renders are exactly
// reproducible; at depth == 0 the generator is structurally OFF: m == 0 and
// the RNG is never advanced (the neutral path stays deterministic and
// bit-identical across runs - brief section 6.7c).
class FlutterGenerator
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        controlInterval = 16;
        controlRate = sampleRate / controlInterval;
        reset();
    }

    void reset() noexcept
    {
        rngState = seed;
        phase1 = 0.0;
        phase2 = 0.0;
        drift = 0.0;
        am1 = am2 = pm1 = pm2 = 0.0;
        currentValue = 0.0;
        targetValue = 0.0;
        samplesUntilUpdate = 0;
        step = 0.0;
    }

    // depth01: the wobble amount (0..1 -> 0..0.5 % W&F).
    // tauSeconds: current delay time (the deviation calibration is
    // referenced to it - slower tape shows more W&F for the same m').
    void setDepth (float depth01, double tauSeconds) noexcept
    {
        depth = juce::jlimit (0.0f, 1.0f, depth01);
        tau = juce::jmax (1.0e-3, tauSeconds);
    }

    // Next audio-rate modulation value m(t) (linear interpolation between
    // control-rate points).
    double getNext() noexcept
    {
        if (depth <= 0.0f)
            return 0.0; // structurally off - no RNG advance, exact zero

        if (samplesUntilUpdate <= 0)
        {
            const auto next = computeControlValue();
            step = (next - targetValue) / static_cast<double> (controlInterval);
            currentValue = targetValue;
            targetValue = next;
            samplesUntilUpdate = controlInterval;
        }

        currentValue += step;
        --samplesUntilUpdate;
        return currentValue;
    }

private:
    // Quasi-periodic component layout (research 3.2 item 2).
    static constexpr double f1Hz = 0.9;
    static constexpr double depth1 = 0.55;
    static constexpr double f2Hz = 5.2;
    static constexpr double depth2 = 0.30;
    static constexpr double harmonic2Level = 0.316; // -10 dB
    static constexpr double driftCutoffHz = 0.2;
    static constexpr double driftDepth = 0.10;
    static constexpr double wanderTauSeconds = 2.0;
    static constexpr double wanderAmount = 0.10;    // +-10 % AM/PM
    static constexpr double maxWowFlutterFraction = 0.005; // 0.5 % at wobble = 1

    // Unit-variance one-pole lowpass of white noise.
    //
    // A plain y = a*y + (1-a)*x collapses the noise as the time constant
    // grows: its output variance is only (1-a)/(1+a) of the input's, which at
    // tau = 2 s and a 3 kHz control rate is -41 dB. Written that way the
    // "+-10 % AM/PM wander" the research specifies actually lands at +-0.05 %,
    // the oscillators stay locked-periodic, and the flutter-lag
    // autocorrelation sits at 0.955 (bar: < 0.95, tests/SlapDelayTests.cpp).
    // Normalising by sqrt((1+a)/(1-a)) restores the intended depth at any
    // control rate or time constant.
    static double onePoleNoise (double state, double coeff, double excitation) noexcept
    {
        const auto normalisation = std::sqrt ((1.0 + coeff) / (1.0 - coeff));
        return coeff * state + (1.0 - coeff) * normalisation * excitation;
    }

    double computeControlValue() noexcept
    {
        const auto T = 1.0 / controlRate;

        // Slow AM/PM wander: one-pole filtered white noise, tau ~ 2 s.
        const auto wanderCoeff = std::exp (-T / wanderTauSeconds);
        am1 = onePoleNoise (am1, wanderCoeff, nextBipolar());
        am2 = onePoleNoise (am2, wanderCoeff, nextBipolar());
        pm1 = onePoleNoise (pm1, wanderCoeff, nextBipolar());
        pm2 = onePoleNoise (pm2, wanderCoeff, nextBipolar());

        phase1 += juce::MathConstants<double>::twoPi * f1Hz * T;
        phase2 += juce::MathConstants<double>::twoPi * f2Hz * T;

        if (phase1 > juce::MathConstants<double>::twoPi)
            phase1 -= juce::MathConstants<double>::twoPi;
        if (phase2 > juce::MathConstants<double>::twoPi)
            phase2 -= juce::MathConstants<double>::twoPi;

        // Leaky-random-walk drift (same normalisation - see onePoleNoise).
        const auto driftCoeff = std::exp (-juce::MathConstants<double>::twoPi * driftCutoffHz * T);
        drift = onePoleNoise (drift, driftCoeff, nextBipolar());

        const auto p1 = phase1 + wanderAmount * pm1;
        const auto p2 = phase2 + wanderAmount * pm2;

        const auto osc = depth1 * (1.0 + wanderAmount * am1) * std::sin (p1)
                       + depth2 * (1.0 + wanderAmount * am2) * (std::sin (p2) + harmonic2Level * std::sin (2.0 * p2 + 1.1))
                       + driftDepth * drift;

        // Calibrate so the peak of tau * |dm/dt| across the component sum
        // equals the configured W&F fraction: each sinusoid contributes
        // depth_k * 2*pi*f_k to |dm/dt|'s peak.
        const auto derivativeSum = depth1 * juce::MathConstants<double>::twoPi * f1Hz
                                 + depth2 * juce::MathConstants<double>::twoPi * (f2Hz + harmonic2Level * 2.0 * f2Hz)
                                 + driftDepth * juce::MathConstants<double>::twoPi * driftCutoffHz;

        const auto wfFraction = static_cast<double> (depth) * maxWowFlutterFraction;
        const auto scale = wfFraction / (tau * derivativeSum);

        return scale * osc;
    }

    double nextBipolar() noexcept
    {
        // xorshift32, deterministic per instance.
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return static_cast<double> (rngState) * (2.0 / 4294967295.0) - 1.0;
    }

    static constexpr std::uint32_t seed = 0x5A17F00Du;

    double sampleRate = 44100.0;
    double controlRate = 44100.0 / 16.0;
    int controlInterval = 16;

    float depth = 0.0f;
    double tau = 0.11;

    std::uint32_t rngState = seed;
    double phase1 = 0.0, phase2 = 0.0;
    double drift = 0.0;
    double am1 = 0.0, am2 = 0.0, pm1 = 0.0, pm2 = 0.0;

    double currentValue = 0.0, targetValue = 0.0, step = 0.0;
    int samplesUntilUpdate = 0;
};
