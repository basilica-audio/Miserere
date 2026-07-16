# Miserere — user manual (v0.2.0)

*Four voices, one prayer — the parallel vocal template in a single unit.*

## What Miserere is

Miserere packages the documented **2010–2023-era parallel vocal template** — the "rough
vocal" workflow popularized in public interviews by mixers such as Andrew Scheps — into one
plugin: a **Direct** path plus four **parallel return busses** (CRUSH, SANDWICH, SPREAD,
SLAP), each with its own return fader, Mute and Audition. This is a documented, publicly
sourced technique from that era (see `research-notes.md`), not an endorsement by or
association with any named person or brand.

**The core idea, and v2's correction over v0.1.0**: the Direct path is a wire. Out of the
box, every optional section on it is OFF, so the dry vocal passes through essentially
untouched — its natural envelope and phrasing survive. Everything else is layered
*underneath* it via the four return busses, which are copies of the direct-path output at
unity, processed hard, and blended back in at a modest level. "Even with all that stuff in
the mix, you'd probably think the vocal is bone dry" is the calibration target.

## Signal flow

```
in → [In Trim] → DIRECT PATH (serial; every section optional, ALL OFF by default:
                   De-Esser (pre) → FET Comp light → Console EQ → Sat → De-Esser (post))
        │ = "the channel". Output feeds the sum at unity AND all four sends (unity taps):
        ├─→ ① CRUSH    : FET limiter, all-buttons character        → return fader
        ├─→ ② SANDWICH : Passive EQ → Opto Leveler → Passive EQ    → return fader
        ├─→ ③ SPREAD   : dual micro-pitch (≈30/50 ms, ±cents, L/R) → return fader
        └─→ ④ SLAP     : ≈110 ms dark single-repeat delay          → return fader
   Σ (direct + returns) → [Parallel macro trim scales returns ①–④] → [Out Trim] → out
```

Busses ①/② are minimum-phase and add zero latency, so they stay sample-aligned with the
direct path — parallel summing never combs regardless of settings. Busses ③/④ are delays by
design (see `architecture.md`). See `research-notes.md` for the sourced findings behind every
default below.

## The Direct path

Off by default, section by section, in signal order:

- **De-Ess Pre** — split-band de-esser, 4–9 kHz tunable, up to 10 dB reduction, placed where
  the vocal's dynamics are greatest (the documented "de-ess at the very beginning" rule).
- **FET Comp** — a light, threshold-based FET-style compressor, fixed 4:1, aiming for a
  gentle 3–4 dB of peak gain reduction — "the one place serial compression is authentic" in
  this topology.
- **Console EQ** — a 1073-class grid: HPF (18 dB/oct, 50/80/160/300 Hz), low shelf (±16 dB,
  35/60/110/220 Hz), a fixed-Q mid bell (±18 dB, six stepped centre frequencies), a fixed
  12 kHz high shelf (±16 dB), and a Drive control blending subtle 2nd/3rd-leaning
  transformer-style harmonics.
- **Sat** — the tape-style saturator retained from v1, an optional "grit" stage.
- **De-Ess Post** — a second de-esser instance at the end of the chain, for sibilance that
  compression or EQ brought up.

## The four return busses

### ① CRUSH — FET limiter, all-buttons character

No threshold knob: **Input** drives the signal into a fixed per-ratio threshold/knee table.
**Ratio** selects 4:1/8:1/12:1/20:1/ALL (ALL is a plateau-shaped curve with a deliberate
give-back and a short attack lag that lets transients punch through before clamping — the
"snap"). **Attack**/**Release** are 1–7 dials where a HIGHER number is FASTER, matching the
hardware convention this is modelled on; release is dual-rate and program-dependent (fast
after brief transients, several times slower after sustained heavy compression). **Style**
switches between All-Buttons and a softer, fixed 2:1 **Gentle** voicing. This bus is meant to
sound "terrible" soloed (use Audition) and good blended in.

### ② SANDWICH — Passive EQ → Opto Leveler → Passive EQ

Two independent Passive EQ instances bracket an opto-style leveler. Each Passive EQ offers a
shared-frequency LF **Boost** and **Cut** (both can run at once — a deliberately
non-cancelling curve, not a simple sum to flat), an HF **Bell Boost** with variable
bandwidth, and an HF **Shelf Atten**. The Opto Leveler has no threshold: **Peak Reduction**
drives into a fixed static curve (soft ~3:1 below −20 dB, hard ceiling above; **Limit**
tightens the soft region toward ~10:1), with a raw-audio detector (no smoothing ahead of the
ballistics) and a two-stage release whose tail lengthens the longer it has been working.
**Emphasis** makes the detector progressively HF-selective (up to −10 dB less LF
sensitivity), so at high settings it reacts mostly to sibilance/presence, "like a
multiband". **Residual** (default on) keeps the Passive EQ's small, never-fully-flat vintage
tilt; defeat it for a cleaner EQ.

### ③ SPREAD — dual micro-pitch

Two short delay taps (~30 ms pitched up, ~50 ms pitched down), hard-panned L/R. **Detune**
sets the pitch offset in cents (default 6 — deliberately small, so the ear reads "pushed to
the outside" rather than chorusing). **Time** scales both base delays together; **Width**
blends from a fully centred sum (0%) to the full hard pan (100%).

### ④ SLAP — single-repeat dark delay

**Time** (50–160 ms, default 110 ms, plain milliseconds — deliberately not tempo-synced).
Feedback is fixed at 0 in v2: there is exactly one repeat, and its darkness comes from a
built-in bucket-brigade-style voicing (**Tone** sweeps a progressive HF loss plus soft
saturation baked into that single repeat) rather than a filtered feedback loop. **Stereo**
switches from the default mono return (the classic mono slap behind a stereo-widened vocal)
to independent L/R delays.

## Fader logic

- Every return bus has **Level** (−60…+6 dB; the bottom is a true off), **Mute**, and
  **Audition**.
- **Audition is exclusive** (engaging one releases the others) and isolates exactly what it
  names — the direct path and the other busses are excluded while a bus is auditioned. It is
  deliberately not called "Solo": the technique's whole point is that these busses should
  never be *judged* in isolation, only used to double-check what they are doing.
- **Mute wins over Audition** on the same bus, console-style.
- **Link** (default off) makes the Crush and Sandwich detectors track a combined L/R signal
  instead of each channel independently — "dual mono" (unlinked) is the documented default
  behaviour for this style of processing.
- **Parallel** is a macro trim (−24…+6 dB) that offsets all four return faders together — the
  "VCA ride back" gesture for quickly backing off the whole parallel layer.

## Starter recipe

1. Leave the Direct path off, or add De-Ess Pre / a touch of Console EQ if the source needs
   it. Keep FET Comp and Sat off unless the vocal specifically needs light insert
   compression.
2. CRUSH starts at −9 dB by default with the ALL-Buttons character already engaged — bring
   Input up until Audition shows heavy, "disaster in solo" compression, then trust the
   default fader level and adjust by ear from there.
3. SANDWICH starts at −12 dB; raise Peak Reduction until the vocal thickens without
   audibly pumping in context.
4. SPREAD and SLAP (−18 dB / −15 dB by default) should each pass the "you only notice it's
   gone when you mute it" test — if either is audible as a discrete effect, pull it back.
5. Use **Parallel** to back the whole layer off quickly on quieter/more organic material.

## Known limitations (v0.2.0)

- The GUI is a functional slider/knob editor (custom vector GUI with per-bus needle meters is
  milestone M3).
- No factory presets yet (M2).
- Out of scope for v2, tracked as M2+/M3 issues: a short plate reverb module, a "BV mode"
  preset, swappable compressor colours beyond the two CRUSH styles, external sidechain, an
  output limiter.
- Dynamics detection is unlinked (independent L/R) by default on Crush and Sandwich; Link
  makes both channels track a shared detector.
- The voicing throughout this plugin is **research-derived, not measured against hardware
  units** — see `research-notes.md` for the sourced findings and their limitations.
