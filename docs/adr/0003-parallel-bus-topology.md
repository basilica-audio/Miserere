# 3. True parallel busses instead of a serial channel strip

* Status: accepted
* Deciders: Yves Vogl
* Date: 2026-07-15
* Related: [`docs/design-brief.md`](../design-brief.md), [`docs/architecture.md`](../architecture.md)

## Context and Problem Statement

Miserere's goal is the "rough vocal template" workflow: a dense-but-dynamic vocal built by blending differently processed copies of the same source. Two topologies can deliver something like this: a **serial channel strip with per-module wet/dry controls** (the established channel-strip pattern), or **true parallel busses** each with its own fader, summed at the end. Which topology should the plugin be built on?

## Decision Drivers

* The workflow being packaged *is* parallel mixing: independent fader balance between a clean chain, a smashed chain, a leveled chain, and an echo return — with mute/solo per chain, like console busses.
* A per-module wet/dry in a serial chain is not equivalent: each module's "dry" is the *previous module's output*, so the truly clean signal is unrecoverable after the first processing stage, and "more smash" always also means "less of everything upstream".
* Parallel summing is an interference experiment: the topology is only viable if the busses can be guaranteed not to comb against each other at any settings.
* Market differentiation: serial strips with per-module mix knobs exist in quantity; a packaged true-parallel vocal template does not.

## Considered Options

* **True parallel busses** (fan-out → four independent chains → faders → sum)
* **Serial channel strip with per-module wet/dry**

## Decision Outcome

Chosen option: **true parallel busses**, because it is the only topology that actually implements the workflow (independent textures balanced by faders, each derived from the *same* clean source), and its one structural risk — phase coherence at the sum — can be eliminated by a verifiable engineering invariant rather than managed by the user.

### The sample-alignment invariant

Busses A ("Direct"), B ("Opto") and C ("Smash") must remain **sample-aligned with each other at all times**: every module on them is restricted to minimum-phase IIR filtering and causal (zero-lookahead, zero-oversampling, zero-internal-delay) gain computation. No FIR/linear-phase stages, no lookahead limiters, no oversampled nonlinearities on these busses — any of those would introduce a bulk sample offset and turn the sum into a comb filter. The invariant is enforced by test (`tests/NullAndAlignmentTests.cpp`: a neutral A+B+C sum of an impulse is a single aligned impulse with nothing else above −100 dBFS) and consciously constrains future module choices on these busses (e.g. an oversampled saturator upgrade would need per-bus latency compensation *on all other busses* before it could ship).

### Why Bus D is exempt

Bus D ("Slap") is a delay **by design** — its entire musical purpose is to be time-offset, and it outputs the wet echo only. A delay cannot comb against the direct signal in the destructive sense the invariant guards against; it *is* the intentional echo. It still contributes zero *reported latency*, because the delay is the effect, not a compensation artefact to be scheduled away by the host.

### Consequences

* Good, because faders/mute/solo behave exactly like console busses, and each bus's texture is always derived from the clean input.
* Good, because reported latency is 0 unconditionally — the plugin is itself safe inside users' own parallel routings.
* Good, because the invariant is testable and tested, not a convention.
* Bad, because ~4× the DSP of a serial strip runs even when parallel faders are down (accepted: the module set is deliberately lightweight, all-IIR).
* Bad, because the invariant forbids attractive techniques (lookahead limiting, oversampled saturation) on busses A–C without a compensation redesign — documented here so the trade-off is visible when M2+ voicing work revisits it.
