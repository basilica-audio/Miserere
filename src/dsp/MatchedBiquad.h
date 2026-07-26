#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>

// Matched ("decramped") second-order digital filters after M. Vicanek,
// "Matched Second Order Digital Filters" (vicanek.de/articles/BiquadFits.pdf,
// v0.5.0 brief F2): poles by impulse invariance (paper eq. 12), zeros by
// magnitude matching against the analog prototype. The bilinear transform
// (RBJ cookbook) pins |H| back to a forced value at Nyquist and squeezes the
// upper skirt of any band placed in the top octave - audible for the
// PassiveEq 10/12/16 kHz bells and the ConsoleEq 12 kHz shelf at 44.1/48 k.
// These fits keep the top octave analog-true with the identical per-sample
// biquad kernel (same 5-coefficient layout as msrr::applyBiquadCoefficients,
// RealtimeCoefficients.h - in place, allocation-free, block-rate updates).
//
// - makeMatchedPeak() is the paper's section 4.4 "Matched Peaking EQ Filter"
//   verbatim (eqs. 42-45 + 29). Citation: Vicanek, BiquadFits.pdf, rev.
//   2016-2022, sections 3.2 and 4.4.
// - makeMatchedHighShelf()/makeMatchedLowShelf(): the paper stops at the
//   peaking EQ, so the shelves use the same "Custom Matched Biquad" scheme
//   the paper builds on (section 4): impulse-invariant poles from the RBJ
//   s-domain shelf prototype's denominator, then the numerator solved from
//   exact |H| matches at DC, Nyquist and fc in the paper's phi basis
//   (eqs. 25-27), minimum-phase b-recovery per eq. 29. This is the
//   "match |H(z)|^2 ... and solve the resulting set of linear equations"
//   route the paper itself names (section 4, first paragraph).
//
// All internal math in double (the paper notes the B-coefficient route
// requires double precision); results returned as the raw {b0,b1,b2,a0,a1,a2}
// float array applyBiquadCoefficients() consumes, a0 == 1.
//
// A gain of exactly 0 dB returns the exact identity {1,0,0,1,0,0} so the
// suite's 0-gain bit-exact-identity convention survives (the caller's
// exact-division normalisation then yields an identity biquad, see
// RealtimeCoefficients.h).
namespace msrr
{
    namespace matched
    {
        struct PhiBasis
        {
            double phi0, phi1, phi2;
        };

        inline PhiBasis phiAt (double omega) noexcept // omega in rad/sample
        {
            const auto s = std::sin (0.5 * omega);
            const auto phi1 = s * s;
            const auto phi0 = 1.0 - phi1;
            return { phi0, phi1, 4.0 * phi0 * phi1 };
        }

        // Impulse-invariant pole pair for an analog denominator
        // s^2 + 2*q*wn*s + wn^2, wn in rad/sample (paper eq. 12).
        inline void impulseInvariantPoles (double wn, double q, double& a1, double& a2) noexcept
        {
            // Guard: keep the (possibly gain-shifted) natural frequency
            // below Nyquist so the cos() argument cannot wrap the pole pair
            // onto an alias (house-rule clamp; unreachable for this suite's
            // parameter ranges).
            wn = std::min (wn, 0.98 * juce::MathConstants<double>::pi);

            const auto e = std::exp (-q * wn);

            if (q <= 1.0)
                a1 = -2.0 * e * std::cos (std::sqrt (1.0 - q * q) * wn);
            else
                a1 = -2.0 * e * std::cosh (std::sqrt (q * q - 1.0) * wn);

            a2 = e * e;
        }

        // Paper eq. 27 (denominator side).
        inline void denominatorBasis (double a1, double a2, double& A0, double& A1, double& A2) noexcept
        {
            A0 = (1.0 + a1 + a2) * (1.0 + a1 + a2);
            A1 = (1.0 - a1 + a2) * (1.0 - a1 + a2);
            A2 = -4.0 * a2;
        }

        // Paper eq. 29: minimum-phase numerator from the B basis.
        inline void recoverNumerator (double B0, double B1, double B2,
                                       double& b0, double& b1, double& b2) noexcept
        {
            const auto sqB0 = std::sqrt (std::max (0.0, B0));
            const auto sqB1 = std::sqrt (std::max (0.0, B1));
            const auto W = 0.5 * (sqB0 + sqB1);

            b0 = 0.5 * (W + std::sqrt (std::max (0.0, W * W + B2)));
            b1 = 0.5 * (sqB0 - sqB1);
            b2 = b0 > 1.0e-12 ? -B2 / (4.0 * b0) : 0.0;
        }

        inline std::array<float, 6> toRaw (double b0, double b1, double b2, double a1, double a2) noexcept
        {
            return { static_cast<float> (b0), static_cast<float> (b1), static_cast<float> (b2),
                     1.0f, static_cast<float> (a1), static_cast<float> (a2) };
        }

        inline constexpr std::array<float, 6> identityRaw { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };

        // NaN-safe input clamps (house rule): fc in [10 Hz, 0.49*fs],
        // Q in [0.05, 40], |gain| <= 24 dB - applied before any coefficient
        // math.
        inline void clampInputs (double sampleRate, float& fc, float& Q, float& gainDb) noexcept
        {
            const auto fs = sampleRate > 0.0 ? sampleRate : 44100.0;
            fc = juce::jlimit (10.0f, static_cast<float> (0.49 * fs), fc);
            Q = juce::jlimit (0.05f, 40.0f, Q);
            gainDb = juce::jlimit (-24.0f, 24.0f, gainDb);
        }
    }

    // Matched peaking EQ (Vicanek section 4.4). Analog prototype:
    //   H(s) = (w0^2 + s*w0*sqrt(G)/Q + s^2) / (w0^2 + s*w0/(sqrt(G)*Q) + s^2)
    inline std::array<float, 6> makeMatchedPeak (double sampleRate, float fcHz, float Q, float gainDb) noexcept
    {
        matched::clampInputs (sampleRate, fcHz, Q, gainDb);

        if (std::abs (gainDb) < 1.0e-6f)
            return matched::identityRaw;

        const auto G = std::pow (10.0, static_cast<double> (gainDb) / 20.0); // linear peak gain
        const auto sqrtG = std::sqrt (G);
        const auto w0 = juce::MathConstants<double>::twoPi * static_cast<double> (fcHz) / sampleRate;

        // Denominator: s^2 + s*w0/(sqrt(G)*Q) + w0^2  ->  q = 1/(2*sqrt(G)*Q).
        double a1 = 0.0, a2 = 0.0;
        matched::impulseInvariantPoles (w0, 1.0 / (2.0 * sqrtG * static_cast<double> (Q)), a1, a2);

        double A0, A1, A2;
        matched::denominatorBasis (a1, a2, A0, A1, A2);

        const auto phi = matched::phiAt (w0);

        // Paper eq. 44/45.
        const auto G2 = G * G;
        const auto R1 = G2 * (A0 * phi.phi0 + A1 * phi.phi1 + A2 * phi.phi2);
        const auto R2 = G2 * (-A0 + A1 + 4.0 * (phi.phi0 - phi.phi1) * A2);

        const auto B0 = A0;
        const auto B2 = (R1 - R2 * phi.phi1 - B0) / (4.0 * phi.phi1 * phi.phi1);
        const auto B1 = R2 + B0 + 4.0 * (phi.phi1 - phi.phi0) * B2;

        double b0, b1, b2;
        matched::recoverNumerator (B0, B1, B2, b0, b1, b2);

        return matched::toRaw (b0, b1, b2, a1, a2);
    }

    namespace matched
    {
        // Shared shelf builder. RBJ s-domain prototypes (A = 10^(dB/40)):
        //   high shelf: H(s) = A*(A*s^2 + (sqrt(A)/Q)*s + 1) / (s^2 + (sqrt(A)/Q)*s + A)
        //   low shelf:  H(s) = A*(s^2 + (sqrt(A)/Q)*s + A) / (A*s^2 + (sqrt(A)/Q)*s + 1)
        // (s normalised to w0). Poles by impulse invariance from the
        // denominator (wn = w0*sqrt(A) resp. w0/sqrt(A), q = 1/(2Q)),
        // numerator from exact magnitude matches at DC, Nyquist and w0.
        inline std::array<float, 6> makeMatchedShelf (double sampleRate, float fcHz, float Q, float gainDb, bool isHighShelf) noexcept
        {
            clampInputs (sampleRate, fcHz, Q, gainDb);

            if (std::abs (gainDb) < 1.0e-6f)
                return identityRaw;

            const auto A = std::pow (10.0, static_cast<double> (gainDb) / 40.0);
            const auto sqrtA = std::sqrt (A);
            const auto Qd = static_cast<double> (Q);
            const auto w0 = juce::MathConstants<double>::twoPi * static_cast<double> (fcHz) / sampleRate;

            // Prototype |H(j*x)|^2 with x = omega/w0.
            const auto protoMagSq = [&] (double x) noexcept
            {
                const auto x2 = x * x;
                const auto bw = (sqrtA / Qd) * x; // shared bandwidth term
                double num, den;

                if (isHighShelf)
                {
                    num = (1.0 - A * x2) * (1.0 - A * x2) + bw * bw;
                    den = (A - x2) * (A - x2) + bw * bw;
                }
                else
                {
                    num = (A - x2) * (A - x2) + bw * bw;
                    den = (1.0 - A * x2) * (1.0 - A * x2) + bw * bw;
                }

                return A * A * num / den;
            };

            const auto wn = isHighShelf ? w0 * sqrtA : w0 / sqrtA;

            double a1 = 0.0, a2 = 0.0;
            impulseInvariantPoles (wn, 1.0 / (2.0 * Qd), a1, a2);

            double A0, A1, A2;
            denominatorBasis (a1, a2, A0, A1, A2);

            const auto phi = phiAt (w0);
            const auto xNyquist = juce::MathConstants<double>::pi / w0;

            const auto tDc = protoMagSq (0.0);
            const auto tNyquist = protoMagSq (xNyquist);
            const auto tCentre = protoMagSq (1.0);

            // Exact matches: DC (phi1 = 0), Nyquist (phi0 = 0), then w0.
            const auto B0 = tDc * A0;
            const auto B1 = tNyquist * A1;
            const auto D0 = A0 * phi.phi0 + A1 * phi.phi1 + A2 * phi.phi2;
            const auto B2 = phi.phi2 > 1.0e-12
                                ? (tCentre * D0 - B0 * phi.phi0 - B1 * phi.phi1) / phi.phi2
                                : 0.0;

            double b0, b1, b2;
            recoverNumerator (B0, B1, B2, b0, b1, b2);

            return toRaw (b0, b1, b2, a1, a2);
        }
    }

    inline std::array<float, 6> makeMatchedHighShelf (double sampleRate, float fcHz, float Q, float gainDb) noexcept
    {
        return matched::makeMatchedShelf (sampleRate, fcHz, Q, gainDb, true);
    }

    inline std::array<float, 6> makeMatchedLowShelf (double sampleRate, float fcHz, float Q, float gainDb) noexcept
    {
        return matched::makeMatchedShelf (sampleRate, fcHz, Q, gainDb, false);
    }
}
