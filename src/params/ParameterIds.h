#pragma once

// Central definition of all AudioProcessorValueTreeState parameter IDs for
// Miserere v2. See docs/architecture.md for the corresponding signal-flow
// diagram and docs/design-brief.md for the binding v2 topology/parameter
// specification.
//
// v2 is a full topology rewrite (docs/adr/0003, superseded framing in the
// design brief) and ships breaking parameter changes deliberately - the v1
// IDs below (busA_*/busB_*/busC_*/busD_*) are gone. Pre-1.0, breaking
// parameter changes are acceptable (see design-brief.md "Versioning");
// `setStateInformation` tolerantly ignores unknown old IDs rather than
// crashing (see StateTests.cpp's v1-import test) - there is no attempt to
// migrate v1 values forward, only to survive loading a v1 session.
//
// FROZEN AS OF THE v0.2.0 PARAMETER LAYOUT:
// IDs below must not change again without a further breaking-version bump.
// Ranges/defaults/skew may still be refined during voicing/tuning work.
//
// Naming convention: global parameters have no prefix; the direct (serial)
// path is prefixed direct_; the four parallel busses are prefixed
// crush_ (1 - FET limiter, all-buttons character), sand_ (2 - Passive EQ ->
// Opto Leveler -> Passive EQ "sandwich"), spread_ (3 - dual micro-pitch),
// slap_ (4 - single-repeat delay).
namespace ParamIDs
{
    //==========================================================================
    // Global
    inline constexpr auto inTrim = "in_trim";
    inline constexpr auto outTrim = "out_trim";
    inline constexpr auto bypass = "bypass";

    // Global stereo-detection link: unlinked (independent L/R detectors) is
    // the default - "dual mono is key" (design brief) - engaging Link makes
    // the Crush and Sandwich detectors track a combined L/R signal instead.
    inline constexpr auto link = "link";

    // "VCA ride back" macro: offsets all four return faders simultaneously,
    // -24...+6 dB.
    inline constexpr auto parallelTrim = "parallel_trim";

    //==========================================================================
    // Direct path (serial, every section optional, ALL OFF by default):
    // De-Esser (pre) -> FET Comp light -> Console EQ -> Sat -> De-Esser (post)
    inline constexpr auto directDeessPreEnabled = "direct_deessPre_enabled";
    inline constexpr auto directDeessPreFreq = "direct_deessPre_freq";
    inline constexpr auto directDeessPreThreshold = "direct_deessPre_threshold";

    inline constexpr auto directFetEnabled = "direct_fet_enabled";
    inline constexpr auto directFetThreshold = "direct_fet_threshold";
    inline constexpr auto directFetAttack = "direct_fet_attack";
    inline constexpr auto directFetRelease = "direct_fet_release";
    inline constexpr auto directFetMakeup = "direct_fet_makeup";

    inline constexpr auto directEqHpfEnabled = "direct_eq_hpfEnabled";
    inline constexpr auto directEqHpfFreq = "direct_eq_hpfFreq";       // choice: 50/80/160/300 Hz
    inline constexpr auto directEqLowFreq = "direct_eq_lowFreq";       // choice: 35/60/110/220 Hz
    inline constexpr auto directEqLowGain = "direct_eq_lowGain";
    inline constexpr auto directEqMidFreq = "direct_eq_midFreq";       // choice: 360/700/1600/3200/4800/7200 Hz
    inline constexpr auto directEqMidGain = "direct_eq_midGain";
    inline constexpr auto directEqHighGain = "direct_eq_highGain";     // fixed 12 kHz shelf
    inline constexpr auto directEqDrive = "direct_eq_drive";           // 2nd+3rd transformer-style harmonics

    inline constexpr auto directSatDrive = "direct_sat_drive"; // 0 dB (parameter minimum) is a bit-exact bypass

    inline constexpr auto directDeessPostEnabled = "direct_deessPost_enabled";
    inline constexpr auto directDeessPostFreq = "direct_deessPost_freq";
    inline constexpr auto directDeessPostThreshold = "direct_deessPost_threshold";

    //==========================================================================
    // Bus (1) CRUSH - FET limiter, all-buttons character
    inline constexpr auto crushInput = "crush_input";     // 0-48 dB drive, no threshold knob
    inline constexpr auto crushRatio = "crush_ratio";     // choice: 4:1/8:1/12:1/20:1/ALL
    inline constexpr auto crushStyle = "crush_style";     // choice: All-Buttons / Gentle
    inline constexpr auto crushAttack = "crush_attack";   // 1-7, inverted taper (7 = fastest)
    inline constexpr auto crushRelease = "crush_release"; // 1-7, inverted taper (7 = fastest)
    inline constexpr auto crushOutput = "crush_output";   // makeup trim

    inline constexpr auto crushLevel = "crush_level";
    inline constexpr auto crushMute = "crush_mute";
    inline constexpr auto crushAudition = "crush_audition"; // exclusive - never call it "solo" (brief)

    //==========================================================================
    // Bus (2) SANDWICH - Passive EQ -> Opto Leveler -> Passive EQ
    inline constexpr auto sandPreLfFreq = "sand_pre_lfFreq";   // choice: 20/30/60/100 Hz
    inline constexpr auto sandPreLfBoost = "sand_pre_lfBoost"; // 0-10 dial
    inline constexpr auto sandPreLfCut = "sand_pre_lfCut";     // 0-10 dial
    inline constexpr auto sandPreHfBellFreq = "sand_pre_hfBellFreq";           // choice: 3/4/5/8/10/12/16 kHz
    inline constexpr auto sandPreHfBellBoost = "sand_pre_hfBellBoost";         // 0-10 dial
    inline constexpr auto sandPreHfBellBandwidth = "sand_pre_hfBellBandwidth"; // 0 (sharp) - 10 (broad)
    inline constexpr auto sandPreHfShelfFreq = "sand_pre_hfShelfFreq";   // choice: 5/10/20 kHz
    inline constexpr auto sandPreHfShelfAtten = "sand_pre_hfShelfAtten"; // 0-10 dial

    inline constexpr auto sandPeakRed = "sand_peakred";   // 0-100%, drive into the fixed static curve
    inline constexpr auto sandLimit = "sand_limit";       // knee ratio ~3:1 -> ~10:1
    inline constexpr auto sandEmphasis = "sand_emphasis"; // 0-100%, detector-only HF-selective compression
    inline constexpr auto sandResidual = "sand_residual"; // defeatable never-flat vintage residual, default ON

    inline constexpr auto sandPostLfFreq = "sand_post_lfFreq";
    inline constexpr auto sandPostLfBoost = "sand_post_lfBoost";
    inline constexpr auto sandPostLfCut = "sand_post_lfCut";
    inline constexpr auto sandPostHfBellFreq = "sand_post_hfBellFreq";
    inline constexpr auto sandPostHfBellBoost = "sand_post_hfBellBoost";
    inline constexpr auto sandPostHfBellBandwidth = "sand_post_hfBellBandwidth";
    inline constexpr auto sandPostHfShelfFreq = "sand_post_hfShelfFreq";
    inline constexpr auto sandPostHfShelfAtten = "sand_post_hfShelfAtten";

    inline constexpr auto sandLevel = "sand_level";
    inline constexpr auto sandMute = "sand_mute";
    inline constexpr auto sandAudition = "sand_audition";

    //==========================================================================
    // Bus (3) SPREAD - dual micro-pitch, ~30/50 ms, L/R
    inline constexpr auto spreadDetune = "spread_detune"; // +/- 0-15 cents, default 6
    inline constexpr auto spreadTime = "spread_time";     // 0.5-2x scale on the 30/50 ms base delays
    inline constexpr auto spreadWidth = "spread_width";   // 0-100%

    inline constexpr auto spreadLevel = "spread_level";
    inline constexpr auto spreadMute = "spread_mute";
    inline constexpr auto spreadAudition = "spread_audition";

    //==========================================================================
    // Bus (4) SLAP - ~110 ms single-repeat dark delay
    inline constexpr auto slapTime = "slap_time";     // 50-160 ms, default 110, plain ms (not tempo-synced)
    inline constexpr auto slapStereo = "slap_stereo"; // false (mono return) is the default
    inline constexpr auto slapTone = "slap_tone";     // 0-100%, dark...darker (BBD-style HF loss + soft sat)

    inline constexpr auto slapLevel = "slap_level";
    inline constexpr auto slapMute = "slap_mute";
    inline constexpr auto slapAudition = "slap_audition";
}
