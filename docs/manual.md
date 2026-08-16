<p align="center"><img src="assets/icon.png" alt="Triptych icon" width="120"/></p>

# Triptych — user manual

*Three panels, one altarpiece — a 3-band multiband compressor for dense mixes.*

## What it is

Triptych is a 3-band multiband compressor built on JUCE 8. It splits the signal into Low, Mid, and High bands with two cascaded 4th-order Linkwitz-Riley (LR4) crossovers, compresses each band independently, and sums them back together with a final output trim. Because the LR4 crossover's low+high sum is magnitude-flat, Triptych is an exact, bit-identical passthrough of the input with every band's compressor disabled (ratio 1:1, makeup 0 dB) - the crossover split itself never colours the sound.

Unlike a single-band compressor, Triptych lets you control dynamics differently per register: squeeze the low end tight without touching cymbal transients, tame a harsh pick-attack band without softening the low-end punch, or duck sibilance in the highs while the low end stays untouched.

## Where it sits in a heavy production chain

Triptych is a **mastering/mix-bus dynamics tool**, not a per-instrument effect. A typical use:

```
Full mix (guitars + orchestra + choir + drums/bass) -> Triptych (multiband glue/control) -> brickwall limiter -> master out
```

Reach for it when a single-band compressor either over-squashes the low end to control high-frequency peaks, or leaves the low end loose while the highs are already well-controlled - the classic heavy-music problem of a dense wall of distorted guitars, orchestral hits, and choir all competing for the same headroom. It also works well as a "glue" stage on a drum bus or a full guitar stack, independent of the full mix.

## Signal flow

```
                    +-> Band strip (Low)  ------------------------------+
Input --> Split @ Low/Mid                 |                             |
                    \-> Split @ Mid/High                                |
                              +-> Band strip (Mid)  -------------------+--> Mute/Solo gate --> Sum --> Mix --> Output --> Out
                              \-> Band strip (High) + optional Limiter -+                       ^
                                                                                                |
Input (dry, delayed to match Lookahead) ----------------------------------------------------------+

Sidechain (optional) --> its own Split pair, same frequencies + slope --> per-band detector key
```

Each band is a strip rather than a single compressor. Inside one strip:

```
key   --> Detector (Peak/RMS law -> Auto Release -> Stereo Link -> Character)
             |  envelope
audio --> Gate (own threshold, own detector) --> Compressor (Knee -> Threshold/Ratio -> Range) --> Makeup
             \___ optionally wrapped in a Mid/Side encode/decode ___/
```

The **key** is the signal the compressor's detector listens to: the band's own audio (SC Source *Internal*), or the matching band of the sidechain (*External*). The **Gate** always keys internally on the band's own audio, whatever SC Source is set to. The gate's gain and the compressor's gain multiply, so gating a band is never masked by that band's own compression curve.

The High band can additionally engage a brickwall-style Limiter after its compressor. Every band's contribution is then gated by its own Mute/Solo state, the three bands are summed, blended against the delay-compensated dry signal by **Mix**, and trimmed by the master **Output** control.

The two split points use the same **Slope** setting (12, 24 or 48 dB/oct), as does the sidechain's own crossover pair. See [`docs/architecture.md`](architecture.md) for the full engineering breakdown (flat-sum crossover property, the soft-knee gain computer, the v0.3.0 upward-ratio/Range extension, detector v2, the lookahead brickwall's zero-overshoot proof, compressor bypass identity, limiter behaviour, parameter smoothing).

**A note on the voicing.** The per-band defaults below (and the factory presets in [`docs/presets.md`](presets.md)) are **research-derived**, sourced from published manufacturer manuals and mastering-engineer technique articles for the multiband-compression reference class - not measured against reference hardware. See [`docs/research-notes.md`](research-notes.md) for the sourced quotes/URLs, [`docs/design-brief.md`](design-brief.md) for the v0.2.0 soft-knee rationale, and [`docs/design-brief-v3-dynamics.md`](design-brief-v3-dynamics.md) for v0.3.0's Ratio/Range dynamics extension.

**v0.5.0: the flagship dynamics core.** Every band's detector gains a selectable **Peak/RMS** law, a program-dependent **Auto Release**, a **VCA** character, and a **Stereo Link** control. A global **Lookahead** and **Mix**, selectable **crossover slopes** (12/24/48 dB/oct), an **external sidechain** with per-band keying and detector-key monitoring, and **hold + hysteresis** on every gate complete the set. All twenty-three new controls default to a fully neutral state, so a v0.4.0 session opened in v0.5.0 sounds exactly as it did - and reports the same zero latency. Existing automation survives too: the new parameters are appended after the fifty-nine that shipped earlier rather than inserted among them, so nothing an older session already automates changes position.

**v0.3.0: true dynamic multiband.** Every band's Ratio now spans **0.2:1 through 20:1** (previously 1:1-20:1) - values below 1:1 are *upward* compression/expansion: signal above threshold gets boosted instead of cut, tapering smoothly through an exact null at 1:1. A new per-band **Range** control clamps the maximum gain change (up or down) so an aggressive Ratio setting stays musically usable instead of running away. Both are described in the tables below.

## Parameter reference

### Crossover

| Parameter | Range | Default | Unit | What it does |
|---|---|---|---|---|
| **Low/Mid Split** | 40 – 1000 | 200 | Hz | The crossover point between the Low and Mid bands. Everything below this frequency is the Low band; everything above feeds the second crossover. A minimum separation from Mid/High Split is enforced at all times, so automation can never invert band order. |
| **Mid/High Split** | 400 – 12000 | 3000 | Hz | The crossover point between the Mid and High bands. |

### Per-band controls (Low, Mid, High - identical ranges on every band; **defaults now differ per band as of v0.2.0** - see the note above)

| Parameter | Range | Low default | Mid default | High default | Unit | What it does musically |
|---|---|---|---|---|---|---|
| **Threshold** | -60 – 0 | -24 | -30 | -20 | dB | The level above which the band's compressor starts reducing gain. Lower it to catch more of the signal; raise it toward 0 dB to only catch the loudest peaks. Mid's lower default leans toward the "density/knit-together" mastering philosophy; Low/High lean toward "peak control" (see the research-derived note above). |
| **Ratio** *(range widened in v0.3.0)* | 0.2:1 – 20:1 | 2.5:1 | 1.8:1 | 2:1 | : 1 | How hard the band compresses once above Threshold. **1:1 is an exact bypass** of that band's compressor (useful for A/B-ing one band's effect against the others), independent of Knee/Range. Above 1:1, higher ratios cut harder (10:1+ approaches limiting). **Below 1:1 (v0.3.0), the same curve boosts instead of cutting** - "upward compression/expansion": signal above Threshold gets lifted, restoring dynamics to over-compressed material or adding density/lift. The knob is centred so 1:1 sits at the middle of its travel. |
| **Knee** | 0 – 100 | 50 | 50 | 50 | % | How gradually the compressor transitions into gain reduction around Threshold. 0% is a hard knee (compression/expansion starts abruptly right at Threshold); 100% is the widest soft-knee transition, scaled to span from Threshold down to twice its distance from 0 dBFS - so the knee's width in dB adapts sensibly whether Threshold sits near 0 dB or near -50 dB. |
| **Attack** | 0.1 – 100 | 25 | 10 | 5 | ms | How quickly the compressor reacts once the signal crosses Threshold. Low's slower default lets low-frequency transients (which "lack fast transients" in the first place) through before gain reduction kicks in; High's faster default catches fast transient material. Fast attack (under ~5 ms) catches transients hard but can dull pick/mallet attack; slower attack preserves punch. |
| **Release** | 10 – 1000 | 180 | 100 | 55 | ms | How quickly gain reduction recovers once the signal drops back below Threshold. Low's longer default (~1.8x Mid) accounts for low-frequency decay characteristics; High's shorter default (~0.5x Mid) suits faster transient material. Fast release can pump audibly with sustained material; slow release smooths gain reduction out but can "duck" the following transient if set too slow relative to the material's tempo. |
| **Makeup** | -12 – +24 | 0 | 0 | 0 | dB | Output trim applied to that band alone, after compression, before the Mute/Solo gate and the sum. Use it to restore the level lost to gain reduction, or to deliberately rebalance a band's contribution to the mix. |

### Per-band Range *(new in v0.3.0)*

| Parameter | Range | Default | Unit | What it does |
|---|---|---|---|---|
| **Range On** (Range Enabled) | Off / On | Off | Engages the band's maximum gain-change clamp. Off by default, so the band's Ratio/Knee curve behaves exactly as the table above describes with no ceiling on how far it can push gain up or down. |
| **Range** | 0 – 30 | 12 | dB | The maximum gain change (cut *or* boost) the band's compressor is allowed to apply, once Range On is engaged. This is what makes an aggressive Ratio setting - especially a strongly upward (well below 1:1) one, whose boost otherwise grows without bound the further above Threshold the signal sits - stay musically usable rather than a runaway. Only takes effect while Range On is engaged; the dB value itself has no effect while it's off. |

### Per-band Gate / downward expander *(new in v0.4.0)*

| Parameter | Range | Low default | Mid default | High default | Unit | What it does |
|---|---|---|---|---|---|---|
| **Gate On** (Gate Enabled) | Off / On | Off | Off | Off | | Engages the band's independent downward expander/gate. Off by default, so the band behaves exactly as the compressor tables above describe with no gating applied at all. |
| **Gate Threshold** | -80 – 0 | -50 | -55 | -45 | dB | The level *below* which the gate starts attenuating - deliberately a separate, independent threshold from the compressor's own Threshold above (typically set well below it, so the gate only reaches into genuinely quiet material/noise floor, not the program material the compressor is shaping). |
| **Gate Ratio** | 1:1 – 100:1 | 2:1 | 2:1 | 2:1 | : 1 | How hard the gate attenuates once below Gate Threshold. 1:1 is an exact bypass; higher ratios attenuate more steeply per dB below threshold (100:1 approaches a hard, on/off-style gate). |
| **Gate Attack** | 0.1 – 50 | 10 | 5 | 2 | ms | How quickly the gate opens back up once the signal rises back above Gate Threshold - deliberately a faster ceiling than the compressor's own Attack, since a gate typically needs to react quickly to avoid clipping the front of a transient. |
| **Gate Release** | 10 – 2000 | 200 | 150 | 100 | ms | How quickly the gate closes once the signal drops below Gate Threshold - deliberately a slower ceiling than the compressor's own Release, since a gate closing too fast on material hovering near the threshold causes audible chatter. |

The gate runs independently of, and in parallel with, that band's own compressor - both are keyed off the same input signal and their gains multiply together, so gating a band is never masked by (or fighting against) that band's own compression curve. Because toggling Gate On is a musical decision rather than a continuous control, expect it to behave like any gate: set Gate Threshold below the quietest material you want to keep, and Gate Ratio/Attack/Release to taste for how aggressively and quickly it should react.

### Per-band Mid/Side *(new in v0.4.0)*

| Parameter | Range | Low default | Mid default | High default | Unit | What it does |
|---|---|---|---|---|---|---|
| **M/S On** (M/S Enabled) | Off / On | Off | Off | Off | | Encodes that band's stereo signal to Mid/Side before its gain computation and decodes back to L/R afterward. Off by default, so the band stays stereo-linked exactly as the tables above describe. Only takes effect on a genuine stereo bus - a defensive no-op on mono. |
| **Side Threshold** | -60 – 0 | -24 | -30 | -20 | dB | The Side (difference/width) component's own, independent Threshold - separate from the main Threshold above, which continues to drive the Mid (centre/sum) component. |
| **Side Ratio** | 0.2:1 – 20:1 | 1:1 | 1:1 | 1:1 | : 1 | The Side component's own Ratio, sharing the band's Knee/Attack/Release/Range with Mid. Defaults to 1:1 (exact bypass) on every band, so simply enabling M/S with no further tweaking compresses only the centre content using the band's existing Threshold/Ratio while leaving the stereo width untouched - a sensible starting point for tightening a mix's centre (vocal, kick, bass) without narrowing its width. |

Because L + R after decode depends only on Mid (algebraically independent of whatever happens to Side), processing the Side component - however aggressively - can never introduce a phase-cancellation artifact into a mono downmix; only changing the Mid component's own processing changes the mono sum, which is the intended, audible effect of compressing/expanding the centre.

### Global controls *(new in v0.5.0)*

| Parameter | Range | Default | Unit | What it does |
|---|---|---|---|---|
| **SC Source** (Sidechain Source) | Internal / External | Internal | | Chooses what every band's compressor detector listens to. *Internal* is the band's own signal, exactly as before. *External* keys the detectors off the sidechain bus instead - split by its own crossover pair at the same frequencies and slope, so each band follows a band-matched key rather than the full-range sidechain. If you select External but the host has not connected a sidechain, Triptych falls back to Internal silently rather than falling quiet. |
| **SC Listen** (Sidechain Listen) | Off / Low / Mid / High | Off | | Replaces the output with the selected band's **detector key** so you can hear exactly what the detectors are following while you set thresholds. This is *not* a band solo - it is the key signal, not the processed audio. A monitoring aid; not meant for automation. |
| **Slope** (Crossover Slope) | 12 / 24 / 48 dB/oct | 24 dB/oct | | The steepness of both crossover splits. 24 dB/oct is the classic setting and what every earlier version used. 12 dB/oct overlaps the bands more (gentler, more "analogue" band interaction); 48 dB/oct separates them hard, which is what you want when a band's processing must not bleed into its neighbours. Switching slope resets the crossover filters and can click - change it while the transport is stopped. |
| **Lookahead** | Off / 1.5 / 3 / 5 ms | Off | | Lets the detectors see the signal before it arrives, so gain reduction is already in place when a transient hits. Costs exactly that much latency, reported to the host for delay compensation (see the latency table below). Off is the default and keeps Triptych at zero latency. |
| **Mix** | 0 – 100 | 100 | % | Dry/wet blend around the whole multiband chain - parallel compression without a separate bus. The dry path is delayed to match the Lookahead setting, so the two stay aligned. Note that blending a *minimum-phase* multiband tree against a dry signal will colour the result even at gentle settings: Triptych's band sum is magnitude-flat but not phase-flat, so the two paths interact. That is inherent to parallel-mixing any minimum-phase multiband processor, not a defect - use your ears rather than assuming 50% is "half the effect". |

### Per-band detector *(new in v0.5.0)*

Identical ranges on every band.

These four controls shape the **compressor's** detector only. The band's Gate keeps its own, separate envelope follower with its own Attack/Release — so switching a band to RMS, or engaging Auto Release or VCA character, changes how that band *compresses* without changing how it *gates*.

| Parameter | Range | Default | Unit | What it does |
|---|---|---|---|---|
| **Detector** | Peak / RMS | Peak | | The detection law. *Peak* follows the waveform's instantaneous peaks - the tighter, more transient-aware behaviour every earlier version used. *RMS* follows the signal's average power instead, which for a sine reads 3 dB below the peak law and generally sounds smoother and more "level-related" than "transient-related". RMS uses a mean-square time constant of `max(Attack, 5 ms)` before the band's own Attack/Release ballistics. |
| **Auto Rel** (Auto Release) | Off / On | Off | | Program-dependent release. Two release constants run in parallel - a fast one (0.15 s) and a slow reservoir (charge 0.6 s, release 4 s) - and the envelope takes whichever is higher. A brief peak recovers quickly; sustained gain reduction builds a multi-second tail that keeps the band from pumping. The Release knob scales both constants, so it still does what you expect; at around 300 ms it reproduces the reference values exactly. |
| **Character** | Clean / VCA | Clean | | *Clean* is the feed-forward behaviour of every earlier version. *VCA* statically approximates a feedback compressor's loop: the knee rounds by a ratio-dependent amount (about 6 dB at 2:1, 4 dB at 4:1, 3 dB at 10:1) and the effective attack gets faster as the ratio rises. There is deliberately **no** added distortion or harmonic stage - the difference between the two is envelope behaviour, which is where the character of this class of compressor actually lives. |
| **Link** (Stereo Link) | 0 – 100 | 0 | % | How much the two channels' detectors share an envelope. At 0% they are fully independent, which is what every earlier version did - and which lets a hard-panned transient pull one side down on its own and shift the stereo image. At 100% both channels apply exactly the same gain, so the image cannot move at all. Anything in between blends the two. In a band running Mid/Side, this links the Mid and Side pair instead. |

**A note on the VCA knee near a 0 dB threshold.** Triptych's knee is *threshold-relative* by design (its width scales with how far the threshold sits below 0 dBFS - see [`docs/architecture.md`](architecture.md)). That means the VCA character's target knee width is unreachable once the threshold is within half a knee-width of 0 dB: from about -3 dB upward the achieved width narrows smoothly toward a hard knee, reaching exactly hard-knee at a 0 dB threshold. This is a property of the knee model, not a bug, and nothing misbehaves numerically there. If you want the full VCA rounding, keep the band's Threshold at -3 dB or below - which is where a multiband compressor's threshold normally sits anyway.

### Per-band gate shaping *(new in v0.5.0)*

| Parameter | Range | Default | Unit | What it does |
|---|---|---|---|---|
| **Gate Hold** | 0 – 500 | 0 | ms | How long the gate stays open after the signal falls back below its threshold. Set it longer than the gaps you want to ignore (the ring-out of a note, the space between two hits of the same part) and the gate stops chopping them up. The timer retriggers on every fresh crossing, so a run of closely spaced hits holds the gate open throughout. |
| **Gate Hyst** (Gate Hysteresis) | 0 – 12 | 0 | dB | Separates the level at which the gate *opens* from the level at which it *closes*, closing this many dB lower. This is the cure for chatter - a signal hovering right at the threshold flipping the gate open and shut many times a second. Set it a few dB above the ripple you can hear; 3-6 dB handles most material. |

Both are 0 by default, which is exactly the v0.4.0 gate. Both are also implemented so that neither can click: hold works on the gate's envelope rather than pinning its output gain, and the threshold moves through the same 50 ms smoother every other threshold change uses.

### Gain-reduction meters *(new in v0.5.0)*

Each band column carries a thin vertical bar showing how much that band is currently pulling the signal down (compressor and gate combined), with a slowly-decaying peak-hold line. Full scale is 24 dB. These are read-only - they exist so you can see at a glance which band is doing the work.

### Controls to change with the transport stopped

Every **continuous** control in Triptych is click-free by design — thresholds, ratios, knees, ranges, gains, stereo link and the gate's hold/hysteresis all move through smoothers, and the gate is specified so its gain can never step more than 0.5 dB in a single sample. Automate them freely.

Four **stepped** controls restructure the signal path rather than move a value, and can click if you change them mid-playback:

- **Slope** — resets the crossover filters.
- **M/S On** — changes what the band's gain computation is applied to.
- **Limiter (enable)** — click-free on its own, but while **Lookahead** is engaged, toggling it moves where that band spends its delay (see below), which restructures the band.
- **Lookahead** — changes the reported latency, so the host has to re-negotiate delay compensation.

None of these is a defect and none is unusual for the job it does; they are simply configuration changes rather than performance controls.

**Where the lookahead delay lives.** With Lookahead engaged, a band spends its delay in one of two places: either the compressor gets the lead (the audio is delayed while the detector reads ahead), or — if that band's brickwall Limiter is also engaged — the Limiter takes the delay instead, which is what makes it genuinely overshoot-proof. Exactly one of them owns it, so the band always adds exactly the lookahead length and all three bands stay aligned in the sum. The trade is that the High band's compressor gives up its lead while the brickwall is on, which is the right call for a stage whose entire job is the ceiling.

### Per-band Mute / Solo (Low, Mid, High)

| Parameter | Values | Default | What it does |
|---|---|---|---|
| **Mute** | Off / On | Off | Silences that band's contribution to the sum. Its compressor keeps running underneath (so there's no re-attack pop when you unmute mid-playback), it just doesn't reach the output. **Mute always wins** over Solo - a band that is both muted and soloed stays silent. |
| **Solo** | Off / On | Off | Isolates that band: when any band is soloed, only soloed (and unmuted) bands reach the sum. Soloing more than one band at once solos all of them together. Use this to audition one band's compression settings in isolation, or to check what a band actually contains before deciding how hard to compress it. |

### High-band limiter

| Parameter | Range | Default | Unit | What it does |
|---|---|---|---|---|
| **Limiter** (enable) | Off / On | Off | Engages an additional brickwall-style limiter after the High band's own compressor + makeup gain, for catching sharp cymbal/harmonic-overtone peaks that a musically-set compressor (long enough attack to preserve transient character) would otherwise let through. Guarantees the High band's output never exceeds 0 dBFS once engaged, regardless of Threshold or upstream Makeup. |
| **Lim. Thresh.** (High Limiter Threshold) | -24 – 0 | -3 | dB | The limiter's threshold. Lower values squash harder and apply proportionally more internal makeup gain to compensate (a "loudness" style limiter, not a simple peak-catcher) - the ceiling itself is always exactly 0 dBFS regardless of this setting; what changes is how much of the High band gets pulled down to make room under it. |

### Output

| Parameter | Range | Default | Unit | What it does |
|---|---|---|---|---|
| **Output** | -24 – +24 | 0 | dB | Master trim applied after the three bands are summed - the final gain stage in the plugin. Use it to match Triptych's output level to whatever follows it in the chain (typically a brickwall limiter on the master bus). |

## Latency and host delay compensation *(new in v0.5.0)*

With **Lookahead** at its default *Off*, Triptych reports **zero latency** - the crossovers are minimum-phase IIR and the detectors are causal, so nothing in the chain delays the signal. Every session saved before v0.5.0 keeps that behaviour, because the parameter did not exist.

Engaging Lookahead reports exactly the following, and hosts compensate automatically:

| Lookahead | 44.1 kHz | 48 kHz | 88.2 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|---|
| Off | 0 | 0 | 0 | 0 | 0 |
| 1.5 ms | 66 | 72 | 132 | 144 | 288 |
| 3 ms | 132 | 144 | 265 | 288 | 576 |
| 5 ms | 221 | 240 | 441 | 480 | 960 |

(Samples, rounded to the nearest whole sample - hosts only accept integer delay compensation.)

Changing Lookahead while the transport is running makes the host re-negotiate its delay compensation, which most hosts handle with a brief interruption. That is industry-normal for any lookahead control; if it bothers you, set it before you press play.

## Sidechain routing per host

The sidechain input is **optional and disabled by default**, so Triptych loads normally in hosts that do not offer one (including its own Standalone build, which has no sidechain at all).

- **Logic Pro (AU)**: pick the source track from the *Side Chain* menu in the plugin header, then set **SC Source** to *External*.
- **Cubase / Nuendo (VST3)**: activate the side-chain input from the plugin header's side-chain button, then route a send to it from the source track.
- **Reaper (VST3)**: give the Triptych track 4 input channels, route the source track to channels 3/4 of it, and pin channels 3/4 to the plugin's sidechain input in the plugin's routing matrix.
- **Ableton Live (VST3/AU)**: Live does not expose sidechain inputs for third-party plugins in the way it does for its own devices; use a return/aux routing workaround or keep **SC Source** at *Internal*.
- **Standalone**: no sidechain bus exists. *External* falls back to *Internal* silently.

A mono sidechain is duplicated to both detector channels. The **gate** always keys internally, on the band's own signal, regardless of this setting - a gate is a bleed/noise tool for the material actually in the band.

## Crossover slopes and phase

Triptych's three bands come from two cascaded crossovers in series: the first splits Low from everything else, the second splits the remainder into Mid and High. That tree is **uncompensated** - the second split's phase rotation lands on Mid and High but not on Low. The band sum stays magnitude-flat (that is the Linkwitz-Riley property Triptych is built on), but it is not phase-flat, and the amount of rotation scales with the slope you choose:

| Slope | Flat-sum accuracy | Phase behaviour |
|---|---|---|
| 12 dB/oct | within ±0.25 dB | half the rotation of the default |
| 24 dB/oct | within ±0.1 dB | the reference behaviour, unchanged since v0.1 |
| 48 dB/oct | within ±0.25 dB | twice the rotation of the default |

This matters in exactly two situations: when you are mixing Triptych's output against a dry copy of the same signal (the **Mix** control, or an external parallel bus), and when someone measures the plugin with a phase-aware analyser. It does not matter for normal serial use. Low-branch all-pass compensation and a linear-phase mode are scheduled together for v0.6.0, where the change in default sound can be handled with a proper state-versioned migration.

## Presets

Triptych ships with nine factory presets (Default, Density Glue, Peak Control, Low-End Tighten, De-Harsh Highs, Mastering Safety Ceiling, Parallel-Style Density, Hard Limiter Ceiling, Glue Master) covering both the peak-control and density mastering philosophies documented in [`docs/research-notes.md`](research-notes.md), plus single-band-focused workflow presets. See [`docs/presets.md`](presets.md) for what each one is for. The preset bar at the top of the plugin window lets you browse factory and user presets, save/rename/delete your own, set a default that loads on every fresh instance, and import/export single presets or whole preset banks (`.basilicapreset`/`.zip`). User presets are stored per-plugin under `~/Library/Audio/Presets/Yves Vogl/Triptych/` on macOS (`%APPDATA%\Yves Vogl\Triptych\Presets\` on Windows).

## Localisation

The preset bar's labels, menus, and dialogs follow your system language automatically - German if your system language starts with "de", English otherwise. This covers only the preset bar's own interface text; parameter names, units, and every other technical term in this manual stay in English regardless of system language, matching every other plugin in the suite.

## Tips

- **Start with the crossover points, not the compressors.** Solo each band in turn (see Mute/Solo above) to hear exactly what content lands where before touching Threshold/Ratio - a Low/Mid Split that's too high will pull kick-drum click or palm-mute pick attack into the Low band, making it compress unpredictably.
- **Use Solo to dial in each band's compressor in isolation**, then un-solo and listen to the full mix - a setting that sounds great soloed can still be wrong in context (over-compression on one band is often more audible against the other two than in isolation).
- **Keep Low band Attack slower than Mid/High** if the low end includes bass or kick - a very fast attack there flattens the punch of low transients quickly, while Mid/High content (guitars, cymbals, choir sibilance) usually tolerates (and often benefits from) a faster attack.
- **Reach for the High-band limiter instead of a lower High Ratio** when the problem is specifically sharp, occasional peaks (cymbal crashes, orchestral hits) rather than the High band's general level - a limiter catches the peaks without audibly squashing everything else in that band the way a low-ratio/low-threshold compressor setting would.
- **Mute, don't just turn Makeup down to -12 dB**, when you want to genuinely audition a mix without one band's contribution - Mute (and Solo) don't touch that band's compressor settings at all, so your dial-in isn't affected by the A/B.
- **Set per-band Makeup to compensate for gain reduction, then use master Output for overall level matching** - keeping those two jobs separate makes it much easier to tell whether a mix problem is a per-band balance issue or a simple "everything's too loud/quiet" issue.
- **Turn Range on before pushing Ratio below 1:1** (upward compression/expansion) - an upward band's boost grows the further above Threshold the signal sits, with no ceiling of its own. Range gives you a musically sane maximum before you start dialling in an aggressive upward setting, rather than discovering the ceiling by ear after the fact.
- **Turn Stereo Link up before you chase a wandering image.** If a hard-panned hit makes the mix lean to one side when Triptych is engaged, that is the two channels' detectors acting independently. Link at 100% forces both channels to apply the same gain, which fixes it outright; 60-80% keeps some of the per-side responsiveness while pinning the image.
- **Reach for Auto Release before you reach for a slower Release knob.** A slow release stops pumping but also makes the band sluggish on transients. Auto Release gives you both: quick recovery from short peaks, a long tail only when the band is genuinely working hard.
- **Use SC Listen to set sidechain thresholds, not your imagination.** Switch it to the band you are keying, set the band's Threshold by ear against what you actually hear, then switch it back Off. It plays the key signal, not the band - which is exactly what a threshold relates to.
- **Lookahead is for the safety ceiling, not for everything.** Its real payoff is the High band's brickwall, which becomes genuinely overshoot-proof once lookahead is on. If you are not using the limiter, the latency usually is not worth it.
- **Pick the crossover slope for the job, not for the number.** 48 dB/oct when a band's processing must not bleed into its neighbours (a hard de-esser band, a tight low-end clamp); 12 dB/oct when you want the bands to hand over gently and behave more like a broadband compressor with a tilt.

## Known limitations

- **The spectrum-on-curve analyser is not in this release.** v0.5.0 ships the three per-band gain-reduction bars only (see "Gain-reduction meters" above); a frequency-domain analyser overlay is scoped for the M3 custom-GUI milestone, not cut late.
- **Deferred to v0.6.0+**: a linear-phase FIR crossover mode (paired with all-pass compensation for the phase behaviour below), per-band sidechain EQ, per-band dry/wet mix, sample-accurate parameter interpolation (today's smoothing resolves on a 50 ms timer rather than per sample), and a gate range floor.
- **The three-band crossover tree is magnitude-flat but not phase-flat** at any Slope setting - see "Crossover slopes and phase" above. This only matters when mixing Triptych's output against a dry copy of the same signal (including via **Mix**) or when measuring with a phase-aware analyser; it does not matter for normal serial use.
- **The VCA knee's target width becomes unreachable within about 3 dB of a 0 dB threshold** - see the note under "Per-band detector" above. The achieved width narrows smoothly toward a hard knee as the threshold approaches 0 dB; nothing misbehaves numerically, but the full VCA rounding needs a threshold at −3 dB or below.
- **Four stepped controls can click if changed mid-playback** - Slope, M/S On, Limiter enable while Lookahead is engaged, and Lookahead itself - see "Controls to change with the transport stopped" above. Every continuous, automatable control is click-free by design.
- **The voicing throughout is research-derived**, sourced from published manufacturer manuals and mastering-engineer technique articles for the multiband-compression reference class - not measured against reference hardware. See [`docs/research-notes.md`](research-notes.md) for the sourced findings.
