#include "dsp/BandCompressor.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 4096;
    constexpr double testFrequencyHz = 500.0;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }
}

TEST_CASE ("BandCompressor: ratio 1:1 + makeup 0 dB is an exact bypass", "[dsp][compressor][null]")
{
    // Threshold is deliberately set well inside the signal's level, so a
    // true bypass test has to prove the VCA gain is unconditionally 1.0
    // (see BandCompressor.h) rather than just "quiet enough to not matter".
    BandCompressor band;
    band.setThresholdDb (-40.0f);
    band.setRatio (1.0f);
    band.setAttackMs (1.0f);
    band.setReleaseMs (50.0f);
    band.setMakeupDb (0.0f);

    const auto spec = makeTestSpec (2);
    band.prepare (spec);

    juce::AudioBuffer<float> reference (2, testBlockSize);
    TestHelpers::fillWithSine (reference, testSampleRate, testFrequencyHz, 0.9f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    band.process (block);

    for (int channel = 0; channel < reference.getNumChannels(); ++channel)
    {
        const auto* refData = reference.getReadPointer (channel);
        const auto* outData = processed.getReadPointer (channel);

        for (int i = 0; i < testBlockSize; ++i)
            CHECK (outData[i] == Catch::Approx (refData[i]).margin (1e-6));
    }
}

TEST_CASE ("BandCompressor: a signal above threshold receives measurable gain reduction", "[dsp][compressor]")
{
    BandCompressor band;
    band.setThresholdDb (-24.0f);
    band.setRatio (8.0f);
    band.setAttackMs (0.5f);
    band.setReleaseMs (50.0f);
    band.setMakeupDb (0.0f);

    const auto spec = makeTestSpec (2);
    band.prepare (spec);

    // -3 dBFS sine, comfortably above the -24 dB threshold, run for enough
    // samples that the envelope follower settles into steady-state gain
    // reduction well before the end of the block.
    juce::AudioBuffer<float> reference (2, testBlockSize);
    TestHelpers::fillWithSine (reference, testSampleRate, testFrequencyHz, 0.7f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    band.process (block);

    // Measure RMS over the settled tail (skip the attack transient).
    constexpr int settleSamples = testBlockSize / 2;

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

    const auto gainReductionDb = juce::Decibels::gainToDecibels (outputRms / inputRms);

    // An 8:1 ratio, 18 dB above threshold, should compress substantially:
    // ideal steady-state gain reduction is about (1 - 1/ratio) * 18 ~= 15.75
    // dB; assert at least several dB of measurable reduction rather than
    // pinning the exact ideal number, since the envelope follower's own
    // ripple/settling behaviour is not the property under test here.
    CHECK (gainReductionDb < -6.0);
    CHECK (TestHelpers::allSamplesFinite (processed));
}

TEST_CASE ("BandCompressor: reset() clears envelope/gain-ramp state without crashing", "[dsp][compressor]")
{
    BandCompressor band;
    band.setThresholdDb (-20.0f);
    band.setRatio (6.0f);
    band.setMakeupDb (6.0f);

    const auto spec = makeTestSpec (2);
    band.prepare (spec);

    juce::AudioBuffer<float> buffer (2, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, 0.9f);

    juce::dsp::AudioBlock<float> block (buffer);
    band.process (block);

    CHECK_NOTHROW (band.reset());
    CHECK (TestHelpers::allSamplesFinite (buffer));

    TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, 0.9f);
    CHECK_NOTHROW (band.process (block));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}
