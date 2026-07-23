#include "PluginProcessor.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
    // Convenience wrapper: fetches a parameter by ID and requires it to
    // exist before returning, so every SECTION below fails loudly (not with
    // a null-deref) if an ID typo ever creeps in.
    juce::RangedAudioParameter* requireParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        auto* param = apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param;
    }

    // Checks that a float parameter's underlying NormalisableRange covers
    // [expectedMin, expectedMax], independent of any skew/log mapping.
    void checkFloatRange (juce::AudioProcessorValueTreeState& apvts,
                           const juce::String& id,
                           float expectedMin,
                           float expectedMax)
    {
        auto* param = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (id));
        REQUIRE (param != nullptr);

        const auto range = param->getNormalisableRange().getRange();
        CHECK (range.getStart() == Catch::Approx (expectedMin));
        CHECK (range.getEnd() == Catch::Approx (expectedMax));
    }

    // Checks a float parameter's default value in real (non-normalised)
    // units, going through convertTo0to1 so log-skewed ranges are handled
    // the same way as linear ones.
    void checkFloatDefault (juce::AudioProcessorValueTreeState& apvts,
                             const juce::String& id,
                             float expectedDefault)
    {
        auto* param = requireParam (apvts, id);
        CHECK (param->getDefaultValue() == Catch::Approx (param->convertTo0to1 (expectedDefault)).margin (1e-4));
    }

    // Ranges are structurally identical across Low/Mid/High (see
    // ParameterLayout.cpp's addBandParameters()) - checked once per band
    // here, independent of the per-band defaults (which differ as of
    // v0.2.0 and are checked separately below via checkBandDefaults()).
    void checkBandRanges (juce::AudioProcessorValueTreeState& apvts,
                           const char* thresholdId,
                           const char* ratioId,
                           const char* kneeId,
                           const char* attackId,
                           const char* releaseId,
                           const char* makeupId)
    {
        checkFloatRange (apvts, thresholdId, -60.0f, 0.0f);
        // v0.3.0: Ratio's lower bound widened from 1.0 (no compression) to
        // 0.2 (Weiss DS1-MK3's documented "1:5" upward-expansion endpoint) -
        // see docs/design-brief-v3-dynamics.md.
        checkFloatRange (apvts, ratioId, 0.2f, 20.0f);
        checkFloatRange (apvts, kneeId, 0.0f, 100.0f);
        checkFloatRange (apvts, attackId, 0.1f, 100.0f);
        checkFloatRange (apvts, releaseId, 10.0f, 1000.0f);
        checkFloatRange (apvts, makeupId, -12.0f, 24.0f);
    }

    // Range (v0.3.0): off by default (unclamped) with a reasoned, unsourced
    // 12 dB "if you turn it on" starting value - see ParameterLayout.cpp.
    void checkRangeParameters (juce::AudioProcessorValueTreeState& apvts,
                                const char* rangeEnabledId,
                                const char* rangeId)
    {
        auto* enabledParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (rangeEnabledId));
        REQUIRE (enabledParam != nullptr);
        CHECK (enabledParam->get() == false);

        checkFloatRange (apvts, rangeId, 0.0f, 30.0f);
        checkFloatDefault (apvts, rangeId, 12.0f);
    }

    // Downward expansion / gating (v0.4.0 / issue #25): off by default on
    // every band, with reasoned musical defaults - see
    // addGateParameters()'s doc comment in ParameterLayout.cpp.
    struct GateDefaults
    {
        float thresholdDb;
        float attackMs;
        float releaseMs;
    };

    void checkGateParameters (juce::AudioProcessorValueTreeState& apvts,
                               const char* gateEnabledId,
                               const char* gateThresholdId,
                               const char* gateRatioId,
                               const char* gateAttackId,
                               const char* gateReleaseId,
                               const GateDefaults& defaults)
    {
        auto* enabledParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (gateEnabledId));
        REQUIRE (enabledParam != nullptr);
        CHECK (enabledParam->get() == false);

        checkFloatRange (apvts, gateThresholdId, -80.0f, 0.0f);
        checkFloatDefault (apvts, gateThresholdId, defaults.thresholdDb);

        checkFloatRange (apvts, gateRatioId, 1.0f, 100.0f);
        checkFloatDefault (apvts, gateRatioId, 2.0f);

        checkFloatRange (apvts, gateAttackId, 0.1f, 50.0f);
        checkFloatDefault (apvts, gateAttackId, defaults.attackMs);

        checkFloatRange (apvts, gateReleaseId, 10.0f, 2000.0f);
        checkFloatDefault (apvts, gateReleaseId, defaults.releaseMs);
    }

    // Per-band Mid/Side processing (v0.4.0 / issue #24): off by default;
    // Side Ratio defaults to 1:1 (bypass) so enabling M/S alone doesn't
    // silently change a band's sound until Side Ratio is actually raised -
    // see addMidSideParameters()'s doc comment in ParameterLayout.cpp.
    void checkMidSideParameters (juce::AudioProcessorValueTreeState& apvts,
                                  const char* midSideEnabledId,
                                  const char* sideThresholdId,
                                  const char* sideRatioId,
                                  float sideThresholdDefaultDb)
    {
        auto* enabledParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (midSideEnabledId));
        REQUIRE (enabledParam != nullptr);
        CHECK (enabledParam->get() == false);

        checkFloatRange (apvts, sideThresholdId, -60.0f, 0.0f);
        checkFloatDefault (apvts, sideThresholdId, sideThresholdDefaultDb);

        checkFloatRange (apvts, sideRatioId, 0.2f, 20.0f);
        checkFloatDefault (apvts, sideRatioId, 1.0f);
    }

    // v0.2.0 (docs/design-brief.md): Threshold/Ratio/Attack/Release defaults
    // now differ per band - Knee and Makeup stay identical across bands (see
    // ParameterLayout.cpp).
    struct BandDefaults
    {
        float thresholdDb;
        float ratio;
        float attackMs;
        float releaseMs;
    };

    void checkBandDefaults (juce::AudioProcessorValueTreeState& apvts,
                             const char* thresholdId,
                             const char* ratioId,
                             const char* kneeId,
                             const char* attackId,
                             const char* releaseId,
                             const char* makeupId,
                             const BandDefaults& defaults)
    {
        checkFloatDefault (apvts, thresholdId, defaults.thresholdDb);
        checkFloatDefault (apvts, ratioId, defaults.ratio);
        checkFloatDefault (apvts, kneeId, 50.0f);
        checkFloatDefault (apvts, attackId, defaults.attackMs);
        checkFloatDefault (apvts, releaseId, defaults.releaseMs);
        checkFloatDefault (apvts, makeupId, 0.0f);
    }
}

TEST_CASE ("Processor instantiates with the expected parameters", "[processor][parameters]")
{
    TriptychAudioProcessor processor;
    auto& apvts = processor.apvts;

    SECTION ("plugin name")
    {
        CHECK (processor.getName() == juce::String ("Triptych"));
    }

    SECTION ("all documented parameter IDs resolve")
    {
        static constexpr const char* allIds[] = {
            ParamIDs::lowMidSplit, ParamIDs::midHighSplit,
            ParamIDs::lowThreshold, ParamIDs::lowRatio, ParamIDs::lowKnee, ParamIDs::lowAttack, ParamIDs::lowRelease, ParamIDs::lowMakeup,
            ParamIDs::midThreshold, ParamIDs::midRatio, ParamIDs::midKnee, ParamIDs::midAttack, ParamIDs::midRelease, ParamIDs::midMakeup,
            ParamIDs::highThreshold, ParamIDs::highRatio, ParamIDs::highKnee, ParamIDs::highAttack, ParamIDs::highRelease, ParamIDs::highMakeup,
            ParamIDs::lowRangeEnabled, ParamIDs::lowRange, ParamIDs::midRangeEnabled, ParamIDs::midRange, ParamIDs::highRangeEnabled, ParamIDs::highRange,
            ParamIDs::lowGateEnabled, ParamIDs::lowGateThreshold, ParamIDs::lowGateRatio, ParamIDs::lowGateAttack, ParamIDs::lowGateRelease,
            ParamIDs::midGateEnabled, ParamIDs::midGateThreshold, ParamIDs::midGateRatio, ParamIDs::midGateAttack, ParamIDs::midGateRelease,
            ParamIDs::highGateEnabled, ParamIDs::highGateThreshold, ParamIDs::highGateRatio, ParamIDs::highGateAttack, ParamIDs::highGateRelease,
            ParamIDs::lowMidSideEnabled, ParamIDs::lowSideThreshold, ParamIDs::lowSideRatio,
            ParamIDs::midMidSideEnabled, ParamIDs::midSideThreshold, ParamIDs::midSideRatio,
            ParamIDs::highMidSideEnabled, ParamIDs::highSideThreshold, ParamIDs::highSideRatio,
            ParamIDs::lowMute, ParamIDs::lowSolo, ParamIDs::midMute, ParamIDs::midSolo, ParamIDs::highMute, ParamIDs::highSolo,
            ParamIDs::highLimiterEnabled, ParamIDs::highLimiterThreshold,
            ParamIDs::output,
        };

        for (const auto* id : allIds)
            CHECK (apvts.getParameter (id) != nullptr);
    }

    SECTION ("total parameter count matches the v0.4.0 layout")
    {
        // 2 splits + 3 bands * 6 (Threshold/Ratio/Knee/Attack/Release/Makeup
        // - Knee added in v0.2.0) + 3 bands * 2 (Range Enabled/Range - added
        // in v0.3.0) + 3 bands * 5 (Gate Enabled/Threshold/Ratio/Attack/
        // Release - added in v0.4.0, issue #25) + 3 bands * 3 (M/S Enabled/
        // Side Threshold/Side Ratio - added in v0.4.0, issue #24) + 3 bands
        // * 2 (Mute/Solo) + 2 (High limiter enable/threshold) + 1 output = 59.
        CHECK (apvts.processor.getParameters().size() == 59);
    }

    SECTION ("Range: off by default on every band, 0-30 dB range, 12 dB reasoned starting value (v0.3.0)")
    {
        checkRangeParameters (apvts, ParamIDs::lowRangeEnabled, ParamIDs::lowRange);
        checkRangeParameters (apvts, ParamIDs::midRangeEnabled, ParamIDs::midRange);
        checkRangeParameters (apvts, ParamIDs::highRangeEnabled, ParamIDs::highRange);
    }

    SECTION ("Gate: off by default on every band, reasoned per-band defaults (v0.4.0, issue #25)")
    {
        checkGateParameters (apvts, ParamIDs::lowGateEnabled, ParamIDs::lowGateThreshold, ParamIDs::lowGateRatio, ParamIDs::lowGateAttack, ParamIDs::lowGateRelease,
                              { -50.0f, 10.0f, 200.0f });
        checkGateParameters (apvts, ParamIDs::midGateEnabled, ParamIDs::midGateThreshold, ParamIDs::midGateRatio, ParamIDs::midGateAttack, ParamIDs::midGateRelease,
                              { -55.0f, 5.0f, 150.0f });
        checkGateParameters (apvts, ParamIDs::highGateEnabled, ParamIDs::highGateThreshold, ParamIDs::highGateRatio, ParamIDs::highGateAttack, ParamIDs::highGateRelease,
                              { -45.0f, 2.0f, 100.0f });
    }

    SECTION ("Mid/Side: off by default on every band, Side Ratio bypassed at 1:1 (v0.4.0, issue #24)")
    {
        checkMidSideParameters (apvts, ParamIDs::lowMidSideEnabled, ParamIDs::lowSideThreshold, ParamIDs::lowSideRatio, -24.0f);
        checkMidSideParameters (apvts, ParamIDs::midMidSideEnabled, ParamIDs::midSideThreshold, ParamIDs::midSideRatio, -30.0f);
        checkMidSideParameters (apvts, ParamIDs::highMidSideEnabled, ParamIDs::highSideThreshold, ParamIDs::highSideRatio, -20.0f);
    }

    SECTION ("Mute/Solo default off for every band")
    {
        for (const auto* id : { ParamIDs::lowMute, ParamIDs::lowSolo,
                                 ParamIDs::midMute, ParamIDs::midSolo,
                                 ParamIDs::highMute, ParamIDs::highSolo })
        {
            auto* param = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (id));
            REQUIRE (param != nullptr);
            CHECK (param->get() == false);
        }
    }

    SECTION ("High limiter: defaults and range")
    {
        auto* enabledParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ParamIDs::highLimiterEnabled));
        REQUIRE (enabledParam != nullptr);
        CHECK (enabledParam->get() == false);

        checkFloatDefault (apvts, ParamIDs::highLimiterThreshold, -3.0f);
        checkFloatRange (apvts, ParamIDs::highLimiterThreshold, -24.0f, 0.0f);
    }

    SECTION ("Low/Mid split: defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::lowMidSplit, 200.0f);
        checkFloatRange (apvts, ParamIDs::lowMidSplit, 40.0f, 1000.0f);
    }

    SECTION ("Mid/High split: defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::midHighSplit, 3000.0f);
        checkFloatRange (apvts, ParamIDs::midHighSplit, 400.0f, 12000.0f);
    }

    SECTION ("Low band: ranges (identical structure across bands)")
    {
        checkBandRanges (apvts,
                          ParamIDs::lowThreshold, ParamIDs::lowRatio, ParamIDs::lowKnee, ParamIDs::lowAttack, ParamIDs::lowRelease, ParamIDs::lowMakeup);
    }

    SECTION ("Mid band: ranges (identical structure across bands)")
    {
        checkBandRanges (apvts,
                          ParamIDs::midThreshold, ParamIDs::midRatio, ParamIDs::midKnee, ParamIDs::midAttack, ParamIDs::midRelease, ParamIDs::midMakeup);
    }

    SECTION ("High band: ranges (identical structure across bands)")
    {
        checkBandRanges (apvts,
                          ParamIDs::highThreshold, ParamIDs::highRatio, ParamIDs::highKnee, ParamIDs::highAttack, ParamIDs::highRelease, ParamIDs::highMakeup);
    }

    // v0.2.0 (docs/design-brief.md): defaults now differ per band, replacing
    // v0.1's single uniform default (-18 dB / 4:1 / 10 ms / 100 ms) shared
    // across Low/Mid/High. Knee defaults to 50% on every band regardless.
    SECTION ("Low band: v0.2.0 per-band defaults")
    {
        checkBandDefaults (apvts,
                            ParamIDs::lowThreshold, ParamIDs::lowRatio, ParamIDs::lowKnee, ParamIDs::lowAttack, ParamIDs::lowRelease, ParamIDs::lowMakeup,
                            { -24.0f, 2.5f, 25.0f, 180.0f });
    }

    SECTION ("Mid band: v0.2.0 per-band defaults")
    {
        checkBandDefaults (apvts,
                            ParamIDs::midThreshold, ParamIDs::midRatio, ParamIDs::midKnee, ParamIDs::midAttack, ParamIDs::midRelease, ParamIDs::midMakeup,
                            { -30.0f, 1.8f, 10.0f, 100.0f });
    }

    SECTION ("High band: v0.2.0 per-band defaults")
    {
        checkBandDefaults (apvts,
                            ParamIDs::highThreshold, ParamIDs::highRatio, ParamIDs::highKnee, ParamIDs::highAttack, ParamIDs::highRelease, ParamIDs::highMakeup,
                            { -20.0f, 2.0f, 5.0f, 55.0f });
    }

    SECTION ("Output: defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::output, 0.0f);
        checkFloatRange (apvts, ParamIDs::output, -24.0f, 24.0f);
    }
}
