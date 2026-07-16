# Miserere — Design Brief v2 (binding; supersedes v1 entirely)

The parallel vocal template — the documented 2010–2023-era workflow popularized in
interviews by mixers such as Andrew Scheps — packaged as one phase-coherent unit.
Research-driven rewrite: every default below is sourced (see docs/research-notes.md).
**No brand or person names in parameters, UI or marketing copy** — generic descriptors
only ("FET Crush", "Opto Sandwich"); the manual may cite public interviews as sources.

## Why v1 was wrong (the two core corrections)

1. **The dry path is sacred.** v1 processed the "direct bus" by default. The technique's
   entire point is the opposite: the dry vocal passes **bit-transparent at unity** so its
   natural envelope/emotion survives; everything else is ADDED underneath ("you'd probably
   think the vocal is bone dry"). Default state = wire.
2. **Parallel chains are copies of the post-insert signal, blended by RETURN faders** —
   post-fader unity sends, no wet/dry crossfades that attenuate dry. Parallel compressors
   are slammed far harder than any insert would be; the blend stays low and is judged only
   in context, never in solo.

## Topology (fixed)

```
in → [In Trim] → DIRECT PATH (serial; every section optional, ALL OFF by default:
                   De-Esser (pre) → FET Comp light → Console EQ → Sat → De-Esser (post))
        │ = "the channel". Output feeds the sum at unity AND all four sends (unity taps):
        ├─→ ① CRUSH    : FET limiter, all-buttons character        → return fader
        ├─→ ② SANDWICH : Passive EQ → Opto Leveler → Passive EQ    → return fader
        ├─→ ③ SPREAD   : dual micro-pitch (≈30/50 ms, ±cents, L/R) → return fader
        └─→ ④ SLAP     : ≈110 ms dark single-repeat delay          → return fader
   Σ (direct + returns) → [PARALLEL macro trim scales returns ①–④] → [Out Trim]
```

- Busses ①/② are minimum-phase, zero added latency, sample-aligned with the direct path
  (parallel summing must never comb). ③/④ are delays by design.
- Per bus: return fader (−60…+6 dB, true-zero at bottom; defaults: ① −9 dB, ② −12 dB,
  ③ −18 dB, ④ −15 dB), Mute, Audition (exclusive solo — labeled *Audition*, because the
  technique forbids judging solo sound). Master: **Parallel** trim (−24…+6 dB offset on
  all four returns simultaneously — the "VCA ride back" gesture), total-GR meter value
  (sum of ①+② GR) exposed for the editor, In/Out trim, global bypass.
- Stereo detection default: **UNLINKED L/R** ("dual mono is key" — direct quote), with a
  global Link switch.

## Module specifications (authentic behaviors, generically named)

### ① CRUSH — FET limiter, all-buttons character
- **Input-drive paradigm:** no threshold knob. `crush_input` (0–48 dB drive into fixed
  internal threshold), `crush_output` (makeup). Per-ratio threshold/knee table: threshold
  rises and knee hardens as ratio increases (≈−30 dB @4:1 … −24 dB @20:1, ref-calibrated
  so −18 dBFS ≙ +4 dBu nominal).
- `crush_ratio`: {4:1, 8:1, 12:1, 20:1, ALL} — ALL = plateau-shaped static curve between
  12:1 and 20:1 with a slight give-back above the kink AND a transient lag that lets the
  initial attack overshoot through before clamping (the signature "snap").
- Ballistics, **inverted taper** (higher = faster, like the hardware): `crush_attack`
  1–7 → 800→20 µs; `crush_release` 1–7 → 1100→50 ms (63%-recovery one-pole definition).
  **Dual-rate program-dependent release:** fast rate after brief transients, slow rate
  after sustained high-RMS compression, with a compression-duration integrator governing
  the transition. Defaults: attack 1 (slowest), release 6 (fast-but-not-fastest).
- Color: detector ripple falls out of correct sample-rate gain computation (keep it);
  add only mild class-A-style asymmetric harmonics + transformer LF saturation,
  level-dependent, <0.5 % THD at moderate GR.
- Style switch `crush_style`: **All-Buttons** (default) / **Gentle** (2:1, the later-era
  rear-bus flavor: slow attack, release ≈6, 2–5 dB GR target).
- Comfortably reaches 20+ dB GR without internal clipping; makeup keeps blend calibrated.

### ② SANDWICH — Passive EQ → Opto Leveler → Passive EQ (the dynamic-tilt trick)
- **Pre-EQ defaults:** wide shelf **CUT −3.8 @ 100 Hz** (bandwidth widest — reaches into
  low-mids) + **bell BOOST +3.6 @ 8 kHz**: starve the opto of lows, make it work the top.
- **Opto Leveler:** detector driven by the raw audio (NO rectifier smoothing — all attack
  ballistics live in the photocell model, ≈10 ms effective). **Two-stage release with
  memory:** ≈60 ms to 50 % recovery, then 0.5–5 s tail whose length scales with the amount
  and duration of previous GR (implement as ≥3 parallel one-pole paths per the measured
  LDR model). Static curve = lookup, NOT fixed ratio: breakaway at −30 dB, ≈3:1 soft-knee
  region over ~10 dB, then hard-limit ceiling (<1 dB output rise for +20 dB input);
  `sand_limit` switch morphs toward ≈10:1+. `sand_emphasis` (0–100 %, default 100): first-
  order low-shelf CUT of up to −10 dB below ≈1 kHz **in the detector only** → HF-selective
  compression ("like a multiband"). `sand_peakred` default targets ≈3 dB GR on vocal peaks.
  GR element itself is clean; a gentle fixed tube/transformer stage after the attenuator
  (low-order, level-dependent, subtle).
- **Post-EQ defaults:** wide shelf **BOOST +2.8 @ 100 Hz** + shelf **CUT −1.8 @ 10 kHz** —
  mirror curve: static tone restored, >1 kHz band dynamically controlled.
- Passive EQ authenticity (both instances share the module): LF selector 20/30/60/100 Hz
  with **simultaneous boost AND atten that must NOT cancel** (boost corner slightly lower,
  cut steeper/slightly higher → resonant shelf + low-mid dip); HF bell boost selector
  3/4/5/8/10/12/16 kHz with continuous bandwidth; HF shelf atten 5/10/20 kHz. Dials 0–10
  with nonlinear taper; hardware-style asymmetric calibration (LF boost max ≈13.5 dB,
  cut ≈17.5 dB). Include the never-flat residual (±0.3–0.4 dB tilt varying with the LF
  selector) as a defeatable "vintage residual" (`sand_residual`, default ON). Makeup stage
  color: subtle, 3rd-leaning (push-pull), well below the opto color.

### ③ SPREAD — dual micro-pitch
- Two delay taps ≈30 ms (pitched up) and ≈50 ms (pitched down), hard-panned L/R.
  `spread_detune` ±0–15 cents (default 6), `spread_time` scale 0.5–2×, `spread_width`.
  Micro-pitch via modulated-delay (glide/crossfade) — the base delay is part of the
  effect. Calibration: at default return level you must NOT hear chorusing; it pushes the
  vocal "to the outside just a little".

### ④ SLAP
- `slap_time` 50–160 ms, **default 110 ms**, plain milliseconds (deliberately NOT tempo-
  synced). **Single repeat: feedback fixed 0 in v2** (drop v1's feedback param). Mono
  return by default (`slap_stereo` off). Dark bucket-brigade-style voicing: gentle
  progressive HF loss (≈3–5 kHz lowpass character) + soft saturation baked into the
  repeat — `slap_tone` (dark…darker). Level calibrated to "you only notice it when muted".

### DIRECT PATH sections (all default OFF → bit-transparent wire)
- De-Esser pre (the "de-ess where dynamics are greatest" position) and De-Esser post —
  reuse v1 DeEsser, two instances, split-band 4–9 kHz.
- FET Comp light: same FET module in insert voicing — default 4:1, slow-ish attack, fast
  release, 3–4 dB peak GR target (the one place serial compression is authentic).
- Console EQ: 1073-class grid — HPF 18 dB/oct @ {50,80,160,300} Hz; low shelf ±16 dB @
  {35,60,110,220} Hz; mid bell ±18 dB fixed-Q @ {360,700,1.6k,3.2k,4.8k,7.2k} Hz; high
  shelf ±16 dB @ 12 kHz (shallow first-order-style shelves, 3-pole HPF). Clean at nominal
  level; `deq_drive` blends near-equal 2nd+3rd (3rd-leaning) transformer-style harmonics.
- Sat: v1 TapeSat retained as optional "grit" section (default off).

## Guarantees & tests (Catch2; keep all still-valid v1 cases, adapt the rest; ≥45 total)

1. **Default = wire:** fresh instance, all defaults → bit-transparent (≤ −120 dBFS diff)
   apart from trims. THE core invariant.
2. Parallel alignment: impulse through ①/② at neutral settings sums to a single aligned
   impulse with the dry path (no pre/post echoes above −100 dBFS).
3. CRUSH: per-ratio static curves within tolerance incl. the ALL plateau (measured curve
   must be non-monotonic in slope: high ratio just above knee, slight give-back above);
   transient-lag overshoot measurable on a step; dual-rate release: release time after a
   50 ms burst ≪ release time after 5 s of sustained GR.
4. SANDWICH: with defaults, a 100 Hz tone passes with ≈0 dB net static change while an
   8 kHz tone above threshold is dynamically reduced (dynamic-tilt proof); opto two-stage
   release: 50 % recovery ≈60 ms, full recovery time grows with GR history (memory test);
   emphasis: equal-GR for LF vs HF at 0 %, ≈10 dB more HF GR at 100 %.
5. Passive EQ: simultaneous LF boost+cut produces the documented non-cancelling curve
   (bump below corner + dip in low-mids — assert both signs at reference frequencies).
6. SPREAD: measurable ±cents pitch offset on L/R (FFT of a sine), base delays ≈30/50 ms.
7. SLAP: first echo exactly at slap_time; NO second echo above −80 dBFS (feedback 0);
   repeat is measurably darker than input (spectral centroid).
8. Returns/master: fader true-zero at bottom; Parallel trim offsets all four returns;
   mute/audition logic (audition exclusive, mute wins).
9. NaN/Inf recovery, oversized-block chunking (Release-safe), state round-trip incl.
   v1→v2 state tolerance (unknown old IDs ignored, no crash), reset() clears everything
   incl. delay lines and opto memory, latency == 0.
10. Unlinked-vs-linked detection: hard-panned L-only burst causes GR on L only when
    unlinked, both when linked.

## Honesty & framing

- docs/research-notes.md ships the sourced findings (quotes + URLs) — the voicing is
  research-derived, not measured against hardware units; say so.
- Manual notes the technique's era (2010–2023 template workflow, as documented publicly)
  without implying endorsement by any person or brand.
- Out of scope for v2 (explicitly): short plate reverb module, BV mode preset, swappable
  compressor colors beyond the two crush styles, external sidechain, output limiter.
  These are M2+/M3 candidates, tracked as issues.

## Versioning

Ships as **v0.2.0** (breaking parameter changes are fine pre-1.0; state migration =
tolerant import). CHANGELOG documents the topology change prominently.
