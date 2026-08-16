#include "BandCompressor.h"

#include <algorithm>
#include <cmath>
#include "GateGainComputer.h"
#include "KneeGainComputer.h"
#include "MidSideCodec.h"

void BandCompressor::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    detector.prepare (spec);

    // Lookahead capacity is always sized for the worst case (5 ms) so that
    // switching the Lookahead parameter never has to allocate - the active
    // length is a plain integer inside the already-allocated line.
    const auto maximumLookahead = static_cast<int> (std::ceil (maximumLookaheadSeconds * spec.sampleRate));

    audioDelay.prepare (spec.numChannels, maximumLookahead);
    lookaheadLimiter.prepare (spec, maximumLookahead);
    lookaheadLimiter.setThresholdDb (lastLimiterThresholdDb);

    keyBuffer.setSize (static_cast<int> (spec.numChannels), static_cast<int> (spec.maximumBlockSize));
    limiterGainScratch.assign (spec.maximumBlockSize, 1.0f);
    detectorKeyFrame.assign (spec.numChannels, 0.0f);
    detectorEnvelopeFrame.assign (spec.numChannels, 0.0f);
    gateEnvelopeFrame.assign (spec.numChannels, 0.0f);
    gateHoldState.assign (spec.numChannels, 0.0f);

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

    gateEnvelopeFilter.prepare (spec);
    gateThresholdSmoothed.reset (spec.sampleRate, smoothingTimeSeconds);
    gateThresholdSmoothed.setCurrentAndTargetValue (lastGateThresholdDb);
    gateRatioSmoothed.reset (spec.sampleRate, smoothingTimeSeconds);
    gateRatioSmoothed.setCurrentAndTargetValue (gateEnabled ? lastGateRatio : 1.0f);

    sideThresholdSmoothed.reset (spec.sampleRate, smoothingTimeSeconds);
    sideThresholdSmoothed.setCurrentAndTargetValue (lastSideThresholdDb);
    sideRatioSmoothed.reset (spec.sampleRate, smoothingTimeSeconds);
    sideRatioSmoothed.setCurrentAndTargetValue (lastSideRatio);

    reset();

    limiter.setThreshold (lastLimiterThresholdDb);
    limiter.setRelease (limiterReleaseMs);
}

void BandCompressor::reset()
{
    detector.reset();
    gateEnvelopeFilter.reset();
    makeupGain.reset();
    limiter.reset();

    audioDelay.reset();
    lookaheadLimiter.reset();

    std::fill (gateHoldState.begin(), gateHoldState.end(), 0.0f);
    gateOpenState = false;
    gateHoldRemaining = 0;
}

void BandCompressor::setLookaheadSamples (int newLookaheadSamples) noexcept
{
    lookaheadSamples = juce::jmax (0, newLookaheadSamples);
}

void BandCompressor::setGateHoldMs (float newHoldMs) noexcept
{
    lastGateHoldMs = juce::jmax (0.0f, newHoldMs);
}

void BandCompressor::setGateHysteresisDb (float newHysteresisDb) noexcept
{
    lastGateHysteresisDb = juce::jmax (0.0f, newHysteresisDb);
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
    detector.setAttackMs (newAttackMs);
}

void BandCompressor::setReleaseMs (float newReleaseMs)
{
    detector.setReleaseMs (newReleaseMs);
}

void BandCompressor::setKneePercent (float newKneePercent)
{
    lastKneePercent = newKneePercent;
    kneeSmoothed.setTargetValue (newKneePercent);
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
    lookaheadLimiter.setThresholdDb (newThresholdDb);
}

void BandCompressor::setGateEnabled (bool shouldBeEnabled) noexcept
{
    gateEnabled = shouldBeEnabled;
    gateRatioSmoothed.setTargetValue (gateEnabled ? lastGateRatio : 1.0f);
}

void BandCompressor::setGateThresholdDb (float newThresholdDb)
{
    lastGateThresholdDb = newThresholdDb;
    gateThresholdSmoothed.setTargetValue (newThresholdDb);
}

void BandCompressor::setGateRatio (float newRatio)
{
    lastGateRatio = newRatio;

    if (gateEnabled)
        gateRatioSmoothed.setTargetValue (lastGateRatio);
}

void BandCompressor::setGateAttackMs (float newAttackMs)
{
    gateEnvelopeFilter.setAttackTime (newAttackMs);
}

void BandCompressor::setGateReleaseMs (float newReleaseMs)
{
    lastGateReleaseMs = newReleaseMs;
    gateEnvelopeFilter.setReleaseTime (newReleaseMs);
}

void BandCompressor::setMidSideEnabled (bool shouldBeEnabled) noexcept
{
    midSideEnabled = shouldBeEnabled;
}

void BandCompressor::setSideThresholdDb (float newThresholdDb)
{
    lastSideThresholdDb = newThresholdDb;
    sideThresholdSmoothed.setTargetValue (newThresholdDb);
}

void BandCompressor::setSideRatio (float newRatio)
{
    lastSideRatio = newRatio;
    sideRatioSmoothed.setTargetValue (newRatio);
}

void BandCompressor::process (juce::dsp::AudioBlock<float>& block,
                               const juce::dsp::AudioBlock<const float>* externalKey,
                               trpt::BandGainReduction* meter) noexcept
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

    // Downward expansion / gating (v0.4.0): its own smoothed ratio,
    // re-derived once per block the same way as the compressor's own. The
    // gate's *threshold* is resolved below - block-rate as in v0.4.0 when
    // hold/hysteresis are neutral, per-sample when they are engaged (see
    // the gate-shaping block).
    const auto gateRatioBlock = gateRatioSmoothed.skip (static_cast<int> (numSamples));

    // Per-band Mid/Side (v0.4.0): Side's own smoothed threshold/ratio.
    const auto sideThresholdDbBlock = sideThresholdSmoothed.skip (static_cast<int> (numSamples));
    const auto sideRatioBlock = sideRatioSmoothed.skip (static_cast<int> (numSamples));

    const auto numChannels = juce::jmin (block.getNumChannels(), detectorKeyFrame.size());

    if (numChannels == 0)
        return;

    // Detector v2 (v0.5.0): resolve the block's detector coefficients and
    // find out whether this block reduces to the exact v0.4.0 path.
    detector.setRatio (ratioBlock);
    detector.updateForBlock (static_cast<int> (numSamples));

    // Gate hold/hysteresis (v0.5.0, brief section 3.9). Both neutral means
    // the shaping machinery is structurally absent and the gate threshold
    // stays the plain block-rate smoothed value v0.4.0 used.
    const bool gateShapingActive = gateEnabled && (lastGateHoldMs > 0.0f || lastGateHysteresisDb > 0.0f);
    const auto gateThresholdDbBlock = gateShapingActive ? 0.0f
                                                        : gateThresholdSmoothed.skip (static_cast<int> (numSamples));

    const auto openThresholdDb = lastGateThresholdDb;
    const auto closeThresholdDb = lastGateThresholdDb - lastGateHysteresisDb;
    const auto openThresholdLinear = juce::Decibels::decibelsToGain (openThresholdDb, -200.0f);
    const auto closeThresholdLinear = juce::Decibels::decibelsToGain (closeThresholdDb, -200.0f);
    const auto holdSamples = static_cast<int> (std::lround (0.001 * static_cast<double> (lastGateHoldMs) * sampleRate));
    const auto gateReleaseCoefficient = std::exp (-1.0f / (0.001f * juce::jmax (1.0e-3f, lastGateReleaseMs) * static_cast<float> (sampleRate)));

    // Lookahead routing (v0.5.0, brief section 3.4). Either the compressor
    // gets the detector lead (audio delayed, detector on the undelayed key),
    // or - when the optional brickwall is also engaged - the very same delay
    // is spent inside the overshoot-proof lookahead limiter instead. Exactly
    // one of them owns the delay, so the band's latency is `lookaheadSamples`
    // in both cases and every band stays sample-aligned in the sum.
    const bool limiterOwnsDelay = lookaheadSamples > 0 && limiterEnabled;
    const bool compressorLead = lookaheadSamples > 0 && ! limiterOwnsDelay;

    audioDelay.setDelaySamples (compressorLead ? lookaheadSamples : 0);
    lookaheadLimiter.setLookaheadSamples (limiterOwnsDelay ? lookaheadSamples : 0);

    // M/S is only meaningful for a genuine stereo pair - defensively skipped
    // on any other channel count (mono, or an unexpected >2), see
    // setMidSideEnabled()'s doc comment in BandCompressor.h.
    const bool applyMidSide = midSideEnabled && numChannels == 2;

    // Key acquisition. The detectors read the band's own audio in v0.4.0's
    // configuration; an external sidechain replaces it, and a compressor
    // lookahead lead needs a copy taken *before* the delay line.
    const auto externalKeyChannels = externalKey != nullptr ? externalKey->getNumChannels() : 0;
    const bool useExternalKey = externalKeyChannels > 0 && externalKey->getNumSamples() >= numSamples;
    const bool useKeyBuffer = useExternalKey || compressorLead;

    if (useKeyBuffer)
    {
        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            auto* keyData = keyBuffer.getWritePointer (static_cast<int> (channel));

            // A mono sidechain is duplicated to every detector channel.
            const auto* source = useExternalKey
                                   ? externalKey->getChannelPointer (juce::jmin (channel, externalKeyChannels - 1))
                                   : block.getChannelPointer (channel);

            std::copy (source, source + numSamples, keyData);
        }
    }

    if (compressorLead)
        audioDelay.processInPlace (block);

    if (applyMidSide)
    {
        auto* leftData = block.getChannelPointer (0);
        auto* rightData = block.getChannelPointer (1);

        for (size_t i = 0; i < numSamples; ++i)
            trpt::encodeMidSide (leftData[i], rightData[i], leftData[i], rightData[i]);

        if (useKeyBuffer)
        {
            auto* keyLeft = keyBuffer.getWritePointer (0);
            auto* keyRight = keyBuffer.getWritePointer (1);

            for (size_t i = 0; i < numSamples; ++i)
                trpt::encodeMidSide (keyLeft[i], keyRight[i], keyLeft[i], keyRight[i]);
        }
    }

    auto deepestCompressorGain = 1.0f;
    auto deepestGateGain = 1.0f;

    for (size_t i = 0; i < numSamples; ++i)
    {
        // 1. Compressor detector. In the neutral configuration this is
        //    literally v0.4.0's single BallisticsFilter call per channel.
        if (detector.isNeutral())
        {
            for (size_t channel = 0; channel < numChannels; ++channel)
            {
                const auto key = useKeyBuffer ? keyBuffer.getReadPointer (static_cast<int> (channel))[i]
                                              : block.getChannelPointer (channel)[i];
                detectorEnvelopeFrame[channel] = detector.processSampleLegacy (static_cast<int> (channel), key);
            }
        }
        else
        {
            for (size_t channel = 0; channel < numChannels; ++channel)
                detectorKeyFrame[channel] = useKeyBuffer ? keyBuffer.getReadPointer (static_cast<int> (channel))[i]
                                                          : block.getChannelPointer (channel)[i];

            detector.processFrame (detectorKeyFrame.data(), numChannels, detectorEnvelopeFrame.data());
        }

        // 2. Gate detector. Keying stays internal this release (the gate is a
        //    bleed/noise tool on the band's own signal), so it always follows
        //    the band audio rather than any external sidechain.
        for (size_t channel = 0; channel < numChannels; ++channel)
            gateEnvelopeFrame[channel] = gateEnvelopeFilter.processSample (static_cast<int> (channel),
                                                                            block.getChannelPointer (channel)[i]);

        auto gateThresholdDb = gateThresholdDbBlock;

        if (gateShapingActive)
        {
            auto loudest = 0.0f;

            for (size_t channel = 0; channel < numChannels; ++channel)
                loudest = juce::jmax (loudest, gateEnvelopeFrame[channel]);

            // Crossing state. The gate counts as open for the whole hold
            // duration, so the armed threshold only flips back to T_open at
            // hold *expiry* - that is what lets hold and hysteresis compose
            // instead of fighting each other.
            if (gateOpenState)
            {
                if (loudest >= closeThresholdLinear)
                    gateHoldRemaining = holdSamples;
                else if (gateHoldRemaining > 0)
                    --gateHoldRemaining;
                else
                    gateOpenState = false;
            }
            else if (loudest >= openThresholdLinear)
            {
                gateOpenState = true;
                gateHoldRemaining = holdSamples;
            }

            // The curve's threshold input has exactly one writer: the
            // existing 50 ms smoother. Retargeting it (rather than stepping
            // the curve) is what keeps an H-sized hysteresis transition from
            // being an H * (ratio - 1) dB single-sample jump.
            gateThresholdSmoothed.setTargetValue (gateOpenState ? closeThresholdDb : openThresholdDb);
            gateThresholdDb = gateThresholdSmoothed.getNextValue();

            // Hold lives in the ENVELOPE domain: a shadow held envelope
            // pinned at T_close while the timer runs, then decaying with the
            // gate's own release coefficient. env_eff = max (envelope, shadow)
            // is continuous at both ends by construction.
            const bool holding = gateOpenState && gateHoldRemaining > 0;

            for (size_t channel = 0; channel < numChannels; ++channel)
            {
                auto& shadow = gateHoldState[channel];
                shadow = holding ? closeThresholdLinear : shadow * gateReleaseCoefficient;
                gateEnvelopeFrame[channel] = juce::jmax (gateEnvelopeFrame[channel], shadow);
            }
        }

        // 3. Gain computation and application.
        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            auto* channelData = block.getChannelPointer (channel);

            // After the encode above, channel 0 holds Mid and channel 1 holds
            // Side when M/S is engaged - the main Threshold/Ratio drive Mid
            // (matching pre-v0.4.0 stereo-linked behaviour when M/S is
            // disabled); Side gets its own independent Threshold/Ratio.
            const bool isSideChannel = applyMidSide && channel == 1;
            const auto channelThresholdDb = isSideChannel ? sideThresholdDbBlock : thresholdDbBlock;
            const auto channelRatio = isSideChannel ? sideRatioBlock : ratioBlock;

            // VCA character overrides the commanded Knee with the emergent,
            // ratio-dependent width; in Clean character this returns
            // kneePercentBlock unchanged, bit for bit.
            const auto channelKnee = detector.resolveKneePercent (kneePercentBlock, channelThresholdDb, channelRatio);

            const auto gain = trpt::computeGainLinear (detectorEnvelopeFrame[channel], channelThresholdDb, channelRatio, channelKnee, rangeDbBlock);

            // The gate's gain is combined multiplicatively against the
            // compressor's, never chained against its output.
            const auto gateGain = trpt::computeGateGainLinear (gateEnvelopeFrame[channel], gateThresholdDb, gateRatioBlock);

            channelData[i] = gain * gateGain * channelData[i];

            deepestCompressorGain = juce::jmin (deepestCompressorGain, gain);
            deepestGateGain = juce::jmin (deepestGateGain, gateGain);
        }
    }

    // Per-band GR telemetry (v0.5.0, brief section 3.7): the deepest reduction
    // actually applied in this block, published once, relaxed.
    if (meter != nullptr)
        meter->store (juce::Decibels::gainToDecibels (deepestCompressorGain, -100.0f),
                       juce::Decibels::gainToDecibels (deepestGateGain, -100.0f));

    if (applyMidSide)
    {
        auto* midData = block.getChannelPointer (0);
        auto* sideData = block.getChannelPointer (1);

        for (size_t i = 0; i < numSamples; ++i)
            trpt::decodeMidSide (midData[i], sideData[i], midData[i], sideData[i]);
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

    // v0.5.0: with lookahead engaged, the limited output comes from the
    // overshoot-proof lookahead brickwall instead of the legacy
    // juce::dsp::Limiter (which keeps running above purely to hold its
    // ballistics continuous for a switch back to lookahead Off). The
    // lookahead path owns the band's delay in this configuration, so total
    // latency is `lookaheadSamples` either way - see the routing comment
    // earlier in this function.
    if (! limiterEnabled)
        return;

    if (limiterOwnsDelay)
    {
        const auto limitedSamples = juce::jmin (numSamples, limiterGainScratch.size());
        auto limitedBlock = block.getSubBlock (0, limitedSamples).getSubsetChannelBlock (0, numChannels);
        lookaheadLimiter.process (limitedBlock, limiterGainScratch.data());
        return;
    }

    // Lookahead Off: splice the legacy limiter's result back in, exactly as
    // v0.4.0 did.
    block.getSubBlock (0, scratchSamples).getSubsetChannelBlock (0, scratchChannels).copyFrom (scratchBlock);
}
