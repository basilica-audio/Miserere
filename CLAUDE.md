# Miserere — parallel vocal chain (vocals)

Per-repo working memory for Claude Code sessions on this plugin. Part of the **Basilica Audio** plugin suite — sacred-architecture DSP for heavy music (`github.com/basilica-audio`).

## What this is
Miserere is the parallel vocal template in a single unit — a bit-transparent-by-default direct path plus four parallel return busses (Crush, Sandwich, Spread, Slap), each with its own return fader, Mute and exclusive Audition, inspired by the documented 2010–2023-era parallel mixing workflow popularized in public interviews by mixers such as Andrew Scheps. Nothing on the market packages this true-parallel-bus topology as one plugin: existing channel strips (e.g. Waves Scheps Omni Channel) are serial with per-module wet/dry. AU / VST3 / Standalone, JUCE 8.

## Status (v0.2.0 — v2 topology rewrite)
v2 replaced v1's topology entirely: v1 processed the "direct bus" by default and treated it as one of four equal faded busses; v2 makes the direct path a bit-transparent "channel" that always sums at unity and feeds all four return busses (unity taps), with every optional direct-path section off by default. See `docs/design-brief.md` for the binding v2 specification (topology, parameters, tests) and `docs/research-notes.md` for the sourced findings the voicing is derived from.

## Design principles (Yves, binding)
- **No brand names anywhere** — modules use generic hardware descriptors only: "Passive Tube EQ", "Opto Leveler", "FET Limiter", "British Console EQ". Visual design may quote the originals' material language (M3) but never copies a faceplate 1:1 (trade-dress risk).
- **Readability of control state**: high-contrast pointer knobs, engraved scale rings, needle meters per bus (M3). v0.1 ships the functional slider editor.

## Build & test
```sh
export CPM_SOURCE_CACHE="$HOME/.cache/CPM"      # shared JUCE 8.0.14 + Catch2 cache
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target Tests Miserere_Standalone --parallel 4
ctest --test-dir build --output-on-failure
```
Release/universal + pluginval + auval run in CI, not locally.

## Conventions & guardrails
- JUCE 8.0.14 via CPM · C++20 · AGPLv3 · Pamplejuce `SharedCode` pattern · manufacturer `Yvsv`, plugin code `Msrr`, `com.yvesvogl.miserere`.
- **Real-time safety:** no alloc/lock/file-IO/logging on the audio thread; allocate in `prepareToPlay`; `reset()` clears ALL state (suite review finding: missing resets ship real bugs); `ScopedNoDenormals`; smoothed params.
- **Filter coefficients on the audio thread:** use `juce::dsp::IIR::ArrayCoefficients` (stack, no heap) — `Coefficients::make*` allocates and is a known suite-wide review finding.
- **Guard oversized blocks in Release builds** (real clamp, not only `jassert` — asserts compile out).
- **Parallel-bus phase discipline:** busses ① Crush and ② Sandwich must stay sample-aligned with the Direct path (minimum-phase IIR only, no lookahead) so parallel summing never combs; busses ③ Spread and ④ Slap are delays by design and exempt (see ADR 0003).
- **DryWetMixer gotcha (JUCE 8.0.14):** prime `setWetMixProportion(mix)` before `reset()` in `prepare()`.
- **ComboBox parameters (JUCE 8.0.14):** `ComboBoxAttachment` does NOT populate items — call `addItemList(param->getAllValueStrings(), 1)` BEFORE attaching (shipped as an apotheosis v0.1.0 bug).
- **`main` is protected** — no direct commits; feature branch + PR, green CI required (Conventional Commits). New DSP needs tests (null/reference, NaN/Inf sweep, state round-trip, latency).

## Roadmap
GitHub milestones (M1 DSP & tests · M2 presets/state — incl. swappable classic-style modules per slot · M3 GUI & a11y · M4 release/signing/v1.0.0) + issues. Read with `gh issue list --repo basilica-audio/miserere`.

## Suite context
Style references: siblings `basilica-audio/Seraph` (vocal processing, de-esser), `basilica-audio/Aureate` (saturation), `basilica-audio/Triptych` (compressors). The suite: overture, tenebrae, nave, silentium, requiem, seraph, aureate, firmament, triptych, apotheosis, crypta, lancet, miserere.
