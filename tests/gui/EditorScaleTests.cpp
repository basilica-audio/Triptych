#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

// Stepped window scaling (75/100/150/200%) - the photoreal-family
// convention (no free resize with prerendered assets). Persistence rides
// on the family's shared APVTS root property ("editorScale", a double), so stored sessions keep round-tripping and
// arbitrary stored values snap to the nearest step.

TEST_CASE ("A fresh instance opens at 100% and its own design size", "[gui][scale]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    CHECK_FALSE (processor.apvts.state.hasProperty (TriptychAudioProcessorEditor::getScaleStatePropertyId()));
    CHECK (TriptychAudioProcessorEditor::readPersistedScaleStepIndex (processor.apvts.state)
           == TriptychAudioProcessorEditor::defaultScaleStepIndex);

    TriptychAudioProcessorEditor editor (processor);

    CHECK (editor.getEditorScale() == Catch::Approx (1.0f));
    CHECK (editor.getWidth() == editor.getDesignWidth());
    CHECK (editor.getHeight() == editor.getDesignHeight());
}

TEST_CASE ("Applying a scale step resizes the window by exactly that factor", "[gui][scale]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    TriptychAudioProcessorEditor editor (processor);

    for (size_t step = 0; step < TriptychAudioProcessorEditor::scaleSteps.size(); ++step)
    {
        editor.applyScaleStep ((int) step);

        const auto scale = TriptychAudioProcessorEditor::scaleSteps[step];
        CHECK (editor.getWidth() == (int) std::lround ((float) editor.getDesignWidth() * scale));
        CHECK (editor.getHeight() == (int) std::lround ((float) editor.getDesignHeight() * scale));
    }
}

TEST_CASE ("Out-of-range step indices clamp instead of crashing", "[gui][scale]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    TriptychAudioProcessorEditor editor (processor);

    editor.applyScaleStep (-3);
    CHECK (editor.getScaleStepIndex() == 0);

    editor.applyScaleStep (99);
    CHECK (editor.getScaleStepIndex() == (int) TriptychAudioProcessorEditor::scaleSteps.size() - 1);
}

TEST_CASE ("Continuous legacy scale values snap to the nearest step", "[gui][scale][state]")
{
    TriptychAudioProcessor processor;

    struct Expectation
    {
        double stored;
        int expectedStep;
    };

    // The shared family property may carry any continuous value.
    const std::vector<Expectation> expectations = {
        { 0.6, 0 }, { 0.8, 0 }, { 0.95, 1 }, { 1.0, 1 }, { 1.2, 1 },
        { 1.4, 2 }, { 1.5, 2 }, { 1.72, 2 }, { 1.9, 3 }, { 2.0, 3 }, { 5.0, 3 },
    };

    for (const auto& expectation : expectations)
    {
        processor.apvts.state.setProperty (TriptychAudioProcessorEditor::getScaleStatePropertyId(),
                                           expectation.stored, nullptr);

        INFO ("stored scale: " << expectation.stored);
        CHECK (TriptychAudioProcessorEditor::readPersistedScaleStepIndex (processor.apvts.state)
               == expectation.expectedStep);
    }
}

TEST_CASE ("The chosen scale survives a full plugin-state round trip", "[gui][scale][state]")
{
    juce::MemoryBlock savedState;

    {
        TriptychAudioProcessor processor;
        processor.prepareToPlay (48000.0, 512);

        TriptychAudioProcessorEditor editor (processor);
        editor.applyScaleStep (2); // 150%

        processor.getStateInformation (savedState);
    }

    TriptychAudioProcessor restored;
    restored.setStateInformation (savedState.getData(), (int) savedState.getSize());

    CHECK (TriptychAudioProcessorEditor::readPersistedScaleStepIndex (restored.apvts.state) == 2);

    TriptychAudioProcessorEditor editor (restored);
    CHECK (editor.getEditorScale() == Catch::Approx (1.5f));
    CHECK (editor.getWidth() == (int) std::lround ((float) editor.getDesignWidth() * 1.5f));
}

TEST_CASE ("Persisting the scale does not disturb parameter state", "[gui][scale][state]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    auto* lowThreshold = processor.apvts.getParameter ("lowThreshold");
    REQUIRE (lowThreshold != nullptr);

    lowThreshold->setValueNotifyingHost (0.73f);

    TriptychAudioProcessorEditor editor (processor);
    editor.applyScaleStep (3);

    CHECK (lowThreshold->getValue() == Catch::Approx (0.73f).margin (1.0e-4));
}
