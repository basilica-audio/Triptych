# Triptych — Design Brief v2 (binding; supersedes v1's implicit defaults)

A mastering-grade 3-band multiband compressor: two cascaded LR4 crossovers feeding three
independently voiced compressors, packaged with the ballistics/knee/threshold conventions
the mastering reference class actually uses instead of one generic mixing-bus default
copy-pasted three times. Research-driven rewrite: every changed default below is sourced
(see `triptych-research-notes.md`). **No brand or person names in parameters, UI or
marketing copy** — generic descriptors only; the manual/research notes may cite public
manuals and articles as sources.

## Why v1 falls short (the two core corrections)

1. **v1 has no knee.** `juce::dsp::Compressor`'s gain formula is a hard knee with zero
   transition width, confirmed by reading `src/dsp/BandCompressor.cpp` directly — there is
   no knee parameter anywhere in `ParameterLayout.cpp`. Every reference source researched
   (Weiss DS1-MK3, FabFilter Pro-MB, the Sound on Sound/iZotope technique articles) treats
   a soft, threshold-relative knee as baseline mastering-compressor behavior, not an extra.
   v2 adds a real knee stage in front of each band's gain computer.
2. **v1 gives all three bands one identical, mid-strength default** (threshold -18 dB,
   ratio 4:1, attack 10 ms, release 100 ms, copy-pasted via `addBandParameters`). The
   reference class instead documents **two opposite mastering philosophies** — peak
   control (high threshold, ratio ~5:1) vs. density/knit-together (low threshold ~-30 to
   -35 dB, ratio ~1.1–2:1) — and **per-band ballistics that differ on purpose**: bass wants
   a slower attack and ~2× the mid band's release, high wants ~0.5× the mid band's release.
   v1's uniform default matches neither philosophy and erases the band-position-dependent
   ballistics the whole category is built on. v2 recalibrates each band's default
   independently instead of reusing one shared value three times.

## Topology (mostly unchanged; corrections noted)

```
in → LR4 @ Low/Mid Split → LR4 @ Mid/High Split (cascaded, unchanged, flat-sum preserved)
        │
        ├─→ BandComp Low  : Knee → Threshold/Ratio gain computer → Attack/Release → Makeup
        ├─→ BandComp Mid  : Knee → Threshold/Ratio gain computer → Attack/Release → Makeup
        └─→ BandComp High : Knee → Threshold/Ratio gain computer → Attack/Release → Makeup
                              (+ optional post-stage brickwall Limiter, unchanged)
        → per-band Mute/Solo gate (unchanged, console-style: Mute wins) → Sum → Output trim
```

- Crossover cascade, LR4 slope, zero-latency guarantee, minimum-runtime-separation between
  split points, Mute/Solo semantics, and the High-band optional limiter are **kept as-is**
  — research confirms v1's fixed LR4/-24 dB/oct choice and default split points (200 Hz /
  3000 Hz, inside the researched 200 Hz / 2–2.5 kHz convergence range) are already
  reference-class-appropriate, not a gap. See honesty section for what stays explicitly
  out of scope (adjustable slope, linear/dynamic-phase modes, M/S, external sidechain).
- **New:** a `Knee` parameter per band (0–100%, default 50%), applied as a soft-knee
  transition in the gain computer, threshold-relative in extent (mirroring the
  documented "0 to twice the threshold value" Weiss behavior) rather than a fixed dB
  width — so the knee scales sensibly whether threshold sits near 0 dB or near -50 dB.

## Module specifications (authentic behaviors, generically named, no brand/person names)

### Per-band compressor (Low / Mid / High — three independently-defaulted instances)

- **Threshold** — range unchanged (-60 to 0 dB). **Defaults now differ per band**,
  reflecting the density-vs-peak-control split documented for mastering multiband use:
  - Low: **-24 dB** (moderate — bass buildup control without full density-mode extremity)
  - Mid: **-30 dB** (density/knit-together region — sourced from the SoS "-35 dB" mid
    example, pulled slightly conservative for a safer out-of-box default)
  - High: **-20 dB** (leans toward peak-control — transients above threshold only)
- **Ratio** — range unchanged (1:1–20:1; keeping the wider top end for users who want
  limiting-style behavior, even though the reference class's *default* sits far gentler).
  **Defaults now differ per band**, replacing the uniform 4:1:
  - Low: **2.5:1**
  - Mid: **1.8:1** (sourced from SoS's documented ~1.1:1 mastering-mid value, nudged up
    for a more generally usable out-of-box starting point while staying in "gentle"
    territory)
  - High: **2:1**
- **Knee** *(new parameter)* — 0–100%, default **50%** all bands. 0% = hard knee (v1's
  exact prior behavior, preserved for backward-compatible sound at the extreme); 100% =
  widest soft-knee transition, extent scaled to twice the threshold-to-0dB span (sourced
  from the Weiss DS1-MK3 "0 to twice the threshold value" description).
- **Attack** — range unchanged (0.1–100 ms, log taper). **Defaults now differ per band**
  per SoS's documented per-band ballistics reasoning:
  - Low: **25 ms** (slower — low frequencies lack fast transients; lets the initial
    transient through, matching the Katz/SoS "let the attack go through" reasoning)
  - Mid: **10 ms** (v1's prior uniform default — "set much as you'd set a full-range
    compressor," kept as the mid-band anchor)
  - High: **5 ms** (faster — catch fast transient material)
- **Release** — range unchanged (10–1000 ms, log taper). **Defaults now differ per band**,
  directly implementing SoS's documented ratio (bass ≈2× mid, high ≈0.5× mid) around a
  100 ms mid-band anchor:
  - Low: **180 ms** (≈1.8× mid — close to the documented ≈2× target, avoiding an
    overly sluggish out-of-box low band)
  - Mid: **100 ms** (v1's prior uniform default — kept as the anchor)
  - High: **55 ms** (≈0.5× mid, per SoS)
- **Makeup** — range and default (-12 to +24 dB, 0 dB) unchanged; makeup is a calibration
  trim, not a voicing parameter, and no source material suggested a non-zero default.

### Crossover (Low/Mid Split, Mid/High Split)

- Ranges and defaults **unchanged** (40 Hz–1 kHz / 400 Hz–12 kHz, default 200 Hz /
  3000 Hz) — research confirms these already sit inside the converged reference-class
  starting-point range (≈200 Hz / ≈2–2.5 kHz).

### High-band limiter, Mute/Solo, Output trim

- Unchanged. No sourced reason to alter threshold range/default (-24 to 0 dB, -3 dB), the
  Mute/Solo console semantics, or the -24…+24 dB output trim.

## Factory Presets (proposal for the upcoming M2 preset system)

1. **Neutral Start** — the new v2 defaults above, unchanged, as the shipped "blank slate."
2. **Density Glue** — all-band low-threshold/low-ratio "knit together" preset per the SoS
   density philosophy: thresholds ~-32/-35/-30 dB, ratios ~1.3:1/1.2:1/1.4:1, knee 75%,
   default attack/release kept, 0 dB makeup — designed to be judged only in context, not
   in solo, at very light (1–2 dB) gain reduction.
3. **Peak Control** — high-threshold/higher-ratio "catch the excesses" preset per the SoS
   peak-control philosophy: thresholds ~-10/-8/-8 dB, ratios ~5:1/4:1/4:1, knee 15% (nearly
   hard), fast attack (Low 8 ms/Mid 5 ms/High 3 ms), release near defaults.
4. **Low-End Tighten** — Low band only engaged meaningfully (threshold -20 dB, ratio
   3.5:1, attack 30 ms, release 200 ms), Mid/High left near-transparent (ratio ~1.2:1) —
   a single-band-focused workflow preset, common mastering use case per SoS's "keep it to
   as few bands as possible" guidance.
5. **De-Harsh Highs** — High band only engaged (threshold -22 dB, ratio 3:1, attack 3 ms,
   release 40 ms, knee 60% for smoothness), Low/Mid near-transparent — targets sibilance/
   cymbal buildup without touching low end.
6. **Mastering Safety Ceiling** — all bands near-transparent (ratio ~1.3:1, threshold
   -28 dB), High-band limiter engaged at default -3 dB threshold — a "insurance, not
   sound-shaping" preset for a mix that's already well-balanced.
7. **Parallel-Style Density** *(name generic, no person attribution)* — density-philosophy
   thresholds pushed lower (~-38 dB) with ratio near 1.15:1 across all bands and Output
   trim compensating +1.5 dB, approximating the "very low threshold / very low ratio for
   all-but-quietest signal" density technique documented by SoS, at higher overall GR than
   preset 2 for a more audible (but still musical) effect.
8. **Hard Limiter Ceiling** — High band ratio pushed to 8:1 with knee 0% (hard) and
   High-band limiter engaged at -1 dB, modeling the "occasional excesses" peak-control
   extreme with the limiter as a true backstop.

Presets are settings snapshots over the existing/extended parameter set only — no new DSP
required beyond the Knee addition above.

## Guarantees & tests (Catch2; keep all 39 v1 cases, extend with knee + per-band-default
coverage — target ≥50 total)

1. **Knee null test:** Knee = 0% reproduces v1's exact hard-knee gain curve bit-for-bit
   (regression guarantee — the new knee stage must not change v1's proven bypass-identity
   or existing GR-measurement tests at the 0% extreme).
2. **Knee curve shape:** at Knee = 100%, measure the static gain-reduction curve across a
   sweep of input levels straddling threshold and assert (a) it is continuous (no
   discontinuity/step at threshold, unlike the 0% case), and (b) the transition span
   scales with threshold (verify at two different threshold values that the knee width in
   dB differs, tracking "extent ∝ distance from threshold to 0 dB").
3. **Flat-sum null test (existing):** kept unchanged — bypassing all three bands (ratio
   1:1, makeup 0 dB, any knee value — new: assert knee has zero audible effect when
   ratio == 1:1, since the gain computer's output is identity regardless of knee shape)
   still reconstructs input to within ±0.1 dB RMS across the existing 11 probe frequencies.
4. **Per-band default divergence:** a "defaults differ" smoke test — instantiate three
   fresh `BandCompressor`s at the new per-band defaults, feed identical above-threshold
   program material, and assert the three measured gain-reduction amounts are NOT equal
   (regression guard against a future refactor silently re-uniforming the defaults).
5. **Attack/release ordering guarantee:** with the shipped defaults, measure the 63%
   step-response settling time for Low/Mid/High attack and confirm
   `lowAttack > midAttack > highAttack`; likewise confirm release settling time follows
   `lowRelease > midRelease > highRelease` (encodes the SoS ≈2×/anchor/≈0.5× relationship
   as a standing invariant, not just a one-time default value).
6. **Existing per-band/whole-engine GR tests, Mute/Solo, limiter hard-ceiling, latency==0,
   NaN/Inf robustness, oversized-block chunking, state round-trip:** kept, re-run against
   the new defaults (numeric tolerances updated where the default values changed).
7. **State migration tolerance:** a v1-shaped `ValueTree` (missing the new Knee parameter
   ID entirely) loads without crash or assertion and produces Knee = its declared default
   (50%) rather than 0/garbage — the "tolerant import" contract from the versioning
   section, tested explicitly rather than assumed.
8. **Preset round-trip:** each of the 8 factory presets in this brief loads, produces the
   documented gain-reduction ballpark on a reference signal (loose tolerance, ±3 dB GR
   band), and round-trips through save/load unchanged.

## Honesty & framing

- `triptych-research-notes.md` ships the sourced findings (quotes + URLs) — the voicing
  above is **research-derived, not measured against hardware units**; the manual/CHANGELOG
  must say so explicitly, matching the sibling Miserere v2 brief's framing.
- Sourcing confidence varies: Weiss DS1-MK3 and FabFilter Pro-MB numbers are manufacturer
  documentation (high confidence); Sound on Sound's per-band ballistics are a named
  engineer's technical article, not a controlled measurement (high-confidence workflow
  lore, not lab data); the Bob Katz attack-time figures are search-synthesized rather than
  directly fetched from a primary Katz text and are used only as supporting color, not as
  a standalone sourced default (see research notes' confidence section).
- **Explicitly out of scope for v0.2.0** (do not imply parity with the reference class
  here — these remain open gaps, tracked as issues):
  - External sidechain (already deferred from M1 for the documented `juce::dsp::Compressor`
    structural reason — unchanged).
  - Adjustable crossover slope / linear- or dynamic-phase modes (already deferred from M1
    for the documented LR8-is-not-LR4-squared structural reason — unchanged; the reference
    class's FabFilter-style 6–48 dB/oct adjustable slope and dynamic-phase modes are real
    gaps versus that specific competitor, not versus the category as a whole).
  - Per-band Mid/Side processing (a genuine, newly-identified reference-class gap — Weiss
    DS1-MK3 ships M/S as a headline feature — but implementing it correctly is a
    structural DSP change of comparable risk to the two items above; M2+/M3 candidate).
  - RMS/Peak-selectable detector mode (v1's existing peak-style detector already matches
    the reference class's *default* behavior per Weiss documentation, so this is
    deprioritized rather than urgent).
  - Program-dependent/auto-release (the reference-class flagship's two-stage release is
    approximated here only via differentiated *static* per-band release defaults, not by
    an actual auto-release DSP mode — a real gap, M2+/M3 candidate, tracked as an issue).

## Versioning

Ships as **v0.2.0**. Breaking parameter changes are acceptable pre-1.0 (new Knee
parameter ID added; Threshold/Ratio/Attack/Release defaults change per band — existing
parameter IDs and ranges are otherwise preserved, so this is additive + default-change,
not a rename). State migration = **tolerant import**: old state trees missing the Knee ID
load cleanly at its declared default rather than failing or crashing (see test guarantee
#7). CHANGELOG documents the per-band default recalibration and the new Knee parameter
prominently, with a one-line pointer to `docs/research-notes.md` for the sourcing.
