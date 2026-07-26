#pragma once

#include <cmath>

// First-order antiderivative anti-aliasing (ADAA1) in RESIDUAL FORM for the
// suite's memoryless saturation curves (v0.5.0 "Circuit Engines" release,
// brief F1 - binding, not an implementation choice).
//
// Plain (whole-curve) ADAA1 evaluates
//
//   y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])
//
// with F the antiderivative of the curve f (Parker/Zavalishin/Le Bivic,
// "Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time
// Convolution", DAFx-16). Its LINEAR-regime response is exactly
// (x[n] + x[n-1])/2 - a half-sample delay convolved with a cos(pi*f/fs)
// lowpass (-11.7 dB @ 20 kHz @ 48 k). Applied whole-curve and in-line that
// would (a) smear a single-sample impulse across two samples, violating the
// suite's parallel-bus sample-alignment invariant (docs/adr/0003,
// tests/NullAndAlignmentTests.cpp), and (b) impose an uncompensated
// sample-rate-dependent HF droop on the direct path.
//
// Residual form therefore decomposes every curve into an exactly-aligned
// linear part plus an ADAA-treated nonlinear residual:
//
//   f(x)   = k*x + r(x),      k = f'(0)  (small-signal gain, incl. drive)
//   y[n]   = k*x[n] + y_r[n]
//   y_r[n] = (R(x[n]) - R(x[n-1])) / (x[n] - x[n-1])   if |x[n]-x[n-1]| > eps
//          = r((x[n] + x[n-1]) / 2)                     otherwise
//
// with R the antiderivative of the residual r = f - k*id, eps = 1e-5.
// ADAA1 is linear in the curve, so ADAA1(f) = ADAA1(k*id) + ADAA1(r) and the
// k*id term aliases in neither rendering: the residual carries ALL
// distortion products and receives the full ADAA treatment, while the
// half-sample smear applies only to distortion products - no HF droop, no
// impulse spreading, no phase-discipline violation (asserted by
// tests/AliasingTests.cpp's linear-path flatness/group-delay cases and the
// unmodified alignment tests).
//
// Parallel-delta rule (binding): where a nonlinear delta is summed against a
// linear path (FetCrush LF-extract delta, ConsoleEq iron term), the delta IS
// the ADAA residual y_r - no midpoint evaluation of any linear leg. Since
// drive -> 0 implies r == 0 structurally, neutral nulls are exact by
// construction.
//
// State per channel per stage: one previous input (double) plus its cached
// antiderivative - reset in reset(), recomputed on parameter change
// (prepareBlock()). Antiderivatives and divided differences are evaluated in
// double: the R(x) expressions subtract nearly-equal quantities at small
// amplitudes and the divided difference amplifies that error by
// 1/(x - x[n-1]); float internals would surface around the test suite's
// -80..-120 dB null bars.
//
// EXEMPTION RULE (binding, brief F1): OptoLeveler's always-on post-colour
// tanh (fixed drive 1.15) is ADAA-EXEMPT and keeps its memoryless per-sample
// evaluation - even the residual's neighbour-sample contribution (~2.8e-5 at
// the alignment tests' operating point) would break the off-impulse
// REQUIRE < 1e-5 bar, while at drive 1.15 the stage is near-linear and its
// alias floor is guarded by tests/AliasingTests.cpp (<= -80 dBFS). The same
// exemption applies to any future always-on near-linear stage on a
// phase-critical bus; a failing guard is a project-owner escalation, never a
// silent ADAA retrofit.
namespace msrr::adaa
{
    inline constexpr double illConditionEpsilon = 1.0e-5;

    // ln(cosh(x)) without overflow: ln cosh x = |x| + log1p(e^(-2|x|)) - ln 2.
    // Exact for all x (never calls cosh on large arguments).
    inline double logCosh (double x) noexcept
    {
        const auto ax = std::abs (x);
        return ax + std::log1p (std::exp (-2.0 * ax)) - 0.69314718055994531;
    }

    // Test-only hook (see tests/AliasingTests.cpp): when true, every
    // residual is evaluated memoryless (no ADAA divided difference), so the
    // aliasing suite can measure the ADAA-on vs ADAA-off spur delta. A
    // runtime flag rather than a compile-time switch because the Tests
    // binary compiles the identical SharedCode sources as the plugin
    // (Pamplejuce pattern - a #define would fork the build). Never touched
    // by product code; costs one predictable branch per sample.
    inline bool& bypassForTests() noexcept
    {
        static bool bypass = false;
        return bypass;
    }

    namespace detail
    {
        // Shared divided-difference plumbing. Derived curves provide
        // residualValue(x) and residualAntiderivative(x) via the CRTP-ish
        // function-pointer-free pattern below (plain member calls - each
        // curve struct owns its own state and parameters).
        template <typename Curve>
        inline float residualStep (Curve& c, float x) noexcept
        {
            const double xd = static_cast<double> (x);

            double out;

            if (bypassForTests())
            {
                out = c.residualValue (xd);
                c.x1 = xd;
                c.R1 = c.residualAntiderivative (xd);
                return static_cast<float> (out);
            }

            if (std::abs (xd - c.x1) > illConditionEpsilon)
            {
                const auto R0 = c.residualAntiderivative (xd);
                out = (R0 - c.R1) / (xd - c.x1);
                c.R1 = R0;
            }
            else
            {
                out = c.residualValue (0.5 * (xd + c.x1));
                c.R1 = c.residualAntiderivative (xd);
            }

            c.x1 = xd;
            return static_cast<float> (out);
        }
    }

    // ---------------------------------------------------------------------
    // f(x) = comp * tanh(g*x)   (TapeSaturator curve: TapeSat, SlapDelay
    // tap, ConsoleEq odd term - NOT OptoLeveler colour, see exemption).
    //
    //   k = comp*g
    //   r = comp*(tanh(g*x) - g*x)
    //   R = comp*(lnCosh(g*x)/g - g*x^2/2)
    struct TanhStage
    {
        void reset() noexcept
        {
            x1 = 0.0;
            R1 = 0.0;
        }

        // Call at block rate whenever drive/compensation may have changed -
        // re-caches the previous sample's antiderivative under the NEW
        // curve so the next divided difference stays consistent.
        void prepareBlock (float driveGainLinear, float compensation) noexcept
        {
            g = static_cast<double> (driveGainLinear);
            comp = static_cast<double> (compensation);
            k = static_cast<float> (comp * g);
            R1 = residualAntiderivative (x1);
        }

        float smallSignalGain() const noexcept { return k; }

        // Full in-line stage: y = k*x + y_r.
        float processSample (float x) noexcept
        {
            return k * x + detail::residualStep (*this, x);
        }

        // Residual only (parallel-delta rule).
        float processResidual (float x) noexcept
        {
            return detail::residualStep (*this, x);
        }

        double residualValue (double x) const noexcept
        {
            return comp * (std::tanh (g * x) - g * x);
        }

        double residualAntiderivative (double x) const noexcept
        {
            return comp * (logCosh (g * x) / g - 0.5 * g * x * x);
        }

        double x1 = 0.0;
        double R1 = 0.0;

    private:
        double g = 1.0;
        double comp = 1.0;
        float k = 1.0f;
    };

    // ---------------------------------------------------------------------
    // Asymmetric f(u) = scale * (tanh(g*u + b) - tanh(b))   (ConsoleEq F8
    // iron term with DC premagnetisation, SlapDelay F5 record-side
    // saturator - research-neve-1073.md section 2.4).
    //
    //   k = scale * g * sech^2(b) = scale * g * (1 - tanh^2(b))
    //   r = f - k*u
    //   R = scale * (lnCosh(g*u + b)/g - u*tanh(b)) - k*u^2/2
    struct AsymTanhStage
    {
        void reset() noexcept
        {
            x1 = 0.0;
            R1 = 0.0;
        }

        void prepareBlock (float driveGainLinear, float dcBias, float outputScale) noexcept
        {
            g = static_cast<double> (driveGainLinear);
            b = static_cast<double> (dcBias);
            scale = static_cast<double> (outputScale);
            tanhB = std::tanh (b);
            k = scale * g * (1.0 - tanhB * tanhB);
            R1 = residualAntiderivative (x1);
        }

        float smallSignalGain() const noexcept { return static_cast<float> (k); }

        float processSample (float x) noexcept
        {
            return static_cast<float> (k) * x + detail::residualStep (*this, x);
        }

        float processResidual (float x) noexcept
        {
            return detail::residualStep (*this, x);
        }

        double residualValue (double u) const noexcept
        {
            return scale * (std::tanh (g * u + b) - tanhB) - k * u;
        }

        double residualAntiderivative (double u) const noexcept
        {
            return scale * (logCosh (g * u + b) / g - u * tanhB) - 0.5 * k * u * u;
        }

        double x1 = 0.0;
        double R1 = 0.0;

    private:
        double g = 1.0;
        double b = 0.0;
        double scale = 1.0;
        double tanhB = 0.0;
        double k = 1.0;
    };

    // ---------------------------------------------------------------------
    // f(x) = x*|x| (class-A asymmetry term): k = 0 - already a pure
    // residual; R = |x|^3 / 3. Kept for suite reuse per the brief (FetCrush
    // no longer calls it after the F3 feedback-FET engine replaced the
    // 0.15*x*|x| colour term with the FET epsilon-term).
    struct XAbsStage
    {
        void reset() noexcept
        {
            x1 = 0.0;
            R1 = 0.0;
        }

        void prepareBlock() noexcept { R1 = residualAntiderivative (x1); }

        float processResidual (float x) noexcept
        {
            return detail::residualStep (*this, x);
        }

        double residualValue (double x) const noexcept { return x * std::abs (x); }

        double residualAntiderivative (double x) const noexcept
        {
            const auto ax = std::abs (x);
            return ax * ax * ax / 3.0;
        }

        double x1 = 0.0;
        double R1 = 0.0;
    };
}
