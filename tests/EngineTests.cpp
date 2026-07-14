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

TEST_CASE ("Engine: zero-sample block is a safe no-op", "[dsp][engine][robustness]")
{
    TriptychEngine engine;

    const auto spec = makeTestSpec (2);
    engine.prepare (spec);

    juce::AudioBuffer<float> buffer (2, 0);
    juce::dsp::AudioBlock<float> block (buffer);

    CHECK_NOTHROW (engine.process (block));
}
