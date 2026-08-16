#include "dsp/MidSideCodec.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>

// Pure-math coverage for src/dsp/MidSideCodec.h - the encode/decode
// transform itself, independent of BandCompressor's envelope/gain-computer
// behaviour (GitHub issue #24).

TEST_CASE ("MidSideCodec: encode/decode round-trips exactly for arbitrary L/R", "[dsp][midside][null]")
{
    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);

    for (int i = 0; i < 1000; ++i)
    {
        const auto left = dist (rng);
        const auto right = dist (rng);

        float mid = 0.0f;
        float side = 0.0f;
        trpt::encodeMidSide (left, right, mid, side);

        float decodedLeft = 0.0f;
        float decodedRight = 0.0f;
        trpt::decodeMidSide (mid, side, decodedLeft, decodedRight);

        CHECK (decodedLeft == Catch::Approx (left).margin (1e-6f));
        CHECK (decodedRight == Catch::Approx (right).margin (1e-6f));
    }
}

TEST_CASE ("MidSideCodec: a mono signal (L == R) encodes to Side == 0", "[dsp][midside]")
{
    for (const auto value : { -0.9f, -0.3f, 0.0f, 0.3f, 0.9f })
    {
        CAPTURE (value);

        float mid = 0.0f;
        float side = 0.0f;
        trpt::encodeMidSide (value, value, mid, side);

        CHECK (side == Catch::Approx (0.0f).margin (1e-6f));
        CHECK (mid == Catch::Approx (value * std::sqrt (2.0f)).margin (1e-5f));
    }
}

TEST_CASE ("MidSideCodec: an out-of-phase signal (L == -R) encodes to Mid == 0", "[dsp][midside]")
{
    for (const auto value : { -0.9f, -0.3f, 0.3f, 0.9f })
    {
        CAPTURE (value);

        float mid = 0.0f;
        float side = 0.0f;
        trpt::encodeMidSide (value, -value, mid, side);

        CHECK (mid == Catch::Approx (0.0f).margin (1e-6f));
    }
}

// Mono-compatibility guarantee (BandCompressor.h/MidSideCodec.h's own doc
// comments): L + R depends only on Mid, never on Side, regardless of
// whatever processing happens to Side in between encode and decode. This is
// the algebraic property that makes attenuating/boosting Side incapable of
// introducing a phase-cancellation artifact into a mono downmix.
TEST_CASE ("MidSideCodec: L + R after decode depends only on Mid, independent of Side's value", "[dsp][midside][mono-compat]")
{
    constexpr float left = 0.6f;
    constexpr float right = -0.2f;

    float mid = 0.0f;
    float originalSide = 0.0f;
    trpt::encodeMidSide (left, right, mid, originalSide);

    const auto originalMonoSum = left + right;

    // Simulate arbitrary processing having changed Side to several different
    // values (as heavy compression/expansion on the Side channel would) -
    // the decoded L + R sum must stay exactly the same in every case,
    // proving it is structurally independent of Side.
    for (const auto processedSide : { 0.0f, originalSide * 0.1f, originalSide * 5.0f, -originalSide, 0.37f })
    {
        CAPTURE (processedSide);

        float decodedLeft = 0.0f;
        float decodedRight = 0.0f;
        trpt::decodeMidSide (mid, processedSide, decodedLeft, decodedRight);

        CHECK ((decodedLeft + decodedRight) == Catch::Approx (originalMonoSum).margin (1e-5f));
    }
}
