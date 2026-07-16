# Factory presets

Ten factory presets ship with Miserere v0.3.0, embedded via BinaryData from
`presets/factory/*.json` (see nave's `docs/preset-system-notes.md` for the
shared build wiring this repo copied). All are designed against
`docs/design-brief.md`'s v2 topology and `docs/research-notes.md`'s sourced
findings - see that document's own honesty framing for what these numbers
are and aren't calibrated against (research-derived, not measured hardware).
No brand or person names appear in any preset name or category.

Every preset leaves the direct/dry path's optional sections at their
bit-transparent-off defaults unless its own intent below says otherwise -
the direct signal stays "the sacred channel" (`docs/design-brief.md`) in
every preset except **Direct Channel Only** and **Aggressive Rock Vocal**.

| Preset | Category | Intent |
|---|---|---|
| **Default** | Init | The plugin's certified out-of-the-box state: every parameter at its `ParameterLayout` default (direct path fully off; the four parallel returns present at the design brief's documented fader positions: Crush −9 dB, Sandwich −12 dB, Spread −18 dB, Slap −15 dB). Also this plugin's M2 default-resolution target (`presetManager.applyStartupDefault()`) - always a one-click way back here. |
| **Classic Parallel Blend** | Vocals | The documented 2010s parallel-vocal template at its canonical fader recipe (Crush −9 / Sandwich −12 / Spread −18 / Slap −15 dB, `docs/research-notes.md`'s primary-source quote), with all four busses driven hard enough (hotter Crush input drive, more Sandwich peak reduction, wider Spread) to be genuinely audible in the blend rather than just nominally present. |
| **Crush Forward** | Vocals | Isolates bus ① CRUSH: hot input drive, the 20:1 ratio point, All-Buttons style, pushed to −4 dB in the blend; Sandwich/Spread/Slap all muted so the FET limiter's plateau-and-snap character is the whole story. |
| **Silk Sandwich** | Vocals | Isolates bus ② SANDWICH: the Pultec-style pre-EQ → Opto Leveler → Pultec-style post-EQ "sandwich" pushed to −6 dB with a touch more peak reduction than default; Crush/Spread/Slap muted so the dynamic-tilt EQ trick reads clearly on its own. |
| **Wide & Wet** | Vocals | Isolates busses ③ SPREAD and ④ SLAP - the two "thickening" effects (`docs/research-notes.md`): wider/more-detuned micro-pitch, a longer stereo slap with darker tone, both pushed up; Crush/Sandwich muted. |
| **Direct Channel Only** | Vocals | The direct/serial path's optional sections switched on (pre de-esser, light FET compressor, console EQ with HPF and a little drive, a touch of tape grit) with all four parallel returns muted - showcases the "channel" half of the v2 topology in isolation from the parallel-bus half. |
| **Gentle Bus** | Vocals | Bus ① CRUSH in its later-era **Gentle** (fixed 2:1) style - "2:1, attack slowest, release around 6" per `docs/research-notes.md` - with lighter input drive for a 2–5 dB glue target rather than a crush effect; Sandwich/Spread/Slap muted so the gentle character reads on its own. |
| **Rough Mix Glue** | Vocals | A light, balanced touch of all four returns at once for quick rough-mix vocal glue - every bus present but restrained (lower drive/peak-reduction/width than Classic Parallel Blend), direct path off. |
| **Whisper Thicken** | Vocals | For thin/quiet/breathy vocals: Crush and Sandwich kept very light (control, not colour), Spread and Slap brought forward to add body and width, with a light pre de-esser for breath-driven sibilance. |
| **Aggressive Rock Vocal** | Vocals | The full chain leaned in hard: a fast, driven direct FET compressor, an aggressive console EQ scoop/lift with drive and tape grit, de-essing on both sides of the direct chain, Crush pushed hot, Sandwich present, a short dark Spread/Slap pair - an in-your-face genre vocal showcasing direct-path and parallel-bus processing together. |
