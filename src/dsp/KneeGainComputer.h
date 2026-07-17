#pragma once

// Pure, envelope-independent static transfer-curve math for BandCompressor's
// knee-aware, bidirectional gain computer (docs/design-brief.md's v0.2.0
// "New: a Knee parameter..." section; docs/design-brief-v3-dynamics.md's
// v0.3.0 hybrid dynamics extension). Deliberately free functions with no DSP
// state (no BallisticsFilter, no smoothing) so the curve shape itself is
// directly unit-testable (tests/KneeGainComputerTests.cpp) independent of
// envelope settling behaviour - BandCompressor::process() calls
// computeGainLinear() once per sample with the live envelope value.
//
// Knee model: threshold-relative extent, sourced from the Weiss DS1-MK3
// manual's "'Soft-knee' ranges from 0 to 1.0 maximum, reaching from 0dBFS
// down to twice the threshold value" (docs/research-notes.md). At
// kneePercent == 100, the knee region spans [2 * thresholdDb, 0], which is
// symmetric around thresholdDb with half-width == |thresholdDb| - i.e. the
// half-width scales linearly with kneePercent from 0 (hard knee) up to
// |thresholdDb| (the Weiss maximum). The in-knee transition itself uses the
// standard quadratic soft-knee interpolation (Giannoulis, Massberg & Reiss,
// "Digital Dynamic Range Compressor Design - A Tutorial and Analysis", JAES
// 2012, eq. 4), which is C0-continuous at both knee edges by construction.
//
// v0.3.0 hybrid dynamics (docs/design-brief-v3-dynamics.md): the same
// threshold/ratio/knee transfer curve above is now continuously defined for
// Ratio values below 1:1, not just above it. Above threshold, the exact same
// closed-form curve (`thresholdDb + delta * ratioInverse`, or its quadratic
// soft-knee blend) that *cuts* signal for ratio > 1 (downward compression)
// symmetrically *boosts* it for ratio < 1 (upward compression/expansion,
// restoring dynamics to over-compressed material - sourced from the Weiss
// DS1-MK3 manual's documented "adjustable from 1000:1 to 1:5... upward
// expansion (for over-compressed signals)", docs/research-notes.md). Ratio
// == 1.0 exactly is the analytic boundary between the two regimes and is a
// bit-exact null (both formulas degenerate to inputDb/1.0 exactly at
// ratioInverse == 1.0 - see computeStaticGainReductionDb()/
// computeGainLinear()'s dedicated `ratio == 1.0f` fast path). Material below
// threshold remains untouched regardless of ratio, in both regimes.
//
// Range (v0.3.0): an additional, optional maximum-gain-change clamp in dB,
// applied symmetrically to both the downward and upward regimes above -
// `rangeDb` defaults to `unlimitedRangeDb`, a sentinel large enough that no
// realistic combination of this engine's own Threshold (-60..0 dB) and
// Ratio (0.2:1..20:1) ranges can ever reach it (see the constant's own doc
// comment), so omitting the argument entirely reproduces v0.2.0's unclamped
// behaviour bit-for-bit via the same linear-domain fast path used at
// Knee == 0%.
namespace trpt
{
    // Sentinel "no clamp requested" value for the `rangeDb` parameter below,
    // and the unconditional defensive numerical ceiling every gain-reduction
    // computation is clamped to regardless of the caller's own rangeDb (see
    // computeStaticGainReductionDb()'s definition) - guards
    // juce::Decibels::decibelsToGain() against overflow at pathological,
    // far-outside-any-realistic-operating-point inputs (e.g. a fuzz test
    // driving the envelope far above 0 dBFS). Chosen comfortably below the
    // ~770 dB point where decibelsToGain() would overflow a 32-bit float,
    // and comfortably above the largest gain change achievable from this
    // engine's own actual parameter ranges even at their extremes
    // (Threshold -60 dB, Ratio down to 0.2:1 => ratioInverse 5, applied to a
    // generously-hot +24 dBFS input: (24 - -60) * 5 = 420 dB, still well
    // under this sentinel).
    inline constexpr float unlimitedRangeDb = 500.0f;

    // Static gain reduction, in dB (negative = cut/downward, positive =
    // boost/upward, exactly 0 at ratio == 1.0 or below threshold), for a
    // signal sitting at `inputLevelDb` against `thresholdDb`/`ratio`/
    // `kneePercent` (0-100). The result is always clamped to
    // +-min(rangeDb, unlimitedRangeDb) (design-brief-v3-dynamics.md's Range
    // guarantee).
    float computeStaticGainReductionDb (float inputLevelDb, float thresholdDb, float ratio, float kneePercent, float rangeDb = unlimitedRangeDb) noexcept;

    // Converts a linear envelope value directly to a linear gain multiplier -
    // BandCompressor::process()'s actual per-sample call. At kneePercent == 0
    // and rangeDb >= unlimitedRangeDb (i.e. Range disabled - see
    // BandCompressor), this is a bit-exact reproduction of
    // juce::dsp::Compressor::processSample()'s hard-knee formula (JUCE
    // 8.0.14, juce_dsp/widgets/juce_Compressor.cpp:
    // `gain = (env < threshold) ? 1.0 : pow(env * thresholdInverse, ratioInverse - 1.0)`),
    // computed via the identical linear-domain pow() path rather than routed
    // through the dB-domain computeStaticGainReductionDb() above, so the
    // "Knee null test" regression guarantee (docs/design-brief.md) holds
    // without floating-point round-trip error through log10/pow10 at the
    // 0%-knee boundary. That same linear pow() path is also what gives the
    // v0.3.0 upward (ratio < 1) case its transfer curve "for free": the
    // exponent (ratioInverse - 1.0) simply becomes positive.
    float computeGainLinear (float envelopeLinear, float thresholdDb, float ratio, float kneePercent, float rangeDb = unlimitedRangeDb) noexcept;
}
