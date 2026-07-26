#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <vector>

// Bus (3) SPREAD (docs/design-brief.md): dual micro-pitch, modelled on the
// documented "dual 910s... 30 and 50 milliseconds approximately, up a tiny
// bit, down a tiny bit" technique (docs/research-notes.md) - two short
// delay taps (~30 ms pitched up, ~50 ms pitched down), hard-panned L/R, at a
// low enough return level that the ear reads "pushed to the outside" rather
// than chorusing.
//
// The module sums its input to mono (the technique is applied to a mono
// vocal source) and feeds two independent "micro-pitch voices", each a
// classic delay-line Doppler pitch shifter: a single delay line read by TWO
// crossfading taps whose read position glides at a rate proportional to the
// desired pitch ratio (read speed != write speed = a pitch shift), each tap
// wrapping and resetting to the opposite phase inside a crossfade window
// before it runs out of buffer.
//
// v0.5.0 quality pass (brief F6):
// - 3rd-order Lagrange interpolation on both shifter delay lines (replaces
//   linear - the interp loss at 10 kHz drops from ~2 dB to under 1 dB,
//   tests/SpreadPitchTests.cpp).
// - detune/timeScale ride per-sample SmoothedValue ramps (50 ms) instead of
//   block-boundary steps - automation is click-free.
// - grain window 40 ms -> 60 ms with an EQUAL-POWER (sin) crossfade
//   replacing the raised-cosine window, taps still half-grain offset: the
//   two tap gains obey g1^2 + g2^2 == 1, so the summed POWER of the
//   (decorrelated) taps stays constant and the periodic envelope ripple on
//   sustained vowels shrinks by more than 6 dB vs v0.4.0.
//
// Voice 0 (base ~30 ms) is detuned UP by `spread_detune` cents and panned
// toward the left; voice 1 (base ~50 ms) is detuned DOWN by the same amount
// and panned toward the right. `spread_width` blends between the two
// voices' fully hard-panned outputs (100%) and an equal centre blend of
// both (0%).
//
// This bus is a delay by design (like SLAP) and is therefore exempt from
// the sample-alignment invariant that busses (1)/(2) must honour - see
// docs/adr/0003. It still reports zero *latency*: the delay is the effect,
// not a compensation artefact.
class SpreadPitch
{
public:
    SpreadPitch() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    void setDetuneCents (float cents) noexcept;
    void setTimeScale (float scale) noexcept;
    void setWidth (float amount01) noexcept;

    // Replaces `block`'s contents with the wet (pitch-shifted) stereo
    // signal. A zero-sample block is a safe no-op. No allocation occurs
    // here. Mono hosts receive the L channel's content only (the caller's
    // block is already channel-count-limited to what prepare() promised).
    void process (juce::dsp::AudioBlock<float>& block) noexcept;

private:
    static constexpr float baseDelayUpMs = 30.0f;
    static constexpr float baseDelayDownMs = 50.0f;
    static constexpr float grainMs = 60.0f; // crossfade window width per voice (F6: was 40 ms)
    static constexpr float maxTimeScale = 2.0f;
    static constexpr float capacityHeadroomMs = 20.0f;
    static constexpr double smoothingTimeSeconds = 0.05;

    struct Voice
    {
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delayLine;
        std::array<float, 2> tapDelaySamples { 0.0f, 0.0f }; // current read delay per crossfading tap
        float pitchRatio = 1.0f;

        void prepare (const juce::dsp::ProcessSpec& monoSpec, float maxDelaySamples);
        void reset (float baseSamples, float grainSamples);
        float processSample (float input, float baseSamples, float grainSamples) noexcept;
    };

    double sampleRate = 44100.0;

    Voice voiceUp;   // ~30 ms, pitched up
    Voice voiceDown; // ~50 ms, pitched down

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> widthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> detuneSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> timeScaleSmoothed;

    float detuneCents = 6.0f;
    float timeScale = 1.0f;
    float width = 0.7f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpreadPitch)
};
