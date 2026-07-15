# Miserere

*Four voices, one prayer — the parallel vocal chain in a single unit.*

[![CI](https://github.com/basilica-audio/miserere/actions/workflows/ci.yml/badge.svg)](https://github.com/basilica-audio/miserere/actions/workflows/ci.yml)
[![License: AGPL v3](https://img.shields.io/badge/License-AGPL%20v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)

> **Work in progress.** Miserere is pre-1.0 and under active development. There are no built binaries or releases yet — building from source is currently the only way to run it. Expect breaking changes until v1.0.0 ships (see [Roadmap](#roadmap)).

## What it is

Miserere is a **parallel vocal chain in a single unit**: a Direct channel strip plus three parallel busses — an opto-leveler sandwich, an all-buttons FET smash, and a filtered slap delay — each with its own fader, mute and exclusive solo, summed like console busses. It packages the classic "rough vocal template" parallel-mixing workflow (blend a clean vocal with crushed and leveled copies of itself) that channel strips with serial per-module wet/dry knobs cannot reproduce: in a serial strip, each module's "dry" is the previous module's output, so the truly clean signal is gone after the first stage.

Built for vocals in heavy music — dense enough to sit on a wall of guitars, dynamic enough to still sound like a performance — and happy to be abused on drum busses (parallel compression was born there).

## Features

- **Bus A — Direct**: switchable HPF (20–300 Hz, 12 dB/oct) → British-console-style 3-band EQ (±15 dB) → FET compressor (4:1/8:1, GR metering) → split-band de-esser (4–9 kHz, max 10 dB) → tape-style saturation (0–24 dB drive, level-compensated).
- **Bus B — Opto**: boost-only passive-style EQ (60/100 Hz + 8/10/12/16 kHz selects) → opto leveler with a program-dependent **two-stage release** (~60 ms fast stage into ~600 ms slow stage — release slows the longer it has been working) → 12 kHz "air" shelf after the leveler.
- **Bus C — Smash**: FET limiter in all-buttons character — ~20:1, 0.05–0.8 ms attack, program-dependent release *shortening*, +6 dB @ 2 kHz mid-forward sidechain, drive and output trim. Aggressive by design.
- **Bus D — Slap**: 60–180 ms fractional delay (wet-only), 0–30% feedback through a HP/LP-filtered, tape-soft-saturated loop, mono switch. Unconditionally stable.
- **Console fader logic**: per-bus level (−60 dB = true off … +6 dB), mute, exclusive solo (mute wins over solo).
- **Sample-aligned parallel summing**: busses A–C are minimum-phase IIR / zero-lookahead only, so the sum never combs — verified by impulse-alignment tests. Bus D is a delay on purpose.
- **Zero reported latency**, bit-transparent neutral path (≤ −120 dBFS null), full state save/recall, 65 Catch2 test cases covering all ten M1 guarantee categories.

## Signal flow

```
                 ┌─ BUS A "Direct":  HPF → Console EQ → FET Comp → De-Esser → Tape Sat ── fader ─┐
in ─ [In Trim] ─┼─ BUS B "Opto":    Passive EQ in → Opto Leveler → Passive Air out ───── fader ─┼─ Σ ─ [Out Trim] ─ out
                 ├─ BUS C "Smash":   FET Limiter (all-buttons, mid-forward sidechain) ─── fader ─┤
                 └─ BUS D "Slap":    Slap Delay (60–180 ms, filtered tape-soft feedback) ─ fader ─┘
```

See [`docs/manual.md`](docs/manual.md) for the parameter reference and starter recipes (lead vocal, aggressive vocal, drum-bus abuse), [`docs/architecture.md`](docs/architecture.md) for the technical design, and [`docs/adr/0003-parallel-bus-topology.md`](docs/adr/0003-parallel-bus-topology.md) for why true parallel busses beat a serial strip with wet/dry knobs.

## Parameters

| Group | Parameters |
|---|---|
| Global | In Trim, Out Trim (±12 dB), Bypass |
| Bus A — Direct | HPF on/off + freq · EQ low / mid (freq, gain, Q) / high · Comp ratio (4:1/8:1), threshold, attack, release, makeup · De-ess on/off, freq, threshold · Sat drive |
| Bus B — Opto | Low boost freq (60/100 Hz) + gain · High boost freq (8/10/12/16 kHz) + gain · Peak reduction, makeup · Air |
| Bus C — Smash | Attack, release, drive, output trim |
| Bus D — Slap | Delay time, feedback, loop HP/LP, mono |
| Per bus | Level (−60…+6 dB), Mute, Solo (exclusive) |

## Roadmap

| Milestone | Scope |
|---|---|
| **M1** (this) | Four-bus DSP core, parameter layout, functional editor, full test suite |
| M2 | Presets & state extras; swappable classic-style module alternatives per slot |
| M3 | Custom GUI (pointer knobs, engraved scale rings, per-bus needle meters) & accessibility |
| M4 | Signing, notarization, v1.0.0 release |

## Installation

No pre-built binaries are published yet (see the work-in-progress notice above). Once releases begin, installation will follow the standard plugin locations:

**macOS**

| Format | Path |
|---|---|
| AU (Component) | `~/Library/Audio/Plug-Ins/Components/` |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/` |

If Logic Pro doesn't pick up the plugin after installing, force a rescan by resetting the AU cache:

```sh
killall -9 AudioComponentRegistrar
auval -a
```

**Windows**

| Format | Path |
|---|---|
| VST3 | `C:\Program Files\Common Files\VST3\` |

## Building from source

Requires JUCE 8.0.14, C++20, and CMake ≥ 3.24. See [`docs/building.md`](docs/building.md) for full prerequisites and step-by-step build/test commands for macOS and Windows.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## License

Miserere is licensed under the [GNU Affero General Public License v3.0](LICENSE) (AGPLv3).

This project uses [JUCE](https://juce.com) 8, whose open-source tier is licensed under AGPLv3 (as of JUCE 8; JUCE 7 and earlier used GPLv3), which is why this project is AGPLv3 rather than GPLv3. See [`docs/adr/0002-agplv3-licensing.md`](docs/adr/0002-agplv3-licensing.md) for the full reasoning.

VST is a registered trademark of Steinberg Media Technologies GmbH.

Miserere is an independent open-source project and is not affiliated with, endorsed by, or sponsored by any plugin manufacturer.
