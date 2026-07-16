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
        ParamIDs::lowThreshold, ParamIDs::lowRatio, ParamIDs::lowKnee, ParamIDs::lowAttack, ParamIDs::lowRelease, ParamIDs::lowMakeup,
        ParamIDs::midThreshold, ParamIDs::midRatio, ParamIDs::midKnee, ParamIDs::midAttack, ParamIDs::midRelease, ParamIDs::midMakeup,
        ParamIDs::highThreshold, ParamIDs::highRatio, ParamIDs::highKnee, ParamIDs::highAttack, ParamIDs::highRelease, ParamIDs::highMakeup,
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

// v0.2.0 state migration tolerance (docs/design-brief.md guarantee #7):
// a v0.1-shaped ValueTree - i.e. one missing the three new Knee parameter
// IDs entirely, as any session/preset saved before v0.2.0 would be - must
// load without crashing or asserting, and each Knee parameter must resolve
// to its declared ParameterLayout default (50%) rather than 0 or garbage.
//
// This works "for free" via AudioProcessorValueTreeState::replaceState()'s
// own existing behaviour (JUCE 8.0.14,
// juce_audio_processors/utilities/juce_AudioProcessorValueTreeState.cpp):
// updateParameterConnectionsToChildTrees() only calls setDenormalisedValue()
// for parameters whose PARAM child *is present* in the incoming tree: a
// parameter absent from it (Knee here) keeps its current in-memory value
// rather than being reset to 0 - so for a freshly constructed processor
// (Knee already sitting at its ParameterLayout default from construction,
// untouched), the missing-ID case resolves to that same default. No
// special-case code is needed in TriptychAudioProcessor::setStateInformation()
// - this test exists to pin that behaviour down explicitly rather than leave
// it merely implied by the general APVTS mechanism.
TEST_CASE ("State migration tolerance: a v0.1-shaped state (missing Knee IDs) loads cleanly with Knee at its default", "[state][regression]")
{
    // Build a v0.1-shaped state by taking a real v0.2.0 state and pruning
    // the three Knee PARAM child nodes out of it - deliberately not just an
    // empty/default tree, so the other restored parameters below prove the
    // pruned state actually gets applied, not just silently ignored.
    TriptychAudioProcessor source;
    source.prepareToPlay (48000.0, 512);

    auto* lowThresholdParam = source.apvts.getParameter (ParamIDs::lowThreshold);
    auto* outputParam = source.apvts.getParameter (ParamIDs::output);
    REQUIRE (lowThresholdParam != nullptr);
    REQUIRE (outputParam != nullptr);

    lowThresholdParam->setValueNotifyingHost (lowThresholdParam->convertTo0to1 (-33.0f));
    outputParam->setValueNotifyingHost (outputParam->convertTo0to1 (6.0f));

    juce::MemoryBlock v020State;
    source.getStateInformation (v020State);
    REQUIRE (v020State.getSize() > 0);

    const std::unique_ptr<juce::XmlElement> xml (source.getXmlFromBinary (v020State.getData(), static_cast<int> (v020State.getSize())));
    REQUIRE (xml != nullptr);

    auto prunedTree = juce::ValueTree::fromXml (*xml);
    REQUIRE (prunedTree.isValid());

    static constexpr const char* kneeIds[] = { ParamIDs::lowKnee, ParamIDs::midKnee, ParamIDs::highKnee };

    for (const auto* kneeId : kneeIds)
    {
        for (int i = prunedTree.getNumChildren() - 1; i >= 0; --i)
        {
            auto child = prunedTree.getChild (i);

            if (child.getProperty ("id").toString() == juce::String (kneeId))
                prunedTree.removeChild (i, nullptr);
        }
    }

    // Sanity: the pruned tree genuinely has fewer PARAM children than a full
    // v0.2.0 state - otherwise this test would silently not exercise the
    // scenario it claims to.
    const auto fullChildCount = juce::ValueTree::fromXml (*xml).getNumChildren();
    REQUIRE (prunedTree.getNumChildren() == fullChildCount - 3);

    const std::unique_ptr<juce::XmlElement> prunedXml (prunedTree.createXml());
    juce::MemoryBlock prunedState;
    juce::AudioProcessor::copyXmlToBinary (*prunedXml, prunedState);

    // A fresh destination processor: every Knee parameter is already
    // sitting at its ParameterLayout default (50%) purely from construction,
    // untouched - this is what the "missing ID keeps its current value"
    // APVTS mechanism relies on for the guarantee under test.
    TriptychAudioProcessor destination;
    destination.prepareToPlay (48000.0, 512);

    CHECK_NOTHROW (destination.setStateInformation (prunedState.getData(), static_cast<int> (prunedState.getSize())));

    // The pruned-but-present parameters were genuinely restored...
    auto* destLowThreshold = destination.apvts.getParameter (ParamIDs::lowThreshold);
    auto* destOutput = destination.apvts.getParameter (ParamIDs::output);
    REQUIRE (destLowThreshold != nullptr);
    REQUIRE (destOutput != nullptr);
    CHECK (destLowThreshold->convertFrom0to1 (destLowThreshold->getValue()) == Catch::Approx (-33.0f).margin (1e-3));
    CHECK (destOutput->convertFrom0to1 (destOutput->getValue()) == Catch::Approx (6.0f).margin (1e-3));

    // ...while every Knee parameter, entirely absent from the pruned state,
    // resolved to its declared default (50%), not 0% or an assertion.
    for (const auto* kneeId : kneeIds)
    {
        auto* kneeParam = destination.apvts.getParameter (kneeId);
        REQUIRE (kneeParam != nullptr);
        CHECK (kneeParam->convertFrom0to1 (kneeParam->getValue()) == Catch::Approx (50.0f).margin (1e-3));
    }

    // Processing must also work cleanly afterwards - no crash/assert.
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    CHECK_NOTHROW (destination.processBlock (buffer, midi));
}
