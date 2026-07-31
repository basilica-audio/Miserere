# Miserere — user manual (v0.5.0)

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
- **Console EQ** — a British-console-class grid: HPF (18 dB/oct, 50/80/160/300 Hz), low shelf
  (±16 dB, 35/60/110/220 Hz), a fixed-Q mid bell (±18 dB, six stepped centre frequencies), a
  fixed 12 kHz high shelf (±16 dB), and a Drive control blending subtle 2nd/3rd-leaning
  transformer-style harmonics.

  Since v0.5.0 the Drive is a transformer model rather than an added harmonic term: the
  signal runs through a flux integrator into a biased saturator and back out through that
  integrator's exact inverse. Because magnetic flux scales as voltage over frequency, the
  third harmonic rises toward the bass on its own (measured +12 dB going from 100 Hz to
  50 Hz) instead of being weighted by hand. At 0 dB the Drive is a bit-exact bypass. The
  12 kHz shelf is also magnitude-matched since v0.5.0, so at 44.1 kHz it keeps its analog
  shape in the top octave instead of being squeezed toward Nyquist.
- **Sat** — the tape-style saturator retained from v1, an optional "grit" stage. Since v0.5.0
  it computes its distortion in the alias-suppressing form described under *Latency and
  aliasing* below; at 0 dB drive it is still a bit-exact bypass.
- **De-Ess Post** — a second de-esser instance at the end of the chain, for sibilance that
  compression or EQ brought up.

## The four return busses

### ① CRUSH — FET limiter, all-buttons character

No threshold knob: **Input** drives the signal into a fixed per-ratio threshold and knee.
**Ratio** selects 4:1/8:1/12:1/20:1/ALL (ALL is a plateau-shaped curve with a deliberate
give-back and a short attack lag that lets transients punch through before clamping — the
"snap"). **Attack**/**Release** are 1–7 dials where a HIGHER number is FASTER, matching the
hardware convention this is modelled on; release is program-dependent (fast after brief
transients, several times slower after sustained heavy compression). **Style**
switches between All-Buttons and a softer, fixed 2:1 **Gentle** voicing. This bus is meant to
sound "terrible" soloed (use Audition) and good blended in.

CRUSH also carries a touch of program-dependent colour: as gain reduction builds, a
transformer-style low-frequency saturation and the FET cell's own residual second harmonic
blend in on top of the limiter's detector-ripple character — negligible at light settings and
growing only as the bus works harder. A clean, barely-compressed signal is unaffected; lean on
Input and Audition to hear it come alive.

Since v0.5.0 none of the above is tabulated. The detector is a real feedback loop: the
sidechain is driven from the bus output through a single-capacitor RC network with Attack in
the charge path and Release in the discharge path, which is why the behaviours this style of
limiter is known for now simply happen rather than being scripted — the effective ratio rises
as a note is held, the Release setting audibly changes how fast the attack arrives, the knee
tightens as you go up the ratio row, and ALL overshoots before settling. ALL is its own
setting, not an interpolation between the numbered ratios.

### ② SANDWICH — Passive EQ → Opto Leveler → Passive EQ

Two independent Passive EQ instances bracket an opto-style leveler. Each Passive EQ offers a
shared-frequency LF **Boost** and **Cut** (both can run at once — a deliberately
non-cancelling curve, not a simple sum to flat), an HF **Bell Boost** with variable
bandwidth, and an HF **Shelf Atten**. The Opto Leveler has no threshold: **Peak Reduction**
drives the cell harder, with **Limit** tightening the compression toward limiting.
**Emphasis** makes the detector progressively HF-selective (up to −10 dB less LF
sensitivity), so at high settings it reacts mostly to sibilance/presence, "like a
multiband". **Residual** (default on) keeps the Passive EQ's small, never-fully-flat vintage
tilt; defeat it for a cleaner EQ.

Since v0.5.0 the leveler is a photocell model rather than a set of drawn curves. The
compression ratio, the two-stage release (a quick initial recovery followed by a long tail),
the memory effect — hold it down longer or harder and it lets go more slowly — and the
program-dependent attack are all consequences of how charge carriers in the cell build up and
drain away. There is no ratio control because there is no ratio parameter in the circuit;
what you hear is the cell's own behaviour, which is the whole appeal of this style of leveler.

The LF Boost and Cut network is likewise now the hardware ladder's exact response. Two
practical consequences: running Boost and Cut together gives the classic low-end shape (a lift
underneath with a dip just above it) because the cut corner genuinely sits above the boost
corner, and the Cut on its own is broad — at full attenuation on the 100 Hz setting it is
still about 1.6 dB down at 2 kHz. That breadth is the circuit's, not a bug; it is why the
control is normally used against the Boost rather than alone.

### ③ SPREAD — dual micro-pitch

Two short delay taps (~30 ms pitched up, ~50 ms pitched down), hard-panned L/R. **Detune**
sets the pitch offset in cents (default 6 — deliberately small, so the ear reads "pushed to
the outside" rather than chorusing). **Time** scales both base delays together; **Width**
blends from a fully centred sum (0%) to the full hard pan (100%).

Since v0.5.0 both shifter delay lines are read with third-order Lagrange interpolation instead
of a linear read, which recovers 0.90 dB of high-frequency content at 10 kHz that the linear
read was losing; the grain crossfade is longer and equal-power, which measurably lowers the
periodic level ripple on sustained tones; and Detune and Time are smoothed per sample, so
automating either no longer steps at block boundaries.

### ④ SLAP — single-repeat dark delay

**Time** (50–160 ms, default 110 ms, plain milliseconds — deliberately not tempo-synced).
Feedback is fixed at 0 in v2: there is exactly one repeat, and its darkness comes from a
built-in tape-style voicing (**Tone** sweeps a progressive HF loss plus soft saturation baked
into that single repeat) rather than a filtered feedback loop. **Stereo** switches from the
default mono return (the classic mono slap behind a stereo-widened vocal) to independent L/R
delays.

Since v0.5.0 the repeat is voiced as an actual tape transport rather than a filtered delay:
the saturation now sits on the record side (ahead of the delay write) instead of being lumped
onto the tap, which is why repeats and input no longer brighten together, and a fixed head
bump adds a small low-mid lift.

**Wobble** (default 0%) is the transport's wow and flutter — a slow pinch-roller waver, a
faster capstan flutter, and a slow random drift, each wandering independently so it never
settles into an obviously repeating pattern. The dial spans roughly 0 to 0.5% wow-and-flutter;
small amounts (10–25%) read as "this was on tape" without sounding broken, and high settings
get seasick on purpose. At 0 the modulation is genuinely switched off, not merely turned down.

**Age** (default 0%) is tape wear: hiss with an asperity component that rides the signal (the
noise breathing with the vocal is most of what makes it read as tape rather than as added
hiss), plus extra head-to-tape spacing loss that dulls the repeat further as the dial rises.
It affects only the SLAP return, never the direct path. At 0 nothing is generated at all.

## Fader logic

- Every return bus has **Level** (−60…+6 dB; the bottom is a true off), **Mute**, and
  **Audition**.
- **Audition is exclusive** (engaging one releases the others) and isolates exactly what it
  names — the direct path and the other busses are excluded while a bus is auditioned. It is
  deliberately not called "Solo": the technique's whole point is that these busses should
  never be *judged* in isolation, only used to double-check what they are doing.
- **Mute wins over Audition** on the same bus, console-style.
- **Mute and Audition do not click.** Since v0.5.0 the bus routing rides a 3 ms ramp instead
  of switching hard between on and off. The ramp lands on exact values, so a muted bus still
  contributes digital silence — exact zeros, not "very quiet" — once it has settled.
- **Link** (default off) makes the Crush and Sandwich detectors track a combined L/R signal
  instead of each channel independently — "dual mono" (unlinked) is the documented default
  behaviour for this style of processing.
- **Parallel** is a macro trim (−24…+6 dB) that offsets all four return faders together — the
  "VCA ride back" gesture for quickly backing off the whole parallel layer.

## Presets

A preset bar sits at the top of the editor: `[<] [PresetName*] [>] [Save] [Save As...]
[Delete] [Import...] [Export...]`. Clicking the preset name opens a Factory/User menu; a
trailing `*` means the current preset has unsaved changes. Twelve factory presets ship in the
box (see `presets.md` for what each one is for) — including **Tape Slap 7.5** and **Worn
Slap**, added in v0.5.0 to exercise Wobble and Age; user presets save to
`~/Library/Audio/Presets/Yves Vogl/Miserere/` on macOS (`%APPDATA%/Yves Vogl/Miserere/Presets/`
on Windows). The preset menu's "Set current as default" makes any preset — factory or user —
load automatically on every fresh instance; "Import..." accepts both single preset files and
zip preset banks.

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

## Latency and aliasing (v0.5.0)

Miserere reports and adds **0 samples of latency**, at every sample rate and with every
section engaged. That is a deliberate constraint, not an oversight: the four return busses are
summed against a bit-transparent direct path, so anything that delayed one bus by even a
fraction of a sample would comb the sum.

Keeping that promise rules out oversampling, which is how most plugins tame the aliasing that
saturation produces. Instead every drive stage here computes its distortion in a form that
suppresses aliasing arithmetically, split so that the clean part of the signal passes through
exactly aligned and untouched — no delay, and no high-frequency dulling at 44.1 kHz.

What that buys, measured at 44.1 kHz: on a programme-realistic probe (a 3 kHz tone at
−12 dBFS through hot-but-musical drive settings) non-harmonic content sits at or below
−60 dBFS. Pushed to deliberately unrealistic extremes — a full-scale 12 kHz tone into maximum
drive — the treatment still removes at least 12 dB of aliasing versus the untreated curve, but
no absolute floor is claimed there, and you should not expect one from any zero-latency
design. If you want a saturated 12 kHz sine at full scale to stay clean, that is what
oversampling is for, and it costs latency.

## Known limitations

- SPREAD's pitch shifter crossfades two taps of one delay line held a fixed distance apart, so
  a sustained pure tone (a synth or a very steady held vowel) meets a mild comb whose depth
  depends on the note. On real programme material this is inaudible; a smarter splice is on
  the roadmap.
- The GUI is a functional slider/knob editor (custom vector GUI with per-bus needle meters is
  milestone M3); the preset bar is a plain functional strip, not yet restyled.
- Out of scope for v2, tracked as M2+/M3 issues: a short plate reverb module, a "BV mode"
  preset, swappable compressor colours beyond the two CRUSH styles, external sidechain, an
  output limiter.
- Dynamics detection is unlinked (independent L/R) by default on Crush and Sandwich; Link
  makes both channels track a shared detector.
- The voicing throughout this plugin is **research-derived, not measured against hardware
  units** — see `research-notes.md` for the sourced findings and their limitations.
