#include "Detector.h"

#include <cmath>

namespace trpt
{
    float vcaKneeWidthDb (float ratio) noexcept
    {
        // Ratios below 1:1 are upward processing; the emergent-knee table is
        // mirrored through 1/ratio so a 1:2 upward setting rounds exactly as
        // much as a 2:1 downward one.
        auto effectiveRatio = ratio;

        if (effectiveRatio < 1.0f)
            effectiveRatio = 1.0f / juce::jmax (effectiveRatio, 1.0e-6f);

        if (effectiveRatio <= 2.0f)
            return 6.0f;

        if (effectiveRatio >= 10.0f)
            return 3.0f;

        const auto logRatio = std::log (effectiveRatio);

        if (effectiveRatio <= 4.0f)
        {
            const auto t = (logRatio - std::log (2.0f)) / (std::log (4.0f) - std::log (2.0f));
            return 6.0f + (4.0f - 6.0f) * t;
        }

        const auto t = (logRatio - std::log (4.0f)) / (std::log (10.0f) - std::log (4.0f));
        return 4.0f + (3.0f - 4.0f) * t;
    }

    float vcaKneePercent (float ratio, float thresholdDb) noexcept
    {
        const auto halfWidthDb = 0.5f * vcaKneeWidthDb (ratio);

        // max() in the denominator, not a bare |T|: see Detector.h.
        return juce::jmin (100.0f, 100.0f * halfWidthDb / juce::jmax (std::abs (thresholdDb), halfWidthDb));
    }

    float vcaAttackScale (float ratio) noexcept
    {
        auto k = ratio >= 1.0f ? ratio - 1.0f
                               : 1.0f / juce::jmax (ratio, 1.0e-6f) - 1.0f;

        k = juce::jmax (0.01f, k);

        return 1.0f / (1.0f + k);
    }
}

float Detector::onePoleCoefficient (float timeConstantSeconds, double sampleRate) noexcept
{
    // Exact zero-order-hold discretisation of a one-pole, matching
    // juce::dsp::BallisticsFilter's own coefficient form. Guarded against a
    // zero/negative time constant (which would divide by zero).
    const auto tau = juce::jmax (1.0e-6f, timeConstantSeconds);
    return std::exp (-1.0f / (tau * static_cast<float> (sampleRate)));
}

void Detector::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    ballistics.prepare (spec);

    meanSquareState.assign (static_cast<size_t> (spec.numChannels), 0.0f);
    slowEnvelopeState.assign (static_cast<size_t> (spec.numChannels), 0.0f);

    linkSmoothed.reset (sampleRate, smoothingTimeSeconds);
    linkSmoothed.setCurrentAndTargetValue (lastLinkPercent * 0.01f);

    characterSmoothed.reset (sampleRate, characterCrossfadeSeconds);
    characterSmoothed.setCurrentAndTargetValue (character == Character::vca ? 1.0f : 0.0f);

    reset();
}

void Detector::reset()
{
    ballistics.reset();

    std::fill (meanSquareState.begin(), meanSquareState.end(), 0.0f);
    std::fill (slowEnvelopeState.begin(), slowEnvelopeState.end(), 0.0f);
}

void Detector::setStereoLinkPercent (float newPercent) noexcept
{
    lastLinkPercent = newPercent;
    linkSmoothed.setTargetValue (juce::jlimit (0.0f, 100.0f, newPercent) * 0.01f);
}

void Detector::updateForBlock (int numSamples) noexcept
{
    characterSmoothed.setTargetValue (character == Character::vca ? 1.0f : 0.0f);

    // Both scalars are block-rate values here (the link blend is applied once
    // per block, the same 50 ms-smoothed, block-boundary-resolved compromise
    // every other real-time scalar in this engine uses - see
    // BandCompressor::process()).
    blockLink = linkSmoothed.skip (numSamples);
    blockCharacterBlend = characterSmoothed.skip (numSamples);

    // The neutral path is *structural*, not numerical: unless something is
    // genuinely engaged, processFrame() is never even called and
    // BandCompressor runs the literal v0.4.0 statement instead.
    neutralPath = law == Law::peak
                   && ! autoReleaseEnabled
                   && blockLink == 0.0f
                   && blockCharacterBlend == 0.0f;

    // VCA character scales the effective attack toward tau / (1 + k); the
    // 10 ms crossfade rides the scale itself, so a Clean <-> VCA switch ramps
    // the coefficient rather than stepping it. In Clean the blend is exactly
    // 0, so this multiplies by exactly 1.0f and the commanded attack reaches
    // the ballistics unchanged, bit for bit.
    const auto attackScale = 1.0f + blockCharacterBlend * (trpt::vcaAttackScale (ratio) - 1.0f);

    ballistics.setAttackTime (attackMs * attackScale);

    if (autoReleaseEnabled)
    {
        // The Release knob scales both branch release constants; the slow
        // branch's *charge* constant is a fixed reservoir property and is
        // deliberately not scaled.
        const auto releaseScale = juce::jmax (1.0e-3f, releaseMs / autoReleaseReferenceMs);

        ballistics.setReleaseTime (autoFastReleaseSeconds * releaseScale * 1000.0f);

        blockSlowChargeCoefficient = onePoleCoefficient (autoSlowChargeSeconds, sampleRate);
        blockSlowReleaseCoefficient = onePoleCoefficient (autoSlowReleaseSeconds * releaseScale, sampleRate);
    }
    else
    {
        ballistics.setReleaseTime (releaseMs);
    }

    if (law == Law::rms)
        blockRmsCoefficient = onePoleCoefficient (juce::jmax (attackMs, minimumRmsTimeConstantMs) * 0.001f, sampleRate);
}

void Detector::processFrame (const float* keys, size_t numChannels, float* envelopes) noexcept
{
    const auto channels = juce::jmin (numChannels, meanSquareState.size());

    // 1. Detection law: per-channel key magnitude.
    for (size_t channel = 0; channel < channels; ++channel)
    {
        const auto key = keys[channel];

        if (law == Law::rms)
        {
            auto& meanSquare = meanSquareState[channel];
            meanSquare = blockRmsCoefficient * meanSquare + (1.0f - blockRmsCoefficient) * key * key;

            // 1e-30 floor: the denormal/log guard from the RMS spec.
            envelopes[channel] = std::sqrt (meanSquare + 1.0e-30f);
        }
        else
        {
            envelopes[channel] = std::abs (key);
        }
    }

    // 2. Stereo link, applied to the detector inputs so that at 100% every
    //    channel integrates literally the same value sequence.
    if (blockLink > 0.0f && channels > 1)
    {
        auto linked = envelopes[0];

        for (size_t channel = 1; channel < channels; ++channel)
            linked = juce::jmax (linked, envelopes[channel]);

        for (size_t channel = 0; channel < channels; ++channel)
            envelopes[channel] += blockLink * (linked - envelopes[channel]);
    }

    // 3. Ballistics (plus the parallel slow reservoir when auto release is on).
    for (size_t channel = 0; channel < channels; ++channel)
    {
        const auto detectorInput = envelopes[channel];
        const auto fast = ballistics.processSample (static_cast<int> (channel), detectorInput);

        if (autoReleaseEnabled)
        {
            auto& slow = slowEnvelopeState[channel];
            const auto coefficient = detectorInput > slow ? blockSlowChargeCoefficient : blockSlowReleaseCoefficient;
            slow = detectorInput + coefficient * (slow - detectorInput);

            envelopes[channel] = juce::jmax (fast, slow);
        }
        else
        {
            envelopes[channel] = fast;
        }
    }
}

float Detector::resolveKneePercent (float commandedKneePercent, float thresholdDb, float channelRatio) const noexcept
{
    if (blockCharacterBlend == 0.0f)
        return commandedKneePercent;

    const auto vcaKnee = trpt::vcaKneePercent (channelRatio, thresholdDb);

    return commandedKneePercent + blockCharacterBlend * (vcaKnee - commandedKneePercent);
}
