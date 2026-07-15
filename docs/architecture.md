# Architecture

## Signal flow

```mermaid
flowchart LR
    IN[Input] --> XO1[LR4 Crossover<br/>@ Low/Mid Split]
    XO1 -->|low| CL[BandCompressor<br/>Low]
    XO1 -->|mid+high| XO2[LR4 Crossover<br/>@ Mid/High Split]
    XO2 -->|mid| CM[BandCompressor<br/>Mid]
    XO2 -->|high| CH["BandCompressor<br/>High (+ optional Limiter)"]
    CL --> MS[Mute/Solo gate]
    CM --> MS
    CH --> MS
    MS --> SUM[Sum]
    SUM --> OUT_GAIN[Output trim]
    OUT_GAIN --> OUT[Output]
```

Two cascaded 4th-order Linkwitz-Riley (LR4) crossovers split the input into three bands. The first splits Low from the remainder at **Low/Mid Split**; the second splits that remainder into Mid and High at **Mid/High Split**. Each band runs through its own `BandCompressor` (threshold/ratio/attack/release + makeup; the High band additionally carries an optional Limiter stage - see [The High-band limiter option](#the-high-band-limiter-option) below), each band's contribution is then gated by its Mute/Solo state (see [Per-band Mute/Solo](#per-band-mutesolo) below), and the surviving bands are summed back together before a final master **Output** trim. All of this lives in `TriptychEngine` (`src/dsp/TriptychEngine.{h,cpp}`).

## Module map

| Directory | Responsibility |
|---|---|
| `src/dsp` | All audio-thread DSP: `Crossover` (the LR4 wrapper, cascaded twice), `BandCompressor` (per-band threshold/ratio/attack/release/makeup), and `TriptychEngine` (the full 3-band signal chain: crossover cascade, three `BandCompressor`s, sum, output trim). No allocation, locks, or I/O once `prepare()` has run. Independent of `juce::AudioProcessor` so it is directly unit-testable (see `tests/CrossoverTests.cpp`, `tests/BandCompressorTests.cpp`, `tests/EngineTests.cpp`). |
| `src/params` | Parameter layout and `AudioProcessorValueTreeState` definitions - parameter IDs, ranges, defaults. Single source of truth for what a preset captures. |
| `src/PluginProcessor.*` | Host plumbing: APVTS construction, `prepareToPlay`/`processBlock`/`reset`, latency reporting, state save/load. Reads APVTS values and pushes them into `TriptychEngine` every block; does not implement any DSP itself. |
| `src/PluginEditor.*` | A simple, functional v0.1 GUI: a top strip of Low/Mid Split, Mid/High Split and Output knobs, above three per-band columns (Low/Mid/High), each with a Mute/Solo toggle pair above its Threshold/Ratio/Attack/Release/Makeup knobs bound via `SliderAttachment`/`ButtonAttachment`. The High column additionally carries the limiter-enable toggle and threshold knob. A custom vector-drawn GUI is a later milestone. |

Dependency direction is one-way: `PluginEditor` -> `params` (via attachments) and `PluginProcessor` -> `params` + `dsp`. `src/dsp` has no upward dependency on the processor or UI, which is what keeps `TriptychEngine` testable in isolation.

## The crossover cascade and its flat-sum property

`Crossover` (`src/dsp/Crossover.{h,cpp}`) wraps a single `juce::dsp::LinkwitzRileyFilter<float>` and uses its dual-output `processSample(channel, input, outputLow, outputHigh)` overload, which runs one cascaded TPT (topology-preserving transform) state per channel and emits matched low/high outputs from that shared state. Per JUCE's own documentation, this construction guarantees that `outputLow + outputHigh` reconstructs the input with a flat magnitude frequency response (the classic LR4 crossover property) - unlike two independently configured lowpass/highpass filters, which would leave a notch or bump at the crossover point.

`TriptychEngine` cascades two `Crossover` instances:

1. The first splits the input into **Low** and a **Mid+High** remainder at `lowMidSplitHz`.
2. The second splits that remainder into **Mid** and **High** at `midHighSplitHz`.

Because each stage's own low+high sum is flat, the cascade's Low+Mid+High sum is flat too. `TriptychEngine` enforces a minimum runtime separation (`minimumSplitSeparationHz`, 20 Hz) between the two split frequencies so automation can never push the Mid/High split at or below the Low/Mid split, which would invert band order and momentarily break this property.

**Important nuance verified by `tests/EngineTests.cpp`:** the LR4 low+high sum is magnitude-flat but is an *all-pass*, not a pure identity/delay - it has its own real, frequency-dependent phase response (compounded further by cascading two stages). A steady-state single-tone null test therefore has to allow for that phase shift (equivalent, for one frequency, to a small time shift) via a correlation search over a modest alignment window, rather than asserting a raw zero-shift per-sample match. The RMS-level (magnitude-only) null test does not need this accommodation and is the primary flat-sum acceptance gate, matching `tests/CrossoverTests.cpp`'s single-stage version of the same check.

## Per-band compression and the bypass identity

`BandCompressor` (`src/dsp/BandCompressor.{h,cpp}`) wraps `juce::dsp::Compressor<float>` (feed-forward VCA compression driven by a causal ballistics-filter envelope follower) followed by `juce::dsp::Gain<float>` for makeup gain.

`juce::dsp::Compressor::processSample` computes `gain = (env < threshold) ? 1.0 : pow(env * thresholdInverse, ratioInverse - 1.0)`. With `ratio == 1.0`, `ratioInverse - 1.0 == 0`, so `gain == 1.0` unconditionally - independent of threshold or the envelope value. Setting `ratio = 1.0` and `makeup = 0 dB` therefore makes a band an exact, bit-identical bypass of its VCA stage, regardless of what its threshold/attack/release happen to be set to. `TriptychEngine`'s flat-sum null test (`tests/EngineTests.cpp`) relies on exactly this: bypassing all three bands this way isolates the crossover cascade's own flat-sum property from any interaction with actual compression.

Threshold and ratio are smoothed (`juce::SmoothedValue`, linear) and re-applied once per block rather than per sample - `juce::dsp::Compressor` has no ramp of its own for these, so an unsmoothed jump (e.g. a fast GUI drag) would otherwise produce an audible, instantaneous step in the VCA gain curve. This is the same block-rate-recompute compromise used for the crossover split frequencies below. Attack and Release are the envelope follower's own time constants, not audio-rate gain values, so they are applied directly (matching `juce::dsp::Compressor`'s own `setAttack`/`setRelease`, which recompute synchronously).

## The High-band limiter option

`BandCompressor` optionally runs an additional `juce::dsp::Limiter<float>` stage after its compressor + makeup gain (`setLimiterEnabled()`/`setLimiterThresholdDb()`); the type is generic (any band could opt in) but only the High band exposes it via APVTS (`ParamIDs::highLimiterEnabled`, `ParamIDs::highLimiterThreshold`) - a fast safety ceiling for cymbal/harmonic transients that would otherwise need retuning the band's own compressor. `juce::dsp::Limiter` (JUCE 8.0.14, `juce_dsp/widgets/juce_Limiter.h`) is internally two cascaded `juce::dsp::Compressor`s plus a hard clip at exactly &plusmn;1.0 (0 dBFS) - the hard clip is unconditional, independent of the user threshold, which is what `tests/MuteSoloAndLimiterTests.cpp`'s "output never exceeds 0 dBFS" test relies on. Its internal makeup-gain compensation (`outputVolume`) actually *increases* as the threshold is pulled down (a loudness-maximiser design, not a simple peak-catcher), so output RMS is **not** monotonic with threshold - only the final hard clip is a strict guarantee, and the test suite is written around that rather than an incorrect monotonicity assumption.

The stage always runs at full strength, unconditionally, against a preallocated scratch copy of the signal, rather than toggling `juce::dsp::Limiter::process`'s own `context.isBypassed` flag. That was the pre-v0.1.1 approach, and it was wrong: `context.isBypassed` (JUCE 8.0.14, `juce_dsp/widgets/juce_Limiter.h:79-83`, which each of its two internal `Compressor`s also honours per `juce_Compressor.h:85-89`) short-circuits to a plain `copyFrom()` as the *first* statement, skipping the `BallisticsFilter` envelope update entirely - so driving it with `isBypassed` while "disabled" froze the limiter's ballistics instead of keeping them continuous (issue #12). Running the limiter unconditionally into a scratch buffer and only splicing the result back into the real output when enabled keeps the envelope honestly tracking the real signal at all times, so re-enabling resumes gain reduction from a state consistent with the current input - see `tests/BandCompressorTests.cpp`'s "limiter ballistics track input while disabled, not frozen" test. No lookahead/delay line anywhere in `juce::dsp::Limiter`, so this adds zero latency either way.

## Per-band Mute/Solo

Resolved once per block in `TriptychEngine::process()`, *after* each band's `BandCompressor` (including the High band's optional limiter) has already run, and applied only at the summing stage as a plain 0.0f/1.0f multiplier per band (`ParamIDs::lowMute`/`lowSolo`, `midMute`/`midSolo`, `highMute`/`highSolo`). Console-style semantics: **Mute always wins** (a muted-and-soloed band is still silent); if **any** band is soloed, only soloed (and unmuted) bands reach the sum. Each band's own compressor/limiter keeps processing unconditionally regardless of Mute/Solo state, so envelope followers and the limiter's ballistics stay continuous - there is no pop or re-attack transient when a band is unmuted mid-playback. All six parameters default to off, so their addition never changes v0.1's original default behaviour. Because an LR4 crossover's -24 dB/octave rolloff means a muted/non-soloed band's own filter path still leaks a measurable (if strongly attenuated, typically 30+ dB down) amount of an in-band signal into neighbouring bands, `tests/MuteSoloAndLimiterTests.cpp` verifies Mute/Solo via a large relative level drop against an unmuted/unsoloed reference rather than asserting literal digital silence.

## Latency

Both the LR4 crossovers (minimum-phase IIR, no lookahead) and `juce::dsp::Compressor` (a causal envelope follower with no lookahead) add zero latency. `TriptychEngine::getLatencySamples()` is therefore a `static constexpr` `0`, and `TriptychAudioProcessor::prepareToPlay()` reports that via `setLatencySamples(0)`. There is no dry-path delay compensation anywhere in this plugin, unlike, e.g. Overture's oversampled clipper - see `tests/LatencyTests.cpp`. The M1 additions above (Mute/Solo, the High-band limiter) are both zero-latency too (see their sections above), so this remains true after them.

## Parameter smoothing

- **Low/Mid Split** and **Mid/High Split** are crossover cutoff frequencies. Recomputing `LinkwitzRileyFilter` coefficients involves a `tan()` call, so these are not cheap to interpolate per sample; each is smoothed with a `juce::SmoothedValue<float, ValueSmoothingTypes::Multiplicative>` (frequencies are perceived logarithmically) and the cutoff is recomputed once per block from the smoothed value, with the minimum-separation clamp applied after smoothing (see above).
- **Threshold** and **Ratio** (per band) are smoothed the same way (linear smoothing) and re-applied once per block, as described above.
- **Attack**, **Release** are applied directly to `juce::dsp::Compressor`, matching its own synchronous `setAttack`/`setRelease` design.
- **Makeup** (per band) and **Output** (master) are plain gain stages (`juce::dsp::Gain<float>`), which ramp sample-accurately via their own internal `SmoothedValue` (`setRampDurationSeconds`).
- All smoothers are seeded to their real starting value in `prepare()` (`lastLowMidSplitHz`/`lastMidHighSplitHz` in `TriptychEngine`, `lastThresholdDb`/`lastRatio` in `BandCompressor`), so re-preparing (sample-rate change, etc.) never resets a live parameter back to a built-in default or lets a smoother ramp from an invalid starting point (e.g. 0 Hz, or a ratio below the `juce::dsp::Compressor::setRatio` contract of `>= 1.0`).

## Real-time safety

- `TriptychAudioProcessor::processBlock()` starts with `juce::ScopedNoDenormals`.
- All DSP state (crossover filters, compressor envelope followers, gain ramps, and the four intermediate per-band `AudioBuffer`s) is allocated in `prepare()`/`prepareToPlay()` and never reallocated on the audio thread.
- `reset()` clears all filter/envelope/gain-ramp state without deallocating (`TriptychEngine::reset()`, called from both `AudioProcessor::reset()` and internally from `prepare()`).
- Parameter values are read via `apvts.getRawParameterValue()` atomics in `processBlock()`, never via `apvts.getParameter()->getValue()` (not guaranteed lock/allocation-free) and never via `String`-keyed lookups on the audio thread.
- `TriptychEngine::process()` treats a zero-sample block as a safe no-op, and defensively clamps to the per-band buffer capacity established in `prepare()` if a host ever calls `process()` with more samples or channels than it promised via `prepareToPlay()` (`tests/RobustnessTests.cpp` exercises this).
- Crossover cutoff frequencies are clamped below Nyquist (`clampBelowNyquist`, in `TriptychEngine.cpp`) as defensive insurance against invalid `LinkwitzRileyFilter` coefficients at unusually low sample rates, and the Mid/High split is always clamped at least `minimumSplitSeparationHz` above the (possibly still-ramping) Low/Mid split so the two crossovers can never invert order mid-automation.

## Deferred from M1: external sidechain and adjustable crossover slopes

Two items from the M1 "Complete and refine the DSP" issue were deliberately **not** landed in v0.1.0 - both were judged too high-risk to implement safely in this pass, and are left for a follow-up:

- **External sidechain.** Adding a sidechain input bus is a bus-layout change, which is high-risk for `auval`/`pluginval` strictness-10 compliance if `isBusesLayoutSupported` and the no-sidechain-connected case aren't both exactly right. More fundamentally, `juce::dsp::Compressor` has no external-detector input at all - it always derives its envelope from the same signal it processes - so true external-sidechain ducking would need a from-scratch gain-computer/envelope-follower replacing `BandCompressor`'s use of `juce::dsp::Compressor`, not just a new bus.
- **Adjustable crossover slopes.** `juce::dsp::LinkwitzRileyFilter` (JUCE 8.0.14) is documented as fixed at -24 dB/octave (LR4) with no order parameter. A mathematically correct steeper slope (e.g. LR8, -48 dB/octave) is *not* obtainable by simply cascading the existing LR4 crossover with itself at the same cutoff - a true LR-2n crossover is built by squaring an *n*th-order Butterworth prototype once, which is a different transfer function from squaring a 2nd-order Butterworth twice. Implementing this correctly needs a from-scratch higher-order Butterworth-based filter, not a reuse of `LinkwitzRileyFilter` - real risk to the flat-sum invariant this whole plugin's null tests are built around, for a filter design task larger than the rest of M1 combined.

Both remain open as GitHub issue #1 (left open, not closed by the v0.1.0 PR); see that issue's comments for the same reasoning. The **spectrum/GR display** portion of the same issue is a GUI concern, already covered by M3's "Custom GUI / LookAndFeel... Add metering where relevant" issue, and is deferred there rather than duplicated here.
