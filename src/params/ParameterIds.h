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

    // Per-band Mid/Side processing (v0.4.0 / GitHub issue #24): encodes a
    // band's stereo signal to Mid/Side (src/dsp/MidSideCodec.h's
    // equal-power, exactly-invertible transform) before that band's gain
    // computation and decodes back after it. The band's main Threshold/
    // Ratio (declared above) continue to drive the Mid component; Side
    // gets its own independent Threshold/Ratio here (the issue's documented
    // "at minimum" scope), sharing the band's Knee/Attack/Release/Range.
    // `xMidSideEnabled` defaults to off (and is a defensive no-op on any
    // non-stereo bus - see BandCompressor.h) so adding these nine IDs never
    // changes existing default behaviour - a v0.3.0-shaped session/preset
    // import resolves to M/S disabled on every band, the same tolerant
    // migration mechanism as the v0.2.0 Knee and v0.3.0 Range additions.
    inline constexpr auto lowMidSideEnabled = "lowMidSideEnabled";
    inline constexpr auto lowSideThreshold = "lowSideThreshold";
    inline constexpr auto lowSideRatio = "lowSideRatio";

    inline constexpr auto midMidSideEnabled = "midMidSideEnabled";
    inline constexpr auto midSideThreshold = "midSideThreshold";
    inline constexpr auto midSideRatio = "midSideRatio";

    inline constexpr auto highMidSideEnabled = "highMidSideEnabled";
    inline constexpr auto highSideThreshold = "highSideThreshold";
    inline constexpr auto highSideRatio = "highSideRatio";

    //==========================================================================
    // v0.5.0 "Flagship Dynamics Core" - twenty-three purely additive IDs.
    //
    // Two binding rules apply to this block and are asserted by
    // tests/ParameterTests.cpp (T22):
    //
    //  1. Every one of these parameters is declared with juce::ParameterID's
    //     versionHint set to *2*, not 1. The 59 IDs above all carry hint 1.
    //     JUCE 8's versionHint exists precisely so that parameters added in a
    //     later release sort AFTER every older one for index-identifying hosts
    //     (AUv2/Logic automation lanes are index-, not name-keyed). Declaring
    //     these with hint 1 would interleave them into the shipped AU
    //     parameter order and silently break v0.4.0 session automation.
    //
    //  2. They are declared as one appended block at the very END of
    //     createParameterLayout(), after every existing declaration - never
    //     interleaved into the per-band helpers, even though house style
    //     groups parameters per band and v0.2's Knee was itself interleaved
    //     mid-band. With versionHint 2 the host-facing order would be safe
    //     either way, but appending keeps the declaration order, this file's
    //     append-only order and the host order visibly consistent.
    //
    // Every default is neutral: a fresh v0.5.0 instance renders bit-identically
    // to v0.4.0 (tests/StateTests.cpp, T1).

    // External sidechain (issue #1, part 1). `scSource` selects between the
    // band's own signal and the (optional, disabled-by-default) sidechain
    // bus; when External is selected but the host has not connected the bus,
    // the engine falls back to Internal silently. `scListen` monitors a single
    // band's *detector key* - deliberately not the same thing as soloing that
    // band's audio.
    inline constexpr auto scSource = "scSource";
    inline constexpr auto scListen = "scListen";

    // Selectable crossover slopes (issue #1, part 2): 12/24/48 dB/oct,
    // applying to both split points. 24 dB/oct is the v0.1-v0.4 LR4 default.
    inline constexpr auto crossoverSlope = "crossoverSlope";

    // Lookahead: Off / 1.5 / 3 / 5 ms, global. Off (the default) keeps the
    // reported latency at exactly 0, which is what every existing session has.
    inline constexpr auto lookahead = "lookahead";

    // Global dry/wet mix, applied around the whole multiband chain and
    // wet-latency compensated. 100% (fully wet) is the v0.4.0 behaviour.
    inline constexpr auto mix = "mix";

    // Per-band detector controls (see src/dsp/Detector.h): detection law,
    // program-dependent auto release, VCA character, stereo link amount; plus
    // the gate's new hold time and hysteresis (see src/dsp/BandCompressor.h).
    inline constexpr auto lowDetectorMode = "lowDetectorMode";
    inline constexpr auto lowAutoRelease = "lowAutoRelease";
    inline constexpr auto lowCharacter = "lowCharacter";
    inline constexpr auto lowStereoLink = "lowStereoLink";
    inline constexpr auto lowGateHold = "lowGateHold";
    inline constexpr auto lowGateHysteresis = "lowGateHysteresis";

    inline constexpr auto midDetectorMode = "midDetectorMode";
    inline constexpr auto midAutoRelease = "midAutoRelease";
    inline constexpr auto midCharacter = "midCharacter";
    inline constexpr auto midStereoLink = "midStereoLink";
    inline constexpr auto midGateHold = "midGateHold";
    inline constexpr auto midGateHysteresis = "midGateHysteresis";

    inline constexpr auto highDetectorMode = "highDetectorMode";
    inline constexpr auto highAutoRelease = "highAutoRelease";
    inline constexpr auto highCharacter = "highCharacter";
    inline constexpr auto highStereoLink = "highStereoLink";
    inline constexpr auto highGateHold = "highGateHold";
    inline constexpr auto highGateHysteresis = "highGateHysteresis";
}
