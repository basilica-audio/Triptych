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

    float computeStaticGainReductionDb (float inputLevelDb, float thresholdDb, float ratio, float kneePercent, float rangeDb) noexcept
    {
        // v0.3.0: ratio == 1.0 is the exact, bit-for-bit boundary between
        // downward (ratio > 1) and upward (ratio < 1) processing - see
        // KneeGainComputer.h. Independent of knee/threshold/range by
        // construction, not by coincidence of floating-point rounding.
        if (ratio == 1.0f)
            return 0.0f;

        const auto ratioInverse = 1.0f / ratio;
        const auto kneeNormalised = juce::jlimit (0.0f, 1.0f, kneePercent * 0.01f);
        const auto halfWidthDb = kneeNormalised * std::abs (thresholdDb);

        float unclampedDb;

        if (halfWidthDb <= 0.0f)
        {
            // Hard knee (kneePercent == 0, or thresholdDb == 0 making the
            // Weiss-style extent collapse to zero regardless of kneePercent)
            // - the dB-domain reformulation of the same closed-form transfer
            // curve computeGainLinear()'s linear fast path uses: below
            // threshold, 0 dB change; at/above, the classic
            // T + (x - T) / ratio hard-knee curve, which cuts for ratio > 1
            // and boosts for ratio < 1 (v0.3.0).
            if (inputLevelDb < thresholdDb)
                unclampedDb = 0.0f;
            else
                unclampedDb = (thresholdDb + (inputLevelDb - thresholdDb) * ratioInverse) - inputLevelDb;
        }
        else
        {
            const auto delta = inputLevelDb - thresholdDb;

            if (delta < -halfWidthDb)
            {
                unclampedDb = 0.0f;
            }
            else if (delta > halfWidthDb)
            {
                unclampedDb = (thresholdDb + delta * ratioInverse) - inputLevelDb;
            }
            else
            {
                // Quadratic soft-knee interpolation (Giannoulis/Massberg/Reiss
                // 2012, eq. 4), with knee width W == 2 * halfWidthDb. The
                // (ratioInverse - 1.0f) factor is negative for downward ratios
                // (> 1) and positive for upward ratios (< 1, v0.3.0) - and
                // exactly 0.0f at ratio == 1.0, though that case is already
                // handled by the early return above.
                unclampedDb = (ratioInverse - 1.0f) * juce::square (delta + halfWidthDb) / (4.0f * halfWidthDb);
            }
        }

        // Range (v0.3.0): clamp to at most +-min(rangeDb, unlimitedRangeDb)
        // in either direction - see KneeGainComputer.h's doc comment on
        // unlimitedRangeDb for why the defensive min() is unconditional.
        const auto effectiveBoundDb = juce::jmin (rangeDb, unlimitedRangeDb);
        return juce::jlimit (-effectiveBoundDb, effectiveBoundDb, unclampedDb);
    }

    float computeGainLinear (float envelopeLinear, float thresholdDb, float ratio, float kneePercent, float rangeDb) noexcept
    {
        // v0.3.0: bit-exact regardless of envelope/threshold/knee/range - see
        // computeStaticGainReductionDb()'s matching early return.
        if (ratio == 1.0f)
            return 1.0f;

        const auto kneeNormalised = juce::jlimit (0.0f, 1.0f, kneePercent * 0.01f);
        const auto rangeIsEffectivelyUnbounded = rangeDb >= unlimitedRangeDb;

        if (kneeNormalised <= 0.0f && rangeIsEffectivelyUnbounded)
        {
            const auto thresholdLinear = juce::Decibels::decibelsToGain (thresholdDb, decibelFloor);
            const auto thresholdInverse = 1.0f / thresholdLinear;
            const auto ratioInverse = 1.0f / ratio;

            return envelopeLinear < thresholdLinear ? 1.0f
                                                       : std::pow (envelopeLinear * thresholdInverse, ratioInverse - 1.0f);
        }

        const auto envelopeDb = juce::Decibels::gainToDecibels (envelopeLinear, decibelFloor);
        const auto gainReductionDb = computeStaticGainReductionDb (envelopeDb, thresholdDb, ratio, kneePercent, rangeDb);

        return juce::Decibels::decibelsToGain (gainReductionDb, decibelFloor);
    }
}
