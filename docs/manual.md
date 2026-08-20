# Miserere — user manual (v0.7.0)

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
   Σ (direct + returns) → [Parallel macro trim scales returns ①–④] → [Out Trim]
                        → [Output Limiter — off by default] → out
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
  this topology. Since v0.6.0 a **Character** switch selects the insert-compressor family
  (generic descriptors, as everywhere in this plugin): **FET** (the default — hard knee,
  panel timing, clean; exactly the previous behaviour), **VCA** (clean bus-style voicing
  with a 6 dB soft knee and a snappier attack; below the knee it is bit-transparent), and
  **Tube Mu** (a wide 12 dB knee, slower attack, longer release, and a touch of
  second-harmonic warmth that only appears while the compressor is actually working —
  gain-reduction-gated, so unity passages stay clean).
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
transients, several times slower after sustained heavy compression). **Style** selects
the limiter's voicing: **All-Buttons** (default), a softer, fixed 2:1 **Gentle** voicing, or
— since v0.6.0 — **Vintage**, an early-revision hot-bias state that keeps the full ratio row
active but bites two dB earlier, runs the feedback loop hotter and more than doubles the FET
cell's residual second-harmonic "hair", for a rattier, more coloured crush at the same
settings. This bus is meant to sound "terrible" soloed (use Audition) and good blended in.

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

Since v0.6.0 a **Colour** switch selects the cell's era: **Classic** (the default — the
photocell calibration exactly as before), **Quick** (a later solid-state-era optical voicing:
faster recovery, much less release memory, an almost clean output stage) and **Deep** (an
earlier-era voicing: slower recovery, a longer memory tail, and a thicker tube/transformer
output stage). The switch changes the cell's charge-carrier kinetics, not its level
calibration — all three land in the same gain-reduction class at the same Peak Reduction
setting, and swapping colours mid-performance is click-free.

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

Since v0.6.0 the splice is period-adaptive: a pitch tracker (autocorrelation on a lowpassed,
decimated copy of the input — a few percent of the bus's CPU) watches the material, and when
it is confidently pitched, the spacing between each shifter's two crossfading taps snaps to a
multiple of the note's period while the crossfade law shifts to amplitude-complementary. Both
taps then reinforce at every harmonic instead of interfering, which removes the note-dependent
level ripple sustained tones used to meet (see Known limitations). Unpitched material leaves
the tracker below its confidence gate and passes through the identical v0.5.0 path. The
tracker only ever reads signal history — reported latency stays 0.

Also since v0.6.0 the crossfade geometry stays causal at short **Time** settings: below 100%
the tap spacing shrinks with the base delay (down to ~15 ms at 50%), which slightly quickens
the splice cadence. Previously a tap could sit against the delay line's lower bound for
stretches of a second or more and leak an unshifted copy of the input into the return.

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

## External sidechain (v0.7.0)

Miserere exposes an optional **Sidechain** input bus, **disabled by default** — enable it in
your host's plugin routing, then switch individual detectors over to it with the **Ext Key**
lamp in the Direct Path, Crush and Sandwich panels. There is no master switch: each detector
decides for itself, so you can key CRUSH from a snare while SANDWICH keeps listening to the
vocal. If the bus is absent or disabled, or the host sends no key, every switch falls back to
internal detection silently. A mono key feeding a stereo instance keys both channels.

**What keying actually does to CRUSH and SANDWICH — read this before assuming it is "the same
sound, different detector source".** Both of those busses are *feedback* designs, and that is
not an implementation detail, it is where their character comes from:

- **CRUSH** drives its rectifier from its own output, one sample back. The soft knee, the
  ratio creeping up as a note is held, the program-dependent release — all of it emerges
  from that loop.
- **SANDWICH**'s opto leveler drives its EL panel from its own compressed output, for the
  same reason: there is no static-curve lookup in the code at all, the curve *is* the loop.

An external key does not add a detector input to those loops. It **replaces the loop drive**,
which converts both modules into **feed-forward keyed compressors** for as long as Ext Key is
engaged. The photocell physics, the ballistics and the colour stages are unchanged, but the
static curve is not: a feedback detector sees the already-reduced signal and backs itself off,
while a feed-forward detector sees the full-scale key and does not. Measured on the same
signal at the same settings (a −22 dBFS tone, 0 dB Input), CRUSH produces about **3 dB** of
gain reduction with internal detection and about **21 dB** when keyed with a copy of its own
input. That is a legitimate and useful mode — it is simply a different compressor, and expecting
your internal-detection settings to carry over unchanged will surprise you.

**The Direct FET is the exception**: it has always been feed-forward (its envelope comes from
the pre-gain input), so keying it really is a pure detector-source swap — same topology, same
curve, different source.

The key is read only. It never reaches the audio path, it is not part of the sum, and it does
not affect the zero-latency guarantee.

## Output limiter (v0.7.0)

The last stage in the plugin, after Out Trim, with its own needle meter on the Global panel.
**Off by default**, and while it is off it is a bit-exact bypass — the default wire stays a
wire.

- **Limiter** engages it. **Ceiling** (−12…0 dB, default −0.3 dB) is the level no output
  sample may exceed. **Release** (5…500 ms, default 60 ms) is how quickly the gain recovers
  once the peak has passed.
- It is a **safety stage, not a colour device**. Attack is instantaneous (there is no
  lookahead anywhere in this plugin), so deep, sustained reduction on bass-heavy material
  will be audible as distortion before it is audible as level control. Use it to catch peaks,
  not to squash a mix.
- Detection is **always L/R-linked**, regardless of the global **Link** switch. Link chooses
  dual-mono vs. linked detection for CRUSH and SANDWICH, where dual mono is part of the
  sound; limiting each channel with its own gain would move the stereo image on every peak,
  so the limiter does not offer that.
- Once the signal is clear of the ceiling's soft knee (3 dB wide, centred on the ceiling) the
  gain returns to exactly unity and the stage is bit-transparent again.

**This is a sample-peak ceiling, not a true-peak ceiling — and it deliberately cannot be
one.** True-peak limiting means detecting the reconstructed waveform *between* the samples,
which requires oversampled detection, whose filters are delays; honouring their verdict means
holding the audio back by that delay, i.e. lookahead. The zero-latency guarantee (see below)
rules that out. What the limiter guarantees is that no output *sample* exceeds the Ceiling;
the analogue waveform your converter reconstructs between those samples still can.

Measured with an 8× windowed-sinc reconstruction (`tests/OutputLimiterTests.cpp`, where these
figures are regression-frozen), at a −0.3 dB ceiling:

| Programme | Inter-sample overshoot above the ceiling |
|---|---|
| 11.025 kHz tone at 44.1 kHz, phased so every sample straddles a crest (the analytic worst case for a sine) | **3.01 dB** |
| 11 kHz tone, arbitrary phase | **0.71 dB** |
| 1 kHz tone, 5 ms release | **0.06 dB** |

Practically: on real programme material the overshoot is a fraction of a dB, and the 3 dB
figure is the mathematical ceiling of what a sine can hide between samples, not a typical
result. If you are delivering to a specification that mandates a true-peak limit (−1 dBTP for
lossy encoding, for instance), set Ceiling with that headroom — or use a true-peak limiter at
the end of your master chain, where its latency costs nothing.

## Presets

A preset bar sits at the top of the editor: `[<] [PresetName*] [>] [Save] [Save As...]
[Delete] [Import...] [Export...]`. Clicking the preset name opens a Factory/User menu; a
trailing `*` means the current preset has unsaved changes. Thirteen factory presets ship in the
box (see `presets.md` for what each one is for) — including **Tape Slap 7.5** and **Worn
Slap**, added in v0.5.0 to exercise Wobble and Age, and **BV Mode**, a
background/stacked-vocal starting point with every return pushed harder than the lead
template; user presets save to
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

- SPREAD's pitch shifter crossfades two taps of one delay line. Since v0.6.0 the tap spacing
  listens to the input: on confidently pitched material (a held vowel, a synth) it snaps to a
  multiple of the note's period so the taps reinforce instead of drawing comb luck — sustained
  tones that previously met a note-dependent comb of up to ~18 dB envelope ripple now stay
  within ~2 dB at any pitch. On unpitched material (consonants, breath, noise) the detector
  stands down and the bus behaves exactly as before. Residual limits: the tracker follows the
  lowest fundamental down to 80 Hz; re-alignment takes roughly a second after a note lands
  (longer for very low notes, so very short notes ride mostly on the standard path), and fast
  vibrato can outrun it — a brief return of the old mild shimmer, which reads as natural
  doubling. Below Time 100% the causal cap on the tap spacing progressively overrides the
  period-aligned spacing (the up voice first), so the sustained-tone protection fades back
  toward the v0.5.0 behaviour as Time shrinks — by ~60% Time it is effectively off. What can
  no longer happen at any Time setting is the pre-v0.6.0 dry leak from a tap parked at the
  delay line's lower bound.
- The GUI is the M3 custom vector editor: one faceplate panel per bus with pointer knobs,
  engraved scale rings and per-bus gain-reduction needle meters (Direct FET, CRUSH,
  SANDWICH), fully keyboard-operable (Arrow/Shift+Arrow/PageUp/PageDown/Home/End,
  Shift-drag for fine mouse adjustment) and screen-reader accessible (per-bus grouping,
  unit-suffixed values). Final manual VoiceOver verification is tracked on the a11y issue.
- The output limiter (v0.7.0) enforces a **sample-peak** ceiling, not a true-peak one, and
  cannot enforce a true-peak one without breaking the zero-latency guarantee — see the
  measured inter-sample overshoot table in the Output limiter section above.
- Keying CRUSH or SANDWICH from the external sidechain converts them from feedback to
  feed-forward compressors, with a measurably different static curve — see the External
  sidechain section above. This is inherent to keying a feedback topology, not a limitation
  that can be tuned away.
- Out of scope for v2, tracked as an M2+/M3 issue: a short plate reverb module. (Swappable
  compressor colours per dynamics slot shipped in
  v0.6.0 — the Direct FET's Character, CRUSH's third Style and SANDWICH's Colour switches
  above; the output limiter, the BV Mode preset and the external sidechain shipped in
  v0.7.0.)
- Dynamics detection is unlinked (independent L/R) by default on Crush and Sandwich; Link
  makes both channels track a shared detector.
- The voicing throughout this plugin is **research-derived, not measured against hardware
  units** — see `research-notes.md` for the sourced findings and their limitations.
