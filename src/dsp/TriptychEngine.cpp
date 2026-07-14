#include "TriptychEngine.h"

namespace
{
    // Keeps a requested split frequency safely below Nyquist regardless of
    // host sample rate, so juce::dsp::LinkwitzRileyFilter::setCutoffFrequency
    // never receives an out-of-range value (its own jassert requires
    // strictly below Nyquist). Matches OvertureEngine's clampBelowNyquist.
    float clampBelowNyquist (float frequencyHz, double sampleRate) noexcept
    {
        const auto nyquist = static_cast<float> (sampleRate) * 0.5f;
        return juce::jlimit (10.0f, nyquist * 0.9f, frequencyHz);
    }
}

TriptychEngine::TriptychEngine() = default;

void TriptychEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    lowMidCrossover.prepare (spec);
    midHighCrossover.prepare (spec);

    lowBand.prepare (spec);
    midBand.prepare (spec);
    highBand.prepare (spec);

    outputGain.setRampDurationSeconds (smoothingTimeSeconds);
    outputGain.prepare (spec);

    const auto numChannels = static_cast<int> (spec.numChannels);
    const auto numSamples = static_cast<int> (spec.maximumBlockSize);

    lowBuffer.setSize (numChannels, numSamples);
    midHighBuffer.setSize (numChannels, numSamples);
    midBuffer.setSize (numChannels, numSamples);
    highBuffer.setSize (numChannels, numSamples);

    lowMidSplitSmoothed.reset (sampleRate, smoothingTimeSeconds);
    lowMidSplitSmoothed.setCurrentAndTargetValue (lastLowMidSplitHz);
    midHighSplitSmoothed.reset (sampleRate, smoothingTimeSeconds);
    midHighSplitSmoothed.setCurrentAndTargetValue (lastMidHighSplitHz);

    lowMuteSoloGain.reset (sampleRate, smoothingTimeSeconds);
    midMuteSoloGain.reset (sampleRate, smoothingTimeSeconds);
    highMuteSoloGain.reset (sampleRate, smoothingTimeSeconds);
    // Snap (don't ramp) to the current Mute/Solo state: re-preparing (e.g. a
    // sample-rate change) must not introduce a spurious fade on top of a
    // state the user already committed to.
    updateMuteSoloGainTargets (true);

    reset();

    // Prime both crossovers' coefficients immediately so the very first
    // process() call runs with correct, non-default cutoffs.
    const auto lowMidHz = clampBelowNyquist (lastLowMidSplitHz, sampleRate);
    const auto midHighHz = clampBelowNyquist (juce::jmax (lastMidHighSplitHz, lastLowMidSplitHz + minimumSplitSeparationHz), sampleRate);

    lowMidCrossover.setCutoffFrequency (lowMidHz);
    midHighCrossover.setCutoffFrequency (midHighHz);
}

void TriptychEngine::reset()
{
    lowMidCrossover.reset();
    midHighCrossover.reset();
    lowBand.reset();
    midBand.reset();
    highBand.reset();
    outputGain.reset();
}

void TriptychEngine::setLowMidSplitHz (float newFrequencyHz)
{
    lastLowMidSplitHz = newFrequencyHz;
    lowMidSplitSmoothed.setTargetValue (newFrequencyHz);
}

void TriptychEngine::setMidHighSplitHz (float newFrequencyHz)
{
    lastMidHighSplitHz = newFrequencyHz;
    midHighSplitSmoothed.setTargetValue (newFrequencyHz);
}

void TriptychEngine::setOutputDb (float newOutputDb)
{
    outputGain.setGainDecibels (newOutputDb);
}

void TriptychEngine::updateMuteSoloGainTargets (bool snapImmediately)
{
    const auto anySoloed = lowSoloed || midSoloed || highSoloed;

    const auto lowTarget = (! lowMuted && (! anySoloed || lowSoloed)) ? 1.0f : 0.0f;
    const auto midTarget = (! midMuted && (! anySoloed || midSoloed)) ? 1.0f : 0.0f;
    const auto highTarget = (! highMuted && (! anySoloed || highSoloed)) ? 1.0f : 0.0f;

    if (snapImmediately)
    {
        lowMuteSoloGain.setCurrentAndTargetValue (lowTarget);
        midMuteSoloGain.setCurrentAndTargetValue (midTarget);
        highMuteSoloGain.setCurrentAndTargetValue (highTarget);
    }
    else
    {
        lowMuteSoloGain.setTargetValue (lowTarget);
        midMuteSoloGain.setTargetValue (midTarget);
        highMuteSoloGain.setTargetValue (highTarget);
    }
}

void TriptychEngine::setLowMute (bool shouldBeMuted) noexcept
{
    lowMuted = shouldBeMuted;
    updateMuteSoloGainTargets (false);
}

void TriptychEngine::setLowSolo (bool shouldBeSoloed) noexcept
{
    lowSoloed = shouldBeSoloed;
    updateMuteSoloGainTargets (false);
}

void TriptychEngine::setMidMute (bool shouldBeMuted) noexcept
{
    midMuted = shouldBeMuted;
    updateMuteSoloGainTargets (false);
}

void TriptychEngine::setMidSolo (bool shouldBeSoloed) noexcept
{
    midSoloed = shouldBeSoloed;
    updateMuteSoloGainTargets (false);
}

void TriptychEngine::setHighMute (bool shouldBeMuted) noexcept
{
    highMuted = shouldBeMuted;
    updateMuteSoloGainTargets (false);
}

void TriptychEngine::setHighSolo (bool shouldBeSoloed) noexcept
{
    highSoloed = shouldBeSoloed;
    updateMuteSoloGainTargets (false);
}

void TriptychEngine::process (juce::dsp::AudioBlock<float>& block)
{
    const auto requestedSamples = block.getNumSamples();

    if (requestedSamples == 0)
        return;

    // Defensive: clamp to the per-band buffer capacity established in
    // prepare(), in case a host ever calls process() with more samples (or
    // channels) than it promised via prepareToPlay() - trimming the working
    // block rather than writing out of bounds.
    const auto numSamples = juce::jmin (requestedSamples, static_cast<size_t> (lowBuffer.getNumSamples()));
    const auto numChannels = juce::jmin (block.getNumChannels(), static_cast<size_t> (lowBuffer.getNumChannels()));

    if (numSamples == 0 || numChannels == 0)
        return;

    auto workingBlock = block.getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels);

    // Coefficient recomputation involves trig calls, so split frequencies
    // are smoothed and re-derived once per block rather than per sample -
    // the same real-time-safe compromise OvertureEngine uses for its filter
    // cutoffs.
    const auto lowMidHz = clampBelowNyquist (lowMidSplitSmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    const auto midHighHz = clampBelowNyquist (
        juce::jmax (midHighSplitSmoothed.skip (static_cast<int> (numSamples)), lowMidHz + minimumSplitSeparationHz),
        sampleRate);

    lowMidCrossover.setCutoffFrequency (lowMidHz);
    midHighCrossover.setCutoffFrequency (midHighHz);

    auto lowBlock = juce::dsp::AudioBlock<float> (lowBuffer).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels);
    auto midHighBlock = juce::dsp::AudioBlock<float> (midHighBuffer).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels);
    auto midBlock = juce::dsp::AudioBlock<float> (midBuffer).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels);
    auto highBlock = juce::dsp::AudioBlock<float> (highBuffer).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels);

    const juce::dsp::AudioBlock<const float> inputBlockConst (workingBlock);
    lowMidCrossover.process (inputBlockConst, lowBlock, midHighBlock);

    const juce::dsp::AudioBlock<const float> midHighBlockConst (midHighBlock);
    midHighCrossover.process (midHighBlockConst, midBlock, highBlock);

    lowBand.process (lowBlock);
    midBand.process (midBlock);
    highBand.process (highBlock);

    // Per-band Mute/Solo (M1): console-style target gain (Mute always wins;
    // if any band is soloed, only soloed-and-unmuted bands reach the sum),
    // but applied sample-accurately via lowMuteSoloGain/midMuteSoloGain/
    // highMuteSoloGain rather than as an instantaneous 0/1 step, so a toggle
    // mid-playback ramps over smoothingTimeSeconds instead of clicking (see
    // updateMuteSoloGainTargets()). Each band's own compressor/limiter above
    // still runs unconditionally regardless of mute/solo state, so envelope
    // followers stay continuous too.
    //
    // Sum the three processed bands back into the working block (the host's
    // own buffer memory). Sample-outer/channel-inner so each sample only
    // advances the three gain ramps once, applying the same instantaneous
    // gain to every channel.
    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        const auto lowGain = lowMuteSoloGain.getNextValue();
        const auto midGain = midMuteSoloGain.getNextValue();
        const auto highGain = highMuteSoloGain.getNextValue();

        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            auto* out = workingBlock.getChannelPointer (channel);
            const auto* lowData = lowBlock.getChannelPointer (channel);
            const auto* midData = midBlock.getChannelPointer (channel);
            const auto* highData = highBlock.getChannelPointer (channel);

            out[sample] = lowData[sample] * lowGain + midData[sample] * midGain + highData[sample] * highGain;
        }
    }

    juce::dsp::ProcessContextReplacing<float> context (workingBlock);
    outputGain.process (context);
}
