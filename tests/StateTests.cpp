#include "PluginProcessor.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

TEST_CASE ("State round-trip preserves non-default values of every parameter", "[state]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    // Bool parameters (Mute/Solo, High limiter enable) are deliberately
    // excluded here and covered by their own round-trip test below: JUCE's
    // AudioParameterBool quantises getValue() to exactly 0.0/1.0 (see
    // juce_AudioParameterBool.cpp), so this test's "distinct fractional
    // normalised value per parameter" technique doesn't apply to them.
    static constexpr const char* allIds[] = {
        ParamIDs::lowMidSplit, ParamIDs::midHighSplit,
        ParamIDs::lowThreshold, ParamIDs::lowRatio, ParamIDs::lowAttack, ParamIDs::lowRelease, ParamIDs::lowMakeup,
        ParamIDs::midThreshold, ParamIDs::midRatio, ParamIDs::midAttack, ParamIDs::midRelease, ParamIDs::midMakeup,
        ParamIDs::highThreshold, ParamIDs::highRatio, ParamIDs::highAttack, ParamIDs::highRelease, ParamIDs::highMakeup,
        ParamIDs::highLimiterThreshold,
        ParamIDs::output,
    };

    std::vector<juce::RangedAudioParameter*> params;
    std::vector<float> savedNormalisedValues;

    for (const auto* id : allIds)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        params.push_back (param);
    }

    // Push every parameter to a distinct, non-default normalised value so
    // the round-trip assertion below can't pass by coincidence (e.g. two
    // parameters both happening to already sit at their post-restore
    // value).
    for (size_t i = 0; i < params.size(); ++i)
    {
        auto normalisedValue = 0.2f + 0.6f * (static_cast<float> (i % 5) / 4.0f);

        // Guard against a coincidental match with this parameter's own
        // default normalised value (e.g. Output's -24..+24 dB range with a
        // 0 dB default sits at exactly 0.5 normalised, which the formula
        // above also produces for i % 5 == 2) - such a match would let the
        // "still non-default after reset" sanity check below pass by
        // accident rather than by an actual round-trip.
        if (std::abs (normalisedValue - params[i]->getDefaultValue()) < 0.05f)
            normalisedValue = std::fmod (normalisedValue + 0.37f, 1.0f);

        params[i]->setValueNotifyingHost (normalisedValue);
        savedNormalisedValues.push_back (params[i]->getValue());
    }

    juce::MemoryBlock savedState;
    processor.getStateInformation (savedState);
    REQUIRE (savedState.getSize() > 0);

    // Reset every parameter back to its default before restoring, so the
    // round-trip assertion below can't pass by accident.
    for (auto* param : params)
        param->setValueNotifyingHost (param->getDefaultValue());

    for (size_t i = 0; i < params.size(); ++i)
        REQUIRE (params[i]->getValue() != Catch::Approx (savedNormalisedValues[i]));

    processor.setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));

    for (size_t i = 0; i < params.size(); ++i)
    {
        INFO ("parameter index = " << i);
        CHECK (params[i]->getValue() == Catch::Approx (savedNormalisedValues[i]).margin (1e-6));
    }
}

// Bool parameters (M1: per-band Mute/Solo, High limiter enable) round-trip
// separately from the float sweep above - see that test's comment for why.
TEST_CASE ("State round-trip preserves every bool parameter (Mute/Solo, High limiter enable)", "[state]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    static constexpr const char* boolIds[] = {
        ParamIDs::lowMute, ParamIDs::lowSolo,
        ParamIDs::midMute, ParamIDs::midSolo,
        ParamIDs::highMute, ParamIDs::highSolo,
        ParamIDs::highLimiterEnabled,
    };

    std::vector<juce::RangedAudioParameter*> params;

    for (const auto* id : boolIds)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        // All default to false (see ParameterLayout.cpp) - flip every one
        // to true so the round-trip assertion below can't pass by
        // coincidentally already sitting at the post-restore value.
        REQUIRE (param->getValue() == Catch::Approx (0.0f));
        param->setValueNotifyingHost (1.0f);
        params.push_back (param);
    }

    juce::MemoryBlock savedState;
    processor.getStateInformation (savedState);
    REQUIRE (savedState.getSize() > 0);

    for (auto* param : params)
        param->setValueNotifyingHost (param->getDefaultValue());

    for (auto* param : params)
        REQUIRE (param->getValue() == Catch::Approx (0.0f));

    processor.setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));

    for (auto* param : params)
        CHECK (param->getValue() == Catch::Approx (1.0f));
}
