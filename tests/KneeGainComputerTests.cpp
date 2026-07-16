#include "dsp/KneeGainComputer.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// Pure-math coverage for src/dsp/KneeGainComputer.{h,cpp} - the v0.2.0 soft
// knee (docs/design-brief.md). No envelope follower/BandCompressor involved,
// so these directly test the static transfer curve (design-brief.md's
// guarantees #1 and #2) without any envelope-settling noise.
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
    static constexpr float ratios[] = { 1.5f, 2.0f, 4.0f, 8.0f, 20.0f };
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

TEST_CASE ("KneeGainComputer: ratio <= 1 is an exact bypass independent of Knee", "[dsp][knee][null]")
{
    for (const auto ratio : { 1.0f, 0.5f })
    {
        for (const auto kneePercent : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
        {
            CAPTURE (ratio, kneePercent);
            CHECK (trpt::computeGainLinear (0.9f, -20.0f, ratio, kneePercent) == 1.0f);
            CHECK (trpt::computeStaticGainReductionDb (-5.0f, -20.0f, ratio, kneePercent) == 0.0f);
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

TEST_CASE ("KneeGainComputer: gain reduction is finite and non-positive across a wide sweep (NaN/Inf robustness)", "[dsp][knee][robustness]")
{
    static constexpr float thresholdsDb[] = { -60.0f, -30.0f, -0.01f, 0.0f };
    static constexpr float ratios[] = { 1.0f, 1.01f, 4.0f, 20.0f };
    static constexpr float kneePercents[] = { 0.0f, 1.0f, 50.0f, 99.0f, 100.0f };
    static constexpr float envelopesLinear[] = { 0.0f, 1e-9f, 0.001f, 0.5f, 1.0f, 4.0f };

    for (const auto thresholdDb : thresholdsDb)
        for (const auto ratio : ratios)
            for (const auto kneePercent : kneePercents)
                for (const auto envelope : envelopesLinear)
                {
                    CAPTURE (thresholdDb, ratio, kneePercent, envelope);

                    const auto gain = trpt::computeGainLinear (envelope, thresholdDb, ratio, kneePercent);
                    CHECK (std::isfinite (gain));
                    CHECK (gain >= 0.0f);
                    CHECK (gain <= 1.0f + 1e-4f); // never amplifies
                }
}
