# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.4.0] - 2026-07-23

### Added

- **Downward expansion / gating** (GitHub issue #25): an independent, per-band noise-gate/expander stage with its own Threshold (`lowGateThreshold`/`midGateThreshold`/`highGateThreshold`, -80 to 0 dB), Ratio (`lowGateRatio` et al., 1:1-100:1, default 2:1), Attack (`lowGateAttack` et al., 0.1-50 ms), and Release (`lowGateRelease` et al., 10-2000 ms), plus a per-band `xGateEnabled` toggle (default off). Reuses the existing `juce::dsp::BallisticsFilter`-based detector topology (a second, independently configured instance) rather than a structurally different detection method, and is keyed off the same pre-compression input sample as the band's own compressor, so gating a band is never masked by, or interacting with, that band's own compression curve. See `docs/architecture.md`'s "Downward expansion / gating (v0.4.0)" section and `src/dsp/GateGainComputer.h` for the sourced transfer-curve model (the standard downward expander).
- Editor: a `Gate On` toggle plus Gate Threshold/Ratio/Attack/Release knobs added to every band's control column.
- All eight factory presets gain the fifteen new Gate parameter keys at their neutral (off) defaults - none of the eight presets engage gating.
- **Per-band Mid/Side processing** (GitHub issue #24): a per-band `M/S Enabled` toggle (`lowMidSideEnabled` et al., default off) encodes that band's stereo signal to Mid/Side (equal-power, exactly-invertible transform - `src/dsp/MidSideCodec.h`) before its gain computation and decodes back after it. The band's existing Threshold/Ratio continue to drive the Mid (centre) component; Side gets its own independent Threshold/Ratio (`lowSideThreshold`/`lowSideRatio` et al., defaulting to the band's own Threshold default / 1:1 bypass respectively), sharing Knee/Attack/Release/Range with Mid. A defensive no-op on any bus that isn't exactly 2 channels. Because `L + R` after decode depends algebraically only on Mid, processing Side - however aggressively - can never introduce a phase-cancellation artifact into a mono downmix. See `docs/architecture.md`'s "Per-band Mid/Side processing (v0.4.0)" section.
- Editor: an `M/S On` toggle plus Side Threshold/Ratio knobs added to every band's control column.
- All eight factory presets gain the nine new M/S parameter keys at their neutral (off, Side Ratio 1:1) defaults - none of the eight presets engage M/S.
- Test suite broadened from 84 to 105 test cases: pure transfer-curve coverage for the gate's closed-form expander formula (`tests/GateGainComputerTests.cpp`), pure encode/decode transform coverage (`tests/MidSideCodecTests.cpp`, including a dedicated mono-compatibility proof), real-audio gain-reduction/regression/bypass-identity/L-R-passthrough/round-trip/mono-sum-independent-of-Side coverage (`tests/BandCompressorTests.cpp`), two v0.3.0-to-v0.4.0 state migration-tolerance tests (Gate and M/S), a per-band Gate default-ordering regression guarantee, a mono-bus-is-a-no-op regression test, and updated parameter-count/round-trip coverage.

## [0.3.0] - 2026-07-17

### Added

- **Ratio extended below 1:1 (upward compression/expansion)** on every band, widening the range from **1:1-20:1** to **0.2:1-20:1**. Values below 1:1 boost signal above threshold instead of cutting it - the same closed-form transfer curve v0.2.0 already used for downward compression, evaluated on the other side of the exact `ratio == 1.0` null point instead of a different formula or a naive inversion (sourced lower bound: Weiss DS1-MK3's documented "adjustable from 1000:1 to 1:5", i.e. 0.2, for "upward expansion (for over-compressed signals)"). `ratio == 1.0` is a bit-exact null, special-cased independent of Knee/Range - not just floating-point-close. See `docs/design-brief-v3-dynamics.md`.
- **Range** (`lowRange`/`midRange`/`highRange`, new per-band parameters, 0-30 dB, default 12 dB, plus `lowRangeEnabled`/`midRangeEnabled`/`highRangeEnabled`, default off): an optional maximum gain-change clamp, bounding a band's cut *or* boost to at most `Range` dB - the reference-class safety valve (FabFilter Pro-MB's "Range knob limits the maximum amount of applied gain change") that makes an aggressive Ratio setting, especially the new upward regime, usable instead of a runaway. Off by default, routing `KneeGainComputer` to an internal `unlimitedRangeDb` (500 dB) sentinel comfortably outside any realistic single-pass operating range, so a band whose Range API is never touched reproduces v0.2.0's behaviour exactly.
- Editor: a `Range On` toggle + `Range` knob added to every band's control column.
- Two factory presets extended (the other six unchanged) to showcase the new capability: **Density Glue**'s Mid band now uses genuine upward compression (0.7:1, was 1.2:1 downward) with Range engaged on all three bands at 8 dB; **Parallel-Style Density** pushes all three bands to upward compression (0.6:1, was ~1.15:1 downward) with Range engaged at 10 dB, without which its deep (-38 dB) threshold combined with a strongly upward ratio could push full-scale peaks far louder than musically useful. See `docs/presets.md`.
- Test suite broadened from 73 to 84 test cases: a measurable proof of the upward-compression transfer curve against its closed-form expected dB value, Range-clamp assertions at both the pure-math and real-audio levels, a bit-exact `ratio == 1.0` null test, a v0.2.0-to-v0.3.0 state migration-tolerance test, a dedicated "fresh v0.3.0 instance is bit-identical to v0.2.0 defaults" test spanning parameter values/Range neutrality/processed audio, and NaN/Inf coverage extended to the new Ratio floor and Range extremes.

### Changed

- `KneeGainComputer::computeStaticGainReductionDb()`/`computeGainLinear()` gained an optional trailing `rangeDb` parameter (default: the unbounded sentinel, preserving every existing call site's behaviour unchanged) and no longer treat `ratio <= 1.0` as a forced bypass - only `ratio == 1.0` exactly is a null point now.
- `docs/manual.md`, `docs/presets.md`, and `docs/architecture.md` updated for the Ratio range widening and the new Range parameter; `docs/design-brief-v3-dynamics.md` added, and `docs/research-notes.md` gained a v0.3.0 addendum (the sourced rationale, and the explicit accounting of what is and isn't parity with the FabFilter Pro-MB reference point).

### Deferred

- **Downward expansion (gating)** was considered and deliberately left unimplemented - a clean per-band gate needs a second, independent threshold that the existing single-threshold Ratio/Range model doesn't cleanly accommodate. **Per-band Mid/Side processing** (named as a v0.2.0 gap) remains open and now has its own tracking issue. See `docs/architecture.md`'s "Deferred from v0.3.0" section.

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
