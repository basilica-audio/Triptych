#include "TriptychEngine.h"

#include <algorithm>

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
    sidechainLowMidCrossover.prepare (spec);
    sidechainMidHighCrossover.prepare (spec);

    lowMidCrossover.setSlope (crossoverSlope);
    midHighCrossover.setSlope (crossoverSlope);
    sidechainLowMidCrossover.setSlope (crossoverSlope);
    sidechainMidHighCrossover.setSlope (crossoverSlope);

    lowBand.setLookaheadSamples (lookaheadSamples);
    midBand.setLookaheadSamples (lookaheadSamples);
    highBand.setLookaheadSamples (lookaheadSamples);

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

    sidechainInputBuffer.setSize (numChannels, numSamples);
    sidechainLowBuffer.setSize (numChannels, numSamples);
    sidechainMidHighBuffer.setSize (numChannels, numSamples);
    sidechainMidBuffer.setSize (numChannels, numSamples);
    sidechainHighBuffer.setSize (numChannels, numSamples);
    listenBuffer.setSize (numChannels, numSamples);

    // DryWetMixer house gotcha (JUCE 8.0.14): prime the wet proportion BEFORE
    // reset(), otherwise the mixer starts its own internal ramp from 100% wet
    // and the first block after prepare is not what the parameter says.
    dryWetMixer.prepare (spec);
    dryWetMixer.setMixingRule (juce::dsp::DryWetMixingRule::linear);
    dryWetMixer.setWetMixProportion (juce::jlimit (0.0f, 1.0f, lastMixPercent * 0.01f));
    dryWetMixer.setWetLatency (static_cast<float> (lookaheadSamples));
    dryWetMixer.reset();

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
    sidechainLowMidCrossover.reset();
    sidechainMidHighCrossover.reset();
    dryWetMixer.reset();
    gainReductionMeter.clear();
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

void TriptychEngine::setCrossoverSlope (Crossover::Slope newSlope)
{
    if (newSlope == crossoverSlope)
        return;

    crossoverSlope = newSlope;

    lowMidCrossover.setSlope (newSlope);
    midHighCrossover.setSlope (newSlope);
    sidechainLowMidCrossover.setSlope (newSlope);
    sidechainMidHighCrossover.setSlope (newSlope);
}

void TriptychEngine::setLookaheadSamples (int newLookaheadSamples) noexcept
{
    lookaheadSamples = juce::jmax (0, newLookaheadSamples);

    lowBand.setLookaheadSamples (lookaheadSamples);
    midBand.setLookaheadSamples (lookaheadSamples);
    highBand.setLookaheadSamples (lookaheadSamples);

    dryWetMixer.setWetLatency (static_cast<float> (lookaheadSamples));
}

void TriptychEngine::setMixPercent (float newMixPercent)
{
    lastMixPercent = juce::jlimit (0.0f, 100.0f, newMixPercent);
    dryWetMixer.setWetMixProportion (lastMixPercent * 0.01f);
}

void TriptychEngine::process (juce::dsp::AudioBlock<float>& block,
                               const juce::dsp::AudioBlock<const float>* sidechain)
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

    // The external sidechain is only usable if it actually carries channels
    // and covers the whole block; anything else (bus disabled, host handing us
    // a short buffer) falls back to internal keying silently.
    const auto usableSidechain = sidechain != nullptr
                                   && sidechain->getNumChannels() > 0
                                   && sidechain->getNumSamples() >= requestedSamples;

    while (position < requestedSamples)
    {
        const auto chunkSamples = juce::jmin (capacity, requestedSamples - position);
        auto chunkBlock = block.getSubBlock (position, chunkSamples).getSubsetChannelBlock (0, numChannels);

        if (usableSidechain)
        {
            const auto sidechainChannels = juce::jmin (sidechain->getNumChannels(), numChannels);
            const auto sidechainChunk = sidechain->getSubBlock (position, chunkSamples).getSubsetChannelBlock (0, sidechainChannels);

            processChunk (chunkBlock, &sidechainChunk);
        }
        else
        {
            processChunk (chunkBlock, nullptr);
        }

        position += chunkSamples;
    }
}

void TriptychEngine::processChunk (juce::dsp::AudioBlock<float> workingBlock,
                                    const juce::dsp::AudioBlock<const float>* sidechainChunk)
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

    // Global dry/wet (v0.5.0): the dry tap is taken before the split and the
    // wet return happens after the output trim, with the mixer's own wet
    // latency set to the lookahead length so the two paths stay aligned.
    // Structurally bypassed at the neutral operating point (fully wet with
    // lookahead Off), so the default signal path never enters the mixer -
    // neutrality here is structural, not arithmetic, because a "+ 0.0f" add
    // can still flip a -0.0 sign.
    const bool mixActive = lastMixPercent < 100.0f || lookaheadSamples > 0;

    if (mixActive)
    {
        const juce::dsp::AudioBlock<const float> dryBlock (workingBlock);
        dryWetMixer.pushDrySamples (dryBlock);
    }

    auto lowBlock = juce::dsp::AudioBlock<float> (lowBuffer).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels);
    auto midHighBlock = juce::dsp::AudioBlock<float> (midHighBuffer).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels);
    auto midBlock = juce::dsp::AudioBlock<float> (midBuffer).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels);
    auto highBlock = juce::dsp::AudioBlock<float> (highBuffer).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels);

    const juce::dsp::AudioBlock<const float> inputBlockConst (workingBlock);
    lowMidCrossover.process (inputBlockConst, lowBlock, midHighBlock);

    const juce::dsp::AudioBlock<const float> midHighBlockConst (midHighBlock);
    midHighCrossover.process (midHighBlockConst, midBlock, highBlock);

    // External sidechain (v0.5.0): the key is split by its own crossover pair
    // at the same frequencies and slope, so every band's detector follows a
    // band-matched key instead of the full-range sidechain signal.
    const bool useExternalKey = sidechainExternal && sidechainChunk != nullptr;

    auto sidechainLowBlock = juce::dsp::AudioBlock<float> (sidechainLowBuffer).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels);
    auto sidechainMidHighBlock = juce::dsp::AudioBlock<float> (sidechainMidHighBuffer).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels);
    auto sidechainMidBlock = juce::dsp::AudioBlock<float> (sidechainMidBuffer).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels);
    auto sidechainHighBlock = juce::dsp::AudioBlock<float> (sidechainHighBuffer).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels);

    if (useExternalKey)
    {
        sidechainLowMidCrossover.setCutoffFrequency (lowMidHz);
        sidechainMidHighCrossover.setCutoffFrequency (midHighHz);

        auto sidechainInputBlock = juce::dsp::AudioBlock<float> (sidechainInputBuffer).getSubBlock (0, numSamples).getSubsetChannelBlock (0, numChannels);

        // A mono sidechain is broadcast to every detector channel.
        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            const auto sourceChannel = juce::jmin (channel, sidechainChunk->getNumChannels() - 1);
            const auto* source = sidechainChunk->getChannelPointer (sourceChannel);
            auto* destination = sidechainInputBlock.getChannelPointer (channel);

            std::copy (source, source + numSamples, destination);
        }

        // Same two-stage tree as the main path, with its own filter state.
        const juce::dsp::AudioBlock<const float> sidechainInput (sidechainInputBlock);
        sidechainLowMidCrossover.process (sidechainInput, sidechainLowBlock, sidechainMidHighBlock);

        const juce::dsp::AudioBlock<const float> sidechainRemainder (sidechainMidHighBlock);
        sidechainMidHighCrossover.process (sidechainRemainder, sidechainMidBlock, sidechainHighBlock);
    }

    // Detector-key monitoring (v0.5.0): capture the audition signal before the
    // bands consume it. Only touched while actually listening.
    if (sidechainListen != SidechainListen::off)
    {
        const auto& source = sidechainListen == SidechainListen::low
                               ? (useExternalKey ? sidechainLowBlock : lowBlock)
                               : sidechainListen == SidechainListen::mid
                                   ? (useExternalKey ? sidechainMidBlock : midBlock)
                                   : (useExternalKey ? sidechainHighBlock : highBlock);

        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            const auto* from = source.getChannelPointer (channel);
            auto* to = listenBuffer.getWritePointer (static_cast<int> (channel));

            std::copy (from, from + numSamples, to);
        }
    }

    const juce::dsp::AudioBlock<const float> lowKey (sidechainLowBlock);
    const juce::dsp::AudioBlock<const float> midKey (sidechainMidBlock);
    const juce::dsp::AudioBlock<const float> highKey (sidechainHighBlock);

    lowBand.process (lowBlock, useExternalKey ? &lowKey : nullptr, &gainReductionMeter.low);
    midBand.process (midBlock, useExternalKey ? &midKey : nullptr, &gainReductionMeter.mid);
    highBand.process (highBlock, useExternalKey ? &highKey : nullptr, &gainReductionMeter.high);

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

    // Detector-key monitoring replaces the processed output entirely - it is
    // a monitoring aid, not a band solo (the bands themselves kept running
    // above, so their envelopes stay continuous while auditioning).
    if (sidechainListen != SidechainListen::off)
    {
        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            const auto* from = listenBuffer.getReadPointer (static_cast<int> (channel));
            auto* to = workingBlock.getChannelPointer (channel);

            std::copy (from, from + numSamples, to);
        }
    }

    juce::dsp::ProcessContextReplacing<float> context (workingBlock);
    outputGain.process (context);

    if (mixActive)
        dryWetMixer.mixWetSamples (workingBlock);
}
