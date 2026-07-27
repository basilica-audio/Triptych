#include "dsp/BandCompressor.h"
#include "dsp/Lookahead.h"
#include "dsp/TriptychEngine.h"

#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>
#include <vector>

// Lookahead (v0.5.0, src/dsp/Lookahead.h + brief section 3.4): the latency
// contract and the overshoot-proof brickwall's headline property.
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

    // A fully bypassed engine: every band at ratio 1:1 with no makeup and no
    // output trim is a bit-exact identity through the compressors, so anything
    // the output does differently is the crossover tree plus the lookahead
    // delay and nothing else.
    void configureBypassed (TriptychEngine& engine)
    {
        engine.setLowRatio (1.0f);
        engine.setMidRatio (1.0f);
        engine.setHighRatio (1.0f);

        engine.setLowMakeupDb (0.0f);
        engine.setMidMakeupDb (0.0f);
        engine.setHighMakeupDb (0.0f);
        engine.setOutputDb (0.0f);
        engine.setMixPercent (100.0f);
    }

    std::vector<float> renderThroughEngine (int lookaheadSamples,
                                             const std::vector<float>& input,
                                             double sampleRate,
                                             int blockSize)
    {
        TriptychEngine engine;
        configureBypassed (engine);
        engine.setLookaheadSamples (lookaheadSamples);
        engine.prepare (makeSpec (sampleRate, blockSize));

        auto working = input;
        juce::AudioBuffer<float> buffer (2, blockSize);

        std::vector<float> output;
        output.reserve (working.size());

        for (size_t position = 0; position < working.size(); position += static_cast<size_t> (blockSize))
        {
            const auto thisBlock = static_cast<int> (std::min<size_t> (static_cast<size_t> (blockSize), working.size() - position));

            buffer.clear();

            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < thisBlock; ++i)
                    buffer.setSample (channel, i, working[position + static_cast<size_t> (i)]);

            auto block = juce::dsp::AudioBlock<float> (buffer).getSubBlock (0, static_cast<size_t> (thisBlock));
            engine.process (block);

            for (int i = 0; i < thisBlock; ++i)
                output.push_back (buffer.getSample (0, i));
        }

        return output;
    }

    int peakIndex (const std::vector<float>& signal)
    {
        auto best = -1;
        auto bestValue = -1.0f;

        for (size_t i = 0; i < signal.size(); ++i)
        {
            const auto magnitude = std::abs (signal[i]);

            if (magnitude > bestValue)
            {
                bestValue = magnitude;
                best = static_cast<int> (i);
            }
        }

        return best;
    }
}

// T7: the latency contract, measured on the signal rather than read off the
// accessor. With every band bypassed, engaging lookahead must delay the whole
// output by exactly L samples and change nothing else - so a delay-compensated
// subtraction against the lookahead-Off render nulls completely.
TEST_CASE ("T7: lookahead delays the signal by exactly L samples and nulls when compensated", "[lookahead][latency]")
{
    constexpr int blockSize = 512;

    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        // A deterministic broadband probe: enough length to cover the longest
        // lookahead plus a settled tail.
        std::vector<float> input (static_cast<size_t> (sampleRate * 0.5), 0.0f);
        juce::Random random (0x5EED);

        for (auto& sample : input)
            sample = 0.35f * (random.nextFloat() * 2.0f - 1.0f);

        const auto reference = renderThroughEngine (0, input, sampleRate, blockSize);

        for (const auto lookaheadSeconds : { 0.0015, 0.003, 0.005 })
        {
            const auto expectedDelay = static_cast<int> (std::lround (lookaheadSeconds * sampleRate));
            REQUIRE (expectedDelay > 0);

            const auto delayed = renderThroughEngine (expectedDelay, input, sampleRate, blockSize);
            REQUIRE (delayed.size() == reference.size());

            // Delay-compensated null. Skip the first L samples (the delay
            // line's own start-up zeros) and the very first block, where both
            // renders are still priming the crossovers identically.
            auto sumOfSquares = 0.0;
            auto count = 0;

            for (size_t i = static_cast<size_t> (expectedDelay) + 64; i < reference.size(); ++i)
            {
                const auto residual = static_cast<double> (delayed[i]) - static_cast<double> (reference[i - static_cast<size_t> (expectedDelay)]);
                sumOfSquares += residual * residual;
                ++count;
            }

            const auto residualRms = count > 0 ? std::sqrt (sumOfSquares / count) : 0.0;
            const auto residualDb = juce::Decibels::gainToDecibels (static_cast<float> (residualRms), -200.0f);

            INFO ("sampleRate=" << sampleRate << " L=" << expectedDelay << " residualDb=" << residualDb);
            CHECK (residualDb <= -120.0f);
        }
    }
}

// T7 (continued): a Dirac's arrival moves by exactly L. The engine's summed
// output is an all-pass reconstruction rather than a literal identity (the
// documented, tested 3-band tree compromise), so the honest formulation is
// that the peak *shifts* by L - not that it lands at index L.
TEST_CASE ("T7: a Dirac's arrival shifts by exactly the lookahead length", "[lookahead][latency]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr int blockSize = 512;

    std::vector<float> input (4096, 0.0f);
    input[128] = 1.0f;

    const auto reference = renderThroughEngine (0, input, sampleRate, blockSize);
    const auto referencePeak = peakIndex (reference);
    REQUIRE (referencePeak >= 0);

    for (const auto lookaheadSeconds : { 0.0015, 0.003, 0.005 })
    {
        const auto expectedDelay = static_cast<int> (std::lround (lookaheadSeconds * sampleRate));
        const auto delayed = renderThroughEngine (expectedDelay, input, sampleRate, blockSize);

        INFO ("L=" << expectedDelay);
        CHECK (peakIndex (delayed) == referencePeak + expectedDelay);
    }
}

// T8: the zero-overshoot property, asserted as a property test over a
// randomised signal corpus rather than a handful of hand-picked cases. The
// proof in Lookahead.h says the output magnitude can never exceed the
// threshold for ANY program material; this is what makes that claim testable.
TEST_CASE ("T8: the lookahead brickwall never overshoots its threshold", "[lookahead][limiter][property]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr int segmentSamples = 128;
    constexpr int numSegments = 10000;
    constexpr auto thresholdDb = -3.0f;

    const auto thresholdLinear = juce::Decibels::decibelsToGain (thresholdDb);

    for (const auto lookaheadSeconds : { 0.0015, 0.003, 0.005 })
    {
        const auto lookaheadSamples = static_cast<int> (std::lround (lookaheadSeconds * sampleRate));

        trpt::LookaheadLimiter limiter;
        limiter.prepare (makeSpec (sampleRate, segmentSamples), static_cast<int> (std::lround (0.005 * sampleRate)));
        limiter.setThresholdDb (thresholdDb);
        limiter.setLookaheadSamples (lookaheadSamples);

        juce::AudioBuffer<float> buffer (2, segmentSamples);
        std::vector<float> scratch (segmentSamples, 1.0f);

        std::mt19937 rng (0xC0FFEE + lookaheadSamples);
        std::uniform_real_distribution<float> uniform (-1.0f, 1.0f);
        std::uniform_int_distribution<int> kind (0, 3);
        std::uniform_real_distribution<float> level (0.0f, 6.0f);

        auto worstOvershoot = 0.0f;
        auto phase = 0.0;

        for (int segment = 0; segment < numSegments; ++segment)
        {
            const auto shape = kind (rng);
            // Deliberately drive far above full scale: the property has to
            // hold for material that is nowhere near "well behaved".
            const auto amplitude = level (rng);

            for (int i = 0; i < segmentSamples; ++i)
            {
                auto value = 0.0f;

                switch (shape)
                {
                    case 0: // sine burst
                        phase += juce::MathConstants<double>::twoPi * 137.0 / sampleRate;
                        value = amplitude * static_cast<float> (std::sin (phase));
                        break;

                    case 1: // sparse impulses
                        value = (i % 37 == 0) ? amplitude : 0.0f;
                        break;

                    case 2: // white/pink-ish noise
                        value = amplitude * uniform (rng);
                        break;

                    default: // DC step
                        value = i < segmentSamples / 2 ? amplitude : -amplitude;
                        break;
                }

                buffer.setSample (0, i, value);
                buffer.setSample (1, i, -value);
            }

            auto block = juce::dsp::AudioBlock<float> (buffer);
            limiter.process (block, scratch.data());

            worstOvershoot = juce::jmax (worstOvershoot, TestHelpers::peakAbsolute (buffer) - thresholdLinear);

            REQUIRE (TestHelpers::allSamplesFinite (buffer));
        }

        INFO ("L=" << lookaheadSamples << " worstOvershoot=" << worstOvershoot);
        CHECK (worstOvershoot <= 1.0e-6f);
    }
}

// The same property through the band it actually ships in: BandCompressor
// routes its optional brickwall to the lookahead path whenever lookahead is
// engaged, and that band's output must respect the ceiling too.
TEST_CASE ("T8: a band's optional brickwall respects its ceiling under lookahead", "[lookahead][limiter][property]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr int blockSize = 256;
    constexpr auto thresholdDb = -6.0f;

    const auto thresholdLinear = juce::Decibels::decibelsToGain (thresholdDb);

    for (const auto lookaheadSeconds : { 0.0015, 0.003, 0.005 })
    {
        BandCompressor band;
        band.setRatio (1.0f);
        band.setMakeupDb (0.0f);
        band.setLimiterEnabled (true);
        band.setLimiterThresholdDb (thresholdDb);
        band.setLookaheadSamples (static_cast<int> (std::lround (lookaheadSeconds * sampleRate)));
        band.prepare (makeSpec (sampleRate, blockSize));

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::Random random (0xBEEF);

        auto worstOvershoot = 0.0f;

        for (int blockIndex = 0; blockIndex < 400; ++blockIndex)
        {
            const auto amplitude = 0.2f + 3.0f * random.nextFloat();

            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (channel, i, amplitude * (random.nextFloat() * 2.0f - 1.0f));

            auto block = juce::dsp::AudioBlock<float> (buffer);
            band.process (block);

            worstOvershoot = juce::jmax (worstOvershoot, TestHelpers::peakAbsolute (buffer) - thresholdLinear);
        }

        INFO ("lookaheadSeconds=" << lookaheadSeconds << " worstOvershoot=" << worstOvershoot);
        CHECK (worstOvershoot <= 1.0e-6f);
    }
}

// The primitives the brickwall is built from, checked in isolation so a
// failure in the property test above can be localised immediately.
TEST_CASE ("Sliding minimum and cascaded box smoother behave as specified", "[lookahead][primitives]")
{
    SECTION ("sliding minimum returns the minimum over its window")
    {
        constexpr int window = 5;

        trpt::SlidingMinimum slidingMinimum;
        slidingMinimum.prepare (window);
        slidingMinimum.setWindow (window);
        slidingMinimum.reset (1.0f);

        const std::vector<float> input { 0.9f, 0.4f, 0.7f, 0.2f, 0.8f, 0.6f, 0.95f, 0.99f, 0.3f, 1.0f };

        for (size_t i = 0; i < input.size(); ++i)
        {
            auto expected = 1.0f;

            for (size_t j = (i + 1 >= static_cast<size_t> (window) ? i + 1 - static_cast<size_t> (window) : 0); j <= i; ++j)
                expected = juce::jmin (expected, input[j]);

            CHECK (slidingMinimum.process (input[i]) == Catch::Approx (expected));
        }
    }

    SECTION ("cascaded box smoother converges to a constant input and never exceeds it")
    {
        constexpr int length = 8;

        trpt::CascadedBoxSmoother smoother;
        smoother.prepare (length);
        smoother.setLength (length);
        smoother.reset (1.0f);

        // Support is 2 * length - 2 samples, which is what the overshoot
        // proof relies on staying at or below the lookahead length.
        CHECK (smoother.getSupport() == 2 * length - 2);

        auto last = 1.0f;

        for (int i = 0; i < 4 * length; ++i)
        {
            const auto value = smoother.process (0.25f);

            // Monotonically descending toward the input, never below it.
            CHECK (value <= last + 1.0e-6f);
            CHECK (value >= 0.25f - 1.0e-6f);
            last = value;
        }

        CHECK (last == Catch::Approx (0.25f).margin (1.0e-5f));
    }
}
