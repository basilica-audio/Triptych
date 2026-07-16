#pragma once

// Pure, envelope-independent static transfer-curve math for BandCompressor's
// v2 soft-knee gain computer (docs/design-brief.md's "New: a Knee
// parameter..." section). Deliberately free functions with no DSP state
// (no BallisticsFilter, no smoothing) so the curve shape itself is directly
// unit-testable (tests/KneeGainComputerTests.cpp) independent of envelope
// settling behaviour - BandCompressor::process() calls
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
namespace trpt
{
    // Static gain reduction, in dB (<= 0), for a signal sitting at
    // `inputLevelDb` against `thresholdDb`/`ratio`/`kneePercent` (0-100).
    // Returns 0 for ratio <= 1 (an exact bypass, independent of knee - see
    // docs/design-brief.md's flat-sum guarantee) and for levels below the
    // knee's lower edge.
    float computeStaticGainReductionDb (float inputLevelDb, float thresholdDb, float ratio, float kneePercent) noexcept;

    // Converts a linear envelope value directly to a linear gain multiplier -
    // BandCompressor::process()'s actual per-sample call. At kneePercent == 0
    // (or ratio <= 1), this is a bit-exact reproduction of
    // juce::dsp::Compressor::processSample()'s hard-knee formula (JUCE
    // 8.0.14, juce_dsp/widgets/juce_Compressor.cpp:
    // `gain = (env < threshold) ? 1.0 : pow(env * thresholdInverse, ratioInverse - 1.0)`),
    // computed via the identical linear-domain pow() path rather than routed
    // through the dB-domain computeStaticGainReductionDb() above, so the
    // "Knee null test" regression guarantee (docs/design-brief.md) holds
    // without floating-point round-trip error through log10/pow10 at the
    // 0%-knee boundary.
    float computeGainLinear (float envelopeLinear, float thresholdDb, float ratio, float kneePercent) noexcept;
}
