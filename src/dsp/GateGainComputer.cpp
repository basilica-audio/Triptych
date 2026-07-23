#include "GateGainComputer.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace trpt
{
    namespace
    {
        // Well below the -80 dB minimum of the Gate Threshold parameter
        // range (see ParameterLayout.cpp), so this floor is never reached by
        // a legitimate envelope/threshold value - it only guards
        // Decibels::gainToDecibels()'s behaviour at exact digital silence
        // (env == 0), matching KneeGainComputer.cpp's own decibelFloor.
        constexpr float decibelFloor = -100.0f;

        // Defensive ceiling on the magnitude of the computed reduction,
        // independent of the caller's own threshold/ratio - guards
        // Decibels::decibelsToGain() against pathological inputs (e.g. a
        // fuzz test driving the gate envelope to exact digital silence
        // against a deep threshold and a steep ratio) the same way
        // KneeGainComputer's unlimitedRangeDb sentinel guards the
        // compressor curve. Comfortably beyond any reduction a real
        // parameter combination (threshold -80..0 dB, ratio 1..100) can
        // produce against decibelFloor above: (decibelFloor - 0) * 100 =
        // -10000 dB in the most extreme case, so this ceiling only ever
        // clips genuinely pathological/synthetic scenarios, never musical
        // use.
        constexpr float maxGateReductionDb = 200.0f;
    }

    float computeGateGainReductionDb (float inputLevelDb, float gateThresholdDb, float ratio) noexcept
    {
        // ratio <= 1.0 is a bit-exact bypass, independent of threshold - the
        // same convention KneeGainComputer.h's ratio == 1.0 null uses. <= 1.0
        // (not just == 1.0) additionally guards against a ratio below 1.0
        // ever reaching here defensively; the ParameterLayout range excludes
        // it, but the pure function itself does not assume its caller
        // enforces that.
        if (ratio <= 1.0f)
            return 0.0f;

        if (inputLevelDb >= gateThresholdDb)
            return 0.0f;

        const auto delta = inputLevelDb - gateThresholdDb; // <= 0, below threshold
        const auto outputDb = gateThresholdDb + delta * ratio;
        const auto reductionDb = outputDb - inputLevelDb;

        return juce::jmax (-maxGateReductionDb, reductionDb);
    }

    float computeGateGainLinear (float envelopeLinear, float gateThresholdDb, float ratio) noexcept
    {
        // Bit-exact regardless of envelope/threshold - see
        // computeGateGainReductionDb()'s matching early return.
        if (ratio <= 1.0f)
            return 1.0f;

        const auto envelopeDb = juce::Decibels::gainToDecibels (envelopeLinear, decibelFloor);
        const auto reductionDb = computeGateGainReductionDb (envelopeDb, gateThresholdDb, ratio);

        return juce::Decibels::decibelsToGain (reductionDb, decibelFloor);
    }
}
