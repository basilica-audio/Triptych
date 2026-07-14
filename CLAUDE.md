# Triptych — 3-band multiband compressor (mastering)

Per-repo working memory for Claude Code sessions on this plugin. Part of the **Metal up your ass** symphonic-metal plugin suite (`github.com/metal-up-your-ass`).

## What this is
Triptych is the "3-band multiband compressor (mastering)" member of the suite. AU / VST3 / Standalone, JUCE 8.

## Status (v0.1 — bootstrap complete)
Core DSP working, **22 Catch2 tests green**, CI (macOS + Windows, pluginval strictness 10 + auval) green. GUI is a functional v0.1 slider editor (custom LookAndFeel is roadmap M3). No signing yet (roadmap M4). Open work is tracked in this repo's GitHub **milestones/issues**.

## DSP
Two cascaded juce::dsp::LinkwitzRileyFilter-based LR4 crossovers (src/dsp/Crossover.h, reused/adapted 1:1 from twist-your-guts' proven wrapper) split the signal into Low/Mid/High at LowMidSplit and MidHighSplit; each band runs its own juce::dsp::Compressor + makeup Gain (src/dsp/BandCompressor.h) with threshold/ratio smoothed and re-applied once per block. juce::dsp::Compressor's own gain formula collapses to an exact unity-gain identity at ratio=1.0 regardless of threshold, so ratio=1:1 + makeup=0dB is a proven bit-exact band bypass, which the flat-sum null test in tests/EngineTests.cpp exploits to verify the crossover cascade reconstructs the input to within +-0.1dB RMS across 11 probe frequencies (including both split points). Both the LR4 crossovers (minimum-phase IIR) and the Compressor's causal, lookahead-free envelope follower add zero latency, so TriptychEngine::getLatencySamples() is a static constexpr 0 with no dry-path delay compensation anywhere in the plugin. A separate correlation-based test confirms the LR4 sum's flat-magnitude reconstruction survives even accounting for its real (non-identity) all-pass phase response, and per-band/whole-engine gain-reduction tests confirm bands above threshold measurably compress.

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
GitHub milestones (M1 DSP & tests · M2 presets/state · M3 GUI & a11y · M4 release/signing/v1.0.0) + issues. Read with `gh issue list --repo metal-up-your-ass/triptych`.

## Suite context
Style references: sibling `metal-up-your-ass/overture` and `metal-up-your-ass/twist-your-guts`. The suite: overture, tenebrae, nave, silentium, requiem, seraph, aureate, firmament, triptych, apotheosis, twist-your-guts.
