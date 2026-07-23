#include "PluginProcessor.h"
#include "dsp/BandCompressor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

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
        ParamIDs::lowRange, ParamIDs::midRange, ParamIDs::highRange, // v0.3.0
        ParamIDs::lowSideThreshold, ParamIDs::lowSideRatio, // v0.4.0
        ParamIDs::midSideThreshold, ParamIDs::midSideRatio,
        ParamIDs::highSideThreshold, ParamIDs::highSideRatio,
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
        ParamIDs::lowRangeEnabled, ParamIDs::midRangeEnabled, ParamIDs::highRangeEnabled, // v0.3.0
        ParamIDs::lowMidSideEnabled, ParamIDs::midMidSideEnabled, ParamIDs::highMidSideEnabled, // v0.4.0
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

// v0.3.0 state migration tolerance (docs/design-brief-v3-dynamics.md): a
// v0.2.0-shaped ValueTree - missing all six new Range parameter IDs (three
// RangeEnabled bools, three Range floats) - must load without crashing or
// asserting, with RangeEnabled resolving to false (unclamped) and Range to
// its declared 12 dB default, exactly mirroring the v0.2.0 Knee migration
// test above.
TEST_CASE ("State migration tolerance: a v0.2.0-shaped state (missing Range IDs) loads cleanly with Range disabled at its default", "[state][regression]")
{
    TriptychAudioProcessor source;
    source.prepareToPlay (48000.0, 512);

    auto* midThresholdParam = source.apvts.getParameter (ParamIDs::midThreshold);
    auto* outputParam = source.apvts.getParameter (ParamIDs::output);
    REQUIRE (midThresholdParam != nullptr);
    REQUIRE (outputParam != nullptr);

    midThresholdParam->setValueNotifyingHost (midThresholdParam->convertTo0to1 (-19.0f));
    outputParam->setValueNotifyingHost (outputParam->convertTo0to1 (-4.0f));

    juce::MemoryBlock v030State;
    source.getStateInformation (v030State);
    REQUIRE (v030State.getSize() > 0);

    const std::unique_ptr<juce::XmlElement> xml (source.getXmlFromBinary (v030State.getData(), static_cast<int> (v030State.getSize())));
    REQUIRE (xml != nullptr);

    auto prunedTree = juce::ValueTree::fromXml (*xml);
    REQUIRE (prunedTree.isValid());

    static constexpr const char* rangeIds[] = {
        ParamIDs::lowRangeEnabled, ParamIDs::lowRange,
        ParamIDs::midRangeEnabled, ParamIDs::midRange,
        ParamIDs::highRangeEnabled, ParamIDs::highRange,
    };

    for (const auto* rangeId : rangeIds)
    {
        for (int i = prunedTree.getNumChildren() - 1; i >= 0; --i)
        {
            auto child = prunedTree.getChild (i);

            if (child.getProperty ("id").toString() == juce::String (rangeId))
                prunedTree.removeChild (i, nullptr);
        }
    }

    const auto fullChildCount = juce::ValueTree::fromXml (*xml).getNumChildren();
    REQUIRE (prunedTree.getNumChildren() == fullChildCount - 6);

    const std::unique_ptr<juce::XmlElement> prunedXml (prunedTree.createXml());
    juce::MemoryBlock prunedState;
    juce::AudioProcessor::copyXmlToBinary (*prunedXml, prunedState);

    TriptychAudioProcessor destination;
    destination.prepareToPlay (48000.0, 512);

    CHECK_NOTHROW (destination.setStateInformation (prunedState.getData(), static_cast<int> (prunedState.getSize())));

    auto* destMidThreshold = destination.apvts.getParameter (ParamIDs::midThreshold);
    auto* destOutput = destination.apvts.getParameter (ParamIDs::output);
    REQUIRE (destMidThreshold != nullptr);
    REQUIRE (destOutput != nullptr);
    CHECK (destMidThreshold->convertFrom0to1 (destMidThreshold->getValue()) == Catch::Approx (-19.0f).margin (1e-3));
    CHECK (destOutput->convertFrom0to1 (destOutput->getValue()) == Catch::Approx (-4.0f).margin (1e-3));

    static constexpr const char* rangeEnabledIds[] = { ParamIDs::lowRangeEnabled, ParamIDs::midRangeEnabled, ParamIDs::highRangeEnabled };
    static constexpr const char* rangeAmountIds[] = { ParamIDs::lowRange, ParamIDs::midRange, ParamIDs::highRange };

    for (const auto* id : rangeEnabledIds)
    {
        auto* param = dynamic_cast<juce::AudioParameterBool*> (destination.apvts.getParameter (id));
        REQUIRE (param != nullptr);
        CHECK (param->get() == false);
    }

    for (const auto* id : rangeAmountIds)
    {
        auto* param = destination.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        CHECK (param->convertFrom0to1 (param->getValue()) == Catch::Approx (12.0f).margin (1e-3));
    }

    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    CHECK_NOTHROW (destination.processBlock (buffer, midi));
}

// v0.3.0's own binding requirement (docs/design-brief-v3-dynamics.md): "a
// fresh v0.3.0 instance MUST be bit-identical to v0.2.0 defaults". Proven at
// three levels: (1) every v0.2.0-era parameter's default value is unchanged
// (regression-pinned against the literal v0.2.0 design-brief numbers), (2)
// every new v0.3.0 parameter defaults to a fully neutral/off state, and (3)
// processing real audio through a fresh engine at these defaults produces
// output whose measured per-band gain reduction is unaffected by the new
// Ratio range extension or Range clamp (both structurally inert at their
// shipped defaults).
TEST_CASE ("v0.3.0 defaults are bit-identical to v0.2.0: parameter values, Range neutrality, and processed audio all unaffected", "[state][regression]")
{
    TriptychAudioProcessor processor;

    // (1) v0.2.0-era defaults, unchanged - see ParameterTests.cpp for the
    // exhaustive per-band version; this is a focused spot-check tying the
    // guarantee directly to the "fresh instance" framing.
    auto checkDefault = [&] (const char* id, float expectedRealValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        CHECK (param->convertFrom0to1 (param->getDefaultValue()) == Catch::Approx (expectedRealValue).margin (1e-3));
    };

    checkDefault (ParamIDs::lowThreshold, -24.0f);
    checkDefault (ParamIDs::lowRatio, 2.5f);
    checkDefault (ParamIDs::midThreshold, -30.0f);
    checkDefault (ParamIDs::midRatio, 1.8f);
    checkDefault (ParamIDs::highThreshold, -20.0f);
    checkDefault (ParamIDs::highRatio, 2.0f);
    checkDefault (ParamIDs::output, 0.0f);

    // (2) Every new v0.3.0 parameter is neutral: Range disabled on every
    // band (RangeEnabled == false).
    for (const auto* id : { ParamIDs::lowRangeEnabled, ParamIDs::midRangeEnabled, ParamIDs::highRangeEnabled })
    {
        auto* param = dynamic_cast<juce::AudioParameterBool*> (processor.apvts.getParameter (id));
        REQUIRE (param != nullptr);
        CHECK (param->get() == false);
    }

    // (3) Fresh v0.3.0 engine, default state, real audio: measured per-band
    // gain reduction must exactly match what BandCompressor's own v0.2.0-era
    // API surface (threshold/ratio/knee/attack/release/makeup only - never
    // touching setRangeEnabled/setRangeDb) produces for the same inputs,
    // proving the Range addition is a true structural no-op at its default.
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 8192;

    auto measureTailRms = [] (const juce::AudioBuffer<float>& buffer)
    {
        constexpr int settleSamples = blockSize / 2;
        double sumOfSquares = 0.0;
        int counted = 0;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int i = settleSamples; i < buffer.getNumSamples(); ++i)
            {
                sumOfSquares += static_cast<double> (data[i]) * static_cast<double> (data[i]);
                ++counted;
            }
        }

        return counted > 0 ? std::sqrt (sumOfSquares / static_cast<double> (counted)) : 0.0;
    };

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (blockSize);
    spec.numChannels = 1;

    // "v0.2.0-only" reference: never touches Range at all.
    BandCompressor v020Reference;
    v020Reference.setThresholdDb (-24.0f);
    v020Reference.setRatio (2.5f);
    v020Reference.setKneePercent (50.0f);
    v020Reference.setAttackMs (25.0f);
    v020Reference.setReleaseMs (180.0f);
    v020Reference.setMakeupDb (0.0f);
    v020Reference.prepare (spec);

    // "v0.3.0 API surface, defaults": explicitly calls the new Range setters
    // with the shipped default state (disabled), rather than omitting them.
    BandCompressor v030AtDefaults;
    v030AtDefaults.setThresholdDb (-24.0f);
    v030AtDefaults.setRatio (2.5f);
    v030AtDefaults.setKneePercent (50.0f);
    v030AtDefaults.setAttackMs (25.0f);
    v030AtDefaults.setReleaseMs (180.0f);
    v030AtDefaults.setMakeupDb (0.0f);
    v030AtDefaults.setRangeEnabled (false);
    v030AtDefaults.setRangeDb (12.0f); // the shipped default dB value, but disabled - must have zero effect
    v030AtDefaults.prepare (spec);

    juce::AudioBuffer<float> input (1, blockSize);
    TestHelpers::fillWithSine (input, sampleRate, 500.0, 0.5f);

    juce::AudioBuffer<float> referenceProcessed;
    referenceProcessed.makeCopyOf (input);
    juce::dsp::AudioBlock<float> referenceBlock (referenceProcessed);
    v020Reference.process (referenceBlock);

    juce::AudioBuffer<float> defaultsProcessed;
    defaultsProcessed.makeCopyOf (input);
    juce::dsp::AudioBlock<float> defaultsBlock (defaultsProcessed);
    v030AtDefaults.process (defaultsBlock);

    const auto referenceGrDb = juce::Decibels::gainToDecibels (measureTailRms (referenceProcessed) / measureTailRms (input));
    const auto defaultsGrDb = juce::Decibels::gainToDecibels (measureTailRms (defaultsProcessed) / measureTailRms (input));

    CHECK (defaultsGrDb == Catch::Approx (referenceGrDb).margin (1e-3));
}

// v0.4.0 state migration tolerance (issue #24): a v0.3.0-shaped ValueTree -
// missing all nine new M/S parameter IDs (three MidSideEnabled bools, three
// each of SideThreshold/SideRatio) - must load without crashing or
// asserting, with MidSideEnabled resolving to false and every Side float
// resolving to its declared default, exactly mirroring the v0.2.0 Knee and
// v0.3.0 Range/v0.4.0 Gate migration tests above.
TEST_CASE ("State migration tolerance: a v0.3.0-shaped state (missing M/S IDs) loads cleanly with M/S disabled at its defaults", "[state][regression]")
{
    TriptychAudioProcessor source;
    source.prepareToPlay (48000.0, 512);

    auto* highThresholdParam = source.apvts.getParameter (ParamIDs::highThreshold);
    auto* outputParam = source.apvts.getParameter (ParamIDs::output);
    REQUIRE (highThresholdParam != nullptr);
    REQUIRE (outputParam != nullptr);

    highThresholdParam->setValueNotifyingHost (highThresholdParam->convertTo0to1 (-17.0f));
    outputParam->setValueNotifyingHost (outputParam->convertTo0to1 (-2.0f));

    juce::MemoryBlock v040State;
    source.getStateInformation (v040State);
    REQUIRE (v040State.getSize() > 0);

    const std::unique_ptr<juce::XmlElement> xml (source.getXmlFromBinary (v040State.getData(), static_cast<int> (v040State.getSize())));
    REQUIRE (xml != nullptr);

    auto prunedTree = juce::ValueTree::fromXml (*xml);
    REQUIRE (prunedTree.isValid());

    static constexpr const char* midSideIds[] = {
        ParamIDs::lowMidSideEnabled, ParamIDs::lowSideThreshold, ParamIDs::lowSideRatio,
        ParamIDs::midMidSideEnabled, ParamIDs::midSideThreshold, ParamIDs::midSideRatio,
        ParamIDs::highMidSideEnabled, ParamIDs::highSideThreshold, ParamIDs::highSideRatio,
    };

    for (const auto* midSideId : midSideIds)
    {
        for (int i = prunedTree.getNumChildren() - 1; i >= 0; --i)
        {
            auto child = prunedTree.getChild (i);

            if (child.getProperty ("id").toString() == juce::String (midSideId))
                prunedTree.removeChild (i, nullptr);
        }
    }

    const auto fullChildCount = juce::ValueTree::fromXml (*xml).getNumChildren();
    REQUIRE (prunedTree.getNumChildren() == fullChildCount - 9);

    const std::unique_ptr<juce::XmlElement> prunedXml (prunedTree.createXml());
    juce::MemoryBlock prunedState;
    juce::AudioProcessor::copyXmlToBinary (*prunedXml, prunedState);

    TriptychAudioProcessor destination;
    destination.prepareToPlay (48000.0, 512);

    CHECK_NOTHROW (destination.setStateInformation (prunedState.getData(), static_cast<int> (prunedState.getSize())));

    auto* destHighThreshold = destination.apvts.getParameter (ParamIDs::highThreshold);
    auto* destOutput = destination.apvts.getParameter (ParamIDs::output);
    REQUIRE (destHighThreshold != nullptr);
    REQUIRE (destOutput != nullptr);
    CHECK (destHighThreshold->convertFrom0to1 (destHighThreshold->getValue()) == Catch::Approx (-17.0f).margin (1e-3));
    CHECK (destOutput->convertFrom0to1 (destOutput->getValue()) == Catch::Approx (-2.0f).margin (1e-3));

    static constexpr const char* midSideEnabledIds[] = { ParamIDs::lowMidSideEnabled, ParamIDs::midMidSideEnabled, ParamIDs::highMidSideEnabled };

    for (const auto* id : midSideEnabledIds)
    {
        auto* param = dynamic_cast<juce::AudioParameterBool*> (destination.apvts.getParameter (id));
        REQUIRE (param != nullptr);
        CHECK (param->get() == false);
    }

    struct SideDefault
    {
        const char* thresholdId;
        const char* ratioId;
        float thresholdDb;
    };

    static const SideDefault sideDefaults[] = {
        { ParamIDs::lowSideThreshold, ParamIDs::lowSideRatio, -24.0f },
        { ParamIDs::midSideThreshold, ParamIDs::midSideRatio, -30.0f },
        { ParamIDs::highSideThreshold, ParamIDs::highSideRatio, -20.0f },
    };

    for (const auto& sideDefault : sideDefaults)
    {
        auto* thresholdParam = destination.apvts.getParameter (sideDefault.thresholdId);
        auto* ratioParam = destination.apvts.getParameter (sideDefault.ratioId);
        REQUIRE (thresholdParam != nullptr);
        REQUIRE (ratioParam != nullptr);

        CHECK (thresholdParam->convertFrom0to1 (thresholdParam->getValue()) == Catch::Approx (sideDefault.thresholdDb).margin (1e-3));
        CHECK (ratioParam->convertFrom0to1 (ratioParam->getValue()) == Catch::Approx (1.0f).margin (1e-3));
    }

    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    CHECK_NOTHROW (destination.processBlock (buffer, midi));
}
