# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.5.0] — 2026-07-27

The "Circuit Engines" release. Every per-bus module that used to be a textbook
approximation is now derived from the circuit it is named after — and all of it still
reports **zero latency**, because the anti-aliasing is done with antiderivative
anti-aliasing and matched filters rather than oversampling.

### Added

- **Wobble** (SLAP, `slap_wobble`, default 0%) — tape-transport wow and flutter: a slow
  pinch-roller wow, a faster capstan flutter with its second harmonic, and a leaky random
  drift, each with slow independent wander so nothing is locked-periodic. 0–0.5% W&F across
  the dial's range. At 0 the modulation is structurally off (the random generator is never
  advanced), so the neutral render stays bit-identical between runs.
- **Age** (SLAP, `slap_age`, default 0%) — tape wear: pink-shaped hiss with level-following
  asperity noise plus extra head-to-tape spacing loss. At 0 both are structurally silent.
- Two factory presets exercising the new controls: **Tape Slap 7.5** and **Worn Slap**.
- New test suite `tests/AliasingTests.cpp` plus roughly 40 new measurement cases across the
  existing suites (161 cases / 72 589 assertions total).

### Changed — v0.5.0 voicing pass (this release changes how non-neutral settings sound)

As with the v0.4.0 M2 pass, the fidelity upgrades below deliberately change the rendered
sound of the SANDWICH and CRUSH busses and of the direct path's drive stages at non-neutral
settings. Neutral is unaffected: with everything off the plugin is still bit-transparent, and
old sessions still load with unchanged settings. Existing factory presets keep their stored
values — none were retuned.

- **CRUSH is now a feedback-FET engine.** The hand-built dual-rate release blend and duration
  integrator are gone. A single-capacitor RC sidechain with the attack pot in the charge path
  and the release pot in the discharge path is driven from the output, and the FET divider
  cell carries the residual second-harmonic "hair" that grows with gain-reduction depth.
  Ratio creep, attack/release interaction, the knee ordering across 4:1…20:1 and the
  all-buttons overshoot are now *emergent* from the loop rather than tabulated — each pinned
  by its own measurement. The all-buttons mode is a genuine fifth setting, never an
  interpolation.
- **SANDWICH is now a real photocell model.** The three-path `max()` detector and the
  hand-authored static curves are gone, replaced by an electroluminescent-panel law feeding a
  two-state carrier-density model. The two-stage release, the memory effect (longer and
  deeper gain reduction releases more slowly) and the program-dependent attack all fall out
  of the physics. Measured: 50% recovery in 30–120 ms with the last 10% taking over 400 ms.
- **Anti-aliasing on every drive stage** via first-order antiderivative anti-aliasing in
  *residual* form — the curve is split into an exactly sample-aligned linear part plus a
  nonlinear residual, and only the residual is filtered. That is what lets the plugin
  suppress aliasing without adding the half-sample delay or the high-frequency droop that
  plain antiderivative anti-aliasing would impose, so the parallel busses stay
  sample-aligned. Measured at 44.1 kHz: at least 12 dB less aliasing than the untreated
  curve at the stress settings, and non-harmonic content at or below −60 dBFS on a
  programme-realistic 3 kHz / −12 dBFS vocal-level anchor. No absolute floor is claimed at
  full-scale stress extremes — see the manual.
- **Decramped top octave.** The console EQ's 12 kHz shelf and the passive EQ's HF bell and
  shelf now use magnitude-matched filters, so at 44.1 kHz the top octave keeps its analog
  shape instead of being squeezed toward Nyquist.
- **Passive EQ LF section rebuilt from component values.** The boost/cut network is now the
  hardware ladder's exact transfer function, so the famous simultaneous boost-and-cut curve,
  the cut corner sitting above the boost corner, and the gain/bandwidth coupling on the HF
  bell are consequences of the circuit rather than of chosen filter settings.
- **Console EQ drive is now a flux-domain iron model.** An ad-hoc squared term is replaced by
  a flux integrator into a biased saturator and back out through the integrator's exact
  inverse. Because magnetic flux scales as voltage over frequency, third-harmonic content now
  rises toward the bass on its own (measured +12 dB from 100 Hz to 50 Hz) and the
  second-to-third-harmonic ratio lands on the hardware measurement anchor.
- **SLAP is a tape transport, not just a filtered delay.** Cubic Hermite fractional reads,
  the record-side saturator moved ahead of the delay write (so repeats and input no longer
  share one lump of saturation), a fixed head bump, and age-scaled spacing loss.
- **SPREAD quality pass.** Third-order Lagrange interpolation on both shifter delay lines
  (0.90 dB less high-frequency loss at 10 kHz than the previous linear read), per-sample
  smoothing of Detune and Time so automation no longer steps at block boundaries, and a
  longer equal-power grain crossfade.
- **Mute and Audition no longer click** — bus routing now rides a 3 ms ramp. Muted busses
  still contribute exact zeros once the ramp has settled.

### Compatibility

- Sessions and presets from v0.2.0–v0.4.0 load with every stored value intact. The two new
  parameters default to exact neutral and are explicitly reset when a state that predates
  them is loaded, so a project cannot inherit stale Wobble/Age from whatever was last dialled
  in. State is now stamped with a schema version.
- Automation lanes are stable: the new parameters are versioned as a later generation so
  hosts cannot re-order them against the existing set.
- Latency is still **0 samples**, and the neutral wire null and parallel-bus impulse
  alignment are unchanged.

### Known limitations

- SPREAD's two crossfading taps read one delay line at a fixed offset, so a sustained pure
  tone still meets a comb whose depth depends on the note. Broadband material is unaffected.
  Correlation-aligned splicing is the fix and is on the roadmap.
- After a long silence the SANDWICH bus rests around −150 dBFS rather than at exact zero
  (roughly 30 dB below a 24-bit least-significant bit). Every other bus reaches exact zero.

## [0.4.0] — 2026-07-23

### Added — M2 voicing pass: CRUSH program-dependent colour

- **Bus ① CRUSH (`src/dsp/FetCrush.{h,cpp}`)**: added the design brief's "Color" line that
  the v2 rewrite had left unbuilt (`docs/research-notes.md`'s FET section: "Less than 0.5%
  THD... at 1.1 seconds release") - a small, gain-reduction-gated pair of colour stages on
  top of the existing (untouched) detector-ripple colouration:
  - A class-A-style asymmetric term (a small even-harmonic addition biasing the two
    half-cycles differently, the classic single-ended-stage signature).
  - A transformer-style LF-selective soft saturation (a one-pole ~150 Hz lowpass extract
    driven into `tanh`, so the added colour concentrates below the cutoff rather than
    spreading broadband, matching a real output transformer's core saturating more at low
    frequencies for a given level).
  - Both terms are gated by how hard the limiter is currently working (0 at no gain
    reduction, full strength at 12 dB of GR), so a quiet, uncompressed signal stays clean.
    Engineering approximations tuned by measurement, not a bench-measured match to any
    specific hardware unit's THD curve - see `docs/architecture.md`'s "M2 voicing pass"
    section for the full reasoning and the honest by-ear-vs-measured framing.
- Regression-frozen with five new Catch2 cases (`tests/FetCrushTests.cpp`'s `[colour]`
  tag): negligible THD below gain reduction, THD growing with GR (level-dependence), a mild
  THD ceiling at moderate GR, more added harmonic content on a low-frequency tone than a
  high-frequency one at equal drive (the LF-selectivity claim), and finite/bounded output at
  full-scale drive. 127 Catch2 tests green (up from 122).
- CPU cost: one extra `tanh` call and one one-pole filter update per sample per channel,
  alongside the module's existing per-sample envelope-to-dB/dB-to-gain transcendental calls;
  a rough timing probe (unoptimised Debug build, single core) processed ~213 s of stereo
  audio in ~1.46 s (~146× real-time) - no oversampling added, no measurable CPU concern.

## [0.3.0] — 2026-07-16

### Added — M2 preset system + German frame localisation

- **Preset system** (`src/presets/PresetManager.{h,cpp}`, `src/presets/PresetBar.{h,cpp}`),
  ported verbatim from `basilica-audio/nave`'s pilot implementation
  (`.scaffold/specs/preset-system-m2.md`, the binding suite-wide spec) and adapted with
  Miserere's own plugin ID/name and factory preset content:
  - Factory presets (`presets/factory/*.json`, embedded via BinaryData) and user presets
    (`~/Library/Audio/Presets/Yves Vogl/Miserere/` on macOS, `%APPDATA%/Yves Vogl/Miserere/Presets/`
    on Windows), created on demand.
  - Save, Save As, rename, delete, next/previous (factory then user, alphabetical),
    single-file import/export, and zip-bank import/export.
  - Forward/backward-tolerant JSON import: unknown parameter IDs are ignored, missing IDs
    keep their current default, a wrong `plugin`/`format` tag refuses the import with a
    friendly, localised error.
  - Default resolution: a fresh instance loads a user `Default` preset if one exists,
    otherwise the factory `Default` preset (`applyStartupDefault()`, called once from the
    processor constructor); "Set current as default"/"Reset default" write/delete that user
    file.
  - `PresetBar`: a horizontal strip docked at the top of the editor
    (`[<] [PresetName*] [>] [Save] [Save As...] [Delete] [Import...] [Export...]`), with a
    Factory/User preset menu and a dirty-state `*` indicator - deliberately plain per the
    spec's "M3 restyles it, do not gold-plate" note.
- **10 factory presets** (`docs/presets.md` documents each one's intent): **Default** (Init -
  the certified out-of-the-box state, also the M2 default-resolution target), **Classic
  Parallel Blend** (the documented 2010s template's canonical fader recipe, driven hard enough
  to be genuinely audible), **Crush Forward** / **Silk Sandwich** / **Gentle Bus** (isolate
  one parallel bus at a time with the other three muted), **Wide & Wet** (Spread + Slap
  showcased together), **Direct Channel Only** (the serial path's optional sections on, all
  four returns muted), **Rough Mix Glue** (light, balanced touch of all four busses),
  **Whisper Thicken** (thin/quiet-vocal thickening via Spread/Slap with light Crush/Sandwich
  control), and **Aggressive Rock Vocal** (the full direct-path-plus-parallel-bus chain
  leaned in hard). No brand or person names anywhere.
- **German frame localisation** (`resources/i18n/de.txt`, embedded via BinaryData, ported
  verbatim from nave): every `PresetBar`/`PresetManager` user-facing frame string (button
  labels, menu items, dialog text, error messages) is wrapped in JUCE's `TRANS()` and
  auto-selected via `SystemStats::getUserLanguage()` at editor construction (`de*` → German,
  else English, no user-facing language switch yet). Parameter names, units and DSP
  terminology (Attack, Release, Threshold, Ratio, Mix, Level, Drive, Hz, dB, %, ...) are never
  translated anywhere in the plugin.
- 20 new Catch2 test cases: 17 in `tests/PresetManagerTests.cpp` (save/load round-trip,
  forward/backward-compat import tolerance, wrong-plugin/wrong-format import refusal, every
  factory preset parses/loads and stays parameter-plausible, the three-way default-resolution
  order, the dirty-flag lifecycle, prev/next wrap-around, rename/delete guards, single-file and
  zip-bank import/export round-trips, and dirty-tracking coexisting safely with real-time
  `processBlock()` calls) and 3 in `tests/LocalisationTests.cpp` (`de.txt` parses as a
  well-formed German `LocalisedStrings` mapping, every `PresetBar`/`PresetManager` frame key
  has a translation, and a representative sample of DSP/parameter terminology is verifiably
  absent from the mapping) - all isolated from the real per-user preset directory via
  `PresetManagerConfig::userPresetsDirectoryOverrideForTests`. 122 tests total, all green.

## [0.2.0] — 2026-07-16

### Changed — topology rewrite (v1 → v2, breaking)

**v1's topology was wrong and has been replaced entirely**, not extended. v1 treated the
"Direct" chain as one of four equal, independently-faded parallel busses, and processed it by
default. That inverts the actual "rough vocal template" technique this plugin packages: the
dry vocal is supposed to pass through untouched, at unity, with its natural envelope and
phrasing intact ("you'd probably think the vocal is bone dry") — everything else gets layered
*underneath* it via return busses that are copies of the dry signal, processed hard, blended
back in at a modest level, and never judged in solo.

- **The Direct path is now the "channel", not a bus.** It always sums at unity and feeds all
  four parallel sends (post-fader unity taps); it is no longer one of the faded/muted/
  auditioned busses.
- **The Direct path is bit-transparent by default.** Every one of its optional sections
  (De-Ess Pre, FET Comp, Console EQ, Sat, De-Ess Post) starts OFF. This is the plugin's core
  invariant — see `tests/NullAndAlignmentTests.cpp`.
- **Four return busses, redesigned**, each a unity-tap copy of the direct-path output:
  - ① **Crush** — FET limiter, all-buttons character, input-drive paradigm (no threshold
    knob), a fixed per-ratio threshold/knee table, inverted-taper 1–7 attack/release dials,
    dual-rate program-dependent release, an ALL-mode plateau with a genuine give-back and a
    transient-lag "snap", and a softer fixed Gentle (2:1) style.
  - ② **Sandwich** — Passive EQ → Opto Leveler → Passive EQ. The Passive EQ (shared, two
    instances) supports simultaneous non-cancelling LF boost+cut, an HF bell with variable
    bandwidth, an HF shelf cut, and a defeatable never-flat "vintage residual". The Opto
    Leveler now uses a raw-audio detector (no separate sidechain smoothing), a three-path
    release with GR-history memory, an explicit static-curve lookup (with a Limit switch),
    and detector-only HF-selective emphasis.
  - ③ **Spread** — new: dual micro-pitch (~30 ms up / ~50 ms down, ±cents, hard-panned L/R)
    via delay-line Doppler pitch shifting with crossfaded taps.
  - ④ **Slap** — feedback is fixed at 0 (dropped as a parameter): a single dark repeat, its
    BBD-style darkness (progressive HF loss + soft saturation) baked directly into that one
    repeat rather than a filtered feedback loop.
- **Return faders are −60…+6 dB (true zero at the bottom)** with brief-specified defaults
  (Crush −9 dB, Sandwich −12 dB, Spread −18 dB, Slap −15 dB), plus Mute and an exclusive
  **Audition** control (renamed from Solo — same isolate-this-bus behaviour, framed
  differently because the technique forbids judging the parallel busses' sound in solo).
- **New global controls**: `Link` (unlinked/dual-mono detection by default, matching the
  documented technique), `Parallel` (a macro trim that offsets all four return faders
  together — the "VCA ride back" gesture).
- **Console EQ redesigned** to a 1073-class grid (stepped HPF/low-shelf/mid-bell/high-shelf
  frequencies, fixed-Q mid bell, an 18 dB/oct 3-pole HPF) with a Drive control blending
  transformer-style 2nd/3rd harmonics; the standalone v1 `Hpf` module is retired (folded in).
- **Breaking parameter changes**: every v1 parameter ID (`busA_*`/`busB_*`/`busC_*`/
  `busD_*`) is gone, replaced by the v2 vocabulary (`direct_*`, `crush_*`, `sand_*`,
  `spread_*`, `slap_*`). Acceptable pre-1.0 per the design brief; a v1 session's state loads
  without crashing (unknown IDs are silently ignored — no automatic value migration is
  attempted).
- App icon added to the plugin bundle build (`ICON_BIG`), a standing mandate for future
  versions.

### Added

- `docs/design-brief.md` replaced with the binding v2 spec; `docs/research-notes.md` added,
  documenting every sourced finding the v2 voicing is derived from (quotes + URLs) — the
  voicing throughout is **research-derived, not measured against hardware units**.
- 90+ Catch2 test cases covering all ten v2 guarantee categories: default-wire null (direct
  path, ≤ −120 dBFS), parallel impulse alignment, per-ratio Crush static curves including the
  ALL-mode plateau's non-monotonic slope and transient-lag overshoot, dual-rate release,
  Opto's dynamic-tilt/emphasis/memory behaviour, non-cancelling Passive EQ, Spread's
  FFT-measured pitch offsets, Slap's single-echo/darkness proof, fader/mute/audition logic,
  NaN/Inf recovery, oversized-block chunking, state round-trip (including a v1-session
  tolerant-import test), `reset()` coverage of every delay line and the opto memory,
  unlinked-vs-linked detection, and a new real-time-safety allocation-guard harness proving
  `processBlock()`/`MiserereEngine::process()` perform zero heap allocations once prepared.

## [0.1.0] — 2026-07-15

### Added

- Project bootstrap: README, license, contributing guide, architecture and build docs, ADRs, and CI workflow.
- M1 DSP core: the complete four-bus parallel vocal chain — Bus A "Direct" (HPF, console-style 3-band EQ, FET compressor with 4:1/8:1 ratios and GR metering, split-band de-esser, level-compensated tape-style saturation), Bus B "Opto" (boost-only passive-style EQ, opto leveler with program-dependent two-stage release, post-leveler air shelf), Bus C "Smash" (all-buttons FET limiter: ~20:1, mid-forward sidechain tilt, program-dependent release shortening, drive), Bus D "Slap" (60–180 ms fractional delay with filtered, tape-soft-saturated feedback loop and mono switch) — with per-bus fader/mute/exclusive-solo and global in/out trims plus bypass.
- Sample-aligned parallel summing on busses A–C (minimum-phase IIR, zero lookahead) with zero reported latency; Bus D is a delay by design (ADR 0003).
- APVTS parameter layout with frozen IDs, full state save/recall, and a functional v0.1 slider/combo/toggle editor.
- 65 Catch2 test cases covering all ten M1 guarantee categories: neutral-path null (≤ −120 dBFS), parallel impulse alignment, FET static curves, opto two-stage release, de-esser band selectivity, smash stability, slap timing/feedback stability, NaN/Inf recovery, oversized-block chunking, and state/latency/mute-solo contracts.
