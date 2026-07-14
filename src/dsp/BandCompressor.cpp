#include "BandCompressor.h"

void BandCompressor::prepare (const juce::dsp::ProcessSpec& spec)
{
    compressor.prepare (spec);

    makeupGain.setRampDurationSeconds (smoothingTimeSeconds);
    makeupGain.prepare (spec);

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
}

void BandCompressor::reset()
{
    compressor.reset();
    makeupGain.reset();
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
}
