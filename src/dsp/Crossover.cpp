#include "Crossover.h"

#include <algorithm>
#include <cmath>

void Crossover::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    filter.prepare (spec);

    lr2States.assign (spec.numChannels, Lr2State {});
    lr8States.assign (spec.numChannels, Lr8State {});

    updateAlternateCoefficients();
}

void Crossover::reset()
{
    filter.reset();

    std::fill (lr2States.begin(), lr2States.end(), Lr2State {});
    std::fill (lr8States.begin(), lr8States.end(), Lr8State {});
}

void Crossover::setCutoffFrequency (float newCutoffHz)
{
    lastCutoffHz = newCutoffHz;

    // The LR4 default path stays exactly the v0.4.0 call, unconditionally, so
    // its coefficient trajectory is untouched by this class gaining slopes.
    filter.setCutoffFrequency (newCutoffHz);

    if (slope != Slope::lr4)
        updateAlternateCoefficients();
}

void Crossover::setSlope (Slope newSlope)
{
    if (newSlope == slope)
        return;

    slope = newSlope;

    // Structural switch: the coefficient topology itself changes, so state is
    // cleared rather than crossfaded (see the class comment in Crossover.h).
    updateAlternateCoefficients();
    reset();
}

void Crossover::updateAlternateCoefficients()
{
    const auto nyquist = static_cast<float> (sampleRate) * 0.5f;
    const auto cutoff = juce::jlimit (10.0f, nyquist * 0.99f, lastCutoffHz);
    const auto g = std::tan (juce::MathConstants<float>::pi * cutoff / static_cast<float> (sampleRate));

    lr2G = g / (1.0f + g);

    const float qValues[2] { butterworth4Q1, butterworth4Q2 };

    for (size_t index = 0; index < 2; ++index)
    {
        auto& coefficients = lr8Coefficients[index];

        coefficients.k = 1.0f / qValues[index];
        coefficients.a1 = 1.0f / (1.0f + g * (g + coefficients.k));
        coefficients.a2 = g * coefficients.a1;
        coefficients.a3 = g * coefficients.a2;
    }
}

void Crossover::process (const juce::dsp::AudioBlock<const float>& input,
                          juce::dsp::AudioBlock<float>& lowOutput,
                          juce::dsp::AudioBlock<float>& highOutput) noexcept
{
    const auto numChannels = input.getNumChannels();
    const auto numSamples = input.getNumSamples();

    jassert (lowOutput.getNumChannels() == numChannels && lowOutput.getNumSamples() == numSamples);
    jassert (highOutput.getNumChannels() == numChannels && highOutput.getNumSamples() == numSamples);

    if (slope == Slope::lr4)
    {
        // Byte-for-byte the v0.4.0 loop - see Crossover.h's neutrality note.
        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            const auto* inputSamples = input.getChannelPointer (channel);
            auto* lowSamples = lowOutput.getChannelPointer (channel);
            auto* highSamples = highOutput.getChannelPointer (channel);

            for (size_t sample = 0; sample < numSamples; ++sample)
                filter.processSample (static_cast<int> (channel), inputSamples[sample], lowSamples[sample], highSamples[sample]);
        }

        return;
    }

    if (slope == Slope::lr2)
    {
        const auto g = lr2G;
        const auto usableChannels = juce::jmin (numChannels, lr2States.size());

        for (size_t channel = 0; channel < usableChannels; ++channel)
        {
            const auto* inputSamples = input.getChannelPointer (channel);
            auto* lowSamples = lowOutput.getChannelPointer (channel);
            auto* highSamples = highOutput.getChannelPointer (channel);
            auto& state = lr2States[channel];

            for (size_t sample = 0; sample < numSamples; ++sample)
            {
                const auto x = inputSamples[sample];

                // Lowpass chain: the same TPT one-pole run twice.
                auto low = x;

                for (auto& integrator : state.lowState)
                {
                    const auto v = (low - integrator) * g;
                    const auto lp = v + integrator;
                    integrator = lp + v;
                    low = lp;
                }

                // Highpass chain: the complementary one-pole, also run twice.
                auto high = x;

                for (auto& integrator : state.highState)
                {
                    const auto v = (high - integrator) * g;
                    const auto lp = v + integrator;
                    integrator = lp + v;
                    high = high - lp;
                }

                lowSamples[sample] = low;

                // LR2's complementary sum is LP - HP; emitting the highpass
                // already inverted keeps every caller's plain LP + HP addition
                // flat, exactly as it is for LR4.
                highSamples[sample] = -high;
            }
        }

        return;
    }

    // Slope::lr8 - squared 4th-order Butterworth, four TPT SVF sections down
    // each chain.
    const auto usableChannels = juce::jmin (numChannels, lr8States.size());

    for (size_t channel = 0; channel < usableChannels; ++channel)
    {
        const auto* inputSamples = input.getChannelPointer (channel);
        auto* lowSamples = lowOutput.getChannelPointer (channel);
        auto* highSamples = highOutput.getChannelPointer (channel);
        auto& state = lr8States[channel];

        for (size_t sample = 0; sample < numSamples; ++sample)
        {
            const auto x = inputSamples[sample];

            auto low = x;
            auto high = x;

            for (size_t section = 0; section < numLr8Sections; ++section)
            {
                const auto& coefficients = lr8Coefficients[section % 2];

                {
                    auto& integrator = state.low[section];
                    const auto v3 = low - integrator.ic2;
                    const auto v1 = coefficients.a1 * integrator.ic1 + coefficients.a2 * v3;
                    const auto v2 = integrator.ic2 + coefficients.a2 * integrator.ic1 + coefficients.a3 * v3;
                    integrator.ic1 = 2.0f * v1 - integrator.ic1;
                    integrator.ic2 = 2.0f * v2 - integrator.ic2;
                    low = v2;
                }

                {
                    auto& integrator = state.high[section];
                    const auto v3 = high - integrator.ic2;
                    const auto v1 = coefficients.a1 * integrator.ic1 + coefficients.a2 * v3;
                    const auto v2 = integrator.ic2 + coefficients.a2 * integrator.ic1 + coefficients.a3 * v3;
                    integrator.ic1 = 2.0f * v1 - integrator.ic1;
                    integrator.ic2 = 2.0f * v2 - integrator.ic2;
                    high = high - coefficients.k * v1 - v2;
                }
            }

            lowSamples[sample] = low;
            highSamples[sample] = high;
        }
    }
}
