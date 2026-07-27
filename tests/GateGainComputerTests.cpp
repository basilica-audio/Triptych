#include "dsp/BandCompressor.h"
#include "LegacyReferenceChain.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>
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

//==============================================================================
// Gate hold and hysteresis (v0.5.0, brief section 3.9).
//
// The gate's gain is a memoryless static curve of a ballistics-smoothed
// envelope, with no gain-domain smoother after it. That makes CONTINUITY the
// hard part of this feature, not the timing: pinning the target gain during
// hold and then releasing the pin steps the output in one sample, and
// hard-switching the threshold between T_open and T_close steps the curve by
// roughly H * (ratio - 1) dB - about 18 dB at H = 6 and 4:1. Both are audible
// clicks that would ship green under timing-only assertions.
//
// So T13 asserts the timing behaviour AND a binding sample-to-sample gain-step
// bound across every transition, including the worst case.

namespace
{
    juce::dsp::ProcessSpec makeGateSpec (double sampleRate, int blockSize)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (blockSize);
        spec.numChannels = 2;
        return spec;
    }

    struct GateRender
    {
        std::vector<float> gainDb;   // the gate's applied gain, in dB
        double worstStepDb = 0.0;    // largest |gain_dB[n] - gain_dB[n-1]|
    };

    // Runs a band configured as a pure gate (compressor bypassed at ratio 1:1)
    // over a caller-supplied amplitude programme and recovers the gate's
    // applied gain by dividing output by input.
    GateRender renderGate (double sampleRate,
                            int blockSize,
                            float gateThresholdDb,
                            float gateRatio,
                            float holdMs,
                            float hysteresisDb,
                            const std::function<float (double)>& amplitudeAtSeconds,
                            double durationSeconds,
                            double measurementStartSeconds = 0.1)
    {
        BandCompressor band;
        band.setRatio (1.0f);          // compressor bypassed - bit-exact identity
        band.setMakeupDb (0.0f);
        band.setGateEnabled (true);
        band.setGateThresholdDb (gateThresholdDb);
        band.setGateRatio (gateRatio);
        band.setGateAttackMs (1.0f);
        band.setGateReleaseMs (100.0f);
        band.setGateHoldMs (holdMs);
        band.setGateHysteresisDb (hysteresisDb);
        band.prepare (makeGateSpec (sampleRate, blockSize));

        juce::AudioBuffer<float> buffer (2, blockSize);

        GateRender result;
        const auto totalBlocks = static_cast<int> (durationSeconds * sampleRate) / blockSize;
        result.gainDb.reserve (static_cast<size_t> (totalBlocks * blockSize));

        auto previousDb = std::numeric_limits<double>::quiet_NaN();
        auto sampleIndex = 0;

        // The gate's own envelope follower charges from zero at the very first
        // sample, so the opening milliseconds legitimately sweep through a
        // large gain range. That start-up transient is excluded from the step
        // bound - it is the detector priming, not a discontinuity in the
        // hold/hysteresis logic under test.
        const auto measurementStartSample = static_cast<int> (measurementStartSeconds * sampleRate);

        for (int blockIndex = 0; blockIndex < totalBlocks; ++blockIndex)
        {
            std::vector<float> inputs (static_cast<size_t> (blockSize), 0.0f);

            for (int i = 0; i < blockSize; ++i)
            {
                const auto seconds = static_cast<double> (sampleIndex + i) / sampleRate;
                // A DC-like carrier keeps the envelope free of waveform
                // ripple, so the measured gain steps are the gate's own
                // behaviour rather than the programme's.
                inputs[static_cast<size_t> (i)] = amplitudeAtSeconds (seconds);

                buffer.setSample (0, i, inputs[static_cast<size_t> (i)]);
                buffer.setSample (1, i, inputs[static_cast<size_t> (i)]);
            }

            auto block = juce::dsp::AudioBlock<float> (buffer);
            band.process (block);

            for (int i = 0; i < blockSize; ++i)
            {
                const auto input = inputs[static_cast<size_t> (i)];

                if (std::abs (input) < 1.0e-7f)
                {
                    // Gain is unrecoverable from a silent sample; carry the
                    // last known value forward rather than inventing a step.
                    result.gainDb.push_back (result.gainDb.empty() ? 0.0f : result.gainDb.back());
                    continue;
                }

                const auto gain = buffer.getSample (0, i) / input;

                // Clamped to the audible range before the step is measured.
                // Below about -60 dB of gate attenuation the signal is
                // inaudible and the transfer curve's own defensive floor
                // (GateGainComputer.cpp's -100 dB level floor and reduction
                // ceiling) dominates the numbers - so steps down there say
                // nothing about clicks. A genuine discontinuity near the
                // operating point, which is where hold expiry and every
                // hysteresis transition happen, still shows at full size, and
                // a jump straight out of the audible range still registers as
                // a 60 dB step and fails.
                const auto gainDb = static_cast<double> (juce::jlimit (-60.0f, 24.0f,
                                                                        juce::Decibels::gainToDecibels (std::abs (gain), -160.0f)));

                if (! std::isnan (previousDb) && sampleIndex + i >= measurementStartSample)
                    result.worstStepDb = std::max (result.worstStepDb, std::abs (gainDb - previousDb));

                previousDb = gainDb;
                result.gainDb.push_back (static_cast<float> (gainDb));
            }

            sampleIndex += blockSize;
        }

        return result;
    }
}

// T13, part 1: hold keeps the gate open for the requested time, then lets it
// close through the curve rather than jumping.
TEST_CASE ("T13: gate hold keeps the gate open for its duration, then releases continuously", "[gate][hold]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr int blockSize = 64;
    constexpr auto holdMs = 100.0f;
    constexpr auto burstSeconds = 0.3;

    // -6 dBFS burst dropping to -80 dBFS, against a -30 dB gate threshold.
    // Deliberately not literal digital silence: the gate's applied gain is
    // recovered by dividing output by input, which is undefined at zero.
    const auto programme = [] (double seconds)
    {
        return seconds < burstSeconds ? 0.5f : 0.0001f;
    };

    const auto render = renderGate (sampleRate, blockSize, -30.0f, 8.0f, holdMs, 0.0f, programme, 1.0);

    REQUIRE (! render.gainDb.empty());

    // At 90% of the hold window the gate is still essentially open.
    const auto sampleAt = [&] (double seconds)
    {
        const auto index = static_cast<size_t> (seconds * sampleRate);
        REQUIRE (index < render.gainDb.size());
        return render.gainDb[index];
    };

    CHECK (sampleAt (burstSeconds + 0.9 * holdMs * 0.001) > -1.0f);

    // Well after the hold has expired it has genuinely closed.
    CHECK (sampleAt (burstSeconds + 0.5) < -6.0f);

    // And it got there without a click.
    INFO ("worst step = " << render.worstStepDb << " dB/sample");
    CHECK (render.worstStepDb <= 0.5);
}

// T13, part 2: retrigger. Bursts closer together than the hold time must never
// let the gate dip between them.
TEST_CASE ("T13: hold retriggers, so closely spaced bursts never dip", "[gate][hold]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr int blockSize = 64;

    // 125 ms-period bursts with a 50 ms hold: the gap between bursts is
    // shorter than hold plus the gate's own release, so the gate must stay
    // open throughout.
    const auto programme = [] (double seconds)
    {
        const auto phase = std::fmod (seconds, 0.125);
        return phase < 0.05 ? 0.5f : 0.02f;
    };

    const auto render = renderGate (sampleRate, blockSize, -30.0f, 4.0f, 50.0f, 0.0f, programme, 1.5);

    // Ignore the first burst while the envelope is still charging.
    const auto start = static_cast<size_t> (0.3 * sampleRate);
    REQUIRE (render.gainDb.size() > start);

    auto deepest = 0.0f;

    for (auto i = start; i < render.gainDb.size(); ++i)
        deepest = std::min (deepest, render.gainDb[i]);

    INFO ("deepest dip between bursts = " << deepest << " dB");
    CHECK (deepest > -3.0f);
    CHECK (render.worstStepDb <= 0.5);
}

// T13, part 3: hysteresis kills chatter. Amplitude dithering around the
// threshold must not produce repeated open/close transitions once the
// separation exceeds the dither.
TEST_CASE ("T13: hysteresis suppresses chatter around the threshold", "[gate][hysteresis]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr int blockSize = 64;
    constexpr auto thresholdDb = -30.0f;

    // +/- 1.5 dB of slow dithering around the threshold.
    const auto programme = [] (double seconds)
    {
        const auto ditherDb = 1.5 * std::sin (juce::MathConstants<double>::twoPi * 7.0 * seconds);
        return juce::Decibels::decibelsToGain (static_cast<float> (thresholdDb + ditherDb));
    };

    // Counts how often the applied gain crosses a "clearly attenuating"
    // boundary - each crossing is one audible open/close event.
    const auto countTransitions = [] (const GateRender& render, size_t start)
    {
        auto transitions = 0;
        auto attenuating = false;

        for (auto i = start; i < render.gainDb.size(); ++i)
        {
            const auto nowAttenuating = render.gainDb[i] < -1.0f;

            if (nowAttenuating != attenuating)
            {
                ++transitions;
                attenuating = nowAttenuating;
            }
        }

        return transitions;
    };

    const auto start = static_cast<size_t> (0.4 * sampleRate);

    const auto withoutHysteresis = renderGate (sampleRate, blockSize, thresholdDb, 4.0f, 0.0f, 0.0f, programme, 2.0);
    const auto withHysteresis = renderGate (sampleRate, blockSize, thresholdDb, 4.0f, 0.0f, 4.0f, programme, 2.0);

    const auto chattering = countTransitions (withoutHysteresis, start);
    const auto settled = countTransitions (withHysteresis, start);

    INFO ("transitions without hysteresis = " << chattering << ", with 4 dB = " << settled);

    // The bare gate chatters...
    CHECK (chattering > 4);

    // ...and 4 dB of hysteresis settles it completely.
    CHECK (settled == 0);
    CHECK (withHysteresis.worstStepDb <= 0.5);
}

// T13, part 4: the binding continuity bound, in the worst case the feature
// supports - maximum practical hysteresis at a steep ratio with the longest
// hold, driven straight into silence.
TEST_CASE ("T13: the gate gain never steps more than 0.5 dB per sample, worst case", "[gate][hold][hysteresis]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr int blockSize = 64;

    const auto programme = [] (double seconds)
    {
        return seconds < 0.4 ? 0.5f : 0.0005f;
    };

    const auto render = renderGate (sampleRate, blockSize, -30.0f, 4.0f, 500.0f, 6.0f, programme, 2.5);

    INFO ("worst step = " << render.worstStepDb << " dB/sample");
    CHECK (render.worstStepDb <= 0.5);
}

// T13, neutrality clause: with hold and hysteresis both at 0 the gate is
// sample-exactly the v0.4.0 gate - the same-binary A/B, not a tolerance.
TEST_CASE ("T13: hold 0 and hysteresis 0 reproduce the v0.4.0 gate exactly", "[gate][regression][neutrality]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr int blockSize = 256;

    const auto spec = makeGateSpec (sampleRate, blockSize);

    BandCompressor shipped;
    LegacyReference::Band legacy;

    const auto configure = [] (auto& band)
    {
        band.setThresholdDb (-20.0f);
        band.setRatio (2.0f);
        band.setKneePercent (50.0f);
        band.setAttackMs (10.0f);
        band.setReleaseMs (120.0f);
        band.setMakeupDb (0.0f);
        band.setGateEnabled (true);
        band.setGateThresholdDb (-38.0f);
        band.setGateRatio (6.0f);
        band.setGateAttackMs (2.0f);
        band.setGateReleaseMs (180.0f);
    };

    configure (shipped);
    configure (legacy);

    // Explicitly neutral - the code path under test.
    shipped.setGateHoldMs (0.0f);
    shipped.setGateHysteresisDb (0.0f);

    shipped.prepare (spec);
    legacy.prepare (spec);

    juce::AudioBuffer<float> shippedBuffer (2, blockSize);
    juce::AudioBuffer<float> legacyBuffer (2, blockSize);
    juce::Random random (0x9A7E);

    auto mismatches = 0;

    for (int blockIndex = 0; blockIndex < 60; ++blockIndex)
    {
        // A programme that repeatedly crosses the gate threshold in both
        // directions, so the hysteresis/hold machinery would engage if it
        // were not structurally absent.
        const auto amplitude = (blockIndex % 4 < 2) ? 0.4f : 0.004f;

        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < blockSize; ++i)
            {
                const auto value = amplitude * (random.nextFloat() * 2.0f - 1.0f);
                shippedBuffer.setSample (channel, i, value);
                legacyBuffer.setSample (channel, i, value);
            }

        auto shippedBlock = juce::dsp::AudioBlock<float> (shippedBuffer);
        shipped.process (shippedBlock);

        auto legacyBlock = juce::dsp::AudioBlock<float> (legacyBuffer);
        legacy.process (legacyBlock);

        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < blockSize; ++i)
                if (shippedBuffer.getSample (channel, i) != legacyBuffer.getSample (channel, i))
                    ++mismatches;
    }

    CHECK (mismatches == 0);
}
