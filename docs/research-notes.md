# Triptych — Research Notes (deep-dive for v0.2.0 design brief)

Reference class for a "mastering-grade 3-band multiband compressor": the units that
define what "authentically voiced" means in this category. Prioritized primary-ish
sources: manufacturer manuals/help pages, one mastering-engineer-authored technical
article (Sound on Sound), one branded-but-technical explainer (iZotope), and forum
engineering discussion for the linear-phase-vs-minimum-phase tradeoff. Hardware not
directly measured — see honesty section in the brief.

## Reference units identified

1. **Weiss DS1-MK3** — the canonical mastering-grade band-selective compressor/limiter
   (hardware DS1, now also a Softube plugin). Industry reference point ("the Weiss") for
   what a mastering compressor's ballistics and ratio range look like.
2. **FabFilter Pro-MB** — modern software reference for full parameter transparency
   (per-band knee, lookahead, adjustable crossover slope, program-dependent attack/release
   framing).
3. **Waves C6** — reference for the "Range" paradigm (replacing raw ratio with a maximum
   gain-change amount) and linked global attack/release, widely taught in mixing/mastering
   tutorials.
4. **Sound on Sound "Multi-band Compression" technique article** — not a product manual,
   but a mastering-engineer-authored technical article with concrete numeric starting
   points and per-band ballistics reasoning; treated as closest available proxy for
   documented "workflow lore."

## Crossover point conventions

- "A common starting point for a 3-band setup is around 200 Hz for Low-to-Mid and around
  2 kHz for Mid-to-High." Also: "a three-band compressor that operates independently below
  200 Hz, from 200–2000Hz, and above 2 kHz." — search synthesis citing multiple
  mixing/mastering tutorials (LANDR, adrianmilea.com, ledgernote), converged number.
  https://blog.landr.com/multiband-compression/ (via search synthesis)
- Sound on Sound: "Low crossover: 120Hz (below vocal range, above deep bass/kick)... High
  crossover: 2.5kHz minimum; 6kHz+ for subtle high-end work." Also explicitly warns:
  "Setting a crossover point in the middle of the vocal range can mess up the vocal sound."
  https://www.soundonsound.com/techniques/multi-band-compression-tips
- **v1 verdict:** Triptych's existing defaults (Low/Mid 200 Hz, Mid/High 3000 Hz) already
  sit inside this converged range — this is a place v1 got it right by instinct, not a
  gap. Keep the defaults; the *ranges* (40 Hz–1 kHz / 400 Hz–12 kHz) are also sane relative
  to what the reference class calls "starting points, adjust from there."

## Ratio, threshold, knee — the core authenticity gap

- iZotope-branded but technically concrete: "a 2:1 ratio, 100-ms attack and release, and a
  soft knee" as the universal starting point across bands, then "pull down the threshold
  until you start to see a little gain reduction in one or two of the bands" — i.e.
  threshold is found empirically per mix, not hard-coded; ratio starts conservative.
  https://www.izotope.com/en/learn/multiband-compression-basics-izotope-mastering-tips
- Sound on Sound documents **two distinct mastering-multiband philosophies**, both far
  from a single uniform default:
  1. **Peak control:** "Higher threshold ('enough already!' level) with higher ratios
     (e.g., 5:1) for occasional excesses."
  2. **Density enhancement:** "Low threshold (-30 to -40dB) with very low ratios
     (typically <1.2:1) for all-but-quietest signals." For the mid band specifically: "a
     ratio of 1.1:1 and a threshold of -35dB" as a "subtle knit-together approach."
  https://www.soundonsound.com/techniques/multi-band-compression-tips
- Weiss DS1-MK3: ratio "adjustable from 1000:1 to 1:5, allowing every kind of dynamic
  processing, from limiting to upward expansion (for over-compressed signals)" — the
  *extremity* of the range at both ends (near-brickwall AND expansion) is itself a
  documented design trait of the reference-class flagship, not just "1:1 to 20:1."
  https://weiss.ch/products/pro-audio/ds1-mk3/ ,
  https://www.softube.com/us/user-manuals/weiss-ds1-mk3
- Weiss DS1-MK3 soft-knee: "'Soft-knee' ranges from 0 to 1.0 maximum, reaching from 0dBFS
  down to twice the threshold value" — i.e. knee is a first-class, continuously variable
  parameter, not a binary toggle, and its extent is defined relative to threshold (not a
  fixed dB width). https://www.softube.com/us/user-manuals/weiss-ds1-mk3
- FabFilter Pro-MB: "The Knee sets the type of knee (hard or soft) for smoother dynamic
  effects. A soft knee setting causes it to react more gradually around the threshold."
  https://www.fabfilter.com/help/pro-mb/using/basicbandcontrols
- **v1 verdict — this is the single biggest gap.** `juce::dsp::Compressor`'s gain formula
  (`gain = pow(env * thresholdInverse, ratioInverse - 1.0)` for `env >= threshold`, else
  `1.0`) is a **hard knee with zero transition width** — confirmed by reading
  `src/dsp/BandCompressor.cpp`/`docs/architecture.md` directly, not by report. No knee
  parameter exists anywhere in v1's `ParameterLayout.cpp`. Every one of the four reference
  sources above treats a soft, threshold-relative knee as baseline mastering-compressor
  behavior. v1's uniform default (threshold -18 dB, ratio 4:1, identical across all three
  bands) also collapses two documented, opposite mastering philosophies (peak-control vs.
  density) into one mid-strength value that matches neither.

## Attack/Release — per-band ballistics, not one shared range

- Sound on Sound, per band:
  - **Bass band:** "Moderately fast attack (very low frequencies lack fast transients)...
    Release time approximately **twice the mid-band setting** to account for frequency
    decay characteristics."
  - **Mid-range band:** "Set much as you'd set a full-range compressor... fairly short
    release, but avoid audible pumping."
  - **High band:** "Potentially faster release time (approximately **half the mid-range
    setting**)... Listen carefully for unnatural gain changes."
  https://www.soundonsound.com/techniques/multi-band-compression-tips
- Bob Katz (search-synthesized, moderate confidence — could not confirm via a primary Katz
  text in this pass, flagged accordingly): for fattening 40–70 Hz material, "a fairly long
  attack time and a medium or fast release," attack "in the ballpark of 50, 75, 100, or
  150 ms," specifically to "let the transient part of the attack go through" — i.e. slow
  attack on the low band is a deliberate transient-preserving choice, not sloppiness.
- FabFilter Pro-MB deliberately uses **percentage-based, not millisecond-based**
  attack/release: "actual attack times are very program dependent, and even depend on the
  placement of the band in the frequency spectrum" — the documented reason FabFilter
  abandoned absolute ms controls for this exact parameter.
  https://www.fabfilter.com/help/pro-mb/using/basicbandcontrols
- Weiss DS1-MK3: release is explicitly two-stage — "Release Fast/Slow" plus a documented
  "Release Delay" that "holds the current level before entering release," i.e.
  program-dependent/auto-release behavior is baseline for the reference-class flagship, not
  an add-on. https://www.softube.com/us/user-manuals/weiss-ds1-mk3
- **v1 verdict:** v1 gives Low/Mid/High identical attack (0.1–100 ms, default 10 ms) and
  release (10–1000 ms, default 100 ms) ranges and defaults. Every reference source above
  says the opposite: bass wants a slower attack and roughly 2× the mid band's release,
  high wants roughly 0.5× the mid band's release, and the reference-class flagship treats
  program-dependent (auto) release as default behavior, not an option.

## Crossover slope / phase philosophy

- "Especially when using higher slopes, phase effects will become very apparent and often
  unpleasant, which makes this method virtually unusable for mastering purposes — the only
  exception is using 6 dB/oct slopes throughout." vs. "Delink bands for the most isolation
  and increase the slope between them for some isolation... isolation giving more control
  but sounding less natural, whereas crossover [overlap] emulates natural musical dynamics
  amongst frequencies but with less control." (Gearspace engineering discussion, forum
  measurement/consensus tier, not a manual — treated as lower-confidence workflow lore.)
  https://gearspace.com/board/mastering-forum/1039240-multiband-master-comp-linear-vs-minimum-phase.html
- "Minimum latency filters result in the most static phase changes... which can have a
  negative sound, while linear phase results in the least phase changes but can cause
  pre-ringing that smears transients... especially in the lower frequencies." Some modern
  units now offer **Dynamic Phase**: "flat/linear phase response when no gain processing is
  applied, but not introducing latency or pre-ringing... minor phase effects only
  introduced when the gain of a band is actually changed."
  https://www.sageaudio.com/articles/how-to-multiband-compress-your-master ,
  FabFilter Pro-MB "Processing mode" help page (dynamic/linear/natural phase modes)
- FabFilter Pro-MB crossover slopes are user-adjustable per-edge, "freely adjusted between
  6 dB/oct and 48 dB/oct." https://www.fabfilter.com/help/pro-mb/using/basicbandcontrols
- **v1 verdict:** v1's fixed LR4 (-24 dB/oct, minimum-phase) is a legitimate, well-inside-
  the-reference-class choice (JUCE's own crossover reasoning doc already argues this well)
  — NOT a gap to "fix," but the fixed-slope limitation should be named explicitly as a
  known scope boundary versus the reference class's adjustable-slope/adjustable-phase-mode
  norm. `docs/architecture.md` already documents this as a deliberately deferred M1 item
  (LR8/adjustable slope) for sound structural-risk reasons; v2 should keep deferring it and
  say so plainly rather than imply parity with FabFilter/Weiss here.

## Mid/Side processing

- Weiss DS1-MK3 is explicitly described as having "M/S mode and parallel compression
  facilities" as a headline capability. https://weiss.ch/products/pro-audio/ds1-mk3/
- iZotope Ozone's Dynamics/multiband modules are the most widely used mastering-context
  reference for M/S-capable multiband processing (band-by-band Stereo/Mid/Side routing is
  a standard Ozone Dynamics control, per iZotope's own module documentation conventions
  cross-referenced across Ozone 8/9/11 help — not independently re-quoted here to avoid
  redundant fetches, moderate confidence).
- **v1 verdict:** v1 has no per-band M/S option at all — it is stereo-linked processing
  only. This is a real reference-class gap for a plugin whose own repo positions Triptych
  as "3-band multiband compressor (mastering)," but implementing true per-band M/S
  correctly (encode/decode + per-band independent processing + mono-compatibility
  guarantees) is a structural DSP change comparable in risk to the already-deferred
  sidechain/crossover-slope items. Recommend flagging explicitly as an **out-of-scope-for-
  v0.2.0, M2+/M3 candidate** rather than rushing it in, consistent with the honesty
  discipline used for Miserere v2.

## Ballistics detector type (peak vs. RMS)

- Weiss DS1-MK3: "the peak measurement is supplemented with an RMS measurement with
  variable averaging time. The left endstop of this parameter switches the detection to
  Peak mode (default one)" — i.e. the reference-class flagship's *default* is peak, with
  RMS as a selectable refinement, not the other way around.
  https://www.softube.com/us/user-manuals/weiss-ds1-mk3
- **v1 verdict:** `juce::dsp::Compressor`'s envelope follower is effectively a peak-type
  one-pole detector already, so v1's default detector *character* is already in line with
  the reference class's default (peak) — this is not a gap worth chasing for v0.2.0. Note
  it in the brief's scope section rather than promising an RMS mode we can't back with a
  measured guarantee yet.

## v0.3.0 addendum: upward compression/expansion and the "Range" paradigm

Targeted follow-up research for the v0.3.0 "hybrid dynamics" paradigm (Ratio extended below 1:1, a new per-band Range clamp) - reuses the Weiss DS1-MK3 quote already captured above rather than re-fetching it, plus one new targeted fetch.

- **Weiss DS1-MK3 (already quoted above):** ratio "adjustable from 1000:1 to 1:5... allowing every kind of dynamic processing, from limiting to upward expansion (for over-compressed signals)". This is the sourced basis for v0.3.0's extended Ratio lower bound: **0.2 (i.e. "1:5")**, taken directly from the documented endpoint rather than an arbitrary round number. https://weiss.ch/products/pro-audio/ds1-mk3/ , https://www.softube.com/us/user-manuals/weiss-ds1-mk3
- **FabFilter Pro-MB, "Ratio" and "Range" (fetched 2026-07-18, `fabfilter.com/help/pro-mb/using/basicbandcontrols`):**
  - Ratio: "The Ratio slider adjusts the amount of compression or expansion that is applied, scaling the dynamic effect of the band on the input signal." The page does not itself discuss ratio-below-1:1 behaviour or give numeric bounds - Pro-MB's actual architecture is structurally different from what v0.3.0 implements (see below).
  - Range: "The Range knob limits the maximum amount of applied gain change. In addition, the Range knob chooses between downward and upward compression or expansion." No numeric dB bounds are documented on this page.
  - Dynamics Mode: "the Dynamics Mode buttons select between compression and expansion. In combination with the Range knob, four different types of dynamic processing are possible" (downward compression, upward compression, downward expansion, upward expansion - selected via a Compress/Expand toggle crossed with the Range knob's sign).
  - Expansion/gating: "When using Expand mode in combination with a negative range, the signal will be attenuated as soon as it drops below the threshold, increasing the perceived dynamics of the signal around the threshold. This is the most common type of expansion and with higher ratio and range values, it's often called gating."
- **v0.3.0 verdict - deliberately a hybrid, not a Pro-MB clone:** Pro-MB's actual architecture uses a separate Dynamics Mode (Compress/Expand) button crossed with a signed Range knob to reach four distinct behaviours. Triptych's v0.3.0 paradigm (per the binding brief) is simpler by design: a single, continuous Ratio parameter spanning 0.2:1 through 20:1 (Weiss DS1-MK3's own continuous-ratio convention, not Pro-MB's mode-button one) with 1:1 as the exact analytic null, plus an independent Range clamp that bounds gain change symmetrically in *either* direction rather than selecting direction via its own sign. This reuses Pro-MB's Range *concept* (a maximum-gain-change safety clamp) without adopting its four-mode UI, and reuses Weiss's continuous-ratio convention for the upward/downward split - see `docs/design-brief-v3-dynamics.md` for the full spec and the honesty section's explicit accounting of what is and isn't Pro-MB parity.
- **No sourced numeric Range default found.** Neither Pro-MB's help page nor the Waves C6 "Range" framing already captured above (search-synthesis only, PDF manual fetch failed - see the original confidence notes below) documents a specific dB range or default value for a maximum-gain-change control. v0.3.0's own 0-30 dB range and 12 dB "if you turn it on" default are therefore a **reasoned engineering choice**, not a sourced number - flagged explicitly in `docs/design-brief-v3-dynamics.md`'s honesty section, consistent with how the High-band limiter's -3 dB default was already handled in v0.1.0/v0.2.0.
- **Downward expansion/gating (Pro-MB's "Expand mode + negative range... often called gating" above) was considered and explicitly deferred**, not folded into v0.3.0's Ratio/Range hybrid - see `docs/design-brief-v3-dynamics.md`'s honesty section for why a clean per-band gate needs a second, independent threshold rather than being reachable from the existing single-threshold Ratio/Range model.

## Confidence notes on sourcing quality

- Weiss DS1-MK3 and FabFilter Pro-MB citations are manufacturer documentation (manual/help
  pages) — high confidence for the specific numbers quoted.
- Sound on Sound is a named, mastering-context technical article by a working engineer,
  not a manufacturer page — treated as high-confidence workflow lore (SoS's editorial bar
  for technique articles is generally reliable), but it is not a controlled measurement.
- The Bob Katz attack/release numbers came from a search-engine synthesis rather than a
  directly fetched primary Katz text (the primary digido.com compression article, fetched
  directly, does NOT contain multiband-specific guidance) — flagged as **moderate**
  confidence and used only as supporting color for the "slow attack / long release on the
  low band" direction already independently confirmed by Sound on Sound.
- Waves C6 PDF manual fetch failed (binary/encoding issue) — C6's "Range" paradigm and
  linked-attack/release description in the brief are drawn from search-result synthesis of
  the product page/reviews, not a direct manual read; used only for context (the "Range"
  reframing idea), not for any numeric default in the brief.
- The iZotope Ozone crossover-default search did not surface documented default Hz values
  (iZotope ships with user-editable, non-fixed defaults) — no Ozone-specific numbers used
  in the brief as a result.
