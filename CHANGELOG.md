# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
