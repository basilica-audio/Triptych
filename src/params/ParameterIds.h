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

    // Low band: threshold/ratio/attack/release/makeup, applied below
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

    // Master output trim, applied after the three bands are summed.
    inline constexpr auto output = "output";
}
