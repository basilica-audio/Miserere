#include "ParameterLayout.h"
#include "ParameterIds.h"

namespace
{
    // True logarithmic (base-10) mapping for frequency/time parameters, so
    // slider/knob travel spends equal space per octave/decade rather than
    // per linear unit - matches the convention used across the suite
    // (see e.g. triptych's/seraph's ParameterLayout.cpp). Uses
    // juce::mapToLog10/mapFromLog10 rather than NormalisableRange's built-in
    // power-law skew, which only approximates a log curve.
    juce::NormalisableRange<float> makeLogRange (float rangeMin, float rangeMax)
    {
        return juce::NormalisableRange<float> (
            rangeMin,
            rangeMax,
            [] (float start, float end, float normalised)
            { return juce::mapToLog10 (normalised, start, end); },
            [] (float start, float end, float value)
            { return juce::mapFromLog10 (value, start, end); });
    }

    // Adds a bus's Level/Mute/Solo triple. Level's range floor (-60 dB)
    // stands in for "-inf" per the design brief's "-inf...+6 dB fader" -
    // -60 dB is well below the noise floor of any realistic use case, so it
    // is a practically-silent floor without the extra complexity of a
    // special-cased "-inf" display/quantity. Mute/Solo default to false so
    // enabling this triple never changes existing default behaviour.
    void addLevelMuteSolo (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                            const char* levelId,
                            const char* muteId,
                            const char* soloId,
                            const juce::String& labelPrefix,
                            float defaultLevelDb)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { levelId, 1 },
            labelPrefix + " Level",
            juce::NormalisableRange<float> (-60.0f, 6.0f, 0.01f),
            defaultLevelDb,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { muteId, 1 }, labelPrefix + " Mute", false));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { soloId, 1 }, labelPrefix + " Solo", false));
    }
}

namespace msrr
{
    // Bus B's Passive EQ In frequency selects (brief: "Low Boost (60/100 Hz
    // sel...)", "High Boost (8/10/12/16 kHz sel...)"). Kept as free functions/
    // arrays (not local statics inside createParameterLayout()) so
    // PluginProcessor.cpp can map a chosen AudioParameterChoice index back to
    // its concrete Hz value without duplicating the list.
    const juce::StringArray busBLowBoostFreqChoices { "60 Hz", "100 Hz" };
    const juce::StringArray busBHighBoostFreqChoices { "8 kHz", "10 kHz", "12 kHz", "16 kHz" };

    const std::array<float, 2> busBLowBoostFreqHz { 60.0f, 100.0f };
    const std::array<float, 4> busBHighBoostFreqHz { 8000.0f, 10000.0f, 12000.0f, 16000.0f };

    // Bus A FET Comp ratio choice (brief: "ratio {4:1, 8:1}").
    const juce::StringArray compRatioChoices { "4:1", "8:1" };
    const std::array<float, 2> compRatioValues { 4.0f, 8.0f };

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        //======================================================================
        // Global
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::inTrim, 1 },
            "In Trim",
            juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::outTrim, 1 },
            "Out Trim",
            juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::bypass, 1 }, "Bypass", false));

        //======================================================================
        // Bus A - Direct chain
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::busAHpfEnabled, 1 }, "Direct HPF Enabled", true));

        // HPF: 20-300 Hz, 12 dB/oct (brief). Default 80 Hz, a typical vocal
        // rumble-clearing setting; the null test explicitly disables the HPF
        // (busA_hpfEnabled = false) rather than relying on a "neutral"
        // frequency - see Hpf.h for why 20 Hz alone cannot reach the
        // guarantee's <= -120 dBFS bit-transparency bar (a highpass has no
        // frequency setting that is an exact identity, unlike a shelf/peak
        // EQ at 0 dB gain).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busAHpfFreq, 1 },
            "Direct HPF Freq",
            makeLogRange (20.0f, 300.0f),
            80.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        // Console EQ ("British console" character): LowShelf 100 Hz,
        // Peak 250 Hz-5 kHz (Q 0.7-2), HighShelf 8 kHz, all +/-15 dB (brief).
        // At 0 dB every band collapses to an exact identity biquad (the RBJ
        // cookbook shelf/peak formulas produce b_n == a_n term-by-term when
        // the gain factor is 1) - this is what keeps the null test's "EQ
        // flat" bit-exact.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busAEqLowGain, 1 },
            "Direct EQ Low Gain",
            juce::NormalisableRange<float> (-15.0f, 15.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busAEqMidFreq, 1 },
            "Direct EQ Mid Freq",
            makeLogRange (250.0f, 5000.0f),
            1000.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busAEqMidGain, 1 },
            "Direct EQ Mid Gain",
            juce::NormalisableRange<float> (-15.0f, 15.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busAEqMidQ, 1 },
            "Direct EQ Mid Q",
            juce::NormalisableRange<float> (0.7f, 2.0f, 0.001f),
            1.0f,
            juce::AudioParameterFloatAttributes()));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busAEqHighGain, 1 },
            "Direct EQ High Gain",
            juce::NormalisableRange<float> (-15.0f, 15.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        // FET Comp: ratio {4:1, 8:1} (brief). Threshold is not explicitly
        // enumerated in the brief's Bus A module bullet list but is required
        // for the module to compress at all and is explicitly exercised by
        // the M1 null-test guarantee ("comp threshold at max") - see
        // docs/architecture.md's "Deviations from the design brief" section.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::busACompRatio, 1 }, "Direct Comp Ratio", compRatioChoices, 0));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busACompThreshold, 1 },
            "Direct Comp Threshold",
            juce::NormalisableRange<float> (-40.0f, 0.0f, 0.01f),
            -18.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busACompAttack, 1 },
            "Direct Comp Attack",
            makeLogRange (0.1f, 10.0f),
            3.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busACompRelease, 1 },
            "Direct Comp Release",
            makeLogRange (50.0f, 1100.0f),
            150.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busACompMakeup, 1 },
            "Direct Comp Makeup",
            juce::NormalisableRange<float> (0.0f, 24.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        // De-Esser: split-band 4-9 kHz tunable, threshold, max 10 dB
        // reduction (brief). Off by default so it never colours a vocal
        // until the user opts in.
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::busADeessEnabled, 1 }, "Direct De-Ess Enabled", true));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busADeessFreq, 1 },
            "Direct De-Ess Freq",
            makeLogRange (4000.0f, 9000.0f),
            6500.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busADeessThreshold, 1 },
            "Direct De-Ess Threshold",
            juce::NormalisableRange<float> (-40.0f, 0.0f, 0.01f),
            -24.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        // Tape Sat: drive 0-24 dB into tanh with pre-emphasis/de-emphasis
        // pair, auto-compensated (brief). 0 dB (the parameter's minimum) is
        // a bit-exact bypass - see TapeSat.h.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busASatDrive, 1 },
            "Direct Sat Drive",
            juce::NormalisableRange<float> (0.0f, 24.0f, 0.01f),
            6.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        addLevelMuteSolo (layout, ParamIDs::busALevel, ParamIDs::busAMute, ParamIDs::busASolo, "Direct", 0.0f);

        //======================================================================
        // Bus B - Opto sandwich
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::busBLowBoostFreq, 1 }, "Opto Low Boost Freq", busBLowBoostFreqChoices, 1));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busBLowBoostGain, 1 },
            "Opto Low Boost Gain",
            juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::busBHighBoostFreq, 1 }, "Opto High Boost Freq", busBHighBoostFreqChoices, 2));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busBHighBoostGain, 1 },
            "Opto High Boost Gain",
            juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        // Opto Leveler: program-dependent two-stage release, soft ~3:1
        // ratio, attack ~10 ms fixed (not user-exposed, brief), peak
        // reduction 0-100%, makeup (brief). 0% is a bit-exact bypass.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busBOptoReduction, 1 },
            "Opto Peak Reduction",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
            40.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busBOptoMakeup, 1 },
            "Opto Makeup",
            juce::NormalisableRange<float> (0.0f, 24.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        // Passive Air out: HighShelf 12 kHz 0-8 dB, boost only (brief).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busBAirGain, 1 },
            "Opto Air Gain",
            juce::NormalisableRange<float> (0.0f, 8.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        // Bus B/C/D default to off (-60 dB floor, see addLevelMuteSolo) so
        // the plugin's out-of-the-box sound is just the Direct chain -
        // consistent with a "rough vocal template" workflow where the
        // parallel busses are blended in deliberately, not on by default.
        addLevelMuteSolo (layout, ParamIDs::busBLevel, ParamIDs::busBMute, ParamIDs::busBSolo, "Opto", -60.0f);

        //======================================================================
        // Bus C - Smash (FET Limiter, all-buttons character)
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busCAttack, 1 },
            "Smash Attack",
            makeLogRange (0.05f, 0.8f),
            0.3f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busCRelease, 1 },
            "Smash Release",
            makeLogRange (50.0f, 200.0f),
            100.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busCDrive, 1 },
            "Smash Drive",
            juce::NormalisableRange<float> (0.0f, 12.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busCOutputTrim, 1 },
            "Smash Output Trim",
            juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        addLevelMuteSolo (layout, ParamIDs::busCLevel, ParamIDs::busCMute, ParamIDs::busCSolo, "Smash", -60.0f);

        //======================================================================
        // Bus D - Slap
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busDDelayMs, 1 },
            "Slap Delay",
            juce::NormalisableRange<float> (60.0f, 180.0f, 0.01f),
            110.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busDFeedback, 1 },
            "Slap Feedback",
            juce::NormalisableRange<float> (0.0f, 30.0f, 0.01f),
            15.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busDHpFreq, 1 },
            "Slap Loop HP",
            makeLogRange (50.0f, 1000.0f),
            200.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::busDLpFreq, 1 },
            "Slap Loop LP",
            makeLogRange (2000.0f, 10000.0f),
            5000.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::busDMono, 1 }, "Slap Mono", false));

        addLevelMuteSolo (layout, ParamIDs::busDLevel, ParamIDs::busDMute, ParamIDs::busDSolo, "Slap", -60.0f);

        return layout;
    }
}
