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
        checkFloatRange (apvts, ratioId, 1.0f, 20.0f);
        checkFloatRange (apvts, kneeId, 0.0f, 100.0f);
        checkFloatRange (apvts, attackId, 0.1f, 100.0f);
        checkFloatRange (apvts, releaseId, 10.0f, 1000.0f);
        checkFloatRange (apvts, makeupId, -12.0f, 24.0f);
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
            ParamIDs::lowMute, ParamIDs::lowSolo, ParamIDs::midMute, ParamIDs::midSolo, ParamIDs::highMute, ParamIDs::highSolo,
            ParamIDs::highLimiterEnabled, ParamIDs::highLimiterThreshold,
            ParamIDs::output,
        };

        for (const auto* id : allIds)
            CHECK (apvts.getParameter (id) != nullptr);
    }

    SECTION ("total parameter count matches the v0.2.0 layout")
    {
        // 2 splits + 3 bands * 6 (Threshold/Ratio/Knee/Attack/Release/Makeup
        // - Knee added in v0.2.0) + 3 bands * 2 (Mute/Solo) + 2 (High limiter
        // enable/threshold) + 1 output = 29.
        CHECK (apvts.processor.getParameters().size() == 29);
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
