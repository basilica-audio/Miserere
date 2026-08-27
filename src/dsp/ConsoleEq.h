#pragma once

#include "AdaaSaturator.h"
#include "MatchedBiquad.h"
#include "RealtimeCoefficients.h"
#include "TapeSaturator.h"

#include <juce_dsp/juce_dsp.h>

#include <vector>

// The Direct path's "1073-class" console EQ (docs/design-brief.md, module
// "Console EQ"): HPF 18 dB/oct @ {50, 80, 160, 300} Hz; low shelf +/-16 dB @
// {35, 60, 110, 220} Hz; mid bell +/-18 dB fixed-Q @ {360, 700, 1600, 3200,
// 4800, 7200} Hz; high shelf +/-16 dB fixed @ 12 kHz (shallow first-order-
// style shelves, 3-pole HPF); a Drive control blending near-equal 2nd+3rd
// (3rd-leaning) transformer-style harmonics, clean at nominal level.
//
// v0.5.0 "Circuit Engines" upgrades (brief F2/F8):
//
// - The 12 kHz high shelf is a Vicanek MATCHED shelf (MatchedBiquad.h): the
//   RBJ/bilinear rendering cramps the top octave at 44.1/48 k (the digital
//   shelf is pinned at Nyquist and reaches its plateau too fast); the
//   matched fit keeps the 20 kHz magnitude within +-1 dB of the analog
//   prototype (tests/ConsoleEqTests.cpp). Low shelf and mid bell stay RBJ -
//   all their selections sit at or below 7.2 kHz where cramping is far
//   smaller, and the brief pins only the 12 kHz shelf (research-neve-1073.md
//   section 3.2).
// - Drive's even-harmonic term is no longer the ad-hoc 0.06*(g*x)^2*sign(x)
//   add-on: it is a flux-domain transformer iron model
//   (research-tape-transformer-magnetics.md section 4.2 "flux sat",
//   research-neve-1073.md section 2.3):
//
//     u = leakyIntegrate(x, pole 3 Hz)         // flux ~ integral(v dt) -> 1/f level scaling
//     s = ADAA_tanh(drive*u + phi_dc)/drive    // DC premagnetisation -> 2nd harmonic
//     y = pairedDifferentiator(s)              // EXACT inverse of the integrator
//
//   Because flux ~ V/f, HD3 rises automatically toward LF (the measured
//   transformer signature - no hand-drawn frequency weighting), and the DC
//   premagnetisation phi_dc (the 1073's class-A standing current through the
//   output transformer primary) sets H2/H3 ~ 0.8-1.0 at hot drive. The
//   nonlinear part is applied per the F1 parallel-delta rule: the iron
//   contribution IS the ADAA residual, differentiated - the linear signal
//   never passes through the integrator/differentiator pair, so drive -> 0
//   nulls exactly by construction (measured: -105 dBFS against the EQ-only
//   render at a near-zero Drive setting). Double state for the integrator
//   (LF error accumulation); see prepare() for why the pair is discretised
//   impulse-invariant rather than trapezoidally.
// - The odd (3rd-leaning) term keeps the shared TapeSaturator curve, now
//   rendered via residual-form ADAA1 (brief F1, AdaaSaturator.h).
//
// The HPF is folded into this class (rather than kept as the standalone v1
// Hpf module) because the brief specifies it as part of the console EQ
// module's stepped-frequency grid, not an independent v1-style continuously
// tunable filter. HPF at 18 dB/oct (3rd order) is built as a cascade of one
// 1st-order highpass and one 2nd-order Butterworth highpass - both
// minimum-phase/causal, zero added latency.
//
// At 0 dB every shelf/peak band's coefficients collapse to an exact identity
// transfer function (see RealtimeCoefficients.h and MatchedBiquad.h's
// 0-gain identity) - this is what keeps the v2 default-wire null test
// bit-exact regardless of probe frequency. Drive 0 dB remains a structural
// bit-exact bypass.
class ConsoleEq
{
public:
    ConsoleEq() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    void setHpfEnabled (bool shouldBeEnabled) noexcept { hpfEnabled = shouldBeEnabled; }
    void setHpfFreqHz (float newFrequencyHz) noexcept;

    void setLowFreqHz (float newFrequencyHz) noexcept;
    void setLowGainDb (float newGainDb) noexcept;
    void setMidFreqHz (float newFrequencyHz) noexcept;
    void setMidGainDb (float newGainDb) noexcept;
    void setHighGainDb (float newGainDb) noexcept;

    // 0 dB (the parameter's minimum) is a bit-exact bypass.
    void setDriveDb (float newDriveDb) noexcept;

    void process (juce::dsp::AudioBlock<float>& block) noexcept;

private:
    static constexpr float lowShelfQ = 0.5f;   // shallow, first-order-style shelf slope
    static constexpr float highShelfQ = 0.5f;
    static constexpr float midQ = 0.8f;        // fixed Q regardless of selected centre frequency (1073 spec)
    static constexpr float highShelfFreqHz = 12000.0f;
    static constexpr float hpfSecondOrderQ = juce::MathConstants<float>::sqrt2 / 2.0f; // Butterworth
    static constexpr double smoothingTimeSeconds = 0.05;

    // F8 iron-term calibration (see class comment + tests/ConsoleEqTests):
    // - integrator pole 3 Hz, unity-gain reference 100 Hz (flux is
    //   normalised so drive numbers stay comparable to the odd term's),
    // - phi_dc tuned so H2/H3 lands on the SoS 1073 measurement anchor
    //   (measured 0.88 at +24 dB drive; test window [0.5, 1.1]),
    // - ironAmount scales the differentiated residual into the mix.
    static constexpr float ironIntegratorPoleHz = 3.0f;
    static constexpr float ironReferenceHz = 100.0f;
    static constexpr float ironDcBias = 0.33f;
    static constexpr float ironAmount = 0.5f;

    static constexpr float neutralGainEpsilonDb = 1.0e-3f;

    // Rest-flush threshold (issue #46): the exact value juce_dsp's own
    // per-block snapToZero() pass used (JUCE_SNAP_TO_ZERO,
    // juce_FloatVectorOperations.h, JUCE 8.0.14) before the fleet disabled
    // JUCE_DSP_ENABLE_SNAP_TO_ZERO. -160 dBFS: genuine musical HPF/shelf
    // tails decay through it within ~100 ms of digital silence, and the
    // parked x86 fixed-point residue this guards against (~1e-34, see
    // process()) sits 25 orders of magnitude below it.
    static constexpr float restFlushThreshold = 1.0e-8f;

    using Duplicator = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;

    double sampleRate = 44100.0;

    Duplicator hpfFirstOrder { new juce::dsp::IIR::Coefficients<float> (1.0f, 0.0f, 1.0f, 0.0f) };
    Duplicator hpfSecondOrder { msrr::makeIdentityBiquad() };
    Duplicator lowShelf { msrr::makeIdentityBiquad() };
    Duplicator midPeak { msrr::makeIdentityBiquad() };
    Duplicator highShelf { msrr::makeIdentityBiquad() };

    // Per-channel drive-stage state (allocated in prepare()).
    std::vector<msrr::adaa::TanhStage> oddStages;      // odd 3rd-leaning term (residual-form ADAA)
    std::vector<msrr::adaa::AsymTanhStage> ironStages; // F8 flux-domain iron term (ADAA residual)

    struct IronState
    {
        double integrator = 0.0;  // leaky flux integrator (double - LF accumulation)
        double diffPrevIn = 0.0;  // differentiator: previous residual sample
    };

    std::vector<IronState> ironStates;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> hpfFreqSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowFreqSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lowGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> midFreqSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> midGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> highGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> driveDbSmoothed;

    // Iron integrator/differentiator coefficients (recomputed in prepare()).
    // The pair is u[n] = a*u[n-1] + G*x[n] and its EXACT inverse
    // x[n] = (u[n] - a*u[n-1]) / G - see prepare() for why this
    // (pole-free) discretisation is used instead of the trapezoidal one.
    double ironIntegratorPole = 0.0;   // a = exp(-2*pi*f_leak/fs)
    double ironIntegratorGain = 0.0;   // G, normalised to unity gain at ironReferenceHz
    double ironDifferentiatorGain = 0.0; // 1/G

    bool hpfEnabled = false;
    float lastHpfFreqHz = 80.0f;
    float lastLowFreqHz = 110.0f;
    float lastLowGainDb = 0.0f;
    float lastMidFreqHz = 1600.0f;
    float lastMidGainDb = 0.0f;
    float lastHighGainDb = 0.0f;
    float lastDriveDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConsoleEq)
};
