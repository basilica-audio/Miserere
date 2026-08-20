#pragma once

#include <juce_dsp/juce_dsp.h>

#include <atomic>

// The final safety stage (issue #24): a zero-latency, no-lookahead
// SAMPLE-PEAK ceiling placed after the Out Trim, i.e. after everything else
// in MiserereEngine::process().
//
// HONESTY NOTE - THIS IS NOT A TRUE-PEAK LIMITER, and cannot be:
// issue #24 mandates zero latency and no lookahead (the suite's core
// guarantee, docs/adr/0003). A true-peak ceiling requires detecting the
// RECONSTRUCTED (oversampled) waveform, and any oversampled detector's
// polyphase/half-band filters are themselves delays - honouring their
// verdict on the sample they describe means holding the audio back by that
// delay, which is lookahead by another name. What this stage guarantees is
// therefore exactly what its name says: no OUTPUT SAMPLE exceeds the
// ceiling. The analogue waveform a converter reconstructs between those
// samples still can, and tests/OutputLimiterTests.cpp measures that
// inter-sample overshoot with an 8x windowed-sinc reconstruction and
// regression-freezes the figure, which docs/manual.md quotes. Set the
// ceiling with that headroom in mind; do not treat this as a mastering-grade
// true-peak brickwall.
//
// Gain computer (infinite ratio, soft knee, all in dB):
//
//   u   = clamp(xDb - (ceilingDb - knee/2), 0, knee)
//   yDb = min(xDb - u^2/(2*knee), ceilingDb)
//
// The quadratic term is the standard soft-knee interpolation (Zoelzer,
// DAFX): it leaves the curve untouched below ceiling - knee/2, joins the
// flat ceiling with matching slope at ceiling + knee/2, and its maximum
// over the knee region is EXACTLY ceilingDb (at u = knee the expression is
// ceiling + knee/2 - knee/2). The outer min() is what makes the ratio
// infinite above the knee rather than 1:1.
//
// Ballistics:
// - **Instantaneous attack** - `gain = min(gain, staticGain)` with no
//   ramp. The only way to hit a hard ceiling without lookahead. It is also
//   why this stage is a SAFETY limiter and not a colour device: an
//   instantaneous gain drop on a bass-heavy signal is audible as distortion
//   long before it is audible as level control.
// - **One-pole release** approaching the static gain FROM BELOW
//   (`gain += coeff*(staticGain - gain)`, coeff in (0, 1)) - a first-order
//   step response can never overshoot its target, so recovery alone can
//   never push a sample back over the ceiling.
// - Once the signal is clear of the knee the static gain is exactly 1.0f
//   and the release lands within `unitySnapEpsilon` of unity, where the
//   gain SNAPS to exactly 1.0f: multiplying by exactly 1.0f is bit-exact
//   for every finite float, so a settled limiter is a wire, not a
//   near-wire. The snap is only taken while the static gain is itself >= 1
//   (nothing to limit), so it can never round a limiting gain UP over the
//   ceiling.
//
// Detection is permanently L/R-LINKED (peak across all channels), and
// deliberately NOT wired to the global `link` voicing control: `link`
// chooses dual-mono vs. linked detection for CRUSH/SANDWICH, where dual
// mono is a documented part of the sound. Unlinked LIMITING is a different
// matter - it applies different gains to L and R on the summed output and
// so pans the stereo image on every peak. A safety stage must not move the
// image.
//
// Disabled (the default) is a bit-exact bypass: process() returns without
// touching a single sample, and parks the loop at unity so re-engaging
// starts from a settled wire.
//
// Real-time safe: no allocation, no locks, no branches on host state. The
// transcendentals in the gain computer are only reached while the signal is
// inside or above the knee - below it the computer early-returns unity.
class OutputLimiter
{
public:
    OutputLimiter() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);

    // Clears the gain loop back to unity (settled wire) without
    // deallocating.
    void reset() noexcept;

    void setEnabled (bool shouldBeEnabled) noexcept { enabled = shouldBeEnabled; }

    // -12...0 dBFS (the parameter range). Smoothed over 50 ms so a moving
    // ceiling never zippers; every sample is still evaluated against the
    // ceiling in force AT that sample, so the guarantee holds throughout
    // the ramp.
    void setCeilingDb (float newCeilingDb) noexcept;

    // 5...500 ms, 63 %-recovery one-pole definition (the same convention
    // the rest of the plugin's release controls use).
    void setReleaseMs (float newReleaseMs) noexcept;

    // Processes `block` in place. A zero-sample block is a safe no-op. No
    // allocation occurs here.
    void process (juce::dsp::AudioBlock<float>& block) noexcept;

    // Current gain reduction in dB (positive = reduction), the deepest
    // value reached in the last processed block - exposed for metering and
    // tests. Written once per block on the audio thread, read by the GUI
    // meter timer: relaxed atomic on both sides.
    float getCurrentGainReductionDb() const noexcept { return currentGainReductionDb.load (std::memory_order_relaxed); }

    // The pure static gain computer (linear peak + ceiling in dB -> linear
    // gain <= 1), exposed so tests can assert the curve directly.
    static float staticGainFor (float peak, float ceilingDb) noexcept;

    // Soft-knee width in dB, centred on the ceiling. 3 dB is narrow enough
    // that the stage stays inaudible until it is genuinely needed, wide
    // enough that the onset of gain reduction is not a step in the
    // derivative.
    static constexpr float kneeDb = 3.0f;

    // Linear-gain distance from unity within which a RECOVERING gain snaps
    // to exactly 1.0f. ~1e-7 is just above the float epsilon at 1.0
    // (1.19e-7), so the snap costs at most one ulp of level and buys a
    // bit-exact settled wire.
    static constexpr float unitySnapEpsilon = 1.0e-7f;

private:
    static constexpr double ceilingSmoothingSeconds = 0.05;

    void updateReleaseCoefficient() noexcept;

    double sampleRate = 44100.0;

    bool enabled = false;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> ceilingDbSmoothed;
    float lastCeilingDb = -0.3f;

    float releaseMs = 60.0f;
    float releaseCoefficient = 1.0f;

    // The limiter's entire loop state: one gain, shared by every channel
    // (see the linked-detection note above).
    //
    // DOUBLE, deliberately: a float one-pole STALLS short of its target
    // long before it reaches unitySnapEpsilon - once
    // releaseCoefficient*(1 - gain) drops below half a float ulp at 1.0
    // (5.96e-8) the increment rounds away entirely, parking the gain at
    // ~1 - 1.6e-4 (60 ms release) or ~1 - 1.3e-3 (500 ms release) for ever,
    // i.e. a permanent, silent ~0.01 dB attenuation after any peak. In
    // double the stall point sits around 1e-13, far below the snap
    // threshold, so recovery genuinely completes. The cast back to float is
    // exact at 1.0, so the settled wire stays bit-exact.
    double currentGain = 1.0;

    std::atomic<float> currentGainReductionDb { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputLimiter)
};
