#pragma once

#include "RealtimeCoefficients.h"
#include "TapeSaturator.h"

#include <juce_dsp/juce_dsp.h>

// Bus A's tape-style saturation stage: drive 0-24 dB into tanh with a
// pre-emphasis/de-emphasis shelf pair, auto-compensated (brief).
//
// Signal flow (when driven):
//
//   x -> pre-emphasis (+6 dB high shelf @ 3 kHz)
//     -> tanh(drive * x) * comp(drive)   (TapeSaturator.h - comp() anchors
//                                         unity gain at a -18 dBFS nominal
//                                         level: the "auto-compensated"
//                                         behaviour)
//     -> de-emphasis (-6 dB high shelf @ 3 kHz)
//
// The pre/de-emphasis pair mimics tape record/playback EQ: highs are boosted
// into the nonlinearity (so they saturate earlier, like tape's HF
// self-erasure) and cut back afterwards, which keeps the perceived tonal
// balance while the *distortion products* stay biased toward the tape-like
// darkened top end. The two shelves are exact spectral inverses (same
// frequency/Q, gains G and 1/G - RBJ shelves with reciprocal gains are
// reciprocal transfer functions), so the linear part of the path stays flat.
//
// Drive == 0 dB (the parameter's minimum) is a BIT-EXACT bypass: process()
// returns without touching the block at all. This is required by the M1
// null-test guarantee ("drive 0") - even a unity-gain pass through the
// emphasis pair and tanh identity limit would accumulate float rounding, so
// the bypass is structural, not numeric. The transition is still continuous
// at program levels: as drive -> 0 dB from above, tanh(g*x)*comp(g)
// approaches the identity for signals around the nominal level (see
// TapeSaturator.h).
//
// Minimum-phase IIR shelves + a memoryless nonlinearity: zero added latency,
// keeping Bus A sample-aligned per the parallel-bus phase discipline. No
// oversampling in M1 - at vocal-level drive amounts the tanh curve's
// aliasing products sit well below the program material; an oversampled
// upgrade is an M2+ voicing decision (see docs/architecture.md).
class TapeSat
{
public:
    TapeSat() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    // 0 dB is a bit-exact bypass (see class comment).
    void setDriveDb (float newDriveDb) noexcept;

    void process (juce::dsp::AudioBlock<float>& block) noexcept;

private:
    static constexpr float emphasisFreqHz = 3000.0f;
    static constexpr float emphasisGainDb = 6.0f;
    static constexpr float emphasisQ = juce::MathConstants<float>::sqrt2 / 2.0f;
    static constexpr double smoothingTimeSeconds = 0.05;

    using Duplicator = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;

    double sampleRate = 44100.0;

    Duplicator preEmphasis { msrr::makeIdentityBiquad() };
    Duplicator deEmphasis { msrr::makeIdentityBiquad() };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> driveDbSmoothed;
    float lastDriveDb = 6.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeSat)
};
