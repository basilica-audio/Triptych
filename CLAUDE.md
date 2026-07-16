# Triptych — 3-band multiband compressor (mastering)

Per-repo working memory for Claude Code sessions on this plugin. Part of the **Basilica Audio** plugin suite — sacred-architecture DSP for heavy music (`github.com/basilica-audio`).

## What this is
Triptych is the "3-band multiband compressor (mastering)" member of the suite. AU / VST3 / Standalone, JUCE 8.

## Status (v0.2.0 — M2 preset system + deep-dive voicing rework done)
Core DSP reworked for v0.2.0, **73/73 Catch2 tests green** locally. GUI is a functional v0.1/v0.2 slider/toggle editor plus a preset bar (custom LookAndFeel is still roadmap M3). No signing yet (roadmap M4). v0.2.0 shipped: a research-derived soft-knee gain computer replacing the v0.1 hard-knee-only `juce::dsp::Compressor` wrapper (new per-band `Knee` parameter, 0-100%, default 50%), a per-band default recalibration (Threshold/Ratio/Attack/Release now differ across Low/Mid/High instead of one uniform default), the suite's M2 preset system (`src/presets/`, copied from the `basilica-audio/nave` pilot), eight factory presets, and a German frame-string localisation. See `docs/design-brief.md`/`docs/research-notes.md` for the sourced voicing rationale. Open work is tracked in this repo's GitHub **milestones/issues**; M1's issue #1 stays open with a comment - external sidechain and adjustable crossover slopes were deliberately deferred (see `docs/architecture.md`'s "Deferred from M1" section); v0.2.0's own honesty section adds per-band M/S, RMS detector mode, and program-dependent release as further deferred items (see `docs/architecture.md`'s "Deferred from v0.2.0" section).

## DSP
Two cascaded juce::dsp::LinkwitzRileyFilter-based LR4 crossovers (src/dsp/Crossover.h, reused/adapted 1:1 from crypta' proven wrapper) split the signal into Low/Mid/High at LowMidSplit and MidHighSplit; each band runs its own juce::dsp::Compressor + makeup Gain (src/dsp/BandCompressor.h) with threshold/ratio smoothed and re-applied once per block. juce::dsp::Compressor's own gain formula collapses to an exact unity-gain identity at ratio=1.0 regardless of threshold, so ratio=1:1 + makeup=0dB is a proven bit-exact band bypass, which the flat-sum null test in tests/EngineTests.cpp exploits to verify the crossover cascade reconstructs the input to within +-0.1dB RMS across 11 probe frequencies (including both split points). Both the LR4 crossovers (minimum-phase IIR) and the Compressor's causal, lookahead-free envelope follower add zero latency, so TriptychEngine::getLatencySamples() is a static constexpr 0 with no dry-path delay compensation anywhere in the plugin. A separate correlation-based test confirms the LR4 sum's flat-magnitude reconstruction survives even accounting for its real (non-identity) all-pass phase response, and per-band/whole-engine gain-reduction tests confirm bands above threshold measurably compress.

**M1 additions:** per-band Mute/Solo (`ParamIDs::lowMute`/`lowSolo` etc.), applied at the summing stage in `TriptychEngine::processChunk()` as a smoothed 0/1 gain (Mute always wins; soloing isolates soloed bands) - each band's own compressor keeps running underneath regardless, so there's no pop on unmute. A High-band limiter option (`BandCompressor::setLimiterEnabled`/`setLimiterThresholdDb`, generic on the class but only exposed for High via APVTS): an opt-in `juce::dsp::Limiter<float>` stage after the band's compressor+makeup, always run at full strength into a scratch buffer so its ballistics stay genuinely continuous while disabled. Both additions are zero-latency, matching the rest of the engine.

**v0.1.1 fixes (review findings, see `docs/architecture.md` for the full writeup):** the High-band limiter's "no pop on re-enable" guarantee was broken - toggling `juce::dsp::Limiter`'s own `context.isBypassed` (the original approach) actually freezes its internal ballistics while disabled rather than keeping them continuous, since JUCE's own `Limiter`/`Compressor::process()` short-circuit to a copy-through *before* touching the envelope filter when bypassed (issue #12). Mute/Solo's resolved gain is now smoothed (`juce::SmoothedValue`, matching every other real-time scalar in this engine) instead of a bare per-block 0/1 constant, which was its own independent source of click on toggle (issue #13). `TriptychEngine::process()` now chunks any block larger than the prepared per-band buffer capacity into `<=` capacity pieces instead of leaving the excess as unprocessed dry passthrough (issue #14).

**v0.2.0 deep-dive rework (`docs/design-brief.md`, sourced in `docs/research-notes.md`):** `BandCompressor` no longer wraps `juce::dsp::Compressor` (a hard knee with zero transition width, confirmed by reading its JUCE 8.0.14 source); it now drives `juce::dsp::BallisticsFilter` directly (the same envelope-follower class `Compressor` used internally) through `src/dsp/KneeGainComputer.{h,cpp}`, a from-scratch, pure static-transfer-curve gain computer implementing a threshold-relative soft knee (new `lowKnee`/`midKnee`/`highKnee` parameters, 0-100%, default 50% - 0% is bit-for-bit identical to v0.1's hard knee, a tested regression guarantee). Per-band Threshold/Ratio/Attack/Release defaults now differ (Low -24dB/2.5:1/25ms/180ms, Mid -30dB/1.8:1/10ms/100ms, High -20dB/2:1/5ms/55ms), replacing v0.1's single uniform default - encoded as a standing invariant (`lowAttack > midAttack > highAttack`, `lowRelease > midRelease > highRelease`), not just a one-time value, in `tests/VoicingGuaranteesTests.cpp`. State migration is tolerant: a pre-v0.2.0 session missing the new Knee IDs loads cleanly with Knee at its declared default via `AudioProcessorValueTreeState::replaceState()`'s own existing "missing ID keeps current value" behaviour - no special-case code needed.

## Build & test
```sh
export CPM_SOURCE_CACHE="$HOME/.cache/CPM"      # shared JUCE 8.0.14 + Catch2 cache
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target Tests Triptych_Standalone --parallel 4
ctest --test-dir build --output-on-failure
```
Release/universal + pluginval + auval run in CI, not locally.

## Conventions & guardrails
- JUCE 8.0.14 via CPM · C++20 · AGPLv3 · Pamplejuce `SharedCode` pattern · manufacturer `Yvsv`, plugin code `Trpt`, `com.yvesvogl.triptych`.
- **Real-time safety:** no alloc/lock/file-IO/logging on the audio thread; allocate in `prepareToPlay`; `reset()` clears all state; `ScopedNoDenormals`; smoothed params; report latency via `setLatencySamples` where the chain adds any.
- **DryWetMixer gotcha (JUCE 8.0.14):** prime `setWetMixProportion(mix)` before `reset()` in `prepare()` (else it ramps from 100% wet). See sibling `overture`.
- **`main` is protected** — no direct commits; feature branch + PR, green CI required (Conventional Commits). New DSP needs tests (null/reference, NaN/Inf sweep, state round-trip, latency).

## Roadmap
GitHub milestones (M1 DSP & tests · M2 presets/state · M3 GUI & a11y · M4 release/signing/v1.0.0) + issues. Read with `gh issue list --repo basilica-audio/triptych`.

## Suite context
Style references: sibling `basilica-audio/overture` and `basilica-audio/crypta`. The suite: overture, tenebrae, nave, silentium, requiem, seraph, aureate, firmament, triptych, apotheosis, crypta.
