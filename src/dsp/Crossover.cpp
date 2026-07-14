#include "Crossover.h"

void Crossover::prepare (const juce::dsp::ProcessSpec& spec)
{
    filter.prepare (spec);
}

void Crossover::reset()
{
    filter.reset();
}

void Crossover::setCutoffFrequency (float newCutoffHz)
{
    filter.setCutoffFrequency (newCutoffHz);
}

void Crossover::process (const juce::dsp::AudioBlock<const float>& input,
                          juce::dsp::AudioBlock<float>& lowOutput,
                          juce::dsp::AudioBlock<float>& highOutput) noexcept
{
    const auto numChannels = input.getNumChannels();
    const auto numSamples = input.getNumSamples();

    jassert (lowOutput.getNumChannels() == numChannels && lowOutput.getNumSamples() == numSamples);
    jassert (highOutput.getNumChannels() == numChannels && highOutput.getNumSamples() == numSamples);

    for (size_t channel = 0; channel < numChannels; ++channel)
    {
        const auto* inputSamples = input.getChannelPointer (channel);
        auto* lowSamples = lowOutput.getChannelPointer (channel);
        auto* highSamples = highOutput.getChannelPointer (channel);

        for (size_t sample = 0; sample < numSamples; ++sample)
            filter.processSample (static_cast<int> (channel), inputSamples[sample], lowSamples[sample], highSamples[sample]);
    }
}
