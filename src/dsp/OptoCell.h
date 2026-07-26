#pragma once

#include <algorithm>
#include <cmath>

// The SANDWICH bus's T4B optical attenuator physics (v0.5.0 brief F4),
// header-only and testable in isolation:
//
// - **EL-panel luminance** (Alfrey-Taylor law, research-opto-la2a.md
//   section 2.3): B = B0 * exp(-b / sqrt(|v| + eps)) - an EVEN function of
//   the drive voltage. The panel is lit directly by the amplified audio
//   waveform (there is NO rectifier in the hardware sidechain); light
//   pulses at 2x the signal frequency and the photocell's own sluggishness
//   performs the averaging. Keeping the sidechain at audio rate preserves
//   the 2f ripple that produces the LF "thickening" harmonics (never
//   decimate - section 3.3).
// - **CdS photocell carrier dynamics** (Najnudel/Mueller/Helie/Roze, DAFx23
//   port-Hamiltonian vactrol model, section 2.4/3.2): two charge states -
//   free electrons q_n and trapped holes q_p - with Shockley-Read-Hall-type
//   recombination, discretised with the paper's semi-implicit
//   (linearly-implicit) Euler so each state update is a scalar linear
//   solve, unconditionally stable for the stiff decay terms:
//
//     kn = max(0, nuN * (qn - qp));  kp = max(0, nuP * (qTau + qp - qn))
//     qn' = (qn + T*g) / (1 + T*kn);  qp' = (qp + T*gP) / (1 + T*kp)
//     Rcell = clamp(1 / (muP*qp' + muN*qn'), Rlight, Rdark)
//
//   What this buys over an RC detector (all emergent, no explicit time
//   constants): release rate proportional to carrier density (fast-then-
//   crawling fat-tail two-stage release), q_p trap-state MEMORY (longer/
//   deeper GR -> slower release), program-dependent attack.
//
// Calibration: the DAFx23 paper fits a VTL5C3/2 - a deliberately FAST
// dual-element vactrol. The T4B's documented behaviour (attack ~1-5 ms,
// 50 % release in 40-80 ms, tail 0.5-5 s, memory) needs slower carrier
// kinetics, so the constants below are recalibrated against those targets
// (tests/OptoLevelerTests.cpp) starting from the paper's fitted set, per the
// brief's "recalibrated in tests to T4B behavior".
//
// DOCUMENTED DEVIATION from the paper's equal photo-generation: the hole
// (trap) state receives only a small fraction eta_p of the generation
// (gP = eta_p * g). With equal generation, q_p's steady state is g*tau_p,
// which for a seconds-scale trap time constant would dwarf q_n and stall
// electron recombination entirely - the binding T4B calibration targets
// (fast stage 40-80 ms AND seconds-scale tail AND memory) are unreachable.
// Physically eta_p < 1 is a trap capture fraction (not every generated
// carrier ends up trapped); flagged here and in the PR per the
// stubs/deviations convention.
//
// Double precision throughout (the q states span decades - brief F4 pins
// double sidechain states); reset() restores the dark equilibrium.
namespace msrr
{
    struct OptoCellParams
    {
        double muN = 4.0;          // electron mobility weight (1/(V*s) class)
        double muP = 35.0;         // hole mobility weight
        double rLightOhm = 2.0;    // fully-lit resistance floor
        double rDarkOhm = 10.0e6;  // dark resistance ceiling
        double qTau = 0.977;       // trap density constant
        double nuN = 1.2e6;        // electron recombination rate (T4B-recalibrated; paper: 1.79e8)
        double nuP = 1.23;         // hole detrap rate (T4B-recalibrated; paper: 1.35e2)
        double etaP = 4.0e-4;      // hole generation fraction (deviation - see header comment)
        double qMax = 10.0;        // abuse clamp on either charge state
    };

    class OptoCell
    {
    public:
        void setParams (const OptoCellParams& newParams) noexcept
        {
            params = newParams;
            reset();
        }

        const OptoCellParams& getParams() const noexcept { return params; }

        // Dark equilibrium: the electron floor that realises Rdark with no
        // trapped holes.
        double darkCharge() const noexcept { return 1.0 / (params.muN * params.rDarkOhm); }

        void reset() noexcept
        {
            qn = darkCharge();
            qp = 0.0;
        }

        // Re-seed gracefully after non-finite abuse (NaN/Inf feed).
        void sanitise() noexcept
        {
            if (! std::isfinite (qn) || ! std::isfinite (qp))
                reset();
        }

        // One semi-implicit Euler step; g >= 0 is the photo-generation rate
        // (from the EL law), T = 1/fs. Returns the cell resistance in ohms,
        // clamped to [Rlight, Rdark].
        double processSample (double g, double T) noexcept
        {
            const auto kn = std::max (0.0, params.nuN * (qn - qp));
            const auto kp = std::max (0.0, params.nuP * (params.qTau + qp - qn));

            qn = (qn + T * g) / (1.0 + T * kn);
            qp = (qp + T * params.etaP * g) / (1.0 + T * kp);

            // Floors keep the maths away from the denormal range and the
            // dark state exactly reachable; ceilings bound abuse input.
            qn = std::clamp (qn, darkCharge(), params.qMax);
            qp = std::clamp (qp, 0.0, params.qMax);

            const auto conductance = params.muP * qp + params.muN * qn;
            return std::clamp (1.0 / std::max (conductance, 1.0e-12), params.rLightOhm, params.rDarkOhm);
        }

        double electronCharge() const noexcept { return qn; }
        double holeCharge() const noexcept { return qp; }

    private:
        OptoCellParams params;
        double qn = 0.0;
        double qp = 0.0;
    };

    // EL-panel luminance (Alfrey-Taylor): B = B0 * exp(-b / sqrt(|v| + eps)).
    // Even in v (no rectifier). B0 absorbs the optical coupling into the
    // generation rate; b sets the turn-on knee sharpness.
    inline double electroluminance (double v, double B0, double b) noexcept
    {
        return B0 * std::exp (-b / std::sqrt (std::abs (v) + 1.0e-9));
    }
}
