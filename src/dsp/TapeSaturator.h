#pragma once

#include <cmath>

// A single tanh soft-saturation curve with a drive-dependent makeup
// compensation, shared between Bus A's Tape Sat module (TapeSat.h) and Bus
// D's Slap Delay feedback loop (SlapDelay.h) - both are "tape-soft"
// character per the design brief. Pure, allocation-free, and stateless, so
// it is unit-testable in isolation and safe to call per-sample from the
// audio thread.
//
//   y = tanh(g * x) * comp(g),  comp(g) = a / tanh(g * a)
//
// where g is the linear drive gain (>= 1) and a is a fixed nominal
// operating level (0.125 == -18 dBFS, a typical vocal RMS ballpark). The
// compensation is the brief's "auto-compensated" behaviour, anchored at the
// nominal level rather than at full scale:
//
//   - A signal sitting at the nominal level keeps (close to) unity gain for
//     ANY drive amount - turning drive up adds saturation density, not
//     loudness, which is what an auto-compensated drive control should do.
//   - As g -> 1 (drive -> 0 dB), comp(g) -> a/tanh(a) ~= 1.0052 and the
//     whole curve approaches identity for program-level signals, so the
//     structural bit-exact bypass at exactly 0 dB drive (see TapeSat.h) is
//     reached continuously, without a level jump at the end of the
//     parameter ramp.
//   - Peaks well above the nominal level compress toward it - that is the
//     saturation itself, not a compensation error.
namespace TapeSaturator
{
    // -18 dBFS: the curve's nominal ("0 VU"-style) operating level.
    inline constexpr float nominalLevel = 0.125f;

    // comp(g) for a given linear drive gain. One tanh call - compute once
    // per block (or once in prepare() for a fixed drive) and pass the
    // result to processSample(), rather than paying it per sample.
    inline float compensationForDrive (float driveGainLinear) noexcept
    {
        const auto safeDrive = driveGainLinear > 1.0e-6f ? driveGainLinear : 1.0e-6f;
        return nominalLevel / std::tanh (safeDrive * nominalLevel);
    }

    inline float processSample (float x, float driveGainLinear, float compensation) noexcept
    {
        return std::tanh (driveGainLinear * x) * compensation;
    }
}
