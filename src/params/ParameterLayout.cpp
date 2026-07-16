#include "ParameterLayout.h"
#include "ParameterIds.h"

namespace
{
    // True logarithmic (base-10) mapping for frequency/time parameters, so
    // slider/knob travel spends equal space per octave/decade rather than
    // per linear unit - matches the convention used across the suite (see
    // e.g. triptych's/seraph's ParameterLayout.cpp). Uses
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

    // Adds a bus's Level/Mute/Audition triple. Level's range floor (-60 dB)
    // stands in for "-inf" per the design brief's "-60...+6 dB fader".
    void addLevelMuteAudition (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                                const char* levelId,
                                const char* muteId,
                                const char* auditionId,
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

        // Audition is a META parameter: engaging one bus's audition releases
        // the other three via the exclusivity listener in PluginProcessor,
        // i.e. changing THIS parameter changes OTHER parameters' values. AU
        // validation (auval) requires the meta flag on any such parameter
        // ("Parameter values are different since last set - probable cause:
        // a Meta Param Flag is NOT set...") - JUCE 8.0.14,
        // RangedAudioParameterAttributes::withMeta.
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { auditionId, 1 }, labelPrefix + " Audition", false,
            juce::AudioParameterBoolAttributes().withMeta (true)));
    }

    // Adds a Passive EQ instance's eight-parameter surface (LF boost/cut, HF
    // bell boost/bandwidth, HF shelf atten - see PassiveEq.h), used four
    // times (Sandwich pre/post).
    void addPassiveEqParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                              const char* lfFreqId, const char* lfBoostId, const char* lfCutId,
                              const char* hfBellFreqId, const char* hfBellBoostId, const char* hfBellBandwidthId,
                              const char* hfShelfFreqId, const char* hfShelfAttenId,
                              const juce::String& labelPrefix,
                              int lfFreqDefaultIndex, float lfBoostDefault, float lfCutDefault,
                              int hfBellFreqDefaultIndex, float hfBellBoostDefault, float hfBellBandwidthDefault,
                              int hfShelfFreqDefaultIndex, float hfShelfAttenDefault)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { lfFreqId, 1 }, labelPrefix + " LF Freq", msrr::sandLfFreqChoices, lfFreqDefaultIndex));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { lfBoostId, 1 }, labelPrefix + " LF Boost",
            juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), lfBoostDefault));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { lfCutId, 1 }, labelPrefix + " LF Cut",
            juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), lfCutDefault));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { hfBellFreqId, 1 }, labelPrefix + " HF Bell Freq", msrr::sandHfBellFreqChoices, hfBellFreqDefaultIndex));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { hfBellBoostId, 1 }, labelPrefix + " HF Bell Boost",
            juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), hfBellBoostDefault));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { hfBellBandwidthId, 1 }, labelPrefix + " HF Bell Bandwidth",
            juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), hfBellBandwidthDefault));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { hfShelfFreqId, 1 }, labelPrefix + " HF Shelf Freq", msrr::sandHfShelfFreqChoices, hfShelfFreqDefaultIndex));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { hfShelfAttenId, 1 }, labelPrefix + " HF Shelf Atten",
            juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), hfShelfAttenDefault));
    }
}

namespace msrr
{
    const juce::StringArray crushRatioChoices { "4:1", "8:1", "12:1", "20:1", "ALL" };
    const juce::StringArray crushStyleChoices { "All-Buttons", "Gentle" };

    const juce::StringArray eqHpfFreqChoices { "50 Hz", "80 Hz", "160 Hz", "300 Hz" };
    const std::array<float, 4> eqHpfFreqHz { 50.0f, 80.0f, 160.0f, 300.0f };
    const juce::StringArray eqLowFreqChoices { "35 Hz", "60 Hz", "110 Hz", "220 Hz" };
    const std::array<float, 4> eqLowFreqHz { 35.0f, 60.0f, 110.0f, 220.0f };
    const juce::StringArray eqMidFreqChoices { "360 Hz", "700 Hz", "1.6 kHz", "3.2 kHz", "4.8 kHz", "7.2 kHz" };
    const std::array<float, 6> eqMidFreqHz { 360.0f, 700.0f, 1600.0f, 3200.0f, 4800.0f, 7200.0f };

    const juce::StringArray sandLfFreqChoices { "20 Hz", "30 Hz", "60 Hz", "100 Hz" };
    const std::array<float, 4> sandLfFreqHz { 20.0f, 30.0f, 60.0f, 100.0f };
    const juce::StringArray sandHfBellFreqChoices { "3 kHz", "4 kHz", "5 kHz", "8 kHz", "10 kHz", "12 kHz", "16 kHz" };
    const std::array<float, 7> sandHfBellFreqHz { 3000.0f, 4000.0f, 5000.0f, 8000.0f, 10000.0f, 12000.0f, 16000.0f };
    const juce::StringArray sandHfShelfFreqChoices { "5 kHz", "10 kHz", "20 kHz" };
    const std::array<float, 3> sandHfShelfFreqHz { 5000.0f, 10000.0f, 20000.0f };

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

        // Unlinked (independent L/R detectors) is the default - "dual mono
        // is key" (design brief).
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::link, 1 }, "Link", false));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::parallelTrim, 1 },
            "Parallel",
            juce::NormalisableRange<float> (-24.0f, 6.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        //======================================================================
        // Direct path - every section optional, ALL OFF by default (the
        // "bit-transparent wire" invariant).
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::directDeessPreEnabled, 1 }, "Direct De-Ess Pre Enabled", false));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::directDeessPreFreq, 1 },
            "Direct De-Ess Pre Freq",
            makeLogRange (4000.0f, 9000.0f),
            6500.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::directDeessPreThreshold, 1 },
            "Direct De-Ess Pre Threshold",
            juce::NormalisableRange<float> (-40.0f, 0.0f, 0.01f),
            -24.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::directFetEnabled, 1 }, "Direct FET Enabled", false));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::directFetThreshold, 1 },
            "Direct FET Threshold",
            juce::NormalisableRange<float> (-40.0f, 0.0f, 0.01f),
            -18.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::directFetAttack, 1 },
            "Direct FET Attack",
            makeLogRange (1.0f, 30.0f),
            8.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::directFetRelease, 1 },
            "Direct FET Release",
            makeLogRange (50.0f, 500.0f),
            150.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::directFetMakeup, 1 },
            "Direct FET Makeup",
            juce::NormalisableRange<float> (0.0f, 24.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::directEqHpfEnabled, 1 }, "Direct EQ HPF Enabled", false));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::directEqHpfFreq, 1 }, "Direct EQ HPF Freq", eqHpfFreqChoices, 1));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::directEqLowFreq, 1 }, "Direct EQ Low Freq", eqLowFreqChoices, 2));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::directEqLowGain, 1 },
            "Direct EQ Low Gain",
            juce::NormalisableRange<float> (-16.0f, 16.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::directEqMidFreq, 1 }, "Direct EQ Mid Freq", eqMidFreqChoices, 2));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::directEqMidGain, 1 },
            "Direct EQ Mid Gain",
            juce::NormalisableRange<float> (-18.0f, 18.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::directEqHighGain, 1 },
            "Direct EQ High Gain",
            juce::NormalisableRange<float> (-16.0f, 16.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::directEqDrive, 1 },
            "Direct EQ Drive",
            juce::NormalisableRange<float> (0.0f, 24.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::directSatDrive, 1 },
            "Direct Sat Drive",
            juce::NormalisableRange<float> (0.0f, 24.0f, 0.01f),
            0.0f, // 0 dB (parameter minimum) is a bit-exact bypass - see TapeSat.h
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::directDeessPostEnabled, 1 }, "Direct De-Ess Post Enabled", false));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::directDeessPostFreq, 1 },
            "Direct De-Ess Post Freq",
            makeLogRange (4000.0f, 9000.0f),
            6500.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::directDeessPostThreshold, 1 },
            "Direct De-Ess Post Threshold",
            juce::NormalisableRange<float> (-40.0f, 0.0f, 0.01f),
            -24.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        //======================================================================
        // Bus (1) CRUSH
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::crushInput, 1 },
            "Crush Input",
            juce::NormalisableRange<float> (0.0f, 48.0f, 0.01f),
            12.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::crushRatio, 1 }, "Crush Ratio", crushRatioChoices, 4)); // default ALL

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::crushStyle, 1 }, "Crush Style", crushStyleChoices, 0)); // default All-Buttons

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::crushAttack, 1 },
            "Crush Attack",
            juce::NormalisableRange<float> (1.0f, 7.0f, 0.01f),
            1.0f, // slowest (brief default)
            juce::AudioParameterFloatAttributes()));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::crushRelease, 1 },
            "Crush Release",
            juce::NormalisableRange<float> (1.0f, 7.0f, 0.01f),
            6.0f, // fast-but-not-fastest (brief default)
            juce::AudioParameterFloatAttributes()));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::crushOutput, 1 },
            "Crush Output",
            juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        addLevelMuteAudition (layout, ParamIDs::crushLevel, ParamIDs::crushMute, ParamIDs::crushAudition, "Crush", -9.0f);

        //======================================================================
        // Bus (2) SANDWICH
        addPassiveEqParams (layout,
            ParamIDs::sandPreLfFreq, ParamIDs::sandPreLfBoost, ParamIDs::sandPreLfCut,
            ParamIDs::sandPreHfBellFreq, ParamIDs::sandPreHfBellBoost, ParamIDs::sandPreHfBellBandwidth,
            ParamIDs::sandPreHfShelfFreq, ParamIDs::sandPreHfShelfAtten,
            "Sand Pre",
            3, 0.0f, 3.6f,   // LF: 100 Hz selected, no boost, cut ~ -3.8 dB (brief default)
            3, 3.4f, 5.0f,   // HF bell: 8 kHz selected, boost ~ +3.6 dB (brief default), moderate bandwidth
            1, 0.0f);        // HF shelf: 10 kHz selected, no attenuation

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::sandPeakRed, 1 },
            "Sand Peak Reduction",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
            40.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::sandLimit, 1 }, "Sand Limit", false));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::sandEmphasis, 1 },
            "Sand Emphasis",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
            100.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::sandResidual, 1 }, "Sand Residual", true));

        addPassiveEqParams (layout,
            ParamIDs::sandPostLfFreq, ParamIDs::sandPostLfBoost, ParamIDs::sandPostLfCut,
            ParamIDs::sandPostHfBellFreq, ParamIDs::sandPostHfBellBoost, ParamIDs::sandPostHfBellBandwidth,
            ParamIDs::sandPostHfShelfFreq, ParamIDs::sandPostHfShelfAtten,
            "Sand Post",
            3, 3.5f, 0.0f,   // LF: 100 Hz selected, boost ~ +2.8 dB (brief default), no cut
            3, 0.0f, 5.0f,   // HF bell: unused by default
            1, 2.2f);        // HF shelf: 10 kHz selected, atten ~ -1.8 dB (brief default)

        addLevelMuteAudition (layout, ParamIDs::sandLevel, ParamIDs::sandMute, ParamIDs::sandAudition, "Sand", -12.0f);

        //======================================================================
        // Bus (3) SPREAD
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::spreadDetune, 1 },
            "Spread Detune",
            juce::NormalisableRange<float> (0.0f, 15.0f, 0.01f),
            6.0f,
            juce::AudioParameterFloatAttributes().withLabel ("cents")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::spreadTime, 1 },
            "Spread Time",
            juce::NormalisableRange<float> (0.5f, 2.0f, 0.001f),
            1.0f,
            juce::AudioParameterFloatAttributes().withLabel ("x")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::spreadWidth, 1 },
            "Spread Width",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
            70.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        addLevelMuteAudition (layout, ParamIDs::spreadLevel, ParamIDs::spreadMute, ParamIDs::spreadAudition, "Spread", -18.0f);

        //======================================================================
        // Bus (4) SLAP
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::slapTime, 1 },
            "Slap Time",
            juce::NormalisableRange<float> (50.0f, 160.0f, 0.01f),
            110.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::slapStereo, 1 }, "Slap Stereo", false));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::slapTone, 1 },
            "Slap Tone",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
            50.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        addLevelMuteAudition (layout, ParamIDs::slapLevel, ParamIDs::slapMute, ParamIDs::slapAudition, "Slap", -15.0f);

        return layout;
    }
}
