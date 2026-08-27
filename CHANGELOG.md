# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **A factory-preset headroom gate** (`tests/PresetHeadroomTests.cpp`). Every shipped factory
  preset is rendered through the real `AudioProcessor` at 48 kHz against the suite reference
  programme (four plucked notes spanning E1 41.203 Hz to A5 880.000 Hz, twelve harmonics each,
  peak-normalised to −12 dBFS) and its output peak asserted below 0 dBFS. It asserts how many
  factory presets it exercised, so a preset library that stopped loading is distinguishable
  from every preset passing, and it measures **both** ways a user arrives at a preset — a
  restored session and a mid-session click in the preset browser. The recall path is held to
  "below full scale **or** below where you already were", so a transition is blamed only for
  clipping it introduced. All nine presets pass both paths.

### Fixed

- **`Parallel-Style Density` pushed the reference programme to +2.94 dBFS.** The preset is
  built on **upward** compression — all three bands at ratio 0.6:1 below a −38 dB threshold
  with a 10 dB range — which is exactly its intent (raise the quiet material until the mix
  feels dense), and it ships a further +1.5 dB of `Output` on top of it, uncompensated. The
  upward gain is deliberate voicing and is untouched; the missing compensation is not.

  `Output` goes from **+1.50 dB to −1.74 dB**: the preset's own measured overshoot plus a
  −0.3 dBFS headroom target, rounded up to the parameter's 0.01 dB step. Nothing else in the
  preset changed, so the density voicing is bit-for-bit what it was and only the level moved.
  It now renders at **−0.30 dBFS**.

  The other eight presets sit between −1.00 and −12.27 dBFS and are **not raised** — that would
  be level-matching the set, which is a taste question and stays open.

### Changed

- **The suite now presents itself as Basilica Audio in every host.** `COMPANY_NAME` moves from
  `Yves Vogl` to `Basilica Audio`, so Triptych files under the brand in Logic's plugin manager,
  Cubase's vendor column and Reaper's FX browser instead of under a person's name. **Plugin
  identity is untouched** and no session is affected: the VST3 class ID derives from
  `PLUGIN_MANUFACTURER_CODE` + `PLUGIN_CODE` alone (JUCE 8.0.14, `juce_VST3ModuleInfo.h`'s
  `VST3Interface::jucePluginId`) and the Audio Unit triple stays `(aufx, <PLUGIN_CODE>, Yvsv)` -
  both diffed on a real build before and after the change. The bundle ID stays
  `com.yvesvogl.triptych` on purpose, because changing it is what would break existing projects, and
  `COMPANY_COPYRIGHT` still names the copyright holder rather than the trading name. See
  [`docs/branding.md`](docs/branding.md) and basilica-audio/.github ADR 0001.
- **User presets now live under `Basilica Audio`, and the ones you already saved come with them.**
  The folder moves to `~/Library/Audio/Presets/Basilica Audio/Triptych/` (macOS) and
  `%APPDATA%\Basilica Audio\Triptych\Presets\` (Windows). On first launch `PresetManager` copies
  every preset out of the old `Yves Vogl` folder into the new one. It **copies rather than moves**,
  so an older build - or a downgrade - still finds its presets where it left them, and it never
  overwrites a file already present under the new name. Nothing is deleted, ever.
- **Plugin metadata now carries the vendor URL, the copyright string, a real description and
  the VST3 sub-category.** `COMPANY_WEBSITE`, `COMPANY_COPYRIGHT` and `DESCRIPTION` were never
  set, so a shipped bundle carried an empty `NSHumanReadableCopyright`, an empty VST3 vendor
  URL, and an AU `description` that was just the plugin name again; `VST3_CATEGORIES` fell back
  to JUCE's bare `Fx` default, which filed every plugin in the suite under the same
  undifferentiated heading in a VST3 host's browser. Triptych now declares
  `Fx Dynamics` (JUCE 8.0.14, `juce_add_plugin`). **Plugin identity is unchanged** — the VST3 class
  ID is derived from `PLUGIN_MANUFACTURER_CODE` + `PLUGIN_CODE` alone
  (`juce_VST3ModuleInfo.h`'s `VST3Interface::jucePluginId`) and the AU type/subtype/manufacturer
  triple is untouched, so existing sessions keep resolving to the same plugin.

### Fixed

- **Release notes are the changelog again, not a list of PR titles.** `release.yml` now builds the
  release body from this file's section for the tag being released, via the suite-wide
  `basilica-audio/.github/release-notes` action, and appends what a downloader actually needs: what
  each archive contains, the signing status per platform stated accurately (macOS signed, notarised
  and stapled; Windows **not** code-signed, so SmartScreen will warn), the install paths, the AU
  rescan hint, and links to the manual and the product page. A tag whose version has no section in
  this file now fails the release job rather than publishing an empty page.
- **The README no longer tells users the binaries do not exist.** The Installation section
  said *"No pre-built binaries are published yet"* while the banner four lines above it linked
  the Releases page, and the banner in turn described the macOS builds as *"currently
  unsigned"*. Both claims were false. The Installation section now describes the actual
  download-and-copy flow, and the banner states what the release workflow actually produces:
  verified against the shipped `v0.6.0` `.component` with `codesign --verify --strict`
  (`Developer ID Application: Yves Vogl (M5WT732AY5)`), `spctl -a -t open`
  (`source=Notarized Developer ID`) and `stapler validate`.
- **The documented factory-preset count matches what ships** (eight -> nine); `presets/factory/` holds 9.

### Added

- **A `Documentation` section in the README** pointing at the user manual, the factory-preset
  reference, the changelog and the product page — the manual was only reachable from a
  sentence in the middle of the Signal flow section.

## [0.6.0] - 2026-08-20

The M3 GUI release. The interim slider editor becomes the suite's fully
vector-drawn, fully accessible surface, and the v0.5.0 gain-reduction bars
become per-band needle meters. Editor-only: no processor or DSP file is
touched, so the audio path is bit-identical to v0.5.1.

### Added

- **M3 custom vector editor** (issue #4, ported from Miserere's merged M3
  implementation, basilica-audio/miserere PR #31): the interim slider/dropdown
  editor is replaced by the suite's fully vector-drawn black/gold surface —
  pointer knobs with engraved scale rings (choice parameters as detented knobs
  announcing the choice *name*), lamp toggles, EB Garamond typography embedded
  via BinaryData (OFL licensed), and four signal-flow panels: a Global strip
  (crossovers, slope, lookahead, sidechain, mix, output) above the three band
  columns (Low / Mid / High). No photoreal PNG assets; everything is drawn at
  runtime with `juce::Graphics`/`juce::Path`.
- **Per-band gain-reduction needle meters**: the v0.5.0 GR bars become vector
  needle meters (one per band column, compressor + gate reduction combined)
  driven by the engine's existing relaxed-atomic `GainReductionMeter` via a
  30 Hz GUI timer with one-pole ballistics.
- **Accessible parameter surface** (WCAG 2.1 AA): every control keyboard-
  operable (WAI-ARIA stepping: Arrow 1%, Shift+Arrow fine, PageUp/Down 10%,
  Home/End extremes), visible focus rings on all custom-painted controls,
  name/value/role for every knob/toggle/meter (unit-suffixed accessible values,
  read-only meter values), section panels as accessibility focus containers
  (grouped AT navigation without trapping Tab), and WCAG-contrast unit tests
  pinned to the exact rendered colour pairs. New test suites:
  `tests/gui/EditorAccessibilityTests.cpp`, `EditorLayoutTests.cpp`,
  `BasilicaLookAndFeelContrastTests.cpp`, `NeedleMeterTests.cpp`.

## [0.5.1] - 2026-07-31

Race-fix patch release.

### Fixed

- **Data races on the lookahead latency handshake** (PR #34, ThreadSanitizer-confirmed). `prepareToPlay()` (host-chosen thread) and `handleAsyncUpdate()` (JUCE message thread) both called `setLatencySamples()`, which mutates a plain non-atomic member inside `juce::AudioProcessor` itself; `appliedLookaheadSamples`/`preparedSampleRate` were likewise plain members shared across unsynchronized threads. Fixed by serializing the two entry points behind a mutex the audio thread never takes and making the shared members atomic. Red/green-verified under TSan (race reproduces 100% with the fix stashed, zero warnings with it restored). New regression guard: `tests/CrossThreadReprepareTests.cpp`; the allocation guard also gained an elision-safe self-test (this repo had none).

## [0.5.0] - 2026-07-27

**Flagship Dynamics Core.** Twenty-three new parameters, every one of them neutral at its default: a fresh v0.5.0 instance and every migrated v0.4.0 session render **sample-exactly** as they did before (proven by a same-binary A/B against the v0.4.0 chain, not by a tolerance - see `tests/LegacyReferenceChain.h`).

### Added

- **Detector v2** (`src/dsp/Detector.{h,cpp}`), per band:
  - **Detection law** (`lowDetectorMode`/`midDetectorMode`/`highDetectorMode`, choice Peak/RMS, default Peak). RMS runs a mean-square one-pole at `tau_rms = max(Attack, 5 ms)` before the existing ballistics, reading the textbook 3.01 dB below peak detection on a sine.
  - **Program-dependent auto release** (`lowAutoRelease` et al., default off): a dual-time-constant approximation of the classic dual-RC ladder - a fast branch (release 0.15 s) in parallel with a slow reservoir (charge 0.6 s, release 4 s), combined by maximum. A brief peak recovers on the fast branch; sustained gain reduction grows a multi-second tail. The Release knob scales both branch release constants, so the 300 ms region reproduces the reference constants exactly.
  - **Per-band character** (`lowCharacter` et al., choice Clean/VCA, default Clean): a static approximation of a feedback compressor's loop, giving a ratio-dependent emergent soft knee (6 dB at 2:1, 4 dB at 4:1, 3 dB at 10:1, log-interpolated) and a ratio-scaled effective attack (`tau / (1 + k)`, `k = ratio - 1`), so higher ratios reach their gain reduction sooner. Deliberately **no** nonlinearity stage - the modelled behaviour is envelope behaviour, not distortion, so no oversampling is implied.
  - **Variable stereo link** (`lowStereoLink` et al., 0-100%, default 0%): a max-law blend applied to the detector *inputs*, so at 100% both channels integrate the same value sequence and their envelopes are bit-identical - a hard-panned transient can no longer shift the stereo image.
- **Lookahead** (`lookahead`, choice Off/1.5/3/5 ms, default Off), reported to the host as exact integer latency and re-reported from the message thread. With it engaged, the High band's optional brickwall becomes a **true lookahead limiter** whose zero-overshoot property is proven in `src/dsp/Lookahead.h` and asserted over 10,000 randomised signals per setting. Lookahead Off keeps the legacy `juce::dsp::Limiter` path, sample for sample, so every existing session is untouched.
- **External sidechain** (issue #1, part 1): a disabled-by-default stereo sidechain bus, `scSource` (Internal/External) and `scListen` (Off/Low/Mid/High). The key is split by its own crossover pair at the same frequencies and slope, so every band's detector follows a band-matched key rather than the full-range sidechain. Selecting External with no sidechain connected falls back to Internal sample-exactly. `scListen` monitors a band's **detector key** - deliberately not the same thing as soloing that band's audio.
- **Selectable crossover slopes** (issue #1, part 2): `crossoverSlope`, choice 12/24/48 dB/oct, default 24 dB/oct (the v0.1-v0.4 LR4 path, byte-untouched). Applies to both split points and to the sidechain's crossover pair.
- **Global Mix** (`mix`, 0-100%, default 100%): a dry/wet blend around the whole multiband chain, wet-latency compensated so the dry path is delayed by exactly the lookahead length. Structurally bypassed at the neutral operating point rather than passed through at unity.
- **Gate hold and hysteresis** (`lowGateHold`/`lowGateHysteresis` et al., 0-500 ms and 0-12 dB, both default 0). Both are specified so every discontinuity is routed through an existing smoother: hold lives in the envelope domain as a shadow held envelope that decays with the gate's own release coefficient, and the effective threshold rides the existing 50 ms smoother. The applied gate gain never steps more than 0.5 dB per sample, asserted across hold expiry and every hysteresis transition including the worst case.
- **Per-band gain-reduction metering**: three thin vertical GR bars in the editor, fed by relaxed atomics written once per block.
- **State schema versioning**: `getStateInformation` now stamps `stateVersion="5"` on the APVTS root. No 4 -> 5 transform is needed (the additions are purely additive and neutral), but the forward-migration hook now exists; an absent attribute means v0.4.0 or older.
- One new factory preset, **Glue Master** - VCA character, auto release, RMS detection, 100% link, 90% mix, 1.5 ms lookahead.
- Test suite broadened from 105 to 155 cases, including `tests/AllocationGuard.{h,cpp}` (ported from sibling plugin aureate): the audio thread is now gated against heap allocation with the full feature matrix engaged.

### Changed

- **Two factory presets deliberately revoiced** (the other six sound exactly as they did): **Mastering Safety Ceiling** gains 1.5 ms lookahead plus the high-band brickwall - which is what finally makes its name honest - along with 100% stereo link and RMS detection; **Density Glue** gains VCA character, auto release and 80% link on every band. See `docs/presets.md`.
- `TriptychEngine::getLatencySamples()` is no longer a static `constexpr 0`. It returns 0 while lookahead is Off - the invariant every pre-v0.5.0 session relies on, still asserted - and otherwise the exact lookahead length in samples.
- The plugin now declares a second (sidechain) input bus. A `BusesLayout` passed to `setBusesLayout()` must describe both input buses; the sidechain may be disabled, mono or stereo, and is never required.

### Notes

- All twenty-three new parameters use JUCE's `versionHint` **2** and are declared as one appended block after the fifty-nine shipped parameters. AUv2 (and therefore Logic) identifies automation lanes by parameter *index*, so this is what keeps v0.4.0 session automation working; a regression test pins the first 59 indices to their exact v0.4.0 IDs and order.
- Deferred to v0.6.0: linear-phase crossover mode, low-branch all-pass compensation, per-band sidechain EQ, per-band mix, and the saturation/oversampling character stage. See `docs/architecture.md`.

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
