#pragma once

#include <juce_dsp/juce_dsp.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Crossover.h"
#include "params/ParameterIds.h"
#include "dsp/GateGainComputer.h"
#include "dsp/KneeGainComputer.h"
#include "dsp/MidSideCodec.h"

#include <vector>

// The v0.4.0 signal chain, rebuilt here out of the same components the
// shipping code links - the reference side of every "bit-identical to v0.4.0"
// assertion in the suite (T1, T2, T6b, T8b, T9a and the T13 neutrality
// clause).
//
// Why this rather than stored golden renders. This repo has never had
// golden-buffer infrastructure, and float-exact stored fixtures are not
// portable anyway: CI runs x86_64 while development is arm64, and floating
// point contraction differs between them, so a stored buffer would fail for
// reasons that have nothing to do with the DSP. Instead every bit-identity
// clause is a **same-binary, same-process A/B render**: buffer A goes through
// the v0.5.0 object at neutral defaults, buffer B through the chain below,
// and both execute the same instructions on the same machine in the same
// process - so `==` on floats is well defined and portable.
//
// What makes that a genuine test rather than a tautology: v0.5.0's
// wrap-don't-reimplement rules mean the legacy code really is still the code
// under test. Detector's Peak/Clean/link-0 configuration resolves to the
// literal juce::dsp::BallisticsFilter call reproduced below; Crossover's
// 24 dB/oct path is the untouched juce::dsp::LinkwitzRileyFilter dual-output
// loop; the lookahead delay lines and the DryWetMixer are structurally
// bypassed rather than passed through at unity. If any of those neutrality
// guarantees regresses into "numerically very close", these comparisons fail.
namespace LegacyReference
{
    // v0.4.0's BandCompressor, transcribed. Only the members and statements
    // that actually participate at neutral defaults are reproduced - the
    // ones this file's users configure.
    class Band
    {
    public:
        Band() = default;

        void prepare (const juce::dsp::ProcessSpec& spec)
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

        void reset()
        {
            envelopeFilter.reset();
            gateEnvelopeFilter.reset();
            makeupGain.reset();
            limiter.reset();
        }

        void setThresholdDb (float v) { lastThresholdDb = v; thresholdSmoothed.setTargetValue (v); }
        void setRatio (float v) { lastRatio = v; ratioSmoothed.setTargetValue (v); }
        void setKneePercent (float v) { lastKneePercent = v; kneeSmoothed.setTargetValue (v); }
        void setAttackMs (float v) { envelopeFilter.setAttackTime (v); }
        void setReleaseMs (float v) { envelopeFilter.setReleaseTime (v); }
        void setMakeupDb (float v) { makeupGain.setGainDecibels (v); }

        void setGateEnabled (bool v) { gateEnabled = v; gateRatioSmoothed.setTargetValue (v ? lastGateRatio : 1.0f); }
        void setGateThresholdDb (float v) { lastGateThresholdDb = v; gateThresholdSmoothed.setTargetValue (v); }
        void setGateRatio (float v) { lastGateRatio = v; if (gateEnabled) gateRatioSmoothed.setTargetValue (v); }
        void setGateAttackMs (float v) { gateEnvelopeFilter.setAttackTime (v); }
        void setGateReleaseMs (float v) { gateEnvelopeFilter.setReleaseTime (v); }

        void setSideThresholdDb (float v) { lastSideThresholdDb = v; sideThresholdSmoothed.setTargetValue (v); }
        void setSideRatio (float v) { lastSideRatio = v; sideRatioSmoothed.setTargetValue (v); }

        void setLimiterEnabled (bool v) noexcept { limiterEnabled = v; }
        void setLimiterThresholdDb (float v) { lastLimiterThresholdDb = v; limiter.setThreshold (v); }

        void process (juce::dsp::AudioBlock<float>& block) noexcept
        {
            const auto numSamples = block.getNumSamples();

            if (numSamples == 0)
                return;

            const auto thresholdDbBlock = thresholdSmoothed.skip (static_cast<int> (numSamples));
            const auto ratioBlock = ratioSmoothed.skip (static_cast<int> (numSamples));
            const auto kneePercentBlock = kneeSmoothed.skip (static_cast<int> (numSamples));
            const auto rangeDbBlock = rangeSmoothed.skip (static_cast<int> (numSamples));

            const auto gateThresholdDbBlock = gateThresholdSmoothed.skip (static_cast<int> (numSamples));
            const auto gateRatioBlock = gateRatioSmoothed.skip (static_cast<int> (numSamples));

            const auto sideThresholdDbBlock = sideThresholdSmoothed.skip (static_cast<int> (numSamples));
            const auto sideRatioBlock = sideRatioSmoothed.skip (static_cast<int> (numSamples));

            const auto numChannels = block.getNumChannels();
            const bool applyMidSide = false; // M/S off at every default this file is used with.

            for (size_t channel = 0; channel < numChannels; ++channel)
            {
                auto* channelData = block.getChannelPointer (channel);

                const bool isSideChannel = applyMidSide && channel == 1;
                const auto channelThresholdDb = isSideChannel ? sideThresholdDbBlock : thresholdDbBlock;
                const auto channelRatio = isSideChannel ? sideRatioBlock : ratioBlock;

                for (size_t i = 0; i < numSamples; ++i)
                {
                    const auto inputValue = channelData[i];
                    const auto envelope = envelopeFilter.processSample (static_cast<int> (channel), inputValue);
                    const auto gain = trpt::computeGainLinear (envelope, channelThresholdDb, channelRatio, kneePercentBlock, rangeDbBlock);

                    const auto gateEnvelope = gateEnvelopeFilter.processSample (static_cast<int> (channel), inputValue);
                    const auto gateGain = trpt::computeGateGainLinear (gateEnvelope, gateThresholdDbBlock, gateRatioBlock);

                    channelData[i] = gain * gateGain * inputValue;
                }
            }

            juce::dsp::ProcessContextReplacing<float> context (block);
            makeupGain.process (context);

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

            if (limiterEnabled)
                block.getSubBlock (0, scratchSamples).getSubsetChannelBlock (0, scratchChannels).copyFrom (scratchBlock);
        }

    private:
        static constexpr double smoothingTimeSeconds = 0.05;
        static constexpr float limiterReleaseMs = 50.0f;

        juce::dsp::BallisticsFilter<float> envelopeFilter;
        juce::dsp::BallisticsFilter<float> gateEnvelopeFilter;
        juce::dsp::Gain<float> makeupGain;
        juce::dsp::Limiter<float> limiter;
        juce::AudioBuffer<float> limiterScratchBuffer;

        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> thresholdSmoothed;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> ratioSmoothed;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> kneeSmoothed;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> rangeSmoothed;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gateThresholdSmoothed;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gateRatioSmoothed;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> sideThresholdSmoothed;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> sideRatioSmoothed;

        bool rangeEnabled = false;
        float lastRangeDb = 12.0f;
        bool gateEnabled = false;
        bool limiterEnabled = false;
        float lastThresholdDb = -18.0f;
        float lastRatio = 4.0f;
        float lastKneePercent = 50.0f;
        float lastGateThresholdDb = -50.0f;
        float lastGateRatio = 2.0f;
        float lastSideThresholdDb = -18.0f;
        float lastSideRatio = 1.0f;
        float lastLimiterThresholdDb = -3.0f;
    };

    // v0.4.0's TriptychEngine, transcribed for the default (nothing muted,
    // nothing soloed) case the neutrality tests use.
    class Engine
    {
    public:
        Engine() = default;

        void prepare (const juce::dsp::ProcessSpec& spec)
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

            lowGainSmoothed.reset (sampleRate, smoothingTimeSeconds);
            midGainSmoothed.reset (sampleRate, smoothingTimeSeconds);
            highGainSmoothed.reset (sampleRate, smoothingTimeSeconds);

            lowMidCrossover.reset();
            midHighCrossover.reset();
            lowBand.reset();
            midBand.reset();
            highBand.reset();
            outputGain.reset();

            lowGainSmoothed.setCurrentAndTargetValue (1.0f);
            midGainSmoothed.setCurrentAndTargetValue (1.0f);
            highGainSmoothed.setCurrentAndTargetValue (1.0f);

            lowMidCrossover.setCutoffFrequency (clampBelowNyquist (lastLowMidSplitHz, sampleRate));
            midHighCrossover.setCutoffFrequency (clampBelowNyquist (juce::jmax (lastMidHighSplitHz, lastLowMidSplitHz + minimumSplitSeparationHz), sampleRate));
        }

        void setLowMidSplitHz (float v) { lastLowMidSplitHz = v; lowMidSplitSmoothed.setTargetValue (v); }
        void setMidHighSplitHz (float v) { lastMidHighSplitHz = v; midHighSplitSmoothed.setTargetValue (v); }
        void setOutputDb (float v) { outputGain.setGainDecibels (v); }

        Band& low() noexcept { return lowBand; }
        Band& mid() noexcept { return midBand; }
        Band& high() noexcept { return highBand; }

        void process (juce::dsp::AudioBlock<float>& workingBlock)
        {
            const auto numSamples = workingBlock.getNumSamples();
            const auto numChannels = workingBlock.getNumChannels();

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

            std::vector<float> lowRamp (numSamples, 0.0f);
            std::vector<float> midRamp (numSamples, 0.0f);
            std::vector<float> highRamp (numSamples, 0.0f);

            lowGainSmoothed.setTargetValue (1.0f);
            midGainSmoothed.setTargetValue (1.0f);
            highGainSmoothed.setTargetValue (1.0f);

            for (size_t sample = 0; sample < numSamples; ++sample)
            {
                lowRamp[sample] = lowGainSmoothed.getNextValue();
                midRamp[sample] = midGainSmoothed.getNextValue();
                highRamp[sample] = highGainSmoothed.getNextValue();
            }

            for (size_t channel = 0; channel < numChannels; ++channel)
            {
                auto* out = workingBlock.getChannelPointer (channel);
                const auto* lowData = lowBlock.getChannelPointer (channel);
                const auto* midData = midBlock.getChannelPointer (channel);
                const auto* highData = highBlock.getChannelPointer (channel);

                for (size_t sample = 0; sample < numSamples; ++sample)
                    out[sample] = lowData[sample] * lowRamp[sample] + midData[sample] * midRamp[sample] + highData[sample] * highRamp[sample];
            }

            juce::dsp::ProcessContextReplacing<float> context (workingBlock);
            outputGain.process (context);
        }

    private:
        static constexpr double smoothingTimeSeconds = 0.05;
        static constexpr float minimumSplitSeparationHz = 20.0f;

        static float clampBelowNyquist (float frequencyHz, double sampleRate) noexcept
        {
            const auto nyquist = static_cast<float> (sampleRate) * 0.5f;
            return juce::jlimit (10.0f, nyquist * 0.9f, frequencyHz);
        }

        Crossover lowMidCrossover;
        Crossover midHighCrossover;

        Band lowBand;
        Band midBand;
        Band highBand;

        juce::dsp::Gain<float> outputGain;

        juce::AudioBuffer<float> lowBuffer;
        juce::AudioBuffer<float> midHighBuffer;
        juce::AudioBuffer<float> midBuffer;
        juce::AudioBuffer<float> highBuffer;

        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowMidSplitSmoothed;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> midHighSplitSmoothed;

        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lowGainSmoothed;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> midGainSmoothed;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> highGainSmoothed;

        float lastLowMidSplitHz = 200.0f;
        float lastMidHighSplitHz = 3000.0f;

        double sampleRate = 44100.0;
    };

    // Configures a legacy engine from a live APVTS, so the A/B really is
    // "the same parameter values down two chains".
    //
    // Deliberately NOT from hard-coded literals: a fresh instance resolves its
    // startup state through the M2 preset system, and a value that round-trips
    // through the factory Default preset's normalised representation can land
    // a few ULPs away from the literal in ParameterLayout.cpp (0 dB of makeup
    // reads back as -2.7e-07, for instance). That is shipped v0.4.0 behaviour,
    // not a v0.5.0 regression - and pinning the reference to literals would
    // turn it into a false failure.
    inline void applyFromState (Engine& engine, juce::AudioProcessorValueTreeState& apvts)
    {
        const auto value = [&apvts] (const char* id)
        {
            auto* raw = apvts.getRawParameterValue (id);
            jassert (raw != nullptr);
            return raw->load();
        };

        const auto flag = [&value] (const char* id) { return value (id) > 0.5f; };

        engine.setLowMidSplitHz (value (ParamIDs::lowMidSplit));
        engine.setMidHighSplitHz (value (ParamIDs::midHighSplit));
        engine.setOutputDb (value (ParamIDs::output));

        const auto configure = [&value, &flag] (Band& band,
                                                 const char* thresholdId, const char* ratioId, const char* kneeId,
                                                 const char* attackId, const char* releaseId, const char* makeupId,
                                                 const char* gateEnabledId, const char* gateThresholdId, const char* gateRatioId,
                                                 const char* gateAttackId, const char* gateReleaseId,
                                                 const char* sideThresholdId, const char* sideRatioId)
        {
            band.setThresholdDb (value (thresholdId));
            band.setRatio (value (ratioId));
            band.setKneePercent (value (kneeId));
            band.setAttackMs (value (attackId));
            band.setReleaseMs (value (releaseId));
            band.setMakeupDb (value (makeupId));

            band.setGateEnabled (flag (gateEnabledId));
            band.setGateThresholdDb (value (gateThresholdId));
            band.setGateRatio (value (gateRatioId));
            band.setGateAttackMs (value (gateAttackId));
            band.setGateReleaseMs (value (gateReleaseId));

            band.setSideThresholdDb (value (sideThresholdId));
            band.setSideRatio (value (sideRatioId));
        };

        configure (engine.low(),
                    ParamIDs::lowThreshold, ParamIDs::lowRatio, ParamIDs::lowKnee,
                    ParamIDs::lowAttack, ParamIDs::lowRelease, ParamIDs::lowMakeup,
                    ParamIDs::lowGateEnabled, ParamIDs::lowGateThreshold, ParamIDs::lowGateRatio,
                    ParamIDs::lowGateAttack, ParamIDs::lowGateRelease,
                    ParamIDs::lowSideThreshold, ParamIDs::lowSideRatio);

        configure (engine.mid(),
                    ParamIDs::midThreshold, ParamIDs::midRatio, ParamIDs::midKnee,
                    ParamIDs::midAttack, ParamIDs::midRelease, ParamIDs::midMakeup,
                    ParamIDs::midGateEnabled, ParamIDs::midGateThreshold, ParamIDs::midGateRatio,
                    ParamIDs::midGateAttack, ParamIDs::midGateRelease,
                    ParamIDs::midSideThreshold, ParamIDs::midSideRatio);

        configure (engine.high(),
                    ParamIDs::highThreshold, ParamIDs::highRatio, ParamIDs::highKnee,
                    ParamIDs::highAttack, ParamIDs::highRelease, ParamIDs::highMakeup,
                    ParamIDs::highGateEnabled, ParamIDs::highGateThreshold, ParamIDs::highGateRatio,
                    ParamIDs::highGateAttack, ParamIDs::highGateRelease,
                    ParamIDs::highSideThreshold, ParamIDs::highSideRatio);

        engine.high().setLimiterEnabled (flag (ParamIDs::highLimiterEnabled));
        engine.high().setLimiterThresholdDb (value (ParamIDs::highLimiterThreshold));
    }
}
