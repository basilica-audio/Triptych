#include "PluginProcessor.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

TEST_CASE ("State round-trip preserves non-default values of every parameter", "[state]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    static constexpr const char* allIds[] = {
        ParamIDs::lowMidSplit, ParamIDs::midHighSplit,
        ParamIDs::lowThreshold, ParamIDs::lowRatio, ParamIDs::lowAttack, ParamIDs::lowRelease, ParamIDs::lowMakeup,
        ParamIDs::midThreshold, ParamIDs::midRatio, ParamIDs::midAttack, ParamIDs::midRelease, ParamIDs::midMakeup,
        ParamIDs::highThreshold, ParamIDs::highRatio, ParamIDs::highAttack, ParamIDs::highRelease, ParamIDs::highMakeup,
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
