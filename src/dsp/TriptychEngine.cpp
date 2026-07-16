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

    // Console-style Mute/Solo resolution, shared by prepare()/reset() (to
    // prime/re-snap the smoothed gains below to a value consistent with the
    // current state) and processChunk() (to re-target them every chunk):
    // Mute always wins; if any band is soloed, only soloed (and unmuted)
    // bands reach the sum.
    float resolveBandGain (bool muted, bool soloed, bool anySoloed) noexcept
    {
        return (! muted && (! anySoloed || soloed)) ? 1.0f : 0.0f;
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
    muteSoloGainBuffer.setSize (3, numSamples);

    lowMidSplitSmoothed.reset (sampleRate, smoothingTimeSeconds);
    lowMidSplitSmoothed.setCurrentAndTargetValue (lastLowMidSplitHz);
    midHighSplitSmoothed.reset (sampleRate, smoothingTimeSeconds);
    midHighSplitSmoothed.setCurrentAndTargetValue (lastMidHighSplitHz);

    // Establish the Mute/Solo gain ramps' length/sample rate here; reset()
    // below (which prepare() always calls) snaps their current value to
    // whatever the current Mute/Solo state resolves to, so the first
    // post-prepare() process() call never ramps up from a stale/default 0 -
    // see lowGainSmoothed's doc comment in TriptychEngine.h.
    lowGainSmoothed.reset (sampleRate, smoothingTimeSeconds);
    midGainSmoothed.reset (sampleRate, smoothingTimeSeconds);
    highGainSmoothed.reset (sampleRate, smoothingTimeSeconds);

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

    // Snap the Mute/Solo gain ramps to whatever they're currently commanded
    // to be, cancelling any in-flight fade rather than leaving it to
    // continue across the discontinuity reset() itself represents (e.g. a
    // transport stop/loop).
    const auto anySoloedNow = lowSoloed || midSoloed || highSoloed;
    lowGainSmoothed.setCurrentAndTargetValue (resolveBandGain (lowMuted, lowSoloed, anySoloedNow));
    midGainSmoothed.setCurrentAndTargetValue (resolveBandGain (midMuted, midSoloed, anySoloedNow));
    highGainSmoothed.setCurrentAndTargetValue (resolveBandGain (highMuted, highSoloed, anySoloedNow));
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

void TriptychEngine::process (juce::dsp::AudioBlock<float>& block)
{
    const auto requestedSamples = block.getNumSamples();

    if (requestedSamples == 0)
        return;

    // Channel count is still defensively clamped to the per-band buffer
    // capacity established in prepare() - a host that violates its own
    // negotiated bus layout (see TriptychAudioProcessor::isBusesLayoutSupported)
    // is not a realistic scenario the way an oversized *block* is (see
    // below), so excess channels beyond capacity are trimmed rather than
    // chunked.
    const auto numChannels = juce::jmin (block.getNumChannels(), static_cast<size_t> (lowBuffer.getNumChannels()));

    if (numChannels == 0)
        return;

    // Chunk any block larger than the per-band buffer capacity established
    // in prepare() into <= capacity-sized pieces, each run through the full
    // signal chain via processChunk() below, rather than defensively
    // trimming and leaving the excess samples as unprocessed dry
    // passthrough (issue #14) - a host is free to call process() with more
    // samples than it declared via prepareToPlay()'s
    // maximumExpectedSamplesPerBlock (e.g. offline bounce/render passes
    // commonly do), and every sample the host hands us has to go through
    // the crossover/compressor/mute-solo/output chain, not just the first
    // prepared-capacity's worth.
    const auto capacity = static_cast<size_t> (lowBuffer.getNumSamples());

    if (capacity == 0)
        return;

    size_t position = 0;

    while (position < requestedSamples)
    {
        const auto chunkSamples = juce::jmin (capacity, requestedSamples - position);
        auto chunkBlock = block.getSubBlock (position, chunkSamples).getSubsetChannelBlock (0, numChannels);

        processChunk (chunkBlock);

        position += chunkSamples;
    }
}

void TriptychEngine::processChunk (juce::dsp::AudioBlock<float> workingBlock)
{
    const auto numSamples = workingBlock.getNumSamples();
    const auto numChannels = workingBlock.getNumChannels();

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

    // Per-band Mute/Solo (M1): console-style - Mute always wins; if any band
    // is soloed, only soloed (and unmuted) bands reach the sum. Each band's
    // own compressor/limiter above still runs unconditionally regardless of
    // mute/solo state, so envelope followers stay continuous and there is no
    // pop when a band is unmuted mid-playback. The resolved gain itself is
    // also smoothed (issue #13) - re-targeting a SmoothedValue with its
    // current value is a cheap no-op (see juce::SmoothedValue::setTargetValue),
    // so retargeting every chunk here is safe even when nothing has changed.
    const auto anySoloed = lowSoloed || midSoloed || highSoloed;
    lowGainSmoothed.setTargetValue (resolveBandGain (lowMuted, lowSoloed, anySoloed));
    midGainSmoothed.setTargetValue (resolveBandGain (midMuted, midSoloed, anySoloed));
    highGainSmoothed.setTargetValue (resolveBandGain (highMuted, highSoloed, anySoloed));

    // Fill this chunk's per-sample gain ramps once (outer loop over samples,
    // not channels) so every channel at a given sample index gets the exact
    // same gain and each smoother advances exactly numSamples steps, not
    // numSamples * numChannels.
    auto* lowGainRamp = muteSoloGainBuffer.getWritePointer (0);
    auto* midGainRamp = muteSoloGainBuffer.getWritePointer (1);
    auto* highGainRamp = muteSoloGainBuffer.getWritePointer (2);

    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        lowGainRamp[sample] = lowGainSmoothed.getNextValue();
        midGainRamp[sample] = midGainSmoothed.getNextValue();
        highGainRamp[sample] = highGainSmoothed.getNextValue();
    }

    // Sum the three processed bands back into the working block (the host's
    // own buffer memory).
    for (size_t channel = 0; channel < numChannels; ++channel)
    {
        auto* out = workingBlock.getChannelPointer (channel);
        const auto* lowData = lowBlock.getChannelPointer (channel);
        const auto* midData = midBlock.getChannelPointer (channel);
        const auto* highData = highBlock.getChannelPointer (channel);

        for (size_t sample = 0; sample < numSamples; ++sample)
            out[sample] = lowData[sample] * lowGainRamp[sample] + midData[sample] * midGainRamp[sample] + highData[sample] * highGainRamp[sample];
    }

    juce::dsp::ProcessContextReplacing<float> context (workingBlock);
    outputGain.process (context);
}
