# Miserere — user manual

*Four voices, one prayer — the parallel vocal chain in a single unit.*

## What Miserere is

Miserere packages the classic "rough vocal template" parallel-mixing workflow into one plugin: a **Direct** vocal chain plus three **parallel busses** — an opto-leveler sandwich, an aggressive FET smash bus, and a slap delay — each with its own fader, mute and solo. You mix the four busses against each other instead of tweaking one serial chain.

Existing channel strips are serial: each module processes what the previous one left over, and "more compression" always means "less dynamics". Miserere's identity is TRUE parallel routing.

## Why parallel beats serial for thick-but-dynamic vocals

A vocal in a heavy mix has two contradictory jobs: it must be **dense** enough to sit on top of a wall of guitars, and **dynamic** enough to still sound like a human being. A serial chain forces you to choose — compress hard and lose the performance, or compress gently and lose the fight against the guitars.

Parallel processing sidesteps the choice. The Direct bus keeps the vocal's transients and phrasing intact; the Smash bus destroys its own copy of the signal (~20:1, fast, mid-forward) and gets blended *underneath* the direct sound. The louder the singer gets, the harder the smash bus limits — so the *blend* stays dense at every dynamic level while the direct bus still breathes on top. The Opto bus adds a third texture: slow, musical leveling with passive-style low/high bloom, filling out the body without touching the transients. The Slap bus adds the short, dark echo that glues a dry vocal into a produced mix without washing it in reverb.

Because busses A–C are built exclusively from minimum-phase, zero-lookahead processing, all three stay **sample-aligned** — you can push any fader anywhere and the sum never combs or hollows out. (The Slap bus is a delay on purpose.)

## Signal flow

```
                 ┌─ BUS A "Direct":  HPF → Console EQ → FET Comp → De-Esser → Tape Sat ── fader ─┐
in ─ [In Trim] ─┼─ BUS B "Opto":    Passive EQ in → Opto Leveler → Passive Air out ───── fader ─┼─ Σ ─ [Out Trim] ─ out
                 ├─ BUS C "Smash":   FET Limiter (all-buttons, mid-forward sidechain) ─── fader ─┤
                 └─ BUS D "Slap":    Slap Delay (60–180 ms, filtered tape-soft feedback) ─ fader ─┘
```

See [`architecture.md`](architecture.md) for the technical breakdown and the phase-discipline design notes.

## The four busses, musically

### Bus A — Direct

The vocal you'd print: a working channel strip that stays polite.

- **HPF** (20–300 Hz, 12 dB/oct, switchable) — clears rumble, plosive thumps and mud below the voice.
- **Console EQ** — a British-console-style three-band: low shelf at 100 Hz, a sweepable mid bell (250 Hz–5 kHz, Q 0.7–2), high shelf at 8 kHz, all ±15 dB. Broad strokes, not surgery.
- **FET Comp** — a fast FET-style compressor at 4:1 or 8:1 with makeup gain. This is the "catch the peaks" stage — 3–6 dB of gain reduction on the loudest lines is the classic setting.
- **De-Esser** — split-band, tunable 4–9 kHz, up to 10 dB of reduction. Placed *after* the compressor because compression brings sibilance up.
- **Tape Sat** — 0–24 dB of drive into a tape-style saturator with pre/de-emphasis, level-compensated at a −18 dBFS nominal level: more drive means more density, not more loudness.

### Bus B — Opto

The "sandwich": EQ into leveler into EQ. Boost lows and highs *into* a slow optical-style leveler and let it catch the excess — the passive-EQ-plus-opto move that makes a voice sound expensive.

- **Passive EQ in** — boost-only low shelf (60 or 100 Hz, 0–10 dB) and high shelf (8/10/12/16 kHz, 0–10 dB), broad and gentle.
- **Opto Leveler** — program-dependent two-stage release (~60 ms fast stage into a ~600 ms slow stage; the longer it has been working, the lazier it releases — like a real photocell that stays warm), soft ~3:1 ratio, fixed ~10 ms attack. One Peak Reduction knob plus makeup.
- **Passive Air out** — a final 12 kHz shelf (0–8 dB, boost only) *after* the leveler, so the air never pumps.

### Bus C — Smash

One module, zero subtlety: a FET limiter in its all-buttons-in character. Ratio around 20:1, attack down to 0.05 ms, program-dependent release *shortening* (the harder it limits, the faster it recovers — the "pumping forward" feel), and a sidechain tilted +6 dB at 2 kHz so the midrange — where the voice lives — drives the limiting. **Drive** slams the input into the fixed threshold; **Output Trim** gain-stages the wreckage. Blend it under the Direct bus until the vocal feels dense, then back the fader off 2 dB.

### Bus D — Slap

A tape-style slap echo, wet-only (the dry voice is Bus A's job): 60–180 ms of fractional delay, up to 30% feedback, with a high-pass/low-pass filter pair *and* soft tape saturation inside the feedback loop, so every repeat gets darker and rounder. A **Mono** switch collapses the echo to centered mono — the classic mono slap behind a wide vocal.

## Fader logic

- Every bus has **Level** (−60…+6 dB; the bottom of the fader is a true off), **Mute**, and **Solo**.
- **Solo is exclusive**: soloing one bus releases any other solo — you always audition exactly one bus.
- **Mute wins over Solo** on the same bus, console-style.
- Out of the box only the Direct bus is up; the three parallel busses start at the fader floor. Miserere does nothing to your vocal until you push a fader.

## Parameter reference

| Parameter | Range | Default | Unit | Notes |
|---|---|---|---|---|
| In Trim / Out Trim | −12…+12 | 0 | dB | Global gain staging around the whole parallel structure. |
| Bypass | off/on | off | – | Host-visible bypass parameter. |
| **Bus A — Direct** | | | | |
| HPF / HPF Freq | off/on, 20–300 | on, 80 | Hz | 12 dB/oct high-pass. |
| EQ Low | −15…+15 | 0 | dB | Low shelf, 100 Hz. |
| Mid Freq / Mid Gain / Mid Q | 250–5k / −15…+15 / 0.7–2 | 1k / 0 / 1 | Hz, dB | Sweepable bell. |
| EQ High | −15…+15 | 0 | dB | High shelf, 8 kHz. |
| Ratio | 4:1, 8:1 | 4:1 | – | FET comp ratio. |
| Threshold | −40…0 | −18 | dB | FET comp threshold. |
| Attack / Release | 0.1–10 / 50–1100 | 3 / 150 | ms | FET comp ballistics. |
| Makeup | 0…24 | 0 | dB | FET comp makeup gain. |
| De-Ess / Freq / Thr | off/on, 4k–9k, −40…0 | on, 6.5k, −24 | Hz, dB | Split-band, max 10 dB reduction. |
| Sat Drive | 0…24 | 6 | dB | 0 dB is an exact bypass. |
| **Bus B — Opto** | | | | |
| Low Boost Freq / Gain | 60/100, 0–10 | 100, 0 | Hz, dB | Boost-only passive-style shelf. |
| High Boost Freq / Gain | 8k/10k/12k/16k, 0–10 | 12k, 0 | Hz, dB | Boost-only passive-style shelf. |
| Peak Reduction | 0–100 | 40 | % | 0% is an exact bypass. |
| Makeup | 0…24 | 0 | dB | Post-leveler gain. |
| Air | 0…8 | 0 | dB | 12 kHz shelf after the leveler. |
| **Bus C — Smash** | | | | |
| Attack / Release | 0.05–0.8 / 50–200 | 0.3 / 100 | ms | Release shortens under deep limiting. |
| Drive | 0…12 | 0 | dB | Input slam into the fixed threshold. |
| Output Trim | −12…+12 | 0 | dB | Post-limiter gain staging. |
| **Bus D — Slap** | | | | |
| Delay | 60–180 | 110 | ms | Fractional (sample-exact) delay. |
| Feedback | 0–30 | 15 | % | Unconditionally stable. |
| Loop HP / Loop LP | 50–1k / 2k–10k | 200 / 5k | Hz | Filters inside the feedback loop. |
| Mono | off/on | off | – | Collapses the echo to mono. |
| **Per bus** | | | | |
| Level | −60…+6 | A: 0, B/C/D: −60 | dB | −60 dB is a true off. |
| Mute / Solo | off/on | off | – | Solo exclusive; Mute wins. |

All continuous parameters are smoothed (no zipper noise) and safe to automate.

## Starter recipes

### Lead vocal (the rough template)

1. Bus A: HPF ~80–100 Hz, a dB or two of 10 kHz-ish air via EQ High, FET comp at 4:1 catching 3–5 dB on the loud lines, de-esser on, Sat Drive at the default 6 dB.
2. Push **Bus B** up to around −6 dB under the direct: Peak Reduction ~40–50%, 2–3 dB of low boost at 100 Hz and high boost at 12 kHz. The voice gets body and sheen without EQing the direct path.
3. Push **Bus C** up from the floor until you *feel* it more than hear it (usually −10 to −6 dB under the direct), Drive to taste. The vocal stops disappearing in the choruses.
4. **Bus D**: 100–120 ms, feedback ~10–15%, Mono on, tucked right at the edge of audibility.

### Aggressive vocal (screams, shouted leads)

1. Bus A: HPF higher (~120 Hz), 8:1 ratio, faster attack (~1 ms), de-esser threshold lower — harsh sources hit harder.
2. Bus C becomes the star: fader only a few dB under the direct, Drive 6–12 dB. The mid-forward sidechain keeps the limiting keyed on the scream's core, not on cymbal bleed.
3. Skip Bus B or keep it subtle — opto bloom can soften an aggressive source too much.
4. Bus D with shorter delay (60–80 ms) and the loop LP pulled down to ~3 kHz reads as "size" rather than "echo" at high aggression.

### Drum bus abuse

Parallel compression was born on drums, and Miserere doesn't check what you feed it:

1. Bus A neutral-ish: HPF off or very low, EQ flat, comp threshold high (or at 0 dB for a clean pass), de-esser off, drive 0 — a clean direct path.
2. Bus C up loud: Drive 6+ dB. That is a classic all-buttons parallel drum crush — explosive room energy under an untouched close-mic image.
3. Bus B instead of (or with) C for a rounder, glued kind of thickness: boost 60 Hz low and 10 kHz high into the leveler.
4. Bus D at 60–80 ms with Mono on adds a trashy short room fake. Genuinely wrong, frequently great.

## Tips

- **Set the Direct bus first, alone.** Solo Bus A, make it sound like a good, slightly conservative vocal channel, then *unsolo* and build the parallel blend around it.
- **Use the faders, not the module knobs, for balance.** The whole point of the topology: once each bus sounds right in isolation, mix them like tracks.
- **The Smash bus is meant to sound terrible soloed.** If it sounds good on its own, it's probably not compressed enough to do its job in the blend.
- **Watch the sum.** Three busses of the same vocal add up: 6–10 dB of headroom on Out Trim is normal when everything is up.
- **Zero latency, parallel-safe.** Miserere reports 0 samples of latency, so it is safe anywhere in a chain, including alongside other parallel busses in your DAW.

## Known limitations (v0.1.0)

- The GUI is a functional slider/knob editor (custom vector GUI with per-bus needle meters is milestone M3).
- No factory presets yet (M2).
- Module slots are fixed; swappable alternatives per slot (e.g. a VCA-style option in the compressor slots) are an M2 roadmap item.
- The Tape Sat stage runs without oversampling in M1 — at vocal-level drive amounts its aliasing products are far below the program material; an oversampled upgrade is an M2+ voicing decision.
- Dynamics detection is per-channel (not stereo-linked) on all busses; on a stereo source with a strongly asymmetric image the two channels can compress slightly differently.
