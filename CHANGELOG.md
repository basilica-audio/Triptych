# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0] - 2026-07-16

### Added

- **Soft knee** (`Knee`, new per-band parameter, 0-100%, default 50%): v0.1 wrapped `juce::dsp::Compressor` directly, whose gain formula is a hard knee with zero transition width - no knee parameter existed at all. v0.2.0 replaces that wrapper with a from-scratch, knee-aware gain computer (`src/dsp/KneeGainComputer.{h,cpp}`) driven by the same `juce::dsp::BallisticsFilter` envelope follower, using the standard quadratic soft-knee interpolation (Giannoulis/Massberg/Reiss, JAES 2012) with the knee's extent scaled threshold-relatively (0% = v0.1's exact hard knee, bit-for-bit preserved as a regression guarantee; 100% = the Weiss DS1-MK3 manual's documented "0 to twice the threshold value" span). See `docs/design-brief.md` and `docs/research-notes.md`.
- **Research-derived per-band default recalibration**: v0.1 gave Low/Mid/High one identical, uniform default (threshold -18 dB, ratio 4:1, attack 10 ms, release 100 ms). Research into the mastering multiband-compressor reference class (Weiss DS1-MK3, FabFilter Pro-MB, Sound on Sound's "Multi-band Compression" technique article) documented two opposite mastering philosophies (peak control vs. density/knit-together) and band-position-dependent ballistics that v0.1's uniform default matched neither of. New defaults: Low -24 dB/2.5:1/25 ms/180 ms, Mid -30 dB/1.8:1/10 ms/100 ms (the v0.1 anchor), High -20 dB/2:1/5 ms/55 ms - implementing the documented bass≈2×mid/high≈0.5×mid release ratio and slower-bass/faster-high attack ballistics as a standing invariant, not just one-time values. **This voicing is research-derived, sourced from published manuals and technique articles, not measured against reference hardware** - see `docs/research-notes.md`'s confidence notes.
- **M2 preset system** (`src/presets/`, copied from the `basilica-audio/nave` pilot implementation - `.scaffold/specs/preset-system-m2.md`): a `PresetBar` strip at the top of the editor (`[<] [PresetName*] [>] [Save] [Save As...] [Delete] [Import...] [Export...]`) backed by `PresetManager` - factory presets (embedded via BinaryData), user presets (`~/Library/Audio/Presets/Yves Vogl/Triptych/` on macOS, `%APPDATA%\Yves Vogl\Triptych\Presets\` on Windows), a settable default, single-file and zip-bank import/export, and dirty-state tracking.
- **Eight factory presets** (`presets/factory/*.json`, see `docs/presets.md`): Default, Density Glue, Peak Control, Low-End Tighten, De-Harsh Highs, Mastering Safety Ceiling, Parallel-Style Density, and Hard Limiter Ceiling - covering both the peak-control and density mastering philosophies plus single-band-focused workflows.
- **German localisation** of the M2 preset bar's frame strings (`resources/i18n/de.txt`), selected automatically for `de*` system languages. Parameter names, units, and other DSP terminology are never translated, matching the rest of the suite.
- Editor: a `Knee` knob added to every band's control column, and the preset bar docked at the top of the window.
- Test suite broadened from 42 to cover the new Knee stage (null test against v0.1's exact hard-knee formula, curve-shape/continuity assertions, per-band-default-divergence and attack/release-ordering regression guarantees), the M2 preset system (save/load round-trip, forward/backward-compat import, factory preset validation, default resolution, dirty-flag lifecycle, prev/next traversal, bank export/import), a v0.1-state migration-tolerance test (a state missing the new Knee IDs loads cleanly with Knee at its declared default), and i18n coverage (every preset-bar translation key present, no parameter names leaked into the German mapping).

### Changed

- `BandCompressor` no longer wraps `juce::dsp::Compressor`; it now drives `juce::dsp::BallisticsFilter` (the same envelope-follower class `Compressor` used internally) through `KneeGainComputer`'s knee-aware gain computation. Behaviourally identical to v0.1 at `Knee = 0%`.
- `docs/manual.md` and `docs/architecture.md` updated for the new Knee parameter, per-band defaults, the preset system, and the i18n frame; `docs/design-brief.md`/`docs/research-notes.md` added (the sourced rationale behind every changed default).

### Deferred

- Per-band Mid/Side processing, an RMS/Peak-selectable detector mode, and program-dependent/auto-release were identified in this pass's research as further reference-class gaps but deliberately left unimplemented - see `docs/architecture.md`'s "Deferred from v0.2.0" section and `docs/design-brief.md`'s honesty section.

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
