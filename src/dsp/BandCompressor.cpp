#include "BandCompressor.h"
#include "KneeGainComputer.h"

void BandCompressor::prepare (const juce::dsp::ProcessSpec& spec)
{
    envelopeFilter.prepare (spec);

    makeupGain.setRampDurationSeconds (smoothingTimeSeconds);
    makeupGain.prepare (spec);

    limiter.prepare (spec);

    limiterScratchBuffer.setSize (static_cast<int> (spec.numChannels), static_cast<int> (spec.maximumBlockSize));

    thresholdSmoothed.reset (spec.sampleRate, smoothingTimeSeconds);
    thresholdSmoothed.setCurrentAndTargetValue (lastThresholdDb);
    ratioSmoothed.reset (spec.sampleRate, smoothingTimeSeconds);
    ratioSmoothed.setCurrentAndTargetValue (lastRatio);
    kneeSmoothed.reset (spec.sampleRate, smoothingTimeSeconds);
    kneeSmoothed.setCurrentAndTargetValue (lastKneePercent);
    rangeSmoothed.reset (spec.sampleRate, smoothingTimeSeconds);
    rangeSmoothed.setCurrentAndTargetValue (rangeEnabled ? lastRangeDb : trpt::unlimitedRangeDb);

    reset();

    limiter.setThreshold (lastLimiterThresholdDb);
    limiter.setRelease (limiterReleaseMs);
}

void BandCompressor::reset()
{
    envelopeFilter.reset();
    makeupGain.reset();
    limiter.reset();
}

void BandCompressor::setThresholdDb (float newThresholdDb)
{
    lastThresholdDb = newThresholdDb;
    thresholdSmoothed.setTargetValue (newThresholdDb);
}

void BandCompressor::setRatio (float newRatio)
{
    lastRatio = newRatio;
    ratioSmoothed.setTargetValue (newRatio);
}

void BandCompressor::setKneePercent (float newKneePercent)
{
    lastKneePercent = newKneePercent;
    kneeSmoothed.setTargetValue (newKneePercent);
}

void BandCompressor::setAttackMs (float newAttackMs)
{
    envelopeFilter.setAttackTime (newAttackMs);
}

void BandCompressor::setReleaseMs (float newReleaseMs)
{
    envelopeFilter.setReleaseTime (newReleaseMs);
}

void BandCompressor::setMakeupDb (float newMakeupDb)
{
    makeupGain.setGainDecibels (newMakeupDb);
}

void BandCompressor::setRangeEnabled (bool shouldBeEnabled) noexcept
{
    rangeEnabled = shouldBeEnabled;
    rangeSmoothed.setTargetValue (rangeEnabled ? lastRangeDb : trpt::unlimitedRangeDb);
}

void BandCompressor::setRangeDb (float newRangeDb)
{
    lastRangeDb = newRangeDb;

    if (rangeEnabled)
        rangeSmoothed.setTargetValue (lastRangeDb);
}

void BandCompressor::setLimiterThresholdDb (float newThresholdDb)
{
    lastLimiterThresholdDb = newThresholdDb;
    limiter.setThreshold (newThresholdDb);
}

void BandCompressor::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0)
        return;

    // Coefficient recomputation is cheap but still not something we
    // interpolate per sample; threshold/ratio/knee are smoothed and
    // re-derived once per block, the same compromise OvertureEngine/
    // TriptychEngine use for IIR filter cutoffs (see BandCompressor.h).
    const auto thresholdDbBlock = thresholdSmoothed.skip (static_cast<int> (numSamples));
    const auto ratioBlock = ratioSmoothed.skip (static_cast<int> (numSamples));
    const auto kneePercentBlock = kneeSmoothed.skip (static_cast<int> (numSamples));
    const auto rangeDbBlock = rangeSmoothed.skip (static_cast<int> (numSamples));

    const auto numChannels = block.getNumChannels();

    for (size_t channel = 0; channel < numChannels; ++channel)
    {
        auto* channelData = block.getChannelPointer (channel);

        for (size_t i = 0; i < numSamples; ++i)
        {
            const auto inputValue = channelData[i];
            const auto envelope = envelopeFilter.processSample (static_cast<int> (channel), inputValue);
            const auto gain = trpt::computeGainLinear (envelope, thresholdDbBlock, ratioBlock, kneePercentBlock, rangeDbBlock);

            channelData[i] = gain * inputValue;
        }
    }

    juce::dsp::ProcessContextReplacing<float> context (block);
    makeupGain.process (context);

    // The optional limiter stage always runs, unconditionally (never
    // context.isBypassed), against a scratch copy of the signal so its
    // internal ballistics genuinely stay continuous whether or not it is
    // currently switched into the output - see setLimiterEnabled's doc
    // comment in BandCompressor.h for why toggling juce::dsp::Limiter's own
    // isBypassed flag (the previous approach) does not achieve that.
    const auto scratchChannels = juce::jmin (numChannels, static_cast<size_t> (limiterScratchBuffer.getNumChannels()));
    const auto scratchSamples = juce::jmin (numSamples, static_cast<size_t> (limiterScratchBuffer.getNumSamples()));

    if (scratchChannels == 0 || scratchSamples == 0)
        return;

    auto scratchBlock = juce::dsp::AudioBlock<float> (limiterScratchBuffer)
                             .getSubBlock (0, scratchSamples)
                             .getSubsetChannelBlock (0, scratchChannels);
    scratchBlock.copyFrom (block.getSubBlock (0, scratchSamples).getSubsetChannelBlock (0, scratchChannels));

    juce::dsp::ProcessContextReplacing<float> limiterContext (scratchBlock);
    limiter.process (limiterContext);

    // Only splice the limited result back into the real output when the
    // limiter is actually enabled; while disabled the band's output stays
    // exactly the unlimited compressor+makeup signal, even though the
    // limiter kept processing "in the background" above.
    if (limiterEnabled)
        block.getSubBlock (0, scratchSamples).getSubsetChannelBlock (0, scratchChannels).copyFrom (scratchBlock);
}
