#include <algorithm>
#include <cmath>
#include <vector>
#include "dsp/TriptychEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 16384; // large single block: settles both
                                          // cascaded LR4 crossovers' turn-on
                                          // transients well before the
                                          // measured tail.
    constexpr int settleSamples = 4096;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    // Bypasses all three bands (ratio 1:1, makeup 0 dB - see
    // BandCompressor.h for why that is an exact VCA-gain-1.0 identity) so
    // the only remaining processing is the two cascaded LR4 crossovers'
    // split + sum, which is flat by design (see Crossover.h /
    // tests/CrossoverTests.cpp for the single-stage version of this
    // property).
    void bypassAllBands (TriptychEngine& engine)
    {
        engine.setLowRatio (1.0f);
        engine.setLowMakeupDb (0.0f);
        engine.setMidRatio (1.0f);
        engine.setMidMakeupDb (0.0f);
        engine.setHighRatio (1.0f);
        engine.setHighMakeupDb (0.0f);
        engine.setOutputDb (0.0f);
    }

    double measureFlatSumDeviationDb (double probeFrequencyHz)
    {
        TriptychEngine engine;
        bypassAllBands (engine);

        // Thresholds deliberately non-neutral: with ratio == 1.0 the VCA
        // gain is 1.0 regardless of threshold (see BandCompressor.h), so a
        // true null test has to prove that, not just be quiet because
        // thresholds happen to sit above the signal.
        engine.setLowThresholdDb (-40.0f);
        engine.setMidThresholdDb (-40.0f);
        engine.setHighThresholdDb (-40.0f);

        engine.setLowMidSplitHz (250.0f);
        engine.setMidHighSplitHz (2500.0f);

        const auto spec = makeTestSpec (1);
        engine.prepare (spec);

        juce::AudioBuffer<float> input (1, testBlockSize);
        TestHelpers::fillWithSine (input, testSampleRate, probeFrequencyHz, 0.5f);

        juce::AudioBuffer<float> processed;
        processed.makeCopyOf (input);

        juce::dsp::AudioBlock<float> block (processed);
        engine.process (block);

        double sumOfSquaresInput = 0.0;
        double sumOfSquaresOutput = 0.0;
        int countedSamples = 0;

        const auto* inData = input.getReadPointer (0);
        const auto* outData = processed.getReadPointer (0);

        for (int i = settleSamples; i < testBlockSize; ++i)
        {
            sumOfSquaresInput += static_cast<double> (inData[i]) * static_cast<double> (inData[i]);
            sumOfSquaresOutput += static_cast<double> (outData[i]) * static_cast<double> (outData[i]);
            ++countedSamples;
        }

        REQUIRE (countedSamples > 0);

        const auto inputRms = std::sqrt (sumOfSquaresInput / static_cast<double> (countedSamples));
        const auto outputRms = std::sqrt (sumOfSquaresOutput / static_cast<double> (countedSamples));

        REQUIRE (inputRms > 0.0);

        return juce::Decibels::gainToDecibels (outputRms / inputRms);
    }
}

TEST_CASE ("Engine flat-sum null test: bypassed bands reconstruct the input within +-0.1 dB", "[dsp][engine][null]")
{
    // Spans well below Low/Mid, at both split points, in the Mid band, and
    // well above Mid/High - the full range a 3-band flat-sum has to hold
    // across.
    const double probeFrequenciesHz[] = {
        30.0, 100.0, 200.0,
        250.0, // exactly at the Low/Mid split
        500.0, 1000.0, 2000.0,
        2500.0, // exactly at the Mid/High split
        4000.0, 8000.0, 15000.0
    };

    for (const auto probeFrequencyHz : probeFrequenciesHz)
    {
        INFO ("probe frequency = " << probeFrequencyHz << " Hz");
        const auto deviationDb = measureFlatSumDeviationDb (probeFrequencyHz);
        CHECK (deviationDb == Catch::Approx (0.0).margin (0.1));
    }
}

TEST_CASE ("Engine flat-sum null test: shape (not just level) matches the input once its own all-pass phase shift is accounted for", "[dsp][engine][null]")
{
    // A stricter, time-domain version of the same property at a single
    // mid-band probe frequency. Per juce::dsp::LinkwitzRileyFilter's own
    // documentation, an LR4 crossover's low+high sum "is equivalent to an
    // all-pass filter with a flat magnitude frequency response" - i.e. it
    // is magnitude-flat but not an identity/pure delay: it has its own
    // real, frequency-dependent phase response, which the cascade of two
    // stages compounds further. Reported engine latency is correctly 0 (see
    // LatencyTests.cpp - no lookahead/oversampling is involved), but a
    // single steady-state tone's all-pass phase shift is equivalent to a
    // small time shift, so this test uses a correlation search over a
    // modest alignment window (the same technique OvertureEngine's
    // near-linearity test uses for unreported IIR group delay) rather than
    // asserting a raw, zero-shift sample match.
    TriptychEngine engine;
    bypassAllBands (engine);
    engine.setLowThresholdDb (-30.0f);
    engine.setMidThresholdDb (-30.0f);
    engine.setHighThresholdDb (-30.0f);
    engine.setLowMidSplitHz (300.0f);
    engine.setMidHighSplitHz (3000.0f);

    const auto spec = makeTestSpec (2);
    engine.prepare (spec);

    juce::AudioBuffer<float> reference (2, testBlockSize);
    TestHelpers::fillWithSine (reference, testSampleRate, 1000.0, 0.5f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    engine.process (block);

    constexpr int measureLength = testBlockSize - settleSamples;
    constexpr int maxAllPassGroupDelaySamples = 64;

    for (int channel = 0; channel < reference.getNumChannels(); ++channel)
    {
        const auto correlation = TestHelpers::bestCorrelationOverShift (
            processed.getReadPointer (channel) + settleSamples,
            reference.getReadPointer (channel) + settleSamples,
            measureLength,
            maxAllPassGroupDelaySamples);

        CHECK (correlation > 0.998);
    }
}

TEST_CASE ("Engine: a band driven above threshold reduces overall output level", "[dsp][engine]")
{
    // Low band only: loud low-frequency tone, low threshold + high ratio.
    // Mid/High bypassed and their thresholds set high so they contribute no
    // gain reduction of their own - isolates the Low band's compression.
    TriptychEngine engine;
    engine.setLowMidSplitHz (250.0f);
    engine.setMidHighSplitHz (2500.0f);

    engine.setLowThresholdDb (-30.0f);
    engine.setLowRatio (10.0f);
    engine.setLowAttackMs (0.5f);
    engine.setLowReleaseMs (50.0f);
    engine.setLowMakeupDb (0.0f);

    engine.setMidRatio (1.0f);
    engine.setMidMakeupDb (0.0f);
    engine.setHighRatio (1.0f);
    engine.setHighMakeupDb (0.0f);
    engine.setOutputDb (0.0f);

    const auto spec = makeTestSpec (2);
    engine.prepare (spec);

    juce::AudioBuffer<float> reference (2, testBlockSize);
    TestHelpers::fillWithSine (reference, testSampleRate, 100.0, 0.8f); // well inside the Low band

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    engine.process (block);

    const auto tailRms = [] (const juce::AudioBuffer<float>& buffer)
    {
        double sumOfSquares = 0.0;
        int counted = 0;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int i = settleSamples; i < buffer.getNumSamples(); ++i)
            {
                sumOfSquares += static_cast<double> (data[i]) * static_cast<double> (data[i]);
                ++counted;
            }
        }

        return counted > 0 ? std::sqrt (sumOfSquares / static_cast<double> (counted)) : 0.0;
    };

    const auto inputRms = tailRms (reference);
    const auto outputRms = tailRms (processed);

    REQUIRE (inputRms > 0.0);
    CHECK (juce::Decibels::gainToDecibels (outputRms / inputRms) < -6.0);
    CHECK (TestHelpers::allSamplesFinite (processed));
}

TEST_CASE ("Engine reset() clears crossover/compressor/gain state without crashing", "[dsp][engine]")
{
    TriptychEngine engine;
    engine.setLowRatio (6.0f);
    engine.setMidRatio (6.0f);
    engine.setHighRatio (6.0f);

    const auto spec = makeTestSpec (2);
    engine.prepare (spec);

    juce::AudioBuffer<float> buffer (2, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, 1000.0, 0.9f);

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    CHECK_NOTHROW (engine.reset());
    CHECK (TestHelpers::allSamplesFinite (buffer));

    TestHelpers::fillWithSine (buffer, testSampleRate, 1000.0, 0.9f);
    CHECK_NOTHROW (engine.process (block));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Engine: a block larger than prepared capacity is fully processed, not dry-passthrough past the boundary (issue #14)", "[dsp][engine][robustness][regression]")
{
    // Regression coverage for issue #14: TriptychEngine::process() used to
    // clamp numSamples to the per-band buffer capacity established in
    // prepare() and only ever touch that first `capacity`-sized region of
    // the host's buffer - samples at [capacity, requestedSamples) were never
    // read or written at all, silently passing the host's raw dry input
    // through untouched (including bypassing the master Output trim) if a
    // host ever called process() with more samples than it declared via
    // prepareToPlay()'s maximumExpectedSamplesPerBlock. This proves the full
    // requested block is actually run through the chain by driving a large,
    // clearly audible Output trim and checking it's applied far past the
    // old clamp boundary, not just within it.
    TriptychEngine engine;
    bypassAllBands (engine); // ratio 1:1 + makeup 0 dB on every band - see helper above
    engine.setOutputDb (-40.0f); // deliberately large and easy to distinguish from "unprocessed" (0 dB)

    constexpr int preparedCapacity = 128;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = testSampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (preparedCapacity);
    spec.numChannels = 1;
    engine.prepare (spec);

    // Deliberately much larger than the 128 declared to prepare() - the
    // exact "block larger than prepared capacity" scenario from the issue
    // title. A single process() call, matching how PluginProcessor.cpp
    // hands the engine one juce::dsp::AudioBlock over the whole host buffer
    // with no chunking loop of its own.
    constexpr int requestedSamples = 4096;
    juce::AudioBuffer<float> reference (1, requestedSamples);
    TestHelpers::fillWithSine (reference, testSampleRate, 1000.0, 0.8f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    engine.process (block);

    CHECK (TestHelpers::allSamplesFinite (processed));

    // Old (buggy) behaviour: only samples [0, 128) got the -40 dB trim;
    // samples [128, 4096) - the overwhelming majority of this buffer - were
    // raw dry passthrough at ~0 dB. Measuring a tail region that starts well
    // past the old 128-sample clamp boundary (and past the crossover's own
    // brief turn-on transient) makes that unambiguous.
    constexpr int tailStart = 1000;
    const auto* refData = reference.getReadPointer (0);
    const auto* outData = processed.getReadPointer (0);

    double sumOfSquaresInput = 0.0;
    double sumOfSquaresOutput = 0.0;

    for (int i = tailStart; i < requestedSamples; ++i)
    {
        sumOfSquaresInput += static_cast<double> (refData[i]) * static_cast<double> (refData[i]);
        sumOfSquaresOutput += static_cast<double> (outData[i]) * static_cast<double> (outData[i]);
    }

    const auto tailInputRms = std::sqrt (sumOfSquaresInput / static_cast<double> (requestedSamples - tailStart));
    const auto tailOutputRms = std::sqrt (sumOfSquaresOutput / static_cast<double> (requestedSamples - tailStart));

    REQUIRE (tailInputRms > 0.0);

    const auto tailGainDb = juce::Decibels::gainToDecibels (tailOutputRms / tailInputRms);

    // The tail (well past the old 128-sample clamp) must show the same
    // ~-40 dB Output trim as samples within the old clamp boundary, not the
    // ~0 dB an unprocessed dry passthrough would show.
    CHECK (tailGainDb == Catch::Approx (-40.0).margin (1.0));
}

TEST_CASE ("Engine: zero-sample block is a safe no-op", "[dsp][engine][robustness]")
{
    TriptychEngine engine;

    const auto spec = makeTestSpec (2);
    engine.prepare (spec);

    juce::AudioBuffer<float> buffer (2, 0);
    juce::dsp::AudioBlock<float> block (buffer);

    CHECK_NOTHROW (engine.process (block));
}

//==============================================================================
// Global Mix and per-band gain-reduction telemetry (v0.5.0, brief sections
// 3.7, 3.8 and 6).

namespace
{
    juce::dsp::ProcessSpec makeMixSpec (double sampleRate, int blockSize)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (blockSize);
        spec.numChannels = 2;
        return spec;
    }

    // A bypassed engine: ratio 1:1 on every band is a bit-exact identity
    // through the compressors, so the only thing left in the path is the
    // crossover tree (and whatever the test engages on top).
    void bypassEveryBand (TriptychEngine& engine)
    {
        engine.setLowRatio (1.0f);
        engine.setMidRatio (1.0f);
        engine.setHighRatio (1.0f);
        engine.setLowMakeupDb (0.0f);
        engine.setMidMakeupDb (0.0f);
        engine.setHighMakeupDb (0.0f);
        engine.setOutputDb (0.0f);
    }

    std::vector<float> renderEngine (TriptychEngine& engine,
                                      const std::vector<float>& probe,
                                      int blockSize)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);

        std::vector<float> output;
        output.reserve (probe.size());

        for (size_t position = 0; position + static_cast<size_t> (blockSize) <= probe.size(); position += static_cast<size_t> (blockSize))
        {
            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (channel, i, probe[position + static_cast<size_t> (i)]);

            auto block = juce::dsp::AudioBlock<float> (buffer);
            engine.process (block);

            for (int i = 0; i < blockSize; ++i)
                output.push_back (buffer.getSample (0, i));
        }

        return output;
    }
}

// T18, part 1: Mix at 100% with lookahead Off is structurally bypassed - the
// mixer is not merely at unity, it is not in the signal path at all. That
// matters because a "+ 0.0f" add can still flip a -0.0 sign, so unity is not
// automatically identity.
TEST_CASE ("T18: Mix at 100% with lookahead Off is sample-exactly the mixer-absent render", "[engine][mix][neutrality]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr int blockSize = 512;

    std::vector<float> probe (static_cast<size_t> (blockSize * 20), 0.0f);
    juce::Random random (0x31AC);

    for (auto& sample : probe)
        sample = 0.4f * (random.nextFloat() * 2.0f - 1.0f);

    TriptychEngine withoutMix;
    bypassEveryBand (withoutMix);
    withoutMix.prepare (makeMixSpec (sampleRate, blockSize));

    TriptychEngine withMixAtUnity;
    bypassEveryBand (withMixAtUnity);
    withMixAtUnity.setMixPercent (100.0f);
    withMixAtUnity.prepare (makeMixSpec (sampleRate, blockSize));

    const auto reference = renderEngine (withoutMix, probe, blockSize);
    const auto unity = renderEngine (withMixAtUnity, probe, blockSize);

    REQUIRE (reference.size() == unity.size());

    auto mismatches = 0;

    for (size_t i = 0; i < reference.size(); ++i)
        if (reference[i] != unity[i])
            ++mismatches;

    CHECK (mismatches == 0);
}

// T18, part 2: the dry path is wet-latency compensated. Asserted directly -
// at 0% mix the output IS the dry path, so an impulse must emerge exactly
// `lookaheadSamples` late, and at 50% the render must equal the analytic
// blend of the delayed dry and the wet path.
//
// Note what this test deliberately does NOT assert: that a 50/50 blend
// reproduces the input level. It cannot, and should not. Even with every band
// bypassed the wet path is the crossover tree, whose Low+Mid+High sum is
// magnitude-flat but genuinely all-pass (the documented, tested 3-band tree
// compromise) - so blending it against a phase-untouched dry signal combs by
// construction, at any mix, with or without lookahead. That is a property of
// parallel-mixing a minimum-phase multiband tree, not a latency bug, and it is
// documented in docs/manual.md.
TEST_CASE ("T18: at 0% mix the dry path emerges exactly one lookahead late", "[engine][mix][lookahead]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr int blockSize = 512;

    for (const auto lookaheadSeconds : { 0.0015, 0.003, 0.005 })
    {
        const auto lookaheadSamples = static_cast<int> (std::lround (lookaheadSeconds * sampleRate));

        TriptychEngine engine;
        bypassEveryBand (engine);
        engine.setMixPercent (0.0f);
        engine.setLookaheadSamples (lookaheadSamples);
        engine.prepare (makeMixSpec (sampleRate, blockSize));

        std::vector<float> probe (static_cast<size_t> (blockSize * 4), 0.0f);
        probe[100] = 1.0f;

        const auto output = renderEngine (engine, probe, blockSize);

        auto peakIndex = -1;
        auto peakValue = 0.0f;

        for (size_t i = 0; i < output.size(); ++i)
        {
            if (std::abs (output[i]) > peakValue)
            {
                peakValue = std::abs (output[i]);
                peakIndex = static_cast<int> (i);
            }
        }

        INFO ("L=" << lookaheadSamples);
        CHECK (peakIndex == 100 + lookaheadSamples);
        CHECK (peakValue == Catch::Approx (1.0f).margin (1.0e-5f));
    }
}

TEST_CASE ("T18: a 50% mix is the analytic blend of the delayed dry and the wet path", "[engine][mix][lookahead]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr int blockSize = 512;
    const auto lookaheadSamples = static_cast<int> (std::lround (0.003 * sampleRate));

    std::vector<float> probe (static_cast<size_t> (blockSize * 20), 0.0f);
    juce::Random random (0x6D1C);

    for (auto& sample : probe)
        sample = 0.4f * (random.nextFloat() * 2.0f - 1.0f);

    const auto renderAtMix = [&] (float mixPercent)
    {
        TriptychEngine engine;
        bypassEveryBand (engine);
        engine.setMixPercent (mixPercent);
        engine.setLookaheadSamples (lookaheadSamples);
        engine.prepare (makeMixSpec (sampleRate, blockSize));

        return renderEngine (engine, probe, blockSize);
    };

    const auto fullyDry = renderAtMix (0.0f);
    const auto fullyWet = renderAtMix (100.0f);
    const auto halfWet = renderAtMix (50.0f);

    REQUIRE (fullyDry.size() == halfWet.size());
    REQUIRE (fullyWet.size() == halfWet.size());

    // Skip the first block, where the mixer's own delay line is still priming.
    const auto start = static_cast<size_t> (blockSize);

    auto sumOfSquares = 0.0;
    auto referenceSumOfSquares = 0.0;

    for (auto i = start; i < halfWet.size(); ++i)
    {
        const auto expected = 0.5 * static_cast<double> (fullyDry[i]) + 0.5 * static_cast<double> (fullyWet[i]);
        const auto residual = static_cast<double> (halfWet[i]) - expected;

        sumOfSquares += residual * residual;
        referenceSumOfSquares += expected * expected;
    }

    const auto residualDb = juce::Decibels::gainToDecibels (
        static_cast<float> (std::sqrt (sumOfSquares / juce::jmax (1.0e-30, referenceSumOfSquares))), -200.0f);

    INFO ("residual = " << residualDb << " dB relative to the analytic blend");
    CHECK (residualDb <= -60.0f);
}

// T19: the gain-reduction taps report what the band actually did. Compared
// against the reduction measured from the output/input level ratio of a driven
// band, and against an idle band that must report exactly nothing.
TEST_CASE ("T19: per-band GR taps match the measured reduction, idle bands report zero", "[engine][metering]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr int blockSize = 512;

    TriptychEngine engine;

    // Only the Mid band compresses; Low and High stay bit-exactly bypassed.
    engine.setLowRatio (1.0f);
    engine.setHighRatio (1.0f);
    engine.setLowMakeupDb (0.0f);
    engine.setHighMakeupDb (0.0f);

    // Slow ballistics on purpose: with a fast follower a 1 kHz tone leaves
    // visible ripple on the envelope, so the deepest reduction reached inside
    // a block (what the tap publishes) sits a couple of dB below the block's
    // average reduction (what an RMS measurement sees). Ballistics well below
    // the tone's own frequency remove that ambiguity, so the two numbers
    // genuinely describe the same thing.
    engine.setMidThresholdDb (-40.0f);
    engine.setMidRatio (8.0f);
    engine.setMidKneePercent (0.0f);
    engine.setMidAttackMs (60.0f);
    engine.setMidReleaseMs (600.0f);
    engine.setMidMakeupDb (0.0f);
    engine.setOutputDb (0.0f);

    // Solo the Mid band so the engine's output really is the Mid band's
    // output. Without this, the (uncompressed) Low and High skirts still leak
    // a little 1 kHz into the sum - normally 38-56 dB down and irrelevant, but
    // once the Mid band is pulled down 26 dB they sit only ~12 dB below it and
    // shift the measured ratio by a couple of dB. That leakage is correct
    // multiband behaviour; it just is not what a per-band GR tap reports.
    engine.setMidSolo (true);

    engine.prepare (makeMixSpec (sampleRate, blockSize));

    juce::AudioBuffer<float> buffer (2, blockSize);

    // Drive a Mid-band tone well above the band's threshold and let it settle.
    auto inputRms = 0.0;
    auto outputRms = 0.0;

    for (int blockIndex = 0; blockIndex < 40; ++blockIndex)
    {
        TestHelpers::fillWithSine (buffer, sampleRate, 1000.0, 0.5f, static_cast<juce::int64> (blockIndex) * blockSize);
        const auto thisInputRms = TestHelpers::rms (buffer);

        auto block = juce::dsp::AudioBlock<float> (buffer);
        engine.process (block);

        inputRms = thisInputRms;
        outputRms = TestHelpers::rms (buffer);
    }

    const auto& meter = engine.getGainReductionMeter();

    const auto measuredReductionDb = juce::Decibels::gainToDecibels (static_cast<float> (outputRms / inputRms), -200.0f);
    const auto reportedReductionDb = meter.mid.loadCompressorDb();

    INFO ("measured=" << measuredReductionDb << " dB reported=" << reportedReductionDb << " dB");

    CHECK (reportedReductionDb < -3.0f);
    CHECK (reportedReductionDb == Catch::Approx (measuredReductionDb).margin (0.5f));

    // The bypassed bands report exactly nothing - a ratio of 1:1 is an exact
    // unity gain, so there is no rounding to allow for.
    CHECK (meter.low.loadCompressorDb() == Catch::Approx (0.0f).margin (0.01f));
    CHECK (meter.high.loadCompressorDb() == Catch::Approx (0.0f).margin (0.01f));

    // Gates are off, so every gate tap reads zero too.
    CHECK (meter.low.loadGateDb() == Catch::Approx (0.0f).margin (0.01f));
    CHECK (meter.mid.loadGateDb() == Catch::Approx (0.0f).margin (0.01f));
    CHECK (meter.high.loadGateDb() == Catch::Approx (0.0f).margin (0.01f));
}
