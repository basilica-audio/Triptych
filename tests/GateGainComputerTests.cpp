#include "dsp/GateGainComputer.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// Pure static-curve tests for src/dsp/GateGainComputer.h, mirroring
// KneeGainComputerTests.cpp's approach: no envelope follower/DSP state, just
// the transfer-curve math itself (GitHub issue #25).

TEST_CASE ("GateGainComputer: ratio == 1.0 is a bit-exact bypass regardless of threshold", "[dsp][gate][null]")
{
    for (const auto thresholdDb : { -80.0f, -50.0f, -20.0f, 0.0f })
    {
        for (const auto inputDb : { -90.0f, -60.0f, -30.0f, -10.0f, 0.0f })
        {
            CAPTURE (thresholdDb, inputDb);
            CHECK (trpt::computeGateGainReductionDb (inputDb, thresholdDb, 1.0f) == 0.0f);
        }
    }

    CHECK (trpt::computeGateGainLinear (0.001f, -40.0f, 1.0f) == 1.0f);
    CHECK (trpt::computeGateGainLinear (0.0f, -40.0f, 1.0f) == 1.0f);
}

TEST_CASE ("GateGainComputer: ratio < 1.0 is defensively treated as bypass", "[dsp][gate][null]")
{
    // The ParameterLayout excludes ratio < 1.0 entirely (see
    // ParameterLayout.cpp's addGateParameters()), but the pure function
    // itself doesn't assume its caller enforces that - a below-1.0 ratio
    // must never invert into an *upward* expander by accident.
    CHECK (trpt::computeGateGainReductionDb (-90.0f, -40.0f, 0.5f) == 0.0f);
    CHECK (trpt::computeGateGainLinear (0.0001f, -40.0f, 0.5f) == 1.0f);
}

TEST_CASE ("GateGainComputer: at/above threshold, gain change is always exactly 0 dB", "[dsp][gate]")
{
    for (const auto ratio : { 2.0f, 4.0f, 20.0f, 100.0f })
    {
        CAPTURE (ratio);
        CHECK (trpt::computeGateGainReductionDb (-40.0f, -40.0f, ratio) == Catch::Approx (0.0f).margin (1e-6));
        CHECK (trpt::computeGateGainReductionDb (-10.0f, -40.0f, ratio) == Catch::Approx (0.0f).margin (1e-6));
        CHECK (trpt::computeGateGainReductionDb (0.0f, -40.0f, ratio) == Catch::Approx (0.0f).margin (1e-6));
    }
}

TEST_CASE ("GateGainComputer: below threshold, gain reduction matches the closed-form expander formula", "[dsp][gate]")
{
    // outputDb = thresholdDb + (inputDb - thresholdDb) * ratio; reductionDb =
    // outputDb - inputDb. A 2:1 ratio, 20 dB below a -40 dB threshold (i.e.
    // inputDb == -60): outputDb = -40 + (-60 - -40) * 2 = -40 - 40 = -80,
    // reductionDb = -80 - -60 = -20 dB of extra attenuation.
    const auto reductionDb = trpt::computeGateGainReductionDb (-60.0f, -40.0f, 2.0f);
    CHECK (reductionDb == Catch::Approx (-20.0f).margin (1e-3));

    // A steeper 10:1 ratio at the same operating point: outputDb = -40 +
    // (-20) * 10 = -240, reductionDb = -240 - -60 = -180 dB (before the
    // defensive ceiling below is applied at a much larger magnitude).
    const auto steepReductionDb = trpt::computeGateGainReductionDb (-60.0f, -40.0f, 10.0f);
    CHECK (steepReductionDb == Catch::Approx (-180.0f).margin (1e-2));
}

TEST_CASE ("GateGainComputer: reduction is monotonically deeper further below threshold, for a fixed ratio", "[dsp][gate]")
{
    constexpr float thresholdDb = -50.0f;
    constexpr float ratio = 4.0f;

    const auto near = trpt::computeGateGainReductionDb (-55.0f, thresholdDb, ratio);
    const auto mid = trpt::computeGateGainReductionDb (-65.0f, thresholdDb, ratio);
    const auto deep = trpt::computeGateGainReductionDb (-75.0f, thresholdDb, ratio);

    CHECK (near < 0.0f);
    CHECK (mid < near);
    CHECK (deep < mid);
}

TEST_CASE ("GateGainComputer: computeGateGainLinear matches the dB-domain formula via Decibels round-trip", "[dsp][gate]")
{
    constexpr float thresholdDb = -50.0f;
    constexpr float ratio = 3.0f;
    constexpr float envelopeLinear = 0.01f; // -40 dBFS, above -50 dB threshold

    const auto expectedReductionDb = trpt::computeGateGainReductionDb (
        juce::Decibels::gainToDecibels (envelopeLinear), thresholdDb, ratio);
    const auto expectedGain = juce::Decibels::decibelsToGain (expectedReductionDb);

    CHECK (trpt::computeGateGainLinear (envelopeLinear, thresholdDb, ratio) == Catch::Approx (expectedGain).margin (1e-6));
}

TEST_CASE ("GateGainComputer: a pathologically deep envelope/threshold/ratio combination stays finite", "[dsp][gate][robustness]")
{
    // Digital silence against a deep threshold and a steep ratio - the
    // scenario the defensive maxGateReductionDb ceiling in
    // GateGainComputer.cpp guards against (see its own doc comment).
    const auto gain = trpt::computeGateGainLinear (0.0f, -80.0f, 100.0f);
    CHECK (std::isfinite (gain));
    CHECK (gain >= 0.0f);
    CHECK (gain <= 1.0f);
}
