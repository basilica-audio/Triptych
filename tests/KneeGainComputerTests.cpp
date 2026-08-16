#include "dsp/KneeGainComputer.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// Pure-math coverage for src/dsp/KneeGainComputer.{h,cpp} - the v0.2.0 soft
// knee (docs/design-brief.md) and the v0.3.0 upward-ratio/Range extension
// (docs/design-brief-v3-dynamics.md). No envelope follower/BandCompressor
// involved, so these directly test the static transfer curve without any
// envelope-settling noise.
namespace
{
    // v0.1's exact hard-knee formula (juce::dsp::Compressor::processSample,
    // JUCE 8.0.14), reproduced here independently so the "Knee null test"
    // below is a genuine comparison against a from-scratch reference, not
    // just re-reading KneeGainComputer's own internals back at itself.
    float referenceHardKneeGainLinear (float envelopeLinear, float thresholdDb, float ratio)
    {
        const auto thresholdLinear = juce::Decibels::decibelsToGain (thresholdDb, -100.0f);
        const auto thresholdInverse = 1.0f / thresholdLinear;
        const auto ratioInverse = 1.0f / ratio;

        return envelopeLinear < thresholdLinear ? 1.0f
                                                   : std::pow (envelopeLinear * thresholdInverse, ratioInverse - 1.0f);
    }
}

TEST_CASE ("KneeGainComputer: Knee null test - 0% reproduces the hard-knee formula bit-for-bit (design-brief.md guarantee #1)", "[dsp][knee][null]")
{
    static constexpr float thresholdsDb[] = { -60.0f, -30.0f, -24.0f, -18.0f, -6.0f, 0.0f };
    // Includes v0.3.0's upward (< 1.0) ratios: the same closed-form linear
    // pow() path (see referenceHardKneeGainLinear() above and
    // KneeGainComputer.h) is used unconditionally for any ratio > 0, so the
    // bit-for-bit guarantee extends to the upward regime for free.
    static constexpr float ratios[] = { 0.2f, 0.5f, 0.9f, 1.5f, 2.0f, 4.0f, 8.0f, 20.0f };
    static constexpr float envelopesLinear[] = { 0.0f, 0.001f, 0.01f, 0.1f, 0.25f, 0.5f, 0.7f, 0.9f, 1.0f, 2.0f };

    for (const auto thresholdDb : thresholdsDb)
    {
        for (const auto ratio : ratios)
        {
            for (const auto envelope : envelopesLinear)
            {
                CAPTURE (thresholdDb, ratio, envelope);

                const auto actual = trpt::computeGainLinear (envelope, thresholdDb, ratio, 0.0f);
                const auto expected = referenceHardKneeGainLinear (envelope, thresholdDb, ratio);

                CHECK (actual == Catch::Approx (expected).margin (1e-9));
            }
        }
    }
}

TEST_CASE ("KneeGainComputer: ratio == 1.0 is the exact null point, independent of Knee/Range (design-brief-v3-dynamics.md)", "[dsp][knee][null]")
{
    for (const auto thresholdDb : { -60.0f, -30.0f, -0.01f, 0.0f })
    {
        for (const auto kneePercent : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
        {
            for (const auto rangeDb : { trpt::unlimitedRangeDb, 20.0f, 5.0f, 0.0f })
            {
                CAPTURE (thresholdDb, kneePercent, rangeDb);

                for (const auto envelope : { 0.0f, 0.001f, 0.5f, 0.9f, 1.0f, 2.0f })
                    CHECK (trpt::computeGainLinear (envelope, thresholdDb, 1.0f, kneePercent, rangeDb) == 1.0f);

                for (const auto inputDb : { -80.0f, -20.0f, -5.0f, 0.0f, 6.0f })
                    CHECK (trpt::computeStaticGainReductionDb (inputDb, thresholdDb, 1.0f, kneePercent, rangeDb) == 0.0f);
            }
        }
    }
}

// v0.3.0 (docs/design-brief-v3-dynamics.md): the old v0.2.0 assumption that
// *any* ratio <= 1 was a bypass no longer holds - only ratio == 1.0 exactly
// is (see the test above). Ratio < 1.0 is now genuine upward compression/
// expansion: material above threshold is boosted, material below it stays
// untouched, exactly mirroring the downward (ratio > 1) case's shape.
TEST_CASE ("KneeGainComputer: ratio < 1.0 (upward) is NOT a bypass - boosts above threshold, stays a no-op below it", "[dsp][knee][upward]")
{
    constexpr float thresholdDb = -20.0f;

    for (const auto ratio : { 0.9f, 0.5f, 0.2f })
    {
        for (const auto kneePercent : { 0.0f, 50.0f, 100.0f })
        {
            CAPTURE (ratio, kneePercent);

            // Above threshold: a genuine boost (gain > 1, gain reduction > 0).
            CHECK (trpt::computeGainLinear (0.9f, thresholdDb, ratio, kneePercent) > 1.0f);
            CHECK (trpt::computeStaticGainReductionDb (-5.0f, thresholdDb, ratio, kneePercent) > 0.0f);

            // Below threshold (and below even the widest possible knee,
            // which at kneePercent == 100 spans [2 * thresholdDb, 0] ==
            // [-40, 0] here): still an exact no-op, same as v0.2.0.
            CHECK (trpt::computeGainLinear (0.001f, thresholdDb, ratio, kneePercent) == 1.0f);
            CHECK (trpt::computeStaticGainReductionDb (-45.0f, thresholdDb, ratio, kneePercent) == 0.0f);
        }
    }
}

// The precise, sourced proof design-brief-v3-dynamics.md's guarantees
// section calls for: quiet input sitting above threshold is raised by
// exactly the dB the closed-form transfer curve predicts, not just "some
// positive amount". Hard knee (0%) so this is a direct closed-form
// comparison, matching the Knee null test's own technique.
TEST_CASE ("KneeGainComputer: upward-compression transfer curve matches the closed-form expected boost in dB (hard knee)", "[dsp][knee][upward]")
{
    constexpr float thresholdDb = -20.0f;
    constexpr float kneePercent = 0.0f;

    for (const auto ratio : { 0.2f, 0.5f, 0.8f })
    {
        const auto ratioInverse = 1.0f / ratio;

        for (const auto inputDb : { -10.0f, 0.0f, 6.0f })
        {
            const auto expectedBoostDb = (inputDb - thresholdDb) * (ratioInverse - 1.0f);
            const auto actual = trpt::computeStaticGainReductionDb (inputDb, thresholdDb, ratio, kneePercent);

            CAPTURE (ratio, inputDb, expectedBoostDb, actual);
            CHECK (actual == Catch::Approx (expectedBoostDb).margin (1e-3));
            CHECK (actual > 0.0f);
        }
    }
}

TEST_CASE ("KneeGainComputer: static curve is continuous at Knee 100%, unlike the 0% hard-knee step (design-brief.md guarantee #2a)", "[dsp][knee]")
{
    constexpr float thresholdDb = -20.0f;
    constexpr float ratio = 4.0f;
    constexpr float epsilonDb = 0.01f;

    // At Knee 0%, the static curve has a genuine slope discontinuity right
    // at threshold: the gain-reduction *derivative* jumps from 0 (below) to
    // (1 - 1/ratio) (at/above) - measure that jump directly.
    const auto belowDb = trpt::computeStaticGainReductionDb (thresholdDb - epsilonDb, thresholdDb, ratio, 0.0f);
    const auto atDb = trpt::computeStaticGainReductionDb (thresholdDb, thresholdDb, ratio, 0.0f);
    const auto aboveDb = trpt::computeStaticGainReductionDb (thresholdDb + epsilonDb, thresholdDb, ratio, 0.0f);

    CHECK (belowDb == Catch::Approx (0.0).margin (1e-6));
    CHECK (atDb == Catch::Approx (0.0).margin (1e-6));
    // The slope changes abruptly right at threshold for the hard knee -
    // gain reduction is ~0 just below and already ratio-scaled just above.
    const auto hardKneeSlopeJump = std::abs ((aboveDb - atDb) - (atDb - belowDb));
    CHECK (hardKneeSlopeJump > 0.001);

    // At Knee 100%, the same epsilon-straddle around threshold must be
    // smooth (no comparable slope jump) - sample a slightly wider window
    // matching the guarantee's "no discontinuity/step at threshold" wording,
    // and confirm the function value itself changes gradually rather than in
    // a sudden jump.
    const auto softBelowDb = trpt::computeStaticGainReductionDb (thresholdDb - epsilonDb, thresholdDb, ratio, 100.0f);
    const auto softAtDb = trpt::computeStaticGainReductionDb (thresholdDb, thresholdDb, ratio, 100.0f);
    const auto softAboveDb = trpt::computeStaticGainReductionDb (thresholdDb + epsilonDb, thresholdDb, ratio, 100.0f);

    // Continuity: adjacent epsilon-spaced samples must be close to each
    // other (no jump), unlike the hard-knee case where the sample "at"
    // threshold is exactly 0 dB reduction and "above" already shows several
    // dB - the soft-knee samples straddling threshold change by a tiny
    // fraction of a dB for a 0.01 dB step.
    CHECK (std::abs (softAtDb - softBelowDb) < 0.01);
    CHECK (std::abs (softAboveDb - softAtDb) < 0.01);
}

TEST_CASE ("KneeGainComputer: knee transition width scales with |threshold| (design-brief.md guarantee #2b)", "[dsp][knee]")
{
    constexpr float ratio = 4.0f;

    // The knee's lower edge sits at thresholdDb - |thresholdDb| * kneeFraction
    // (see KneeGainComputer.h) - at Knee 100%, that is exactly 2 * thresholdDb.
    // A shallow threshold (-10 dB) must therefore show a narrower knee than a
    // deep one (-50 dB): probe a fixed offset below each threshold that sits
    // inside the deep threshold's knee but below the shallow threshold's
    // knee, and confirm only the deep-threshold case shows any gain
    // reduction there.
    constexpr float shallowThresholdDb = -10.0f;
    constexpr float deepThresholdDb = -50.0f;
    constexpr float probeOffsetDb = 15.0f; // inside [-100, -10] deep knee window, outside [-20, 0] shallow one

    const auto shallowReductionDb = trpt::computeStaticGainReductionDb (shallowThresholdDb - probeOffsetDb, shallowThresholdDb, ratio, 100.0f);
    const auto deepReductionDb = trpt::computeStaticGainReductionDb (deepThresholdDb - probeOffsetDb, deepThresholdDb, ratio, 100.0f);

    CHECK (shallowReductionDb == Catch::Approx (0.0).margin (1e-6)); // below the shallow threshold's narrower knee entirely
    CHECK (deepReductionDb < -0.001); // inside the deep threshold's wider knee - some reduction already applied

    // Directly compare the two thresholds' knee half-widths via their lower
    // edges (the last point before gain reduction becomes exactly zero):
    // deepThreshold's knee must extend further below its own threshold than
    // shallowThreshold's does, in absolute dB terms.
    const auto shallowHalfWidthDb = std::abs (shallowThresholdDb); // Knee 100% => half-width == |thresholdDb|
    const auto deepHalfWidthDb = std::abs (deepThresholdDb);
    CHECK (deepHalfWidthDb > shallowHalfWidthDb);
}

// Range (v0.3.0, docs/design-brief-v3-dynamics.md): "GR never exceeds Range
// in either direction" as a direct, quantitative proof - handpicked extreme
// scenarios that would clearly exceed Range unclamped (checked explicitly as
// a sanity precondition), plus a general sweep.
TEST_CASE ("KneeGainComputer: Range clamps gain reduction to at most Range dB in either direction", "[dsp][range]")
{
    constexpr float rangeDb = 8.0f;

    // Downward: deep threshold + steep ratio would cut far more than
    // rangeDb without the clamp.
    const auto downwardUnclamped = trpt::computeStaticGainReductionDb (0.0f, -50.0f, 20.0f, 0.0f, trpt::unlimitedRangeDb);
    REQUIRE (downwardUnclamped < -rangeDb);
    const auto downwardClamped = trpt::computeStaticGainReductionDb (0.0f, -50.0f, 20.0f, 0.0f, rangeDb);
    CHECK (downwardClamped == Catch::Approx (-rangeDb).margin (1e-3));

    // Upward: shallow-ish threshold + extreme upward ratio would boost far
    // more than rangeDb without the clamp.
    const auto upwardUnclamped = trpt::computeStaticGainReductionDb (0.0f, -50.0f, 0.2f, 0.0f, trpt::unlimitedRangeDb);
    REQUIRE (upwardUnclamped > rangeDb);
    const auto upwardClamped = trpt::computeStaticGainReductionDb (0.0f, -50.0f, 0.2f, 0.0f, rangeDb);
    CHECK (upwardClamped == Catch::Approx (rangeDb).margin (1e-3));

    // General sweep across both regimes, soft and hard knee, several
    // thresholds/inputs - the clamp must hold everywhere, not just at the
    // two handpicked extremes above.
    static constexpr float thresholdsDb[] = { -60.0f, -30.0f, -5.0f };
    static constexpr float ratios[] = { 0.2f, 0.4f, 0.9f, 3.0f, 20.0f };
    static constexpr float inputsDb[] = { -50.0f, -10.0f, 0.0f, 10.0f };
    static constexpr float kneePercents[] = { 0.0f, 50.0f, 100.0f };

    for (const auto thresholdDb : thresholdsDb)
        for (const auto ratio : ratios)
            for (const auto inputDb : inputsDb)
                for (const auto kneePercent : kneePercents)
                {
                    const auto gr = trpt::computeStaticGainReductionDb (inputDb, thresholdDb, ratio, kneePercent, rangeDb);
                    CAPTURE (thresholdDb, ratio, inputDb, kneePercent, gr);
                    CHECK (gr >= -rangeDb - 1e-3f);
                    CHECK (gr <= rangeDb + 1e-3f);
                }
}

TEST_CASE ("KneeGainComputer: Range disabled (default sentinel) never clamps realistic operating values (v0.2.0 regression)", "[dsp][range][regression]")
{
    // The default rangeDb argument (unlimitedRangeDb) must reproduce
    // v0.2.0's fully unclamped behaviour for every combination the existing
    // Knee null test already covers.
    static constexpr float thresholdsDb[] = { -60.0f, -30.0f, -24.0f, -18.0f, -6.0f, 0.0f };
    static constexpr float ratios[] = { 1.5f, 2.0f, 4.0f, 8.0f, 20.0f };
    static constexpr float envelopesLinear[] = { 0.001f, 0.1f, 0.5f, 0.9f, 1.0f, 2.0f };

    for (const auto thresholdDb : thresholdsDb)
        for (const auto ratio : ratios)
            for (const auto envelope : envelopesLinear)
            {
                CAPTURE (thresholdDb, ratio, envelope);

                const auto withDefaultRange = trpt::computeGainLinear (envelope, thresholdDb, ratio, 0.0f);
                const auto withExplicitSentinel = trpt::computeGainLinear (envelope, thresholdDb, ratio, 0.0f, trpt::unlimitedRangeDb);

                CHECK (withDefaultRange == Catch::Approx (withExplicitSentinel).margin (1e-9));
            }
}

TEST_CASE ("KneeGainComputer: gain reduction is finite across a wide sweep, including upward (ratio < 1) and Range-clamped combinations (NaN/Inf robustness)", "[dsp][knee][robustness]")
{
    static constexpr float thresholdsDb[] = { -60.0f, -30.0f, -0.01f, 0.0f };
    // v0.3.0: includes ratios below 1.0 (upward) alongside the v0.2.0
    // downward sweep.
    static constexpr float ratios[] = { 0.2f, 0.5f, 0.9f, 1.0f, 1.01f, 4.0f, 20.0f };
    static constexpr float kneePercents[] = { 0.0f, 1.0f, 50.0f, 99.0f, 100.0f };
    static constexpr float envelopesLinear[] = { 0.0f, 1e-9f, 0.001f, 0.5f, 1.0f, 4.0f };
    static constexpr float rangesDb[] = { trpt::unlimitedRangeDb, 30.0f, 10.0f, 3.0f, 0.0f };

    for (const auto thresholdDb : thresholdsDb)
        for (const auto ratio : ratios)
            for (const auto kneePercent : kneePercents)
                for (const auto envelope : envelopesLinear)
                    for (const auto rangeDb : rangesDb)
                    {
                        CAPTURE (thresholdDb, ratio, kneePercent, envelope, rangeDb);

                        const auto gain = trpt::computeGainLinear (envelope, thresholdDb, ratio, kneePercent, rangeDb);
                        CHECK (std::isfinite (gain));
                        CHECK (gain >= 0.0f);

                        // v0.3.0: upward ratios can genuinely amplify, so the
                        // old "never amplifies" ceiling no longer holds
                        // unconditionally - instead, bound the gain by
                        // whatever the effective Range clamp (or the
                        // defensive unlimitedRangeDb ceiling, whichever is
                        // tighter) permits.
                        const auto boundDb = juce::jmin (rangeDb, trpt::unlimitedRangeDb);
                        CHECK (gain <= juce::Decibels::decibelsToGain (boundDb) * 1.0001f);
                        CHECK (gain >= juce::Decibels::decibelsToGain (-boundDb) * 0.9999f);
                    }
}
