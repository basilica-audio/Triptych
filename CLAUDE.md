# Triptych — 3-band multiband compressor (mastering)

Per-repo working memory for Claude Code sessions on this plugin. Part of the **Basilica Audio** plugin suite — sacred-architecture DSP for heavy music (`github.com/basilica-audio`).

## What this is
Triptych is the "3-band multiband compressor (mastering)" member of the suite. AU / VST3 / Standalone, JUCE 8.

## Status (v0.1.0 — M1 DSP completion & test coverage done)
Core DSP complete for v0.1.0, **39/39 Catch2 tests green** locally. GUI is a functional v0.1 slider/toggle editor covering every parameter (custom LookAndFeel is roadmap M3). No signing yet (roadmap M4). Open work is tracked in this repo's GitHub **milestones/issues**; M1's issue #1 stays open with a comment - external sidechain and adjustable crossover slopes were deliberately deferred (see `docs/architecture.md`'s "Deferred from M1" section).

## DSP
Two cascaded juce::dsp::LinkwitzRileyFilter-based LR4 crossovers (src/dsp/Crossover.h, reused/adapted 1:1 from crypta' proven wrapper) split the signal into Low/Mid/High at LowMidSplit and MidHighSplit; each band runs its own juce::dsp::Compressor + makeup Gain (src/dsp/BandCompressor.h) with threshold/ratio smoothed and re-applied once per block. juce::dsp::Compressor's own gain formula collapses to an exact unity-gain identity at ratio=1.0 regardless of threshold, so ratio=1:1 + makeup=0dB is a proven bit-exact band bypass, which the flat-sum null test in tests/EngineTests.cpp exploits to verify the crossover cascade reconstructs the input to within +-0.1dB RMS across 11 probe frequencies (including both split points). Both the LR4 crossovers (minimum-phase IIR) and the Compressor's causal, lookahead-free envelope follower add zero latency, so TriptychEngine::getLatencySamples() is a static constexpr 0 with no dry-path delay compensation anywhere in the plugin. A separate correlation-based test confirms the LR4 sum's flat-magnitude reconstruction survives even accounting for its real (non-identity) all-pass phase response, and per-band/whole-engine gain-reduction tests confirm bands above threshold measurably compress.

**M1 additions:** per-band Mute/Solo (`ParamIDs::lowMute`/`lowSolo` etc.), resolved once per block at the summing stage in `TriptychEngine::process()` as a plain 0/1 gain (Mute always wins; soloing isolates soloed bands) - each band's own compressor keeps running underneath regardless, so there's no pop on unmute. A High-band limiter option (`BandCompressor::setLimiterEnabled`/`setLimiterThresholdDb`, generic on the class but only exposed for High via APVTS): an opt-in `juce::dsp::Limiter<float>` stage after the band's compressor+makeup, always run with `context.isBypassed` toggled (cheap, real-time-safe, keeps its ballistics continuous) rather than skipped in code. Both additions are zero-latency, matching the rest of the engine.

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
