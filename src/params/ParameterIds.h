#pragma once

// Central definition of all AudioProcessorValueTreeState parameter IDs for
// Triptych. See docs/architecture.md for the corresponding signal-flow
// diagram.
//
// FROZEN AS OF THE v0.1 PARAMETER LAYOUT:
// Parameter IDs below must NEVER change once shipped - saved sessions and
// presets persist the APVTS state keyed by these string IDs, and renaming or
// removing one would silently break every user's saved state. Ranges,
// defaults, and skew MAY still be refined during voicing/tuning milestones;
// only the IDs themselves are frozen.
namespace ParamIDs
{
    // Crossover split frequencies: Low/Mid and Mid/High, in that order along
    // the signal path. TriptychEngine enforces a minimum separation between
    // them at run time so automation can never invert band order.
    inline constexpr auto lowMidSplit = "lowMidSplit";
    inline constexpr auto midHighSplit = "midHighSplit";

    // Low band: threshold/ratio/knee/attack/release/makeup, applied below
    // lowMidSplit.
    inline constexpr auto lowThreshold = "lowThreshold";
    inline constexpr auto lowRatio = "lowRatio";
    inline constexpr auto lowAttack = "lowAttack";
    inline constexpr auto lowRelease = "lowRelease";
    inline constexpr auto lowMakeup = "lowMakeup";

    // Mid band: between lowMidSplit and midHighSplit.
    inline constexpr auto midThreshold = "midThreshold";
    inline constexpr auto midRatio = "midRatio";
    inline constexpr auto midAttack = "midAttack";
    inline constexpr auto midRelease = "midRelease";
    inline constexpr auto midMakeup = "midMakeup";

    // High band: above midHighSplit.
    inline constexpr auto highThreshold = "highThreshold";
    inline constexpr auto highRatio = "highRatio";
    inline constexpr auto highAttack = "highAttack";
    inline constexpr auto highRelease = "highRelease";
    inline constexpr auto highMakeup = "highMakeup";

    // Knee (v0.2.0 / design-brief.md): 0-100%, applied threshold-relative in
    // extent (see src/dsp/KneeGainComputer.h). Added after the v0.1 IDs
    // above rather than interleaved with them, so the frozen-ID contract in
    // this file's header comment is unambiguous about what shipped in v0.1
    // vs. what v0.2.0 adds - existing IDs are untouched, these three are
    // purely additive. State migration: a v0.1-shaped ValueTree missing
    // these IDs loads tolerantly at the declared default (50%) - see
    // tests/StateTests.cpp's migration-tolerance test.
    inline constexpr auto lowKnee = "lowKnee";
    inline constexpr auto midKnee = "midKnee";
    inline constexpr auto highKnee = "highKnee";

    // Range (v0.3.0 / docs/design-brief-v3-dynamics.md): per-band maximum
    // gain-change clamp, in dB, applying to BOTH downward (cut) and upward
    // (boost - see lowRatio et al.'s v0.3.0 extended range below 1:1)
    // processing - the reference-class safety valve that makes aggressive
    // Ratio settings usable without runaway gain. `xRangeEnabled` defaults
    // to off so a fresh v0.3.0 instance and v0.2.0-shaped session/preset
    // imports both resolve to the same fully unclamped behaviour - see
    // tests/StateTests.cpp's v0.2.0 migration-tolerance test.
    inline constexpr auto lowRangeEnabled = "lowRangeEnabled";
    inline constexpr auto lowRange = "lowRange";
    inline constexpr auto midRangeEnabled = "midRangeEnabled";
    inline constexpr auto midRange = "midRange";
    inline constexpr auto highRangeEnabled = "highRangeEnabled";
    inline constexpr auto highRange = "highRange";

    // Per-band Mute/Solo (M1): applied at the summing stage in
    // TriptychEngine, not inside BandCompressor itself - muting/soloing is a
    // mix-bus decision, not part of a band's own dynamics processing.
    // Standard console semantics: Mute always silences its band; if any band
    // is soloed, only soloed (and unmuted) bands reach the sum.
    inline constexpr auto lowMute = "lowMute";
    inline constexpr auto lowSolo = "lowSolo";
    inline constexpr auto midMute = "midMute";
    inline constexpr auto midSolo = "midSolo";
    inline constexpr auto highMute = "highMute";
    inline constexpr auto highSolo = "highSolo";

    // High-band limiter option (M1): an optional juce::dsp::Limiter stage
    // after the High band's compressor + makeup gain, for taming cymbal/
    // harmonic transients without retuning the band's compressor. Zero
    // added latency - juce::dsp::Limiter (JUCE 8.0.14) has no lookahead.
    inline constexpr auto highLimiterEnabled = "highLimiterEnabled";
    inline constexpr auto highLimiterThreshold = "highLimiterThreshold";

    // Master output trim, applied after the three bands are summed.
    inline constexpr auto output = "output";

    // Downward expansion / gating (v0.4.0 / GitHub issue #25): an
    // independent, per-band noise-gate/expander stage with its own
    // threshold/ratio/attack/release, reusing the same BallisticsFilter
    // detector topology as the compressor above rather than a second,
    // structurally different detection method (see
    // src/dsp/BandCompressor.h/GateGainComputer.h). `xGateEnabled` defaults
    // to off so adding these fifteen IDs never changes existing default
    // behaviour - a v0.3.0-shaped session/preset import resolves to gate
    // disabled on every band with every other gate value at its declared
    // default, the same tolerant-migration mechanism as the v0.2.0 Knee and
    // v0.3.0 Range additions (see tests/StateTests.cpp).
    inline constexpr auto lowGateEnabled = "lowGateEnabled";
    inline constexpr auto lowGateThreshold = "lowGateThreshold";
    inline constexpr auto lowGateRatio = "lowGateRatio";
    inline constexpr auto lowGateAttack = "lowGateAttack";
    inline constexpr auto lowGateRelease = "lowGateRelease";

    inline constexpr auto midGateEnabled = "midGateEnabled";
    inline constexpr auto midGateThreshold = "midGateThreshold";
    inline constexpr auto midGateRatio = "midGateRatio";
    inline constexpr auto midGateAttack = "midGateAttack";
    inline constexpr auto midGateRelease = "midGateRelease";

    inline constexpr auto highGateEnabled = "highGateEnabled";
    inline constexpr auto highGateThreshold = "highGateThreshold";
    inline constexpr auto highGateRatio = "highGateRatio";
    inline constexpr auto highGateAttack = "highGateAttack";
    inline constexpr auto highGateRelease = "highGateRelease";
}
