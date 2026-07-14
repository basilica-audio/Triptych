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

    // Checks all five per-band parameters (Threshold/Ratio/Attack/Release/
    // Makeup) share the same ranges/defaults, since Low/Mid/High all use the
    // identical set built by ParameterLayout.cpp's addBandParameters().
    void checkBandDefaultsAndRanges (juce::AudioProcessorValueTreeState& apvts,
                                      const char* thresholdId,
                                      const char* ratioId,
                                      const char* attackId,
                                      const char* releaseId,
                                      const char* makeupId)
    {
        checkFloatDefault (apvts, thresholdId, -18.0f);
        checkFloatRange (apvts, thresholdId, -60.0f, 0.0f);

        checkFloatDefault (apvts, ratioId, 4.0f);
        checkFloatRange (apvts, ratioId, 1.0f, 20.0f);

        checkFloatDefault (apvts, attackId, 10.0f);
        checkFloatRange (apvts, attackId, 0.1f, 100.0f);

        checkFloatDefault (apvts, releaseId, 100.0f);
        checkFloatRange (apvts, releaseId, 10.0f, 1000.0f);

        checkFloatDefault (apvts, makeupId, 0.0f);
        checkFloatRange (apvts, makeupId, -12.0f, 24.0f);
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
            ParamIDs::lowThreshold, ParamIDs::lowRatio, ParamIDs::lowAttack, ParamIDs::lowRelease, ParamIDs::lowMakeup,
            ParamIDs::midThreshold, ParamIDs::midRatio, ParamIDs::midAttack, ParamIDs::midRelease, ParamIDs::midMakeup,
            ParamIDs::highThreshold, ParamIDs::highRatio, ParamIDs::highAttack, ParamIDs::highRelease, ParamIDs::highMakeup,
            ParamIDs::lowMute, ParamIDs::lowSolo, ParamIDs::midMute, ParamIDs::midSolo, ParamIDs::highMute, ParamIDs::highSolo,
            ParamIDs::highLimiterEnabled, ParamIDs::highLimiterThreshold,
            ParamIDs::output,
        };

        for (const auto* id : allIds)
            CHECK (apvts.getParameter (id) != nullptr);
    }

    SECTION ("total parameter count matches the v0.1.0 layout")
    {
        // 2 splits + 3 bands * 5 + 3 bands * 2 (Mute/Solo) + 2 (High limiter
        // enable/threshold) + 1 output = 26.
        CHECK (apvts.processor.getParameters().size() == 26);
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

    SECTION ("Low band: defaults and ranges")
    {
        checkBandDefaultsAndRanges (apvts,
                                     ParamIDs::lowThreshold, ParamIDs::lowRatio, ParamIDs::lowAttack, ParamIDs::lowRelease, ParamIDs::lowMakeup);
    }

    SECTION ("Mid band: defaults and ranges")
    {
        checkBandDefaultsAndRanges (apvts,
                                     ParamIDs::midThreshold, ParamIDs::midRatio, ParamIDs::midAttack, ParamIDs::midRelease, ParamIDs::midMakeup);
    }

    SECTION ("High band: defaults and ranges")
    {
        checkBandDefaultsAndRanges (apvts,
                                     ParamIDs::highThreshold, ParamIDs::highRatio, ParamIDs::highAttack, ParamIDs::highRelease, ParamIDs::highMakeup);
    }

    SECTION ("Output: defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::output, 0.0f);
        checkFloatRange (apvts, ParamIDs::output, -24.0f, 24.0f);
    }
}
