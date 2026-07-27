#include "dsp/Crossover.h"

#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

// Selectable crossover slopes (v0.5.0, issue #1 part 2, brief section 3.6).
// T9 in three parts: (a) the LR4 default is bit-identical to a directly
// instantiated legacy crossover, (b) every slope's low+high sum reconstructs
// the input flat within its own documented tolerance, and (c) each slope's
// measured rolloff really is 12/24/48 dB/oct.
namespace
{
    juce::dsp::ProcessSpec makeSpec (double sampleRate, int blockSize = 512, int numChannels = 2)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (blockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    struct SplitResult
    {
        std::vector<float> low;
        std::vector<float> high;
    };

    // Runs a steady sine through one crossover and returns both outputs.
    SplitResult splitSine (Crossover& crossover,
                            double sampleRate,
                            double frequencyHz,
                            int numSamples,
                            int blockSize = 512)
    {
        SplitResult result;
        result.low.resize (static_cast<size_t> (numSamples), 0.0f);
        result.high.resize (static_cast<size_t> (numSamples), 0.0f);

        juce::AudioBuffer<float> inputBuffer (2, blockSize);
        juce::AudioBuffer<float> lowBuffer (2, blockSize);
        juce::AudioBuffer<float> highBuffer (2, blockSize);

        auto position = 0;

        while (position < numSamples)
        {
            const auto thisBlock = std::min (blockSize, numSamples - position);

            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < thisBlock; ++i)
                {
                    const auto phase = juce::MathConstants<double>::twoPi * frequencyHz
                                        * static_cast<double> (position + i) / sampleRate;
                    inputBuffer.setSample (channel, i, static_cast<float> (std::sin (phase)));
                }

            const juce::dsp::AudioBlock<const float> input (juce::dsp::AudioBlock<float> (inputBuffer).getSubBlock (0, static_cast<size_t> (thisBlock)));
            auto low = juce::dsp::AudioBlock<float> (lowBuffer).getSubBlock (0, static_cast<size_t> (thisBlock));
            auto high = juce::dsp::AudioBlock<float> (highBuffer).getSubBlock (0, static_cast<size_t> (thisBlock));

            crossover.process (input, low, high);

            for (int i = 0; i < thisBlock; ++i)
            {
                result.low[static_cast<size_t> (position + i)] = lowBuffer.getSample (0, i);
                result.high[static_cast<size_t> (position + i)] = highBuffer.getSample (0, i);
            }

            position += thisBlock;
        }

        return result;
    }

    // Amplitude of a steady sine, estimated from its RMS over the settled
    // second half of a trace. Deliberately NOT the largest sampled value: at
    // 12 kHz against a 48 kHz rate there are only four samples per cycle, so
    // the sampled peak can sit up to ~0.9 dB below the true peak purely
    // because of where the samples fall - an artefact that would otherwise be
    // misread as crossover error.
    double settledAmplitude (const std::vector<float>& trace)
    {
        const auto start = trace.size() / 2;
        auto sumOfSquares = 0.0;
        auto count = 0;

        for (auto i = start; i < trace.size(); ++i)
        {
            sumOfSquares += static_cast<double> (trace[i]) * static_cast<double> (trace[i]);
            ++count;
        }

        return count > 0 ? std::sqrt (2.0 * sumOfSquares / count) : 0.0;
    }

    double settledAmplitudeDb (const std::vector<float>& trace)
    {
        return juce::Decibels::gainToDecibels (static_cast<float> (settledAmplitude (trace)), -200.0f);
    }

    // Flat-sum error in dB, against a unit-amplitude input sine.
    double sumErrorDb (const SplitResult& split)
    {
        std::vector<float> summed (split.low.size(), 0.0f);

        for (size_t i = 0; i < summed.size(); ++i)
            summed[i] = split.low[i] + split.high[i];

        return settledAmplitudeDb (summed);
    }
}

// T9a: the 24 dB/oct default is the v0.1-v0.4 path, byte for byte. The
// reference here is a directly instantiated Crossover left at its default
// slope - the same-binary A/B methodology the whole v0.5.0 neutrality story
// rests on (there are no stored golden fixtures anywhere in this repo, and
// float-exact fixtures would not be portable across architectures anyway).
TEST_CASE ("T9a: the 24 dB/oct slope is sample-exactly the legacy crossover", "[crossover][slope][regression]")
{
    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto spec = makeSpec (sampleRate);
        constexpr auto cutoff = 800.0f;

        Crossover reference;
        reference.prepare (spec);
        reference.setCutoffFrequency (cutoff);

        Crossover selectable;
        selectable.prepare (spec);
        selectable.setSlope (Crossover::Slope::lr4);
        selectable.setCutoffFrequency (cutoff);

        REQUIRE (reference.getSlope() == Crossover::Slope::lr4);

        const auto numSamples = static_cast<int> (0.25 * sampleRate);
        const auto referenceSplit = splitSine (reference, sampleRate, 1000.0, numSamples);
        const auto selectableSplit = splitSine (selectable, sampleRate, 1000.0, numSamples);

        auto mismatches = 0;

        for (size_t i = 0; i < referenceSplit.low.size(); ++i)
            if (referenceSplit.low[i] != selectableSplit.low[i] || referenceSplit.high[i] != selectableSplit.high[i])
                ++mismatches;

        INFO ("sampleRate=" << sampleRate);
        CHECK (mismatches == 0);
    }
}

// T9b: every slope's low + high sum reconstructs a flat magnitude response.
// The tolerances differ per slope on purpose: the 3-band tree is
// uncompensated (documented in Crossover.h), so a 12 dB/oct split halves and
// a 48 dB/oct split doubles the phase rotation the sum carries.
TEST_CASE ("T9b: every slope's low+high sum reconstructs the input flat", "[crossover][slope]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto cutoff = 1000.0f;

    // Eleven probes spanning the audible band, including the split itself and
    // its immediate neighbourhood where any flat-sum error is largest.
    const std::vector<double> probes { 40.0, 100.0, 250.0, 500.0, 800.0, 1000.0, 1250.0, 2000.0, 4000.0, 8000.0, 12000.0 };

    struct SlopeCase
    {
        Crossover::Slope slope;
        float toleranceDb;
        const char* name;
    };

    const std::vector<SlopeCase> cases {
        { Crossover::Slope::lr2, 0.25f, "12 dB/oct" },
        { Crossover::Slope::lr4, 0.1f, "24 dB/oct" },
        { Crossover::Slope::lr8, 0.25f, "48 dB/oct" }
    };

    for (const auto& slopeCase : cases)
    {
        for (const auto probe : probes)
        {
            Crossover crossover;
            crossover.prepare (makeSpec (sampleRate));
            crossover.setSlope (slopeCase.slope);
            crossover.setCutoffFrequency (cutoff);

            const auto split = splitSine (crossover, sampleRate, probe, static_cast<int> (0.25 * sampleRate));
            const auto sumDb = sumErrorDb (split);

            INFO (slopeCase.name << " probe=" << probe << " Hz sumDb=" << sumDb);
            CHECK (std::abs (sumDb) <= slopeCase.toleranceDb);
        }
    }
}

// T9c: the slopes are what they claim. Measured two octaves either side of
// the split, where the asymptotic rolloff has fully established itself.
TEST_CASE ("T9c: measured rolloff matches 12/24/48 dB per octave", "[crossover][slope]")
{
    constexpr auto sampleRate = 96000.0;
    constexpr auto cutoff = 1000.0f;

    struct SlopeCase
    {
        Crossover::Slope slope;
        double expectedDbPerOctave;
        double toleranceDb;
        // How far from the split the two measurement points sit, in octaves.
        // Steeper slopes reach their asymptote sooner AND run out of float
        // dynamic range sooner (48 dB/oct is already 144 dB down three
        // octaves out), so the 48 dB/oct case is measured one-to-two octaves
        // out rather than two-to-three.
        double nearOctaves;
        const char* name;
    };

    const std::vector<SlopeCase> cases {
        { Crossover::Slope::lr2, 12.0, 1.5, 2.0, "12 dB/oct" },
        { Crossover::Slope::lr4, 24.0, 2.0, 2.0, "24 dB/oct" },
        { Crossover::Slope::lr8, 48.0, 3.0, 1.0, "48 dB/oct" }
    };

    for (const auto& slopeCase : cases)
    {
        const auto measureAt = [&] (double frequencyHz)
        {
            Crossover crossover;
            crossover.prepare (makeSpec (sampleRate));
            crossover.setSlope (slopeCase.slope);
            crossover.setCutoffFrequency (cutoff);

            const auto split = splitSine (crossover, sampleRate, frequencyHz, static_cast<int> (0.3 * sampleRate));

            return std::pair<double, double> { settledAmplitudeDb (split.low), settledAmplitudeDb (split.high) };
        };

        const auto nearFactor = std::pow (2.0, slopeCase.nearOctaves);
        const auto farFactor = nearFactor * 2.0;

        // Lowpass skirt, above the split.
        const auto highSideNear = measureAt (cutoff * nearFactor);
        const auto highSideFar = measureAt (cutoff * farFactor);
        const auto lowpassSlope = highSideNear.first - highSideFar.first;

        // Highpass skirt, the mirrored distance below.
        const auto lowSideNear = measureAt (cutoff / nearFactor);
        const auto lowSideFar = measureAt (cutoff / farFactor);
        const auto highpassSlope = lowSideNear.second - lowSideFar.second;

        INFO (slopeCase.name << " lowpass=" << lowpassSlope << " dB/oct highpass=" << highpassSlope << " dB/oct");
        CHECK (lowpassSlope == Catch::Approx (slopeCase.expectedDbPerOctave).margin (slopeCase.toleranceDb));
        CHECK (highpassSlope == Catch::Approx (slopeCase.expectedDbPerOctave).margin (slopeCase.toleranceDb));
    }
}

// Switching slope is a structural change: state is cleared and processing
// stays finite and stable straight afterwards (the documented click caveat is
// about audibility, not about numerical health).
TEST_CASE ("Switching crossover slope resets cleanly and keeps processing finite", "[crossover][slope][robustness]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr int blockSize = 256;

    Crossover crossover;
    crossover.prepare (makeSpec (sampleRate, blockSize));
    crossover.setCutoffFrequency (900.0f);

    juce::AudioBuffer<float> inputBuffer (2, blockSize);
    juce::AudioBuffer<float> lowBuffer (2, blockSize);
    juce::AudioBuffer<float> highBuffer (2, blockSize);

    for (const auto slope : { Crossover::Slope::lr2, Crossover::Slope::lr8, Crossover::Slope::lr4, Crossover::Slope::lr8 })
    {
        crossover.setSlope (slope);
        CHECK (crossover.getSlope() == slope);

        for (int blockIndex = 0; blockIndex < 8; ++blockIndex)
        {
            TestHelpers::fillWithSine (inputBuffer, sampleRate, 500.0, 0.9f, blockIndex * blockSize);

            auto inputBlock = juce::dsp::AudioBlock<float> (inputBuffer);
            const juce::dsp::AudioBlock<const float> input (inputBlock);
            auto low = juce::dsp::AudioBlock<float> (lowBuffer);
            auto high = juce::dsp::AudioBlock<float> (highBuffer);

            crossover.process (input, low, high);

            CHECK (TestHelpers::allSamplesFinite (lowBuffer));
            CHECK (TestHelpers::allSamplesFinite (highBuffer));
        }
    }
}
