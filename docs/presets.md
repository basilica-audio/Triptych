# Factory presets

Eight factory presets ship with Triptych v0.2.0 (`presets/factory/*.json`, embedded via BinaryData - see `docs/preset-system-notes.md` in `basilica-audio/nave`, the M2 preset system's pilot implementation, for the file format and wiring this repo copied). Every preset is a settings snapshot over the existing parameter set - no bespoke DSP beyond the v0.2.0 Knee addition. See `docs/design-brief.md` for the full research-derived rationale and `docs/research-notes.md` for the sourced numbers each preset draws on.

| Preset | Category | Intent |
|---|---|---|
| **Default** | Init | The shipped v0.2.0 defaults, unchanged - the blank-slate starting point (moderate Low, density-leaning Mid, peak-control-leaning High). |
| **Density Glue** | Master | All-band low-threshold/low-ratio "knit together" density philosophy (thresholds ~-32/-35/-30 dB, ratios ~1.3/1.2/1.4:1, wide 75% knee) - judge only in context, at light (1-2 dB) gain reduction. |
| **Peak Control** | Master | High-threshold/higher-ratio "catch the excesses" philosophy (thresholds ~-10/-8/-8 dB, ratios ~5/4/4:1, near-hard 15% knee, fast attack) - only the loudest peaks are touched. |
| **Low-End Tighten** | Bus | Single-band-focused workflow: only the Low band compresses meaningfully (threshold -20 dB, ratio 3.5:1, slower ballistics); Mid/High left near-transparent (ratio 1.2:1). |
| **De-Harsh Highs** | Bus | Only the High band compresses meaningfully (threshold -22 dB, ratio 3:1, fast attack, smoothing 60% knee) - targets sibilance/cymbal buildup without touching the low end. |
| **Mastering Safety Ceiling** | Master | All bands near-transparent (ratio ~1.3:1, threshold -28 dB); the High-band limiter is engaged at its default -3 dB threshold as insurance, not sound-shaping, for a mix that's already well-balanced. |
| **Parallel-Style Density** | Master | Density philosophy pushed lower (~-38 dB thresholds, ~1.15:1 ratios) for a more audible effect than Density Glue, with +1.5 dB Output trim compensating for the extra gain reduction. |
| **Hard Limiter Ceiling** | Master | High band pushed to an 8:1 ratio with a 0% (hard) knee, backstopped by the High-band limiter at -1 dB - the peak-control extreme with the limiter as a true safety net. |

All presets are research-derived starting points, not measurements against reference hardware - see `docs/design-brief.md`'s honesty section.
