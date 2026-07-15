#include "BandCompressor.h"

void BandCompressor::prepare (const juce::dsp::ProcessSpec& spec)
{
    compressor.prepare (spec);

    makeupGain.setRampDurationSeconds (smoothingTimeSeconds);
    makeupGain.prepare (spec);

    limiter.prepare (spec);

    limiterScratchBuffer.setSize (static_cast<int> (spec.numChannels), static_cast<int> (spec.maximumBlockSize));

    thresholdSmoothed.reset (spec.sampleRate, smoothingTimeSeconds);
    thresholdSmoothed.setCurrentAndTargetValue (lastThresholdDb);
    ratioSmoothed.reset (spec.sampleRate, smoothingTimeSeconds);
    ratioSmoothed.setCurrentAndTargetValue (lastRatio);

    reset();

    // Prime the compressor's threshold/ratio immediately so the very first
    // process() call runs with the correct, non-default values rather than
    // juce::dsp::Compressor's built-in defaults (0 dB threshold, 1:1 ratio).
    compressor.setThreshold (lastThresholdDb);
    compressor.setRatio (lastRatio);

    limiter.setThreshold (lastLimiterThresholdDb);
    limiter.setRelease (limiterReleaseMs);
}

void BandCompressor::reset()
{
    compressor.reset();
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

void BandCompressor::setAttackMs (float newAttackMs)
{
    compressor.setAttack (newAttackMs);
}

void BandCompressor::setReleaseMs (float newReleaseMs)
{
    compressor.setRelease (newReleaseMs);
}

void BandCompressor::setMakeupDb (float newMakeupDb)
{
    makeupGain.setGainDecibels (newMakeupDb);
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

    // Coefficient recomputation (Decibels::decibelsToGain, pow) is cheap but
    // still not something we interpolate per sample; threshold/ratio are
    // smoothed and re-derived once per block, the same compromise
    // OvertureEngine/TriptychEngine use for IIR filter cutoffs.
    compressor.setThreshold (thresholdSmoothed.skip (static_cast<int> (numSamples)));
    compressor.setRatio (ratioSmoothed.skip (static_cast<int> (numSamples)));

    juce::dsp::ProcessContextReplacing<float> context (block);
    compressor.process (context);
    makeupGain.process (context);

    // The optional limiter stage always runs, unconditionally (never
    // context.isBypassed), against a scratch copy of the signal so its
    // internal ballistics genuinely stay continuous whether or not it is
    // currently switched into the output - see setLimiterEnabled's doc
    // comment in BandCompressor.h for why toggling juce::dsp::Limiter's own
    // isBypassed flag (the previous approach) does not achieve that.
    const auto numChannels = juce::jmin (block.getNumChannels(), static_cast<size_t> (limiterScratchBuffer.getNumChannels()));
    const auto scratchSamples = juce::jmin (numSamples, static_cast<size_t> (limiterScratchBuffer.getNumSamples()));

    if (numChannels == 0 || scratchSamples == 0)
        return;

    auto scratchBlock = juce::dsp::AudioBlock<float> (limiterScratchBuffer)
                             .getSubBlock (0, scratchSamples)
                             .getSubsetChannelBlock (0, numChannels);
    scratchBlock.copyFrom (block.getSubBlock (0, scratchSamples).getSubsetChannelBlock (0, numChannels));

    juce::dsp::ProcessContextReplacing<float> limiterContext (scratchBlock);
    limiter.process (limiterContext);

    // Only splice the limited result back into the real output when the
    // limiter is actually enabled; while disabled the band's output stays
    // exactly the unlimited compressor+makeup signal, even though the
    // limiter kept processing "in the background" above.
    if (limiterEnabled)
        block.getSubBlock (0, scratchSamples).getSubsetChannelBlock (0, numChannels).copyFrom (scratchBlock);
}
