# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] — 2026-07-15

### Added

- Project bootstrap: README, license, contributing guide, architecture and build docs, ADRs, and CI workflow.
- M1 DSP core: the complete four-bus parallel vocal chain — Bus A "Direct" (HPF, console-style 3-band EQ, FET compressor with 4:1/8:1 ratios and GR metering, split-band de-esser, level-compensated tape-style saturation), Bus B "Opto" (boost-only passive-style EQ, opto leveler with program-dependent two-stage release, post-leveler air shelf), Bus C "Smash" (all-buttons FET limiter: ~20:1, mid-forward sidechain tilt, program-dependent release shortening, drive), Bus D "Slap" (60–180 ms fractional delay with filtered, tape-soft-saturated feedback loop and mono switch) — with per-bus fader/mute/exclusive-solo and global in/out trims plus bypass.
- Sample-aligned parallel summing on busses A–C (minimum-phase IIR, zero lookahead) with zero reported latency; Bus D is a delay by design (ADR 0003).
- APVTS parameter layout with frozen IDs, full state save/recall, and a functional v0.1 slider/combo/toggle editor.
- 65 Catch2 test cases covering all ten M1 guarantee categories: neutral-path null (≤ −120 dBFS), parallel impulse alignment, FET static curves, opto two-stage release, de-esser band selectivity, smash stability, slap timing/feedback stability, NaN/Inf recovery, oversized-block chunking, and state/latency/mute-solo contracts.
