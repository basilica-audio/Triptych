#include "KneeGainComputer.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>

namespace trpt
{
    namespace
    {
        // Well below the -60 dB minimum of the Threshold parameter range, so
        // this floor is never reached by a legitimate envelope/threshold
        // value - it only guards Decibels::gainToDecibels()'s behaviour at
        // exact digital silence (env == 0).
        constexpr float decibelFloor = -100.0f;
    }

    float computeStaticGainReductionDb (float inputLevelDb, float thresholdDb, float ratio, float kneePercent) noexcept
    {
        if (ratio <= 1.0f)
            return 0.0f;

        const auto ratioInverse = 1.0f / ratio;
        const auto kneeNormalised = juce::jlimit (0.0f, 1.0f, kneePercent * 0.01f);
        const auto halfWidthDb = kneeNormalised * std::abs (thresholdDb);

        if (halfWidthDb <= 0.0f)
        {
            // Hard knee (kneePercent == 0, or thresholdDb == 0 making the
            // Weiss-style extent collapse to zero regardless of kneePercent)
            // - the dB-domain reformulation of juce::dsp::Compressor's own
            // gain formula (see BandCompressor.h/KneeGainComputer.h): below
            // threshold, 0 dB reduction; at/above, the classic
            // T + (x - T) / ratio hard-knee transfer curve.
            if (inputLevelDb < thresholdDb)
                return 0.0f;

            const auto outputDb = thresholdDb + (inputLevelDb - thresholdDb) * ratioInverse;
            return outputDb - inputLevelDb;
        }

        const auto delta = inputLevelDb - thresholdDb;
        float outputDb;

        if (delta < -halfWidthDb)
        {
            outputDb = inputLevelDb;
        }
        else if (delta > halfWidthDb)
        {
            outputDb = thresholdDb + delta * ratioInverse;
        }
        else
        {
            // Quadratic soft-knee interpolation (Giannoulis/Massberg/Reiss
            // 2012, eq. 4), with knee width W == 2 * halfWidthDb.
            outputDb = inputLevelDb + (ratioInverse - 1.0f) * juce::square (delta + halfWidthDb) / (4.0f * halfWidthDb);
        }

        return outputDb - inputLevelDb;
    }

    float computeGainLinear (float envelopeLinear, float thresholdDb, float ratio, float kneePercent) noexcept
    {
        if (ratio <= 1.0f)
            return 1.0f;

        const auto kneeNormalised = juce::jlimit (0.0f, 1.0f, kneePercent * 0.01f);

        if (kneeNormalised <= 0.0f)
        {
            const auto thresholdLinear = juce::Decibels::decibelsToGain (thresholdDb, decibelFloor);
            const auto thresholdInverse = 1.0f / thresholdLinear;
            const auto ratioInverse = 1.0f / ratio;

            return envelopeLinear < thresholdLinear ? 1.0f
                                                       : std::pow (envelopeLinear * thresholdInverse, ratioInverse - 1.0f);
        }

        const auto envelopeDb = juce::Decibels::gainToDecibels (envelopeLinear, decibelFloor);
        const auto gainReductionDb = computeStaticGainReductionDb (envelopeDb, thresholdDb, ratio, kneePercent);

        return juce::Decibels::decibelsToGain (gainReductionDb, decibelFloor);
    }
}
