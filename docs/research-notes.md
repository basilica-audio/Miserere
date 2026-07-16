# Research notes (v2)

This document collects the sourced findings behind Miserere v2's topology and module
voicing (docs/design-brief.md). **The voicing is research-derived, not measured against
hardware units** — every number below comes from public documentation, manuals, interviews
or community measurement, filtered through a plugin implementation, not from probing a real
FET limiter, opto leveler, passive EQ or console strip on a bench. Where a source's own
measurement methodology is uncertain (community posts, secondary summaries), that is noted.
No brand or person names appear in the plugin's parameters, UI or code — this document is the
one place that may cite public interviews and manuals as sources (design-brief.md's "Honesty
& framing" section).

The technique modelled here is the documented **2010–2023-era parallel vocal template**
popularized in public interviews by mixers such as Andrew Scheps. As of late 2024 that
mixer has publicly moved away from the technique in his own current work — Miserere
recreates the classic, widely-documented era, not a claim about anyone's present-day
practice.

## Topology

- **Post-fader unity sends, not wet/dry crossfades.** "if you have the sends default to
  being post fader at zero and following the pan, then whatever you decide to send to that
  compressor, you're basically just picking up a copy of what's in the mix bus"; "The key is
  that the sends to it are at 0 and post fader... you are making a copy of your mix...,
  compressing it and then blending it back in with the uncompressed tracks." (Audiofanzine
  "Mixing in Parallel" interview; Gearspace layout thread). This is the direct basis for the
  brief's "direct path output feeds the sum at unity AND all four sends (unity taps)".
- **Parallel chains are simple; EQ around them corrects artefacts, it doesn't define them.**
  "usually the chains are actually really simple. It's usually just a compressor. Every once
  in a while there will be EQ before or after to take care of some of the artifacts."
- **Parallel compressors are driven far harder than any insert would be.** "a parallel
  compressor should be more compressed than you would ever put on the insert directly
  because you only want to blend in a little bit... you've got full range with all of your
  transients... and then you start slamming this slab of the same sound that doesn't have the
  dynamic range and you're blending it in."
- **The dry signal must survive untouched.** "whenever I directly affect a sound, especially
  with a dynamics processor like a compressor, I feel as though there's something about the
  original source that I'm losing... the natural transient of a kick drum or the more
  natural envelope of a vocal, which is where you get your emotional reaction." And: "Even
  with all that stuff in the mix, you'd probably think the vocal is bone dry. But if you
  soloed it, and took that stuff off, it would be like, 'Oh, now THAT's dry.'" — the direct
  basis for v2's core correction over v1 (default = wire).
- **Never judge a parallel return in solo.** "don't spend a huge amount of time listening to
  what the parallel thing is [solo], spend a huge amount of time listening to it as you
  blend it in." — the basis for labelling the per-bus solo control "Audition" rather than
  "Solo" in the UI (same signal-flow behaviour, different framing).
- **Unlinked (dual-mono) stereo detection is a stated signature.** "I love stereo compressing
  things with the compressors unlinked. I don't like them linked" (The Pro Audio Files
  masterclass transcript); "Dual Mono is key" (Gearspace). Default `link` = off in Miserere.
- **A macro trim that scales all returns together is an authentic workflow gesture.** "I use
  a bit less of the parallel processing when mixing more organic sounding music which is why
  I have VCAs so I can really quickly back off the returns." — the basis for `parallel_trim`.
- **The exact vocal-specific module set is a direct primary-source quote**: "a big part of
  my template is parallel compression and there is some for the vocals, including an
  all-buttons-in 1176 and a chain with a couple of pultecs sandwiching an LA2A... I also have
  some effects (micro-pitch slap and slap delay) that are more for thickening than discrete
  effects." This one sentence is the direct basis for Miserere's four busses (CRUSH,
  SANDWICH, SPREAD, SLAP).

## FET (bus ① CRUSH, and the Direct path's lighter "FET Comp" instance)

- **Input-drive paradigm, no threshold knob.** "Peak limiting is accomplished by utilizing an
  FET as a voltage variable resistor" in a feedback configuration; "turning up the Input knob
  has the same effect as turning down the Threshold control on other compressors." An
  academic analysis confirms "selecting higher ratios also raises the threshold level" with a
  harder knee at higher ratios, softer knee at 4:1 (roughly −30 dB @ 4:1 up to −24 dB @ 20:1,
  referenced to 0.775 V) — the basis for `crush`'s fixed per-ratio threshold/knee table
  (`FetCrush::ratioPointFor`).
- **Inverted-taper ballistics.** Attack "less than 20 microseconds ... adjustable to 800
  microseconds with front-panel control"; release "50 milliseconds minimum, 1.1 seconds
  maximum (for 63% recovery)" — the 63%-recovery phrasing is a one-pole time-constant
  definition, which is what `crush_attack`/`crush_release`'s exponential ballistics implement.
  Both knobs reverse-taper (7 = fastest).
- **Dual-rate, program-dependent release.** Release "responds faster after transients and
  slower after sustained compression to reduce pumping"; modelled with a fast time constant,
  a slow time constant, and a transition governed by how long the signal has been compressing
  — the direct basis for `FetCrush`'s duration integrator blending a fast/slow release.
- **ALL-buttons plateau + transient lag.** "the ratio goes to somewhere between 12:1 and
  20:1, and the bias points change all over the circuit"; the curve is "more like severe
  plateau" with "a lag time on initial transients" so "transient punch is maintained before
  that compression hits hard", and "compression distortion increases radically." Modelled as
  a steep ratio just above the knee giving back to a softer ratio past a fixed overshoot
  ("kink"), plus a short extra attack lag exclusive to ALL mode.
- **Distortion is small and mostly a side effect of fast ballistics, not a baked-in
  waveshaper.** "Less than 0.5% THD... at 1.1 seconds release." No dedicated nonlinearity is
  modelled beyond what the correct sample-rate gain computer and detector ripple already
  produce.
- **The rear-bus/all-buttons recipe used across many community threads**: "all buttons in,
  attack slowest, release fastest... he doesn't even know how much GR it's doing, just sets
  it by ear" (Gearspace, quoting a Waves demo video); a later template moved to "2:1, attack
  slowest, release around 6" — the direct basis for the `Gentle` style (fixed 2:1) alongside
  `All-Buttons`.
- **Typical target GR on the crush-style bus**: "not... more than maybe 3 to max 5db on loud
  passages" for the gentler recipe, vs. "an easy 10-12 dB" or "20dB+" for a dedicated "crush"
  layer — informs the design brief's "comfortably reaches 20+ dB GR" requirement.

## Opto (the middle of bus ② SANDWICH)

- **No sidechain rectifier/filter.** "Since the light is produced directly from the audio
  voltage, the response is instantaneous. Rectification and filtering of the audio to
  produce a control signal are not necessary"; the practically audible ~10 ms attack is
  attributed to the photoresistor itself, not a detector filter — the basis for driving the
  detector with the raw (optionally emphasis-filtered) audio directly, with no separate
  smoothing stage ahead of the ballistics.
- **Two-stage release with memory.** "approximately 0.06 seconds for 50% release, 0.5 to 5
  seconds for complete release depending upon amount of previous reduction"; "about 60
  milliseconds for 50% release, and then a gradual release over a period of 1 to 15 seconds."
  A companion generic-LDR measurement paper models the same asymmetric turn-on/turn-off
  behaviour with three parallel one-pole filters at different time constants — the direct
  basis for `OptoLeveler`'s three parallel release paths (fast/mid/slow), combined via max().
- **Static curve = lookup.** "Compressor action occurs from the breakaway point at −30 DB
  input and up to −20 DB, at which point the curve becomes horizontal to exhibit limiting
  action. The input increases an additional 20 DB, but the output increases less than 1 dB."
  The compression zone is characterised as averaging ~3:1, with a Limit-style mode moving it
  toward ~10:1 — the direct basis for `OptoLeveler::staticCurveOutputDb`.
- **Detector-only HF emphasis.** "Increasing the resistance of R37 reduces the amplitude of
  the low frequency voltage applied to the Peak Reduction control... Maximum high frequency
  response will provide approximately 10 DB more reduction at 15 KC than at frequencies below
  1 KC" — modelled as a low-shelf cut of up to −10 dB below ~1 kHz applied to the detector
  copy only, never the audio path.
- **The GR element itself is clean; colour comes from a separate, fixed stage.** Gain
  reduction up to 40 dB causes "no wave form or harmonic distortion"; the output stage is
  characterised as a low-distortion push-pull follower — modelled as a small, always-on,
  fixed-drive saturator placed strictly after the (clean) gain multiply.
- **Recommended operating point.** "Continuous extreme reduction... does tend to reduce the
  dynamic range of music. Maximum benefit is obtained by running 4 to 8 DB of compression
  continuously" — informs the SANDWICH bus's target GR framing (`sand_peakred`'s default).
- **The Pultec-LA2A-Pultec "sandwich" itself**, with concrete community-reconstructed values:
  pre-EQ wide LF cut (~100 Hz, ~2.8–3.8 dB) plus HF bell boost (~8 kHz, ~3–4 dB) to "starve"
  the leveler of lows and make it react to the top; the leveler's own HF-emphasis control
  rotated toward "only compresses the top-end above 1k, sort of like a multiband compressor",
  ~2–3 dB max GR; post-EQ mirrors with LF boost (~100 Hz, ~2.5–2.8 dB) and HF cut (~8–10 kHz,
  ~1.5–1.8 dB) to restore static tone while leaving the >1 kHz band dynamically controlled —
  the direct basis for SANDWICH's pre/post-EQ defaults.

## Passive EQ (shared module, two instances inside SANDWICH)

- **Exact band grid** (from two independently corroborating sources): LF shelf boost AND cut
  at a shared frequency selector (20/30/60/100 Hz); HF bell boost at 3/4/5/8/10/12/16 kHz
  with continuously variable bandwidth; HF shelf cut at 5/10/20 kHz. Notably asymmetric
  architecture: boost is a resonant bell-like shelf only in the HF section; cut is a shelf
  only; nothing lives in the mids.
- **Asymmetric gain calibration.** LF boost "to a maximum of 13.5 dB" vs. cut "17.5 dB"
  (hardware analysis); HF bell boost up to ~18–20 dB, HF shelf cut ~16–18 dB. Dials are 0–10,
  not dB-calibrated — the basis for `PassiveEq`'s nonlinear (power-law) dial taper and its
  13.5/17.5 dB LF asymmetry.
- **The "low-end trick": simultaneous boost+cut does not cancel.** "While Boost and
  Attenuation work on the same cutoff frequency, they do not cancel one another out when
  applied simultaneously; rather, they create a resonant shelf"; a hardware analysis
  describes "the Boost control has slightly higher gain than the Attenuation has cut, and the
  frequencies they affect are slightly different", citing a 30 Hz example producing a bump
  around ~80 Hz and a dip around ~200 Hz. **Implementation note**: reproducing this exact
  interaction from a real passive LC network with two independent digital shelf filters
  proved not to fall out "for free" from two literal low-shelves at the same corner (both
  reach full effect at DC regardless of corner placement, so cut > boost nets negative at
  DC). `PassiveEq` instead models boost as a *resonant* low shelf whose corner sits AT the
  selector frequency (Q > 0.707, so it peaks at its own corner) and cut as a broader,
  non-resonant *peaking dip* centred well above it (6× the selector) — chosen and verified
  numerically during implementation (see the PR description) to reproduce the qualitative
  "bump below/at corner, dip in the low-mids" shape with comfortable margins, not a literal
  circuit simulation.
- **Never truly flat.** With all gains at zero the hardware imprints a ±0.3–0.4 dB tilt that
  varies with the LF frequency selector, plus measurable transformer HF rolloff — the basis
  for the defeatable `sand_residual` (default ON).
- **Makeup stage colour.** A passive LC network followed by a fixed-gain push-pull tube
  amplifier stage with negative feedback — described as predominantly low-order, subtle,
  "increase[s] the overall signal level just by getting inserted" rather than adding an
  explicit output-gain control. Not separately modelled as a distinct stage in v2 beyond the
  Opto leveler's own gentle post-attenuator colouration (see above) — a simplification noted
  here rather than silently assumed.

## Console EQ (Direct path)

- **Exact curve grid**, quoted from an official spec sheet: high shelf fixed 12 kHz ±16 dB;
  mid bell ±18 dB **fixed Q** (not proportional-Q) with selectable centre frequencies at
  0.36/0.7/1.6/3.2/4.8/7.2 kHz; low shelf ±16 dB at 35/60/110/220 Hz; HPF 18 dB/oct,
  switchable between 50/80/160/300 Hz. This grid maps 1:1 onto `ConsoleEq`'s parameter
  layout, including the 3-pole (1st-order + 2nd-order Butterworth cascade) HPF construction
  used to reach 18 dB/oct with minimum-phase, zero-latency filters.
- **Saturation voicing.** Baseline is extremely clean (≤0.07% THD at +20 dBu); when driven,
  community harmonic analysis reports "almost equal amounts of second and third harmonic,
  with the latter slightly greater", attributed to input/output transformers and all-class-A
  discrete stages — the basis for `direct_eq_drive` blending a 3rd-leaning tanh curve
  (reusing the shared `TapeSaturator` curve) with a small additional even-harmonic term.

## Spread (bus ③)

- **Exact technique and values, direct quote**: "the next send along here is spread, which...
  is just going to be dual 910s. Tiny bit of pitch shift, tiny bit of delay. So 30 and 50
  milliseconds approximately, up a tiny bit, down a tiny bit... you don't actually want to
  hear things chorusing... you can hear it just pushed to the outside just a little bit." —
  the direct basis for `SpreadPitch`'s two voices (≈30 ms up, ≈50 ms down, hard-panned) and
  the calibration goal ("must not chorus at default level").
- **Implementation note**: the source describes hardware pitch-shifters (a well-known
  delay-line-based "glide/crossfade" Doppler technique); `SpreadPitch` implements the same
  family of technique (two crossfading variable-delay taps per voice) rather than a specific
  unit's internal algorithm.

## Slap (bus ④)

- **Exact technique, direct quotes**: "Usually in my template, the slap is a very short 100
  millisecond delay... 122 milliseconds here [in this session]" (a session-specific example);
  "a slap somewhere in the 110 ms range" — the basis for the 110 ms default and the
  50–160 ms range.
- **Single repeat, darkness from the delay's own character, not filtering/feedback.** "I use
  BBD Delay... for vocal slap" with "no feedback or filtering ever mentioned - the darkness
  comes from the BBD emulation itself" — the direct basis for v2 dropping the feedback
  parameter entirely (fixed at 0) and voicing the darkness (progressive HF loss + soft
  saturation) into the single repeat itself, structurally different from a filtered feedback
  loop.
- **Community-converged generic slap parameters** (60–120 ms, deliberately not tempo-synced,
  zero feedback, filtered HP/LP, mono, "you only notice it's gone when you mute it") were
  used as secondary corroboration for the level-calibration goal, not as the primary source
  for the darkness mechanism (which is the BBD-emulation quote above).

## Calibration cross-reference

- Hardware nominal operating levels (FET thresholds ≈ −30…−24 dB re 0.775 V; opto breakaway
  at −30 dBm, both designed around +4/+10 dBm broadcast lines with onset of compression
  roughly 30 dB below ceiling) inform the internal dBFS reference the modules' fixed
  thresholds are calibrated against, so that default knob positions land in a musically
  similar place to the hardware's own recommended operating range rather than requiring
  extreme settings to do anything audible.
- Historical caveat, direct quote (Oct 2024): "over the last year, year and a half I've
  gotten to the point where I am using almost no parallel compression anymore... the rear bus
  hasn't made an appearance in a mix in probably a year." Miserere therefore documents and
  recreates the 2010–2023 template era specifically, not a claim about current practice by
  any person or brand.

## Sourcing method

These notes were compiled from a four-lens research pass (86 sourced findings total across
official manuals/spec sheets, direct-quote interviews, community-consensus threads, and one
inferred cross-module simplification pass) prior to implementation. Two items were flagged
`OPEN` in the source material and are called out here rather than silently resolved:

- The exact factory default module order for a cited channel-strip reference product is only
  weakly documented (conflicting screenshots between two manual versions) — not load-bearing
  for Miserere's own fixed topology, which does not offer reorderable modules in v2.
- The opto leveler's detector topology (feedforward vs. feedback) is described inconsistently
  between the primary manual and later secondary analyses — `OptoLeveler` implements a
  feedforward-style detector (audio → detector → gain computer → multiply), which is what the
  primary manual's own block-diagram framing supports.
