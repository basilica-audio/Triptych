#pragma once

// Pure, envelope-independent transfer-curve math for BandCompressor's
// downward expansion/gating stage (GitHub issue #25). Mirrors
// KneeGainComputer.h's design: deliberately free functions with no DSP state
// (no BallisticsFilter, no smoothing), so the curve shape itself is directly
// unit-testable (tests/GateGainComputerTests.cpp) independent of envelope
// settling behaviour - BandCompressor::process() calls computeGateGainLinear()
// once per sample with its own gate envelope value (see BandCompressor.h).
//
// Model: the standard downward expander (Zolzer, "Digital Audio Signal
// Processing", 2nd ed., the dynamics-processing chapter; Giannoulis, Massberg
// & Reiss, "Digital Dynamic Range Compressor Design - A Tutorial and
// Analysis", JAES 2012, section on expanders/gates). Below `gateThresholdDb`,
// the output is pulled further below threshold by `ratio`:
//
//   outputDb = gateThresholdDb + (inputDb - gateThresholdDb) * ratio
//
// `inputDb - gateThresholdDb` is <= 0 below threshold, so a ratio > 1
// steepens the descent: e.g. a 2:1 expansion ratio means every 1 dB the
// input sits below threshold becomes 2 dB below threshold at the output -
// 1 dB of *extra* attenuation per dB below threshold, the classic downward
// expander/gate behaviour. At or above threshold, gain change is always
// exactly 0 dB (untouched), and `ratio == 1.0` is a bit-exact bypass
// independent of threshold - the same "ratio == 1.0 is an unconditional
// null" convention KneeGainComputer.h's compressor curve uses.
//
// Deliberately a hard-knee expander with no soft-knee transition and no
// Range-style clamp of its own - issue #25's own recommendation was a
// from-scratch design-brief pass for anything more elaborate (soft knee,
// hold time, etc.); this ships the well-established minimal expander model
// rather than partially reproducing a more elaborate reference design.
// A defensive floor bounds the reduction so pathologically deep envelope/
// threshold combinations (e.g. a fuzz-tested extreme threshold against
// digital silence) can't push juce::Decibels::decibelsToGain() into
// undefined range - see computeGateGainReductionDb()'s definition.
//
// Reuses the same detector topology as the compressor stage: BandCompressor
// owns a second juce::dsp::BallisticsFilter instance (its own independent
// attack/release, per issue #25's explicit ask that gate ballistics not
// just reuse the compressor's own Attack/Release), not a structurally
// different envelope-follower/detection method.
namespace trpt
{
    // Static gain reduction, in dB (always <= 0; exactly 0 at/above
    // `gateThresholdDb` or at `ratio <= 1.0`), for a signal sitting at
    // `inputLevelDb` against `gateThresholdDb`/`ratio` (ratio >= 1.0 - the
    // ParameterLayout enforces this range, see ParameterLayout.cpp).
    float computeGateGainReductionDb (float inputLevelDb, float gateThresholdDb, float ratio) noexcept;

    // Converts a linear envelope value directly to a linear gain multiplier -
    // BandCompressor::process()'s actual per-sample call.
    float computeGateGainLinear (float envelopeLinear, float gateThresholdDb, float ratio) noexcept;
}
