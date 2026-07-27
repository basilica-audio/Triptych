#include "PluginProcessor.h"
#include "dsp/BandCompressor.h"
#include "params/ParameterIds.h"
#include "LegacyReferenceChain.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

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
        ParamIDs::lowGateThreshold, ParamIDs::lowGateRatio, ParamIDs::lowGateAttack, ParamIDs::lowGateRelease, // v0.4.0
        ParamIDs::midGateThreshold, ParamIDs::midGateRatio, ParamIDs::midGateAttack, ParamIDs::midGateRelease,
        ParamIDs::highGateThreshold, ParamIDs::highGateRatio, ParamIDs::highGateAttack, ParamIDs::highGateRelease,
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
        ParamIDs::lowGateEnabled, ParamIDs::midGateEnabled, ParamIDs::highGateEnabled, // v0.4.0
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

// v0.4.0 state migration tolerance (issue #25): a v0.3.0-shaped ValueTree -
// missing all fifteen new Gate parameter IDs (three GateEnabled bools, three
// each of GateThreshold/GateRatio/GateAttack/GateRelease) - must load without
// crashing or asserting, with GateEnabled resolving to false and every Gate
// float resolving to its declared default, exactly mirroring the v0.2.0 Knee
// and v0.3.0 Range migration tests above.
TEST_CASE ("State migration tolerance: a v0.3.0-shaped state (missing Gate IDs) loads cleanly with Gate disabled at its defaults", "[state][regression]")
{
    TriptychAudioProcessor source;
    source.prepareToPlay (48000.0, 512);

    auto* lowThresholdParam = source.apvts.getParameter (ParamIDs::lowThreshold);
    auto* outputParam = source.apvts.getParameter (ParamIDs::output);
    REQUIRE (lowThresholdParam != nullptr);
    REQUIRE (outputParam != nullptr);

    lowThresholdParam->setValueNotifyingHost (lowThresholdParam->convertTo0to1 (-27.0f));
    outputParam->setValueNotifyingHost (outputParam->convertTo0to1 (2.0f));

    juce::MemoryBlock v040State;
    source.getStateInformation (v040State);
    REQUIRE (v040State.getSize() > 0);

    const std::unique_ptr<juce::XmlElement> xml (source.getXmlFromBinary (v040State.getData(), static_cast<int> (v040State.getSize())));
    REQUIRE (xml != nullptr);

    auto prunedTree = juce::ValueTree::fromXml (*xml);
    REQUIRE (prunedTree.isValid());

    static constexpr const char* gateIds[] = {
        ParamIDs::lowGateEnabled, ParamIDs::lowGateThreshold, ParamIDs::lowGateRatio, ParamIDs::lowGateAttack, ParamIDs::lowGateRelease,
        ParamIDs::midGateEnabled, ParamIDs::midGateThreshold, ParamIDs::midGateRatio, ParamIDs::midGateAttack, ParamIDs::midGateRelease,
        ParamIDs::highGateEnabled, ParamIDs::highGateThreshold, ParamIDs::highGateRatio, ParamIDs::highGateAttack, ParamIDs::highGateRelease,
    };

    for (const auto* gateId : gateIds)
    {
        for (int i = prunedTree.getNumChildren() - 1; i >= 0; --i)
        {
            auto child = prunedTree.getChild (i);

            if (child.getProperty ("id").toString() == juce::String (gateId))
                prunedTree.removeChild (i, nullptr);
        }
    }

    const auto fullChildCount = juce::ValueTree::fromXml (*xml).getNumChildren();
    REQUIRE (prunedTree.getNumChildren() == fullChildCount - 15);

    const std::unique_ptr<juce::XmlElement> prunedXml (prunedTree.createXml());
    juce::MemoryBlock prunedState;
    juce::AudioProcessor::copyXmlToBinary (*prunedXml, prunedState);

    TriptychAudioProcessor destination;
    destination.prepareToPlay (48000.0, 512);

    CHECK_NOTHROW (destination.setStateInformation (prunedState.getData(), static_cast<int> (prunedState.getSize())));

    auto* destLowThreshold = destination.apvts.getParameter (ParamIDs::lowThreshold);
    auto* destOutput = destination.apvts.getParameter (ParamIDs::output);
    REQUIRE (destLowThreshold != nullptr);
    REQUIRE (destOutput != nullptr);
    CHECK (destLowThreshold->convertFrom0to1 (destLowThreshold->getValue()) == Catch::Approx (-27.0f).margin (1e-3));
    CHECK (destOutput->convertFrom0to1 (destOutput->getValue()) == Catch::Approx (2.0f).margin (1e-3));

    static constexpr const char* gateEnabledIds[] = { ParamIDs::lowGateEnabled, ParamIDs::midGateEnabled, ParamIDs::highGateEnabled };

    for (const auto* id : gateEnabledIds)
    {
        auto* param = dynamic_cast<juce::AudioParameterBool*> (destination.apvts.getParameter (id));
        REQUIRE (param != nullptr);
        CHECK (param->get() == false);
    }

    struct GateDefault
    {
        const char* thresholdId;
        const char* ratioId;
        const char* attackId;
        const char* releaseId;
        float thresholdDb;
        float attackMs;
        float releaseMs;
    };

    static const GateDefault gateDefaults[] = {
        { ParamIDs::lowGateThreshold, ParamIDs::lowGateRatio, ParamIDs::lowGateAttack, ParamIDs::lowGateRelease, -50.0f, 10.0f, 200.0f },
        { ParamIDs::midGateThreshold, ParamIDs::midGateRatio, ParamIDs::midGateAttack, ParamIDs::midGateRelease, -55.0f, 5.0f, 150.0f },
        { ParamIDs::highGateThreshold, ParamIDs::highGateRatio, ParamIDs::highGateAttack, ParamIDs::highGateRelease, -45.0f, 2.0f, 100.0f },
    };

    for (const auto& gateDefault : gateDefaults)
    {
        auto* thresholdParam = destination.apvts.getParameter (gateDefault.thresholdId);
        auto* ratioParam = destination.apvts.getParameter (gateDefault.ratioId);
        auto* attackParam = destination.apvts.getParameter (gateDefault.attackId);
        auto* releaseParam = destination.apvts.getParameter (gateDefault.releaseId);
        REQUIRE (thresholdParam != nullptr);
        REQUIRE (ratioParam != nullptr);
        REQUIRE (attackParam != nullptr);
        REQUIRE (releaseParam != nullptr);

        CHECK (thresholdParam->convertFrom0to1 (thresholdParam->getValue()) == Catch::Approx (gateDefault.thresholdDb).margin (1e-3));
        CHECK (ratioParam->convertFrom0to1 (ratioParam->getValue()) == Catch::Approx (2.0f).margin (1e-3));
        CHECK (attackParam->convertFrom0to1 (attackParam->getValue()) == Catch::Approx (gateDefault.attackMs).margin (1e-3));
        CHECK (releaseParam->convertFrom0to1 (releaseParam->getValue()) == Catch::Approx (gateDefault.releaseMs).margin (1e-3));
    }

    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    CHECK_NOTHROW (destination.processBlock (buffer, midi));
}


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

//==============================================================================
// v0.5.0 "Flagship Dynamics Core" neutrality and migration (brief section 6).
//
// The methodology for every "bit-identical to v0.4.0" clause below is the
// same-binary, same-process A/B render described in tests/LegacyReferenceChain.h:
// side A is the shipping v0.5.0 object at neutral defaults, side B is the
// v0.4.0 chain rebuilt from the very components v0.5.0 still links. There are
// no stored golden buffers anywhere in this repo, and float-exact stored
// fixtures would not survive the arch/compiler difference between CI and a
// development machine anyway.

namespace
{
    // A deterministic broadband probe plus a multitone - between them they
    // exercise every band, the knee region and the crossover skirts.
    std::vector<float> makeNeutralityProbe (double sampleRate, int numSamples)
    {
        std::vector<float> signal (static_cast<size_t> (numSamples), 0.0f);
        juce::Random random (0x7A1E);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto t = static_cast<double> (i) / sampleRate;

            // Multitone: one partial per band, plus noise.
            auto value = 0.30 * std::sin (juce::MathConstants<double>::twoPi * 80.0 * t);
            value += 0.25 * std::sin (juce::MathConstants<double>::twoPi * 900.0 * t);
            value += 0.20 * std::sin (juce::MathConstants<double>::twoPi * 6500.0 * t);
            value += 0.15 * (random.nextDouble() * 2.0 - 1.0);

            signal[static_cast<size_t> (i)] = static_cast<float> (value);
        }

        return signal;
    }

    std::vector<float> renderThroughProcessor (TriptychAudioProcessor& processor,
                                                const std::vector<float>& probe,
                                                double sampleRate,
                                                int blockSize)
    {
        processor.prepareToPlay (sampleRate, blockSize);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        std::vector<float> output;
        output.reserve (probe.size());

        for (size_t position = 0; position + static_cast<size_t> (blockSize) <= probe.size(); position += static_cast<size_t> (blockSize))
        {
            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (channel, i, probe[position + static_cast<size_t> (i)]);

            processor.processBlock (buffer, midi);

            for (int i = 0; i < blockSize; ++i)
                output.push_back (buffer.getSample (0, i));
        }

        return output;
    }

    std::vector<float> renderThroughLegacyChain (TriptychAudioProcessor& parameterSource,
                                                  const std::vector<float>& probe,
                                                  double sampleRate,
                                                  int blockSize)
    {
        LegacyReference::Engine engine;
        LegacyReference::applyFromState (engine, parameterSource.apvts);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (blockSize);
        spec.numChannels = 2;
        engine.prepare (spec);

        juce::AudioBuffer<float> buffer (2, blockSize);

        std::vector<float> output;
        output.reserve (probe.size());

        for (size_t position = 0; position + static_cast<size_t> (blockSize) <= probe.size(); position += static_cast<size_t> (blockSize))
        {
            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (channel, i, probe[position + static_cast<size_t> (i)]);

            auto block = juce::dsp::AudioBlock<float> (buffer);
            engine.process (block);

            for (int i = 0; i < blockSize; ++i)
                output.push_back (buffer.getSample (0, i));
        }

        return output;
    }

    int countMismatches (const std::vector<float>& a, const std::vector<float>& b)
    {
        auto mismatches = 0;

        for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
            if (a[i] != b[i])
                ++mismatches;

        return mismatches;
    }

    // Builds a v0.4.0-shaped state tree: the 59 shipped IDs and nothing else.
    juce::ValueTree makeV040ShapedState (TriptychAudioProcessor& source)
    {
        auto state = source.apvts.copyState();

        static constexpr const char* v050Ids[] = {
            ParamIDs::scSource, ParamIDs::scListen, ParamIDs::crossoverSlope, ParamIDs::lookahead, ParamIDs::mix,
            ParamIDs::lowDetectorMode, ParamIDs::lowAutoRelease, ParamIDs::lowCharacter, ParamIDs::lowStereoLink, ParamIDs::lowGateHold, ParamIDs::lowGateHysteresis,
            ParamIDs::midDetectorMode, ParamIDs::midAutoRelease, ParamIDs::midCharacter, ParamIDs::midStereoLink, ParamIDs::midGateHold, ParamIDs::midGateHysteresis,
            ParamIDs::highDetectorMode, ParamIDs::highAutoRelease, ParamIDs::highCharacter, ParamIDs::highStereoLink, ParamIDs::highGateHold, ParamIDs::highGateHysteresis
        };

        for (const auto* id : v050Ids)
        {
            auto child = state.getChildWithProperty ("id", juce::String (id));

            if (child.isValid())
                state.removeChild (child, nullptr);
        }

        // v0.4.0 never wrote a schema stamp.
        state.removeProperty (TriptychAudioProcessor::stateVersionProperty, nullptr);

        return state;
    }
}

// T1: a fresh v0.5.0 instance renders bit-identically to v0.4.0. Every one of
// the twenty-three additions is structurally inert at its default - not
// "close enough", literally the same instructions on the same samples.
TEST_CASE ("T1: a fresh v0.5.0 instance is sample-exactly the v0.4.0 chain", "[state][regression][neutrality]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr int blockSize = 512;

    const auto probe = makeNeutralityProbe (sampleRate, blockSize * 40);

    TriptychAudioProcessor processor;
    const auto shipped = renderThroughProcessor (processor, probe, sampleRate, blockSize);
    const auto legacy = renderThroughLegacyChain (processor, probe, sampleRate, blockSize);

    REQUIRE (shipped.size() == legacy.size());
    REQUIRE (! shipped.empty());

    // Sanity: the probe genuinely drives the compressors, so this is not a
    // comparison of two silent buffers.
    auto peak = 0.0f;
    for (const auto sample : shipped)
        peak = std::max (peak, std::abs (sample));
    CHECK (peak > 0.1f);

    CHECK (countMismatches (shipped, legacy) == 0);
}

// T1 (continued): every one of the twenty-three new parameters really does
// default to its neutral value.
TEST_CASE ("T1: every v0.5.0 parameter defaults neutral", "[state][regression][neutrality]")
{
    TriptychAudioProcessor processor;

    const auto checkChoice = [&] (const char* id, int expectedIndex)
    {
        auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (id));
        REQUIRE (parameter != nullptr);
        INFO (id);
        CHECK (parameter->getIndex() == expectedIndex);
    };

    const auto checkBool = [&] (const char* id)
    {
        auto* parameter = dynamic_cast<juce::AudioParameterBool*> (processor.apvts.getParameter (id));
        REQUIRE (parameter != nullptr);
        INFO (id);
        CHECK (parameter->get() == false);
    };

    const auto checkFloat = [&] (const char* id, float expected)
    {
        auto* parameter = processor.apvts.getParameter (id);
        REQUIRE (parameter != nullptr);
        INFO (id);
        CHECK (parameter->convertFrom0to1 (parameter->getValue()) == Catch::Approx (expected).margin (1e-3));
    };

    checkChoice (ParamIDs::scSource, 0);        // Internal
    checkChoice (ParamIDs::scListen, 0);        // Off
    checkChoice (ParamIDs::crossoverSlope, 1);  // 24 dB/oct - the v0.4.0 LR4 path
    checkChoice (ParamIDs::lookahead, 0);       // Off - latency stays 0
    checkFloat (ParamIDs::mix, 100.0f);         // fully wet

    for (const auto* id : { ParamIDs::lowDetectorMode, ParamIDs::midDetectorMode, ParamIDs::highDetectorMode })
        checkChoice (id, 0);                    // Peak

    for (const auto* id : { ParamIDs::lowCharacter, ParamIDs::midCharacter, ParamIDs::highCharacter })
        checkChoice (id, 0);                    // Clean

    for (const auto* id : { ParamIDs::lowAutoRelease, ParamIDs::midAutoRelease, ParamIDs::highAutoRelease })
        checkBool (id);

    for (const auto* id : { ParamIDs::lowStereoLink, ParamIDs::midStereoLink, ParamIDs::highStereoLink })
        checkFloat (id, 0.0f);

    for (const auto* id : { ParamIDs::lowGateHold, ParamIDs::midGateHold, ParamIDs::highGateHold })
        checkFloat (id, 0.0f);

    for (const auto* id : { ParamIDs::lowGateHysteresis, ParamIDs::midGateHysteresis, ParamIDs::highGateHysteresis })
        checkFloat (id, 0.0f);
}

// T2: tolerant migration. A v0.4.0-shaped session (the 59 shipped IDs, no
// schema stamp, none of the twenty-three additions) loads with every new
// parameter at its neutral default and renders sample-exactly like T1.
TEST_CASE ("T2: a v0.4.0-shaped session migrates to neutral defaults and renders identically", "[state][regression][migration]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr int blockSize = 512;

    TriptychAudioProcessor source;
    const auto v040State = makeV040ShapedState (source);

    // The stripped tree really is v0.4.0-shaped.
    CHECK (! v040State.hasProperty (TriptychAudioProcessor::stateVersionProperty));
    CHECK (! v040State.getChildWithProperty ("id", juce::String (ParamIDs::mix)).isValid());
    CHECK (v040State.getChildWithProperty ("id", juce::String (ParamIDs::lowThreshold)).isValid());

    juce::MemoryBlock encoded;
    {
        const std::unique_ptr<juce::XmlElement> xml (v040State.createXml());
        REQUIRE (xml != nullptr);
        juce::AudioProcessor::copyXmlToBinary (*xml, encoded);
    }

    TriptychAudioProcessor migrated;
    migrated.setStateInformation (encoded.getData(), static_cast<int> (encoded.getSize()));

    // Every addition resolved to its neutral default.
    const auto expectFloat = [&] (const char* id, float expected)
    {
        auto* parameter = migrated.apvts.getParameter (id);
        REQUIRE (parameter != nullptr);
        INFO (id);
        CHECK (parameter->convertFrom0to1 (parameter->getValue()) == Catch::Approx (expected).margin (1e-3));
    };

    expectFloat (ParamIDs::mix, 100.0f);
    expectFloat (ParamIDs::lowStereoLink, 0.0f);
    expectFloat (ParamIDs::midGateHold, 0.0f);
    expectFloat (ParamIDs::highGateHysteresis, 0.0f);

    for (const auto* id : { ParamIDs::scSource, ParamIDs::scListen, ParamIDs::lookahead,
                             ParamIDs::lowDetectorMode, ParamIDs::midCharacter, ParamIDs::highDetectorMode })
    {
        auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (migrated.apvts.getParameter (id));
        REQUIRE (parameter != nullptr);
        INFO (id);
        CHECK (parameter->getIndex() == 0);
    }

    {
        auto* slope = dynamic_cast<juce::AudioParameterChoice*> (migrated.apvts.getParameter (ParamIDs::crossoverSlope));
        REQUIRE (slope != nullptr);
        CHECK (slope->getIndex() == 1);
    }

    // Latency is still zero, exactly as the migrated session expects.
    migrated.prepareToPlay (sampleRate, blockSize);
    CHECK (migrated.getLatencySamples() == 0);

    // And the audio is sample-exactly the legacy render.
    const auto probe = makeNeutralityProbe (sampleRate, blockSize * 30);
    const auto migratedRender = renderThroughProcessor (migrated, probe, sampleRate, blockSize);
    const auto legacy = renderThroughLegacyChain (migrated, probe, sampleRate, blockSize);

    REQUIRE (migratedRender.size() == legacy.size());
    CHECK (countMismatches (migratedRender, legacy) == 0);
}

// The schema-versioning hook itself: v0.5.0 stamps 5, and a stamped tree
// round-trips through save/load with the attribute intact.
TEST_CASE ("State schema version 5 is stamped on save and survives a round-trip", "[state][migration]")
{
    TriptychAudioProcessor source;

    juce::MemoryBlock encoded;
    source.getStateInformation (encoded);

    const std::unique_ptr<juce::XmlElement> xml (juce::AudioProcessor::getXmlFromBinary (encoded.getData(), static_cast<int> (encoded.getSize())));
    REQUIRE (xml != nullptr);

    const auto tree = juce::ValueTree::fromXml (*xml);
    REQUIRE (tree.isValid());
    CHECK (static_cast<int> (tree.getProperty (TriptychAudioProcessor::stateVersionProperty, 0)) == TriptychAudioProcessor::stateSchemaVersion);
    CHECK (TriptychAudioProcessor::stateSchemaVersion == 5);

    TriptychAudioProcessor destination;
    destination.setStateInformation (encoded.getData(), static_cast<int> (encoded.getSize()));

    juce::MemoryBlock reEncoded;
    destination.getStateInformation (reEncoded);

    const std::unique_ptr<juce::XmlElement> reXml (juce::AudioProcessor::getXmlFromBinary (reEncoded.getData(), static_cast<int> (reEncoded.getSize())));
    REQUIRE (reXml != nullptr);

    const auto reTree = juce::ValueTree::fromXml (*reXml);
    CHECK (static_cast<int> (reTree.getProperty (TriptychAudioProcessor::stateVersionProperty, 0)) == TriptychAudioProcessor::stateSchemaVersion);
}
