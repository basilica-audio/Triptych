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

Reach for it when a single-band compressor either over-squashes the low end to control high-frequency peaks, or leaves the low end loose while the highs are already well-controlled - the classic symphony-metal problem of a dense wall of distorted guitars, orchestral hits, and choir all competing for the same headroom. It also works well as a "glue" stage on a drum bus or a full guitar stack, independent of the full mix.

## Signal flow

```
                    +-> BandComp (Low)  --------------------------------+
Input --> LR4 @ Low/Mid Split             |                             |
                    \-> LR4 @ Mid/High Split                            |
                              +-> BandComp (Mid)  ----------------------+--> Mute/Solo gate --> Sum --> Output --> Out
                              \-> BandComp (High) + optional Limiter ---+
```

Each band's own compressor (Threshold/Ratio/Attack/Release + Makeup) runs first; the High band can additionally engage a brickwall-style Limiter after its compressor. Every band's contribution is then gated by its own Mute/Solo state before the three bands are summed and trimmed by the master Output control. See [`docs/architecture.md`](architecture.md) for the full engineering breakdown (flat-sum crossover property, compressor bypass identity, limiter behaviour, parameter smoothing).

## Parameter reference

### Crossover

| Parameter | Range | Default | Unit | What it does |
|---|---|---|---|---|
| **Low/Mid Split** | 40 – 1000 | 200 | Hz | The crossover point between the Low and Mid bands. Everything below this frequency is the Low band; everything above feeds the second crossover. A minimum separation from Mid/High Split is enforced at all times, so automation can never invert band order. |
| **Mid/High Split** | 400 – 12000 | 3000 | Hz | The crossover point between the Mid and High bands. |

### Per-band controls (Low, Mid, High - identical ranges on every band)

| Parameter | Range | Default | Unit | What it does musically |
|---|---|---|---|---|
| **Threshold** | -60 – 0 | -18 | dB | The level above which the band's compressor starts reducing gain. Lower it to catch more of the signal; raise it toward 0 dB to only catch the loudest peaks. |
| **Ratio** | 1:1 – 20:1 | 4:1 | : 1 | How hard the band compresses once above Threshold. 1:1 is an exact bypass of that band's compressor (useful for A/B-ing one band's effect against the others). Higher ratios (10:1+) approach limiting. |
| **Attack** | 0.1 – 100 | 10 | ms | How quickly the compressor reacts once the signal crosses Threshold. Fast attack (under ~5 ms) catches transients hard but can dull pick/mallet attack on drums and guitars; slower attack lets transients through before gain reduction kicks in, preserving punch. |
| **Release** | 10 – 1000 | 100 | ms | How quickly gain reduction recovers once the signal drops back below Threshold. Fast release can pump audibly with sustained material (bass, sustained pads); slow release smooths gain reduction out but can "duck" the following transient if it's set too slow relative to the material's tempo. |
| **Makeup** | -12 – +24 | 0 | dB | Output trim applied to that band alone, after compression, before the Mute/Solo gate and the sum. Use it to restore the level lost to gain reduction, or to deliberately rebalance a band's contribution to the mix. |

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

## Tips

- **Start with the crossover points, not the compressors.** Solo each band in turn (see Mute/Solo above) to hear exactly what content lands where before touching Threshold/Ratio - a Low/Mid Split that's too high will pull kick-drum click or palm-mute pick attack into the Low band, making it compress unpredictably.
- **Use Solo to dial in each band's compressor in isolation**, then un-solo and listen to the full mix - a setting that sounds great soloed can still be wrong in context (over-compression on one band is often more audible against the other two than in isolation).
- **Keep Low band Attack slower than Mid/High** if the low end includes bass or kick - a very fast attack there flattens the punch of low transients quickly, while Mid/High content (guitars, cymbals, choir sibilance) usually tolerates (and often benefits from) a faster attack.
- **Reach for the High-band limiter instead of a lower High Ratio** when the problem is specifically sharp, occasional peaks (cymbal crashes, orchestral hits) rather than the High band's general level - a limiter catches the peaks without audibly squashing everything else in that band the way a low-ratio/low-threshold compressor setting would.
- **Mute, don't just turn Makeup down to -12 dB**, when you want to genuinely audition a mix without one band's contribution - Mute (and Solo) don't touch that band's compressor settings at all, so your dial-in isn't affected by the A/B.
- **Set per-band Makeup to compensate for gain reduction, then use master Output for overall level matching** - keeping those two jobs separate makes it much easier to tell whether a mix problem is a per-band balance issue or a simple "everything's too loud/quiet" issue.
