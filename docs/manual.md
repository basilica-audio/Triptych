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
                    +-> BandComp (Low)  --------------------------------+
Input --> LR4 @ Low/Mid Split             |                             |
                    \-> LR4 @ Mid/High Split                            |
                              +-> BandComp (Mid)  ----------------------+--> Mute/Solo gate --> Sum --> Output --> Out
                              \-> BandComp (High) + optional Limiter ---+
```

Each band's own compressor (Knee → Threshold/Ratio → Range → Attack/Release + Makeup) runs first; the High band can additionally engage a brickwall-style Limiter after its compressor. Every band's contribution is then gated by its own Mute/Solo state before the three bands are summed and trimmed by the master Output control. See [`docs/architecture.md`](architecture.md) for the full engineering breakdown (flat-sum crossover property, the soft-knee gain computer, the v0.3.0 upward-ratio/Range extension, compressor bypass identity, limiter behaviour, parameter smoothing).

**A note on the voicing.** The per-band defaults below (and the factory presets in [`docs/presets.md`](presets.md)) are **research-derived**, sourced from published manufacturer manuals and mastering-engineer technique articles for the multiband-compression reference class - not measured against reference hardware. See [`docs/research-notes.md`](research-notes.md) for the sourced quotes/URLs, [`docs/design-brief.md`](design-brief.md) for the v0.2.0 soft-knee rationale, and [`docs/design-brief-v3-dynamics.md`](design-brief-v3-dynamics.md) for v0.3.0's Ratio/Range dynamics extension.

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

## Presets

Triptych ships with eight factory presets (Default, Density Glue, Peak Control, Low-End Tighten, De-Harsh Highs, Mastering Safety Ceiling, Parallel-Style Density, Hard Limiter Ceiling) covering both the peak-control and density mastering philosophies documented in [`docs/research-notes.md`](research-notes.md), plus single-band-focused workflow presets. See [`docs/presets.md`](presets.md) for what each one is for. The preset bar at the top of the plugin window lets you browse factory and user presets, save/rename/delete your own, set a default that loads on every fresh instance, and import/export single presets or whole preset banks (`.basilicapreset`/`.zip`). User presets are stored per-plugin under `~/Library/Audio/Presets/Yves Vogl/Triptych/` on macOS (`%APPDATA%\Yves Vogl\Triptych\Presets\` on Windows).

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
