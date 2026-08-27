# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- **The suite now presents itself as Basilica Audio in every host.** `COMPANY_NAME` moves from
  `Yves Vogl` to `Basilica Audio`, so Miserere files under the brand in Logic's plugin manager,
  Cubase's vendor column and Reaper's FX browser instead of under a person's name. **Plugin
  identity is untouched** and no session is affected: the VST3 class ID derives from
  `PLUGIN_MANUFACTURER_CODE` + `PLUGIN_CODE` alone (JUCE 8.0.14, `juce_VST3ModuleInfo.h`'s
  `VST3Interface::jucePluginId`) and the Audio Unit triple stays `(aufx, <PLUGIN_CODE>, Yvsv)` -
  both diffed on a real build before and after the change. The bundle ID stays
  `com.yvesvogl.miserere` on purpose, because changing it is what would break existing projects, and
  `COMPANY_COPYRIGHT` still names the copyright holder rather than the trading name. See
  [`docs/branding.md`](docs/branding.md) and basilica-audio/.github ADR 0001.
- **User presets now live under `Basilica Audio`, and the ones you already saved come with them.**
  The folder moves to `~/Library/Audio/Presets/Basilica Audio/Miserere/` (macOS) and
  `%APPDATA%\Basilica Audio\Miserere\Presets\` (Windows). On first launch `PresetManager` copies
  every preset out of the old `Yves Vogl` folder into the new one. It **copies rather than moves**,
  so an older build - or a downgrade - still finds its presets where it left them, and it never
  overwrites a file already present under the new name. Nothing is deleted, ever.
- **Plugin metadata now carries the vendor URL, the copyright string, a real description and
  the VST3 sub-category.** `COMPANY_WEBSITE`, `COMPANY_COPYRIGHT` and `DESCRIPTION` were never
  set, so a shipped bundle carried an empty `NSHumanReadableCopyright`, an empty VST3 vendor
  URL, and an AU `description` that was just the plugin name again; `VST3_CATEGORIES` fell back
  to JUCE's bare `Fx` default, which filed every plugin in the suite under the same
  undifferentiated heading in a VST3 host's browser. Miserere now declares
  `Fx Dynamics` (JUCE 8.0.14, `juce_add_plugin`). **Plugin identity is unchanged** — the VST3 class
  ID is derived from `PLUGIN_MANUFACTURER_CODE` + `PLUGIN_CODE` alone
  (`juce_VST3ModuleInfo.h`'s `VST3Interface::jucePluginId`) and the AU type/subtype/manufacturer
  triple is untouched, so existing sessions keep resolving to the same plugin.

### Fixed

- **Release notes are the changelog again, not a list of PR titles.** `release.yml` now builds the
  release body from this file's section for the tag being released, via the suite-wide
  `basilica-audio/.github/release-notes` action, and appends what a downloader actually needs: what
  each archive contains, the signing status per platform stated accurately (macOS signed, notarised
  and stapled; Windows **not** code-signed, so SmartScreen will warn), the install paths, the AU
  rescan hint, and links to the manual and the product page. A tag whose version has no section in
  this file now fails the release job rather than publishing an empty page.
- **The README no longer tells users the binaries do not exist.** The Installation section
  said *"No pre-built binaries are published yet"* while the banner four lines above it linked
  the Releases page, and the banner in turn described the macOS builds as *"currently
  unsigned"*. Both claims were false. The Installation section now describes the actual
  download-and-copy flow, and the banner states what the release workflow actually produces:
  verified against the shipped `v0.7.0` `.component` with `codesign --verify --strict`
  (`Developer ID Application: Yves Vogl (M5WT732AY5)`), `spctl -a -t open`
  (`source=Notarized Developer ID`) and `stapler validate`.

### Added

- **A `Documentation` section in the README** pointing at the user manual, the factory-preset
  reference, the changelog and the product page — the manual was only reachable from a
  sentence in the middle of the Signal flow section.

### Fixed — bypass is click-free, latency-compensated and no longer freezes the chain (#41)

`processBlock()` implemented bypass as an early return: the buffer was handed straight back
and `MiserereEngine::process()` was never called at all. Three defects followed from that one
line, and they are fixed together because they share a cause. The same defect and the same fix
in the sibling plugin are basilica-audio/Crypta#87 / #90; this is a port of that architecture,
including its naming.

- **Click-free.** Bypass is now a continuous crossfade between the engine output and a copy of
  the untouched input, driven by a `juce::SmoothedValue<float, Linear>` (`bypassWetMix`) reset
  in `prepareToPlay()` to a fixed, **sample-rate-independent** 20 ms
  (`bypassCrossfadeDurationSeconds`) — a time constant, not a sample count, because a bypass
  toggle is a performance-time event. Measured sample-to-sample step at the switch, with all
  four busses up (48 kHz, 110 Hz probe at 0.5): **0.518 → 0.0086 engaging**, **0.673 → 0.0076
  disengaging**, against a steady-state slew of 0.0068–0.0072 at either endpoint. Both are now
  inside the same 4×-own-slew bound every other transition in the suite is held to.
- **The chain no longer freezes while bypassed.** `MiserereEngine::process()` now runs
  unconditionally. Previously every module's state — filter memory, envelope followers, both
  delay-line busses, the limiter's release — stopped dead the instant bypass engaged, which is
  a second, independent click on the way back **out** (stale state resuming into a live signal)
  and is why the post-bypass "steady state" was itself broken: the SPREAD/SLAP busses switched
  back on abruptly as their emptied delay lines refilled, measuring a 0.353 max step where the
  never-bypassed chain measures 0.0068. A state-continuity test (30 blocks bypassed mid-render,
  moving envelope, compared against a never-bypassed reference) goes from **−1.9 dB to
  numerical zero**.
- **Latency-compensated by construction.** A new `bypassDryDelay`
  (`juce::dsp::DelayLine`, `None` interpolation) delays the dry path by exactly
  `getLatencySamples()`, armed in `prepareToPlay()` and kept running every block so its history
  is never stale at a toggle. **Miserere reports 0 samples of latency at all times** (ADR-0003)
  and measured 0 both before and after — a dirac lands at sample 0, matching the reported
  figure — so unlike Crypta this half was never a live defect here. It is implemented anyway
  because the invariant is "the dry path is delayed by exactly the reported latency", not "by
  zero": the day any stage stops being zero-latency, bypass must not silently become a phase
  error. A settled bypassed instance nulls against the latency-shifted dry signal at
  **numerical zero**.
- **NaN/Inf policy extended to the dry path.** The dry copy goes around the engine and
  therefore around the engine's final-sum sanitiser, so it is sanitised on capture (before the
  delay line, so nothing non-finite can be echoed back out later). Without it a non-finite
  input multiplied by a zero blend gain is NaN, which would have poisoned the output even with
  bypass fully disengaged. Bypassed non-finite input is now cleaned too, where the early return
  used to hand it straight back to the host.
- Real-time safety unchanged: **0 heap allocations** measured across 40 mid-stream bypass
  toggles; every buffer and the delay line are allocated in `prepareToPlay()`.
- New tests: `tests/BypassTests.cpp` (7 cases — click bounds on engage/disengage, reported
  latency stable across the toggle, dirac latency, null test against the delayed dry copy,
  non-finite input on the dry path, engine-state continuity) and a bypass-toggle allocation
  case in `tests/AllocationGuardTests.cpp`. Catch2 suite: 233 → 241 cases.

## [0.7.0] — 2026-08-20

Three additions that each defend the same invariant. The chain gains an optional external
key for its three keyable detectors, a background/stacked-vocal starting point, and a final
safety limiter — and every one of them is disabled, unengaged or off by default, so the
default bus layout and the default sound stay byte-identical to v0.6.0. Still zero reported
latency, and no lookahead anywhere. Catch2 suite: 212 → 233 cases.

### Added — external sidechain input (#23)

An optional **Sidechain** input bus, **disabled by default** so the default layout — the one
auval and pluginval's default run see, and the one every existing session carries — is
byte-identical to the pre-v0.7.0 plugin. `isBusesLayoutSupported()` admits the key disabled,
mono or stereo alongside the existing "main out == main in, mono or stereo" rule; anything
wider is refused rather than silently half-used.

- Three independent switches, one per keyable detector, with no master switch:
  `direct_fet_key_ext`, `crush_key_ext`, `sand_key_ext` (all default off, AU version hint 4),
  surfaced as **Ext Key** lamps in the Direct Path, Crush and Sandwich panels. An absent or
  disabled bus, or a host that sends no key, falls back to internal detection silently. A mono
  key into a stereo instance keys both channels.
- `processBlock()` now takes the audio path as the MAIN bus's view of the host buffer rather
  than as the whole buffer, so the key channels are never processed as if they were audio, and
  chunks the key in lockstep with the oversized-block chunking. The key is read-only and never
  copied.

**Topology note, stated plainly because it changes the sound:** CRUSH (`FetCrush`, whose
rectifier is driven by `y[n-1]`) and SANDWICH (`OptoLeveler`, whose EL panel is driven by its
own compressed output) are **feedback** topologies — their soft knee, emergent ratio and
static curve all come out of those loops. An external key does not add a detector input to
them; it **replaces the loop drive**, converting both into **feed-forward keyed compressors**
while engaged, with a measurably different static curve. Measured on the same signal at the
same settings (−22 dBFS tone, 0 dB Input): CRUSH produces ~3 dB of gain reduction with
internal detection and ~21 dB when keyed with a copy of its own input. That is legitimate and
useful, and it is documented as a different compressor rather than as "the same sound from a
different detector source". The Direct FET was already feed-forward, so keying it really is a
pure detector-source swap.

- 9 new Catch2 test cases (233 total, up from 224): the default layout is unchanged and the
  bus starts disabled; every disabled/mono/stereo combination is accepted and a wider key
  refused; key switches with no bus enabled are a silent no-op; a stereo key drives CRUSH and
  SANDWICH while the audio itself is too quiet to; a mono-key-into-stereo-main edge asserted
  through `getBusBuffer (buffer, true, 1)` directly; the feedback-to-feed-forward curve
  difference on both CRUSH and SANDWICH; the Direct FET's pure detector-source swap (silent
  key leaves it fully open, hot key closes it); and the default-wire null plus zero latency
  with a hot key present.

### Added — "BV Mode" factory preset (#21)

A background/stacked-vocal starting point rather than a lead one: every return sits harder
than the lead template (CRUSH at −6 dB off a hot 26 dB input, SANDWICH at −8 dB with 70 %
peak reduction) and the two thickening busses come forward most — SPREAD at −9 dB with
maximum width and 11 cents of detune, a stereo SLAP at −12 dB with a touch of tape wear. The
only direct-path section it engages anywhere is the 160 Hz HPF, to clear the low end for the
lead the stack sits behind; the rest of the channel stays a wire.

Voiced from the template's own documented logic (parallel returns are driven far harder than
any insert; SPREAD/SLAP exist "for thickening") rather than from a BV-specific source — there
is no primary-source quote about background vocals in `docs/research-notes.md`, and none is
claimed.

### Fixed — factory-preset registration drift (#21)

`CMakeLists.txt` has embedded twelve factory presets since v0.5.0, but
`tests/PresetManagerTests.cpp`'s independent copy of the asset list still carried ten
(`tapeSlap75`/`wornSlap` were shipping untested) and asserted `factoryCount == 10`, so the
count assertion was passing against a stale list rather than against what the plugin ships.
Both lists now carry all thirteen presets, the assertion reads 13, and `docs/presets.md`
documents the three previously undocumented entries (Tape Slap 7.5, Worn Slap, BV Mode).

### Added — output limiter (#24)

A final safety stage after the Out Trim, **off by default** and a bit-exact bypass while off,
so the plugin's default-wire invariant is untouched.

- `limiter_enabled` (off), `limiter_ceiling` (−12…0 dB, default −0.3 dB) and
  `limiter_release` (5…500 ms, default 60 ms), plus three controls appended to the Global
  panel and a fourth needle meter in that panel's meter bay. State schema stamped
  `stateVersion` 4; the three newcomers take AU version hint 4, so existing automation lanes
  cannot remap.
- **Sample-peak ceiling, explicitly not true-peak.** Issue #24 mandates zero latency and no
  lookahead; true-peak detection needs an oversampled reconstruction whose filters are
  delays, and honouring their verdict means holding the audio back by that delay — lookahead
  under another name. What is guaranteed is that no output *sample* exceeds the ceiling. The
  inter-sample overshoot that necessarily remains is **measured** with an 8× windowed-sinc
  reconstruction and regression-frozen in `tests/OutputLimiterTests.cpp`: **3.01 dB** for the
  analytic worst-case sine (a Nyquist/4 tone phased so every sample straddles a crest),
  **0.71 dB** for an arbitrary-phase 11 kHz tone, **0.06 dB** for a 1 kHz tone. The manual
  quotes these figures; no user-facing string claims a true-peak ceiling.
- Gain computer: infinite ratio with a 3 dB soft knee in dB
  (`yDb = min(xDb − u²/(2·knee), ceiling)`, `u = clamp(xDb − (ceiling − knee/2), 0, knee)`),
  whose maximum over the knee is exactly the ceiling. Instantaneous attack (the only way to
  hold a ceiling without lookahead) and a one-pole release that approaches the static gain
  strictly from below, so recovery can never breach the ceiling. Once the signal is clear of
  the knee the gain snaps to exactly 1.0f and the stage is bit-transparent again — the loop
  state is kept in double precision because a float one-pole stalls ~1.6e-4 short of unity
  and would leave a permanent, silent ~0.01 dB attenuation after every peak.
- Detection is **permanently L/R-linked** and deliberately not governed by the global `link`
  control: `link` chooses dual-mono vs. linked detection for CRUSH/SANDWICH, where dual mono
  is part of the sound, but unlinked *limiting* applies different gains to L and R on the
  summed output and pans the image on every peak.
- 11 new Catch2 test cases (223 total, up from 212): bit-exact bypass, the static curve's
  unity/ceiling contract, "no output sample exceeds the ceiling" across five ceilings × three
  release settings, instantaneous attack on a silence-to-+18 dBFS step, monotone
  never-overshooting release that lands on exact unity, settled bit-exact wire, image-preserving
  linked detection, the frozen inter-sample overshoot measurement, a robustness battery
  (zero-length blocks, denormals, NaN/Inf, sample-rate change) and a plugin-level ordering
  test proving the limiter sits after the Out Trim.

## [0.6.0] — 2026-08-20

The M3 GUI release. The functional slider editor becomes a fully vector-drawn, fully
accessible instrument, every dynamics slot gains swappable classic-style voicings, and
SPREAD stops combing on sustained tones — all of it still at zero reported latency.

### Added — swappable classic-style compressor colours per dynamics slot (#20)

Every dynamics slot now carries a voicing switch, implemented as engine tuples the way
`FetCrush::Style` always worked — no polymorphic module swapping, so the parameter surfaces,
the zero-latency/phase discipline and the real-time guarantees are untouched. Generic
hardware descriptors only, per the binding design principles. Every switch defaults to the
pre-existing voicing (index 0), so v0.5.0 sessions load sound-identical, and every swap is
click-free mid-stream (10 ms tuple crossfade on CRUSH, 50 ms smoothed knee/colour ramps on
the Direct FET, a state-preserving carrier-kinetics update plus smoothed colour drive on the
opto).

- **Direct FET `direct_fet_colour`** — Character: **FET** (default, the exact previous
  hard-knee voicing, bit-identical), **VCA** (6 dB soft knee, snappier attack, clean —
  bit-transparent below the knee, null-tested) and **Tube Mu** (12 dB knee, slower attack,
  longer release, GR-gated second-harmonic warmth; ≥1 % H2 under deep gain reduction,
  DC-rejected).
- **CRUSH `crush_style`** — a third style, **Vintage**, appended after All-Buttons/Gentle
  (stored indices keep their meaning): the per-ratio all-buttons tuples in a hot
  early-revision bias state — threshold −2 dB, loop ×1.12, residual-mismatch "hair" ×2.2,
  under-damped charge path. Measurably earlier/deeper GR and >1.3× the harmonic colour at
  the same settings, with the ratio row fully active.
- **SANDWICH `sand_colour`** — Colour: **Classic** (default, the v0.5.0 T4B calibration,
  bit-identical — the golden static curve still holds), **Quick** (solid-state-era optical:
  ~6× faster carrier recombination with a √6 mobility compensation so the GR depth stays in
  the same class, a quarter of the trap memory, near-clean output stage) and **Deep**
  (earlier-era: slower recovery, tripled trap memory, thicker tube/transformer stage).
- State schema stamped `stateVersion` 3; the two new choices take AU version hint 3
  (v0.6.0 generation) so existing automation lanes cannot remap. Documented pre-1.0 caveat:
  appending CRUSH's third style rescales that parameter's normalised axis, so
  host-recorded `crush_style` automation from before v0.6.0 lands on different indices
  (saved sessions/presets store raw indices and are unaffected).
- New editor knobs (detented PointerKnobs like every choice): Direct Path "Character",
  Sandwich "Colour"; layout/accessibility contracts extended to 73 parameters / 56 knobs.

### Added — M3 custom vector editor and accessible parameter surface

- **Vector editor** (#25) — the functional slider/knob editor is replaced by a fully
  vector-drawn GUI in the suite's black/gold language: pointer knobs with engraved scale
  rings (choice parameters get one engraved detent tick per position), lamp toggles, one
  faceplate panel per bus (Global / Direct / Crush / Sandwich / Spread / Slap), a restyled
  preset bar, and EB Garamond (OFL) as the embedded suite serif. No photoreal PNG assets —
  everything is drawn at runtime by `BasilicaLookAndFeel` and the `src/gui` components.
- **Per-bus needle meters** — vector gain-reduction needle meters for the three GR sources
  the engine exposes (Direct FET, CRUSH, SANDWICH opto), with GUI-thread ballistic
  smoothing fed by a 30 Hz poll of the engine's per-block metering (now relaxed-atomic on
  both sides).
- **Accessible parameter surface** (#26) — designed in from the start, not retrofitted:
  every control is keyboard-focusable and steppable (Arrow 1 %, Shift+Arrow 0.1 %,
  PageUp/Down 10 %, Home/End extremes, one detent per press on choice knobs; Ctrl/Alt/Cmd
  left to the host), Shift-drag fine mouse adjustment, visible focus rings on every
  control, unit-suffixed accessible value strings (set after the `SliderAttachment` to
  survive JUCE 8.0.14's `textFromValueFunction` clobber), per-bus accessibility focus
  containers (screen readers hear "Crush Bus, Ratio"; Tab still walks the whole editor),
  read-only accessible dB values on the meters, and WCAG-AA-contrast-locked colour pairs.
- New test suites under `tests/gui/`: editor accessibility (names/values/roles, keyboard
  stepping, focus containers), needle-meter ballistics/angle mapping/NaN sanitisation,
  WCAG contrast on the exact rendered colour pairs, and layout invariants (containment,
  no overlap, one control per parameter, label-in-name).

### Added — period-adaptive SPREAD splice: comb-free sustained tones (#19)

- SPREAD's two crossfading taps interfere continuously (`env² = 1 + sin(2π·pos) ·
  cos(2π·f·τ)`), so a sustained tone landing near an anti-phase tooth of the 1/τ comb
  rippled by up to ~18 dB. The tap separation τ itself now adapts: a progressive
  normalized-autocorrelation **PeriodDetector** (lowpassed, ~12 kHz-decimated history
  ring, flat ~100 MACs/sample, every size fixed in `prepare()`) snaps the separation to a
  whole number of detected periods on confident periodicity (NACF gate with 0.6/0.5
  hysteresis), so the taps sum in phase at the fundamental and every harmonic, and
  splices land phase-continuous.
- The live separation converges via a masked micro-slew (≤ ~3.5 cents, applied on the
  quieter tap) plus an exact click-free re-seat at window wraps; the crossfade law blends
  sin → sin² (amplitude-complementary) gated on confidence *and* achieved alignment, so a
  coherent in-phase sum has constant amplitude.
- Measured: sustained-tone ripple uniformly ≤ 2.1 dB at any pitch (bar: 2.6 dB —
  deliberately below the 3.01 dB a separation-only fix would show, pinning the sin² blend
  too). Non-periodic treble-band material is bit-identical with the adaptation on or off
  (the NACF gate is scale-invariant, so aliased treble cannot fake confidence), and
  latency stays zero — the detector only reads already-written history.

### Added — test-suite hardening (#29)

- Allocation-guard **self-test**: the guard behind every `allocationCount() == 0`
  assertion is now itself proven to fire on a real heap allocation (an elision-proof
  direct `::operator new` canary with an observable `volatile` write, counted outside
  Catch2's assertion macros) and to stay silent on pure stack work.
- Sample-rate-matrix reprepare coverage (`tests/SampleRateMatrixTests.cpp`): one
  processor instance reprepared across 44.1k → 96k → 192k → 44.1k with 16…16384-sample
  blocks, all four busses and the v0.5.0 circuit paths engaged and churned between steps
  — all-finite output, `getLatencySamples()` exactly 0 at every step, APVTS state
  surviving every reprepare.
- With the new `tests/gui/` and colour suites the whole suite now stands at **212 Catch2
  test cases** (162 at v0.5.0).

### Fixed

- **SPREAD stays causal at `timeScale < 1`** (#28) — below ts ≈ 0.6 a voice's crossfade
  window straddled zero delay, pinning a tap at the interpolator's 2-sample read clamp
  with up to −3 dB of window gain: an unshifted dry copy of the input in the return,
  ~1.7 s per traversal. Three guards inside `Voice` — a per-sample causal cap on the
  separation target (seeded by `reset()` too, so a session restored at `timeScale` 0.5
  starts causal), a 64-sample causal-floor fade that silences a tap still reaching the
  clamp during fast downward Time automation, and a gain-gated 10× catch-up slew —
  take the bleed from −13…−30 dB to −66…−80 dB (acceptance bar: −40 dB) on both voices.
- **SANDWICH rests at exact zero after silence** (#15) — `PassiveEq`'s LF ladder is
  genuinely first-order whenever one pot sits at exactly 0 (the normal case: every
  shipped instance drives exactly one pot), but was bilinear-transformed at second
  order, leaving a pole/zero pair at Nyquist that should cancel and, rounded to float,
  no longer does — marginally unstable (radius 1.00000002) on the cut-only shape, so the
  bus rang at ~−150 dBFS indefinitely once excited. The degenerate `(z+1)` factor is now
  divided out analytically and the first-order transform emitted with exact-zero
  second-order coefficients; the voicing is unchanged (old and new coefficients
  re-derived independently and matched). Same undamped-Nyquist-pole hazard class as
  `ConsoleEq`'s iron integrator, now recorded in `docs/architecture.md`.

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
