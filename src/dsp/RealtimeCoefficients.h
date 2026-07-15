#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>

// Real-time-safe biquad coefficient updates for juce::dsp::IIR::Filter.
//
// juce::dsp::IIR::Coefficients<float>::makeLowShelf/makeHighShelf/
// makePeakFilter/makeHighPass/... (the usual way to build filter
// coefficients) heap-allocate a brand new Coefficients object on every call -
// fine once in prepareToPlay() for a genuinely fixed filter, not fine on the
// audio thread for anything whose frequency/gain/Q can be automated every
// block (see this repo's CLAUDE.md: "Coefficients::make* allocates and is a
// known suite-wide review finding").
//
// juce::dsp::IIR::ArrayCoefficients<float>::makeXxx returns the same
// coefficients as a plain std::array (stack storage, zero allocation). This
// header writes that array's values directly into an *already-allocated*
// Coefficients<float> object's raw coefficient storage (normalising by a0
// exactly the way Coefficients' own constructor does), so repeated calls
// during processBlock() never touch the heap.
//
// JUCE 8.0.14, juce_dsp/processors/juce_IIRFilter.h (Coefficients::
// getRawCoefficients()/getFilterOrder() and the normalised-by-a0
// {b0,b1,b2,a1,a2} / {b0,b1,a1} raw storage layout this mirrors).
namespace msrr
{
    // Writes a normalised 2nd-order {b0,b1,b2,a1,a2} set (5 raw coefficients)
    // computed from a raw {b0,b1,b2,a0,a1,a2} array (as returned by
    // juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf/makeHighShelf/
    // makePeakFilter/makeHighPass/makeLowPass/makeBandPass/...) into
    // `target`, which must already hold a 2nd-order filter's coefficient
    // storage (i.e. have been constructed via the 6-argument Coefficients
    // constructor, or a prior makeXxx() call, at least once - typically
    // during prepareToPlay()).
    // Note the explicit divisions by a0 (raw[3]) rather than a multiply by
    // its precomputed reciprocal: division guarantees bn/a0 == an/a0
    // *bit-exactly* whenever bn == an (as RBJ shelf/peak coefficients are at
    // 0 dB gain), which is what makes a neutral EQ band a true identity
    // biquad. A reciprocal multiply leaves a 1-ulp error on b0 that
    // recirculates through the filter's feedback path and surfaces around
    // -96 dB - failing the M1 null test's -120 dBFS bar. Five divisions per
    // filter per block are negligible.
    inline void applyBiquadCoefficients (juce::dsp::IIR::Coefficients<float>& target,
                                          const std::array<float, 6>& raw) noexcept
    {
        jassert (target.getFilterOrder() == 2);

        auto* dest = target.getRawCoefficients();
        const auto a0 = raw[3];

        dest[0] = raw[0] / a0; // b0
        dest[1] = raw[1] / a0; // b1
        dest[2] = raw[2] / a0; // b2
        dest[3] = raw[4] / a0; // a1
        dest[4] = raw[5] / a0; // a2
    }

    // Same idea for a 1st-order {b0,b1,a1} set (3 raw coefficients) computed
    // from a raw {b0,b1,a0,a1} array (as returned by
    // ArrayCoefficients<float>::makeFirstOrderLowPass/HighPass/AllPass).
    inline void applyFirstOrderCoefficients (juce::dsp::IIR::Coefficients<float>& target,
                                              const std::array<float, 4>& raw) noexcept
    {
        jassert (target.getFilterOrder() == 1);

        auto* dest = target.getRawCoefficients();
        const auto a0 = raw[2];

        dest[0] = raw[0] / a0; // b0
        dest[1] = raw[1] / a0; // b1
        dest[2] = raw[3] / a0; // a1
    }

    // A pre-allocated identity 2nd-order Coefficients object, ready for
    // applyBiquadCoefficients() to overwrite in place. Constructing it via
    // the 6-argument constructor (rather than a makeXxx() call) avoids any
    // allocation-on-the-audio-thread ambiguity at the single call site
    // (prepare()) that owns it.
    inline juce::dsp::IIR::Coefficients<float>::Ptr makeIdentityBiquad()
    {
        return new juce::dsp::IIR::Coefficients<float> (1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    }
}
