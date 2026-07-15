# Miserere — M1 Design Brief (binding)

A parallel vocal chain in one unit — the classic "rough vocal template" workflow
(popularized by mixers like Andrew Scheps) packaged as a single plugin: a direct chain
plus three parallel busses, each with its own fader. Existing channel strips are serial;
Miserere's identity is TRUE PARALLEL routing. This document is the binding M1 spec;
deviations need an ADR. **No brand names in code, params, docs or UI** — generic module
descriptors only.

## Signal topology

```
                 ┌─ BUS A "Direct":  HPF → Console EQ → FET Comp → De-Esser → Tape Sat ─ fader ─┐
in ─ [In Trim] ─┼─ BUS B "Opto":    Passive EQ (low/high boost) → Opto Leveler → Passive Air ─ fader ─┼─ Σ ─ [Out Trim] ─ out
                 ├─ BUS C "Smash":   FET Limiter (all-buttons mode, mid-forward sidechain) ─ fader ─┤
                 └─ BUS D "Slap":    Slap Delay (60–180 ms, filtered, tape-soft feedback) ─ fader ─┘
```

- Busses A–C are **sample-aligned** (minimum-phase IIR only, no lookahead) so parallel
  summing never combs. Bus D is a delay by design.
- Every bus has `busX_level` (−inf…+6 dB fader), `busX_mute`; solo is exclusive-OR
  (`busX_solo`).

## Modules (all generic, no trademark references)

**Bus A — Direct chain**
- HPF: 20–300 Hz, 12 dB/oct.
- Console EQ ("British console" character): LowShelf 100 Hz ±15 dB, Peak 250 Hz–5 kHz
  (Q 0.7–2) ±15 dB, HighShelf 8 kHz ±15 dB.
- FET Comp: ratio {4:1, 8:1}, attack 0.1–10 ms, release 50–1100 ms, makeup, GR metering value exposed.
- De-Esser: split-band 4–9 kHz tunable, threshold, max 10 dB reduction.
- Tape Sat: drive 0–24 dB into tanh with pre-emphasis/de-emphasis pair, auto-compensated.

**Bus B — Opto sandwich**
- Passive EQ in: Low Boost (60/100 Hz sel, 0–10 dB), High Boost (8/10/12/16 kHz sel, 0–10 dB) —
  broad, gentle passive-style curves.
- Opto Leveler: program-dependent two-stage release (~60 ms fast stage into ~600 ms slow
  stage, release slows with sustained GR), soft ratio (~3:1 effective), attack ~10 ms fixed,
  peak reduction 0–100 %, makeup.
- Passive Air out: HighShelf 12 kHz 0–8 dB.

**Bus C — FET smash**
- FET Limiter in all-buttons character: ratio ~20:1, attack 0.05–0.8 ms, release 50–200 ms
  with program-dependent shortening, sidechain tilt +6 dB @ 2 kHz (mid-forward),
  drive 0–12 dB, output trim. Aggressive by design.

**Bus D — Slap**
- Delay 60–180 ms (default 110 ms), feedback 0–30 %, bandpass in the loop
  (HP 200 Hz / LP 5 kHz defaults, tunable), soft tape saturation in the loop, mono switch.

## Global
`in_trim` ±12 dB, `out_trim` ±12 dB, `bypass`. Latency 0. All choice params
(`ratio`, `freq select`) are AudioParameterChoice — remember the ComboBoxAttachment
item-population gotcha in the editor.

## Guarantees & tests (Catch2, ≥35 cases)

1. **Null:** Bus A solo, all modules neutral (EQ flat, comp threshold at max, de-esser off,
   drive 0), fader unity → bit-transparent apart from trim (≤ −120 dBFS diff).
2. **Parallel alignment:** busses A/B/C fed an impulse, all modules neutral → summed output
   is a single aligned impulse (no pre/post echoes above −100 dBFS).
3. FET comp: GR within ±1.5 dB of expected static curve for steady sine (both ratios).
4. Opto: release measurably slows with longer GR history (two-stage behavior verified).
5. De-esser: 8 kHz sibilance band reduced ≥6 dB at heavy setting, 1 kHz untouched (±0.5 dB).
6. Smash bus: 20:1 static curve sanity, output finite at max drive.
7. Slap: first echo at the exact configured delay in samples; feedback decays (stability
   at max feedback for 10 s of noise → bounded output).
8. NaN/Inf sweep → finite output; state recovers after reset().
9. Oversized-block clamp (Release-safe, real guard not just jassert).
10. State round-trip; reset() clears all module state incl. delay line; latency == 0;
    mute/solo logic (solo is exclusive, mute wins over solo on the same bus).

## Explicitly out of scope for M1
Swappable module alternatives per slot (M2 — e.g. VCA option in the comp slots), micro-pitch
doubler, reverb, external sidechain, photoreal GUI (M3 — v0.1 ships the standard slider
editor like all siblings).
