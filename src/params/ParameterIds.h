#pragma once

// Central definition of all AudioProcessorValueTreeState parameter IDs for
// Miserere. See docs/architecture.md for the corresponding signal-flow
// diagram and docs/design-brief.md for the binding M1 topology/parameter
// specification.
//
// FROZEN AS OF THE v0.1.0 PARAMETER LAYOUT:
// Parameter IDs below must NEVER change once shipped - saved sessions and
// presets persist the APVTS state keyed by these string IDs, and renaming or
// removing one would silently break every user's saved state. Ranges,
// defaults, and skew MAY still be refined during voicing/tuning milestones;
// only the IDs themselves are frozen.
//
// Naming convention: global parameters have no prefix; per-bus parameters
// are prefixed busA_/busB_/busC_/busD_ matching the design brief's bus
// letters (A = Direct, B = Opto, C = Smash, D = Slap).
namespace ParamIDs
{
    //==========================================================================
    // Global
    inline constexpr auto inTrim = "in_trim";
    inline constexpr auto outTrim = "out_trim";
    inline constexpr auto bypass = "bypass";

    //==========================================================================
    // Bus A - Direct chain: HPF -> Console EQ -> FET Comp -> De-Esser -> Tape Sat
    inline constexpr auto busAHpfEnabled = "busA_hpfEnabled";
    inline constexpr auto busAHpfFreq = "busA_hpfFreq";

    inline constexpr auto busAEqLowGain = "busA_eqLowGain";   // fixed 100 Hz low shelf
    inline constexpr auto busAEqMidFreq = "busA_eqMidFreq";   // sweepable peak, 250 Hz-5 kHz
    inline constexpr auto busAEqMidGain = "busA_eqMidGain";
    inline constexpr auto busAEqMidQ = "busA_eqMidQ";
    inline constexpr auto busAEqHighGain = "busA_eqHighGain"; // fixed 8 kHz high shelf

    inline constexpr auto busACompRatio = "busA_compRatio";       // choice: 4:1 / 8:1
    inline constexpr auto busACompThreshold = "busA_compThreshold";
    inline constexpr auto busACompAttack = "busA_compAttack";
    inline constexpr auto busACompRelease = "busA_compRelease";
    inline constexpr auto busACompMakeup = "busA_compMakeup";

    inline constexpr auto busADeessEnabled = "busA_deessEnabled";
    inline constexpr auto busADeessFreq = "busA_deessFreq";
    inline constexpr auto busADeessThreshold = "busA_deessThreshold";

    inline constexpr auto busASatDrive = "busA_satDrive"; // 0 dB (parameter minimum) is a bit-exact bypass

    inline constexpr auto busALevel = "busA_level";
    inline constexpr auto busAMute = "busA_mute";
    inline constexpr auto busASolo = "busA_solo";

    //==========================================================================
    // Bus B - Opto sandwich: Passive EQ in -> Opto Leveler -> Passive Air out
    inline constexpr auto busBLowBoostFreq = "busB_lowBoostFreq";   // choice: 60 Hz / 100 Hz
    inline constexpr auto busBLowBoostGain = "busB_lowBoostGain";   // 0-10 dB, boost only
    inline constexpr auto busBHighBoostFreq = "busB_highBoostFreq"; // choice: 8/10/12/16 kHz
    inline constexpr auto busBHighBoostGain = "busB_highBoostGain"; // 0-10 dB, boost only

    inline constexpr auto busBOptoReduction = "busB_optoReduction"; // 0-100%, 0% is a bit-exact bypass
    inline constexpr auto busBOptoMakeup = "busB_optoMakeup";

    inline constexpr auto busBAirGain = "busB_airGain"; // fixed 12 kHz high shelf, 0-8 dB boost only

    inline constexpr auto busBLevel = "busB_level";
    inline constexpr auto busBMute = "busB_mute";
    inline constexpr auto busBSolo = "busB_solo";

    //==========================================================================
    // Bus C - Smash: FET Limiter, all-buttons character
    inline constexpr auto busCAttack = "busC_attack";
    inline constexpr auto busCRelease = "busC_release";
    inline constexpr auto busCDrive = "busC_drive";
    inline constexpr auto busCOutputTrim = "busC_outputTrim";

    inline constexpr auto busCLevel = "busC_level";
    inline constexpr auto busCMute = "busC_mute";
    inline constexpr auto busCSolo = "busC_solo";

    //==========================================================================
    // Bus D - Slap: fractional delay, filtered tape-soft feedback loop
    inline constexpr auto busDDelayMs = "busD_delayMs";
    inline constexpr auto busDFeedback = "busD_feedback";
    inline constexpr auto busDHpFreq = "busD_hpFreq";
    inline constexpr auto busDLpFreq = "busD_lpFreq";
    inline constexpr auto busDMono = "busD_mono";

    inline constexpr auto busDLevel = "busD_level";
    inline constexpr auto busDMute = "busD_mute";
    inline constexpr auto busDSolo = "busD_solo";
}
