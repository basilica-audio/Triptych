# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.1] - 2026-07-16

### Changed

- Housekeeping: canonical squircle icon cutout embedded into the plugin binary (`ICON_BIG`) and README/manual, org link sweep, heavy-music copy reframe, README pointed at GitHub Releases, and the signed tag-triggered release CI workflow added.

### Fixed

- **BandCompressor**: the High-band limiter's documented "no pop on re-enable" guarantee was broken - toggling `juce::dsp::Limiter`'s own `context.isBypassed` flag while disabled actually froze its internal ballistics (JUCE 8.0.14's `Limiter`/`Compressor::process()` skip the envelope-filter update entirely when bypassed) instead of keeping them continuous, so re-enabling could resume gain reduction from a stale, pre-disable envelope state rather than one consistent with the current input. The limiter now always runs at full strength into a preallocated scratch buffer, splicing the limited result back into the output only when enabled (#12).
- **TriptychEngine**: a host-supplied block larger than the capacity established in `prepareToPlay()` left the excess samples as unprocessed dry passthrough (bypassing every processing stage, including the master Output trim) instead of erroring or being fully processed. `process()` now chunks any oversized block into `<=`-capacity pieces, each run through the full signal chain (#14).
- **TriptychEngine**: per-band Mute/Solo resolved to a bare block-rate 0.0f/1.0f gain multiplier, producing an audible click on toggle mid-playback from the hard step discontinuity at the block boundary. The resolved gain is now smoothed via `juce::SmoothedValue` (#13).

## [0.1.0] - 2026-07-14

### Added

- Project bootstrap: README, license, contributing guide, architecture and build docs, ADRs, and CI workflow.
- DSP core: initial working Triptych signal path (two cascaded LR4 crossovers, three independent band compressors, output trim) with unit tests.
- **Per-band Mute/Solo** (Low/Mid/High), resolved at the summing stage in `TriptychEngine`: console-style semantics (Mute always wins; soloing isolates soloed bands), all defaulting to off. Each band's own compressor keeps running underneath regardless of Mute/Solo state, so there is no re-attack pop when a band is unmuted mid-playback.
- **High-band limiter option**: an opt-in `juce::dsp::Limiter` stage after the High band's compressor + makeup gain (`ParamIDs::highLimiterEnabled`, default off; `ParamIDs::highLimiterThreshold`, -24 to 0 dB, default -3 dB), guaranteeing the High band never exceeds 0 dBFS once engaged. Zero added latency. The underlying `BandCompressor::setLimiterEnabled`/`setLimiterThresholdDb` support is generic (any band could opt in), though only the High band currently exposes it via APVTS.
- `docs/manual.md`: full user manual (what Triptych is, where it sits in a symphonic-metal chain, signal flow, complete parameter reference, usage tips).
- Editor controls for the new Mute/Solo toggles (every band) and the High-band limiter enable toggle + threshold knob, so every automatable parameter has a working v0.1 control.
- Broadened Catch2 suite (22 → 39 test cases): per-band Mute/Solo isolation and "Mute wins over Solo" tests, High-band limiter hard-clip and threshold-sweep coverage, sample-rate sweep (44.1-192 kHz), mono/stereo/rejected bus-layout coverage, long-run (several-second) NaN/Inf stability, a dedicated bool-parameter state round-trip test, and rapid Mute/Solo/Limiter automation coverage.

### Deferred

- **External sidechain** and **adjustable crossover slopes** from the M1 "Complete and refine the DSP" issue were deliberately not implemented in v0.1.0 - both were judged too high-risk to land safely in this pass (sidechain needs a from-scratch gain-computer/envelope-follower plus a bus-layout change; adjustable slopes beyond LR4 need a from-scratch higher-order Butterworth-based filter, not a reuse of `juce::dsp::LinkwitzRileyFilter`). See `docs/architecture.md`'s "Deferred from M1" section for the full reasoning; the GitHub issue stays open.

### Changed

- `docs/architecture.md` and `README.md` updated to describe the full v0.1.0 signal path (Mute/Solo gate, optional High-band limiter) and parameter table.
