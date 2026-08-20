#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "gui/BusPanel.h"
#include "gui/NeedleMeter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <vector>

// M3 accessible parameter surface (issue #4): asserts the actual
// AccessibilityHandler-level behaviour of the vector editor, not just that
// it constructs. juce::ScopedJuceInitialiser_GUI is installed once for the
// whole test binary in tests/TestMain.cpp, so constructing Components is
// safe in this headless console executable with no message loop or native
// window/peer.
//
// Deliberately calls createAccessibilityHandler() directly rather than
// getAccessibilityHandler(): the latter (JUCE 8.0.14
// juce_Component.cpp:3323-3326) only returns a handler once the component
// has a live native window peer, which this headless binary never has.
// createAccessibilityHandler() is public API safely callable independent of
// any live OS accessibility bridge (see its docs in juce_Component.h).
namespace
{
    // The editor's controls live inside one BusPanel per section - all
    // lookups walk the tree recursively. Labels repeat across the three
    // band columns (every band has a "Threshold"), so lookups are always
    // panel-scoped where ambiguity exists.
    template <typename ComponentType>
    ComponentType* findDescendantByTitle (juce::Component& parent, const juce::String& title)
    {
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
        {
            auto* child = parent.getChildComponent (i);

            if (auto* typed = dynamic_cast<ComponentType*> (child))
                if (typed->getTitle() == title)
                    return typed;

            if (auto* found = findDescendantByTitle<ComponentType> (*child, title))
                return found;
        }

        return nullptr;
    }

    template <typename ComponentType>
    void visitDescendants (juce::Component& parent, const std::function<void (ComponentType&)>& visit)
    {
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
        {
            auto* child = parent.getChildComponent (i);

            if (auto* typed = dynamic_cast<ComponentType*> (child))
                visit (*typed);

            visitDescendants<ComponentType> (*child, visit);
        }
    }

    // juce::Button::createAccessibilityHandler() (unlike juce::Slider's) is
    // declared PROTECTED (JUCE 8.0.14 juce_Button.h). Per [class.access.virt]
    // access is checked against the STATIC type naming the call, so calling
    // through juce::Component& (where it is public) compiles and virtual
    // dispatch still invokes the most-derived override. Used uniformly for
    // all component types tested here.
    std::unique_ptr<juce::AccessibilityHandler> createHandlerForTest (juce::Component& component)
    {
        return component.createAccessibilityHandler();
    }

    constexpr const char* sectionPanelTitles[] = { "Global", "Low Band", "Mid Band", "High Band" };
}

TEST_CASE ("Knob accessible value strings include their declared unit", "[gui][a11y]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    TriptychAudioProcessorEditor editor (processor);

    struct Expectation
    {
        const char* panel;
        const char* label;
        const char* unitSuffix;
    };

    // One representative per unit declared in ParameterLayout.cpp
    // (.withLabel("dB"/":1"/"%"/"ms"/"Hz")), scoped to a panel because
    // labels like "Threshold"/"Ratio" repeat across the three band columns.
    // The A-02 gap this guards against is units being dropped entirely by
    // the SliderAttachment's own textFromValueFunction (JUCE 8.0.14
    // juce_ParameterAttachments.cpp:128 assigns it in the attachment
    // constructor, silently clobbering anything set before -
    // PluginEditor.cpp must set its unit-suffixing function AFTER
    // constructing the attachment).
    const Expectation expectations[] = {
        { "Global", "Low/Mid", "Hz" },
        { "Global", "Mix", "%" },
        { "Low Band", "Threshold", "dB" },
        { "Low Band", "Ratio", ":1" },
        { "Mid Band", "Knee", "%" },
        { "High Band", "Attack", "ms" },
        { "High Band", "Lim Thr", "dB" },
    };

    for (const auto& expectation : expectations)
    {
        auto* panel = findDescendantByTitle<basilica::gui::BusPanel> (editor, expectation.panel);
        REQUIRE (panel != nullptr);

        auto* knob = findDescendantByTitle<juce::Slider> (*panel, expectation.label);
        REQUIRE (knob != nullptr);

        const auto handler = createHandlerForTest (*knob);
        REQUIRE (handler != nullptr);

        auto* valueInterface = handler->getValueInterface();
        REQUIRE (valueInterface != nullptr);

        const auto valueText = valueInterface->getCurrentValueAsString();
        INFO ("knob \"" << expectation.panel << " / " << expectation.label
              << "\" accessible value = \"" << valueText.toStdString() << "\"");
        CHECK (valueText.endsWith (expectation.unitSuffix));
    }
}

TEST_CASE ("Choice knobs announce the current choice by NAME, not by index", "[gui][a11y]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    TriptychAudioProcessorEditor editor (processor);

    struct Expectation
    {
        const char* panel;
        const char* label;
        const char* defaultChoiceName; // per ParameterLayout.cpp defaults
    };

    const Expectation expectations[] = {
        { "Global", "Slope", "24 dB/oct" },   // crossoverSlope, default index 1
        { "Global", "Lookahead", "Off" },     // lookahead, default index 0
        { "Global", "Sidechain", "Internal" },// scSource, default index 0
        { "Global", "Listen", "Off" },        // scListen, default index 0
        { "Low Band", "Detector", "Peak" },   // lowDetectorMode, default index 0
        { "High Band", "Character", "Clean" },// highCharacter, default index 0
    };

    for (const auto& expectation : expectations)
    {
        auto* panel = findDescendantByTitle<basilica::gui::BusPanel> (editor, expectation.panel);
        REQUIRE (panel != nullptr);

        auto* knob = findDescendantByTitle<juce::Slider> (*panel, expectation.label);
        REQUIRE (knob != nullptr);

        const auto handler = createHandlerForTest (*knob);
        REQUIRE (handler != nullptr);

        auto* valueInterface = handler->getValueInterface();
        REQUIRE (valueInterface != nullptr);

        const auto valueText = valueInterface->getCurrentValueAsString();
        INFO ("choice knob \"" << expectation.panel << " / " << expectation.label
              << "\" accessible value = \"" << valueText.toStdString() << "\"");
        CHECK (valueText == expectation.defaultChoiceName);
    }
}

TEST_CASE ("Every interactive control is keyboard-focusable", "[gui][a11y]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    TriptychAudioProcessorEditor editor (processor);

    int slidersSeen = 0, togglesSeen = 0;

    visitDescendants<juce::Slider> (editor, [&] (juce::Slider& slider)
    {
        ++slidersSeen;
        INFO ("knob \"" << slider.getTitle().toStdString() << "\"");
        CHECK (slider.getWantsKeyboardFocus());
        CHECK (slider.getTitle().isNotEmpty());
    });

    visitDescendants<juce::ToggleButton> (editor, [&] (juce::ToggleButton& toggle)
    {
        ++togglesSeen;
        INFO ("toggle \"" << toggle.getTitle().toStdString() << "\"");
        CHECK (toggle.getWantsKeyboardFocus());
        CHECK (toggle.getTitle().isNotEmpty());
    });

    // 53 float + 10 choice parameters = 63 knobs; 19 bool parameters = 19
    // toggles (see ParameterIds.h). A zero-match walk must not pass
    // vacuously.
    CHECK (slidersSeen == 63);
    CHECK (togglesSeen == 19);

    // The preset bar's buttons are stock juce::TextButtons - focusable by
    // default, and none may have opted out. Save/Delete start DISABLED
    // (factory preset active, nothing dirty), and JUCE 8.0.14's
    // getWantsKeyboardFocus() reports false while a component is disabled
    // (juce_Component.cpp:2874) - correct WCAG behaviour (disabled controls
    // leave the tab order), so the assertion is focusable-iff-enabled.
    int presetButtonsSeen = 0, disabledPresetButtonsSeen = 0;

    visitDescendants<juce::TextButton> (editor, [&] (juce::TextButton& button)
    {
        ++presetButtonsSeen;

        if (! button.isEnabled())
            ++disabledPresetButtonsSeen;

        INFO ("preset-bar button \"" << button.getButtonText().toStdString() << "\"");
        CHECK (button.getWantsKeyboardFocus() == button.isEnabled());
    });

    CHECK (presetButtonsSeen == 8); // prev/name/next/save/save-as/delete/import/export
    CHECK (disabledPresetButtonsSeen == 2); // Save (not dirty) + Delete (factory preset)
}

TEST_CASE ("Arrow keys step knobs by a practical amount, Shift+Arrow steps finer", "[gui][a11y]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    TriptychAudioProcessorEditor editor (processor);

    // Mix: linear 0-100%, 0.01% interval (ParameterLayout.cpp). The stock
    // Slider::Pimpl::keyPressed would step by the raw 0.01% interval
    // (10,000 presses for a full sweep) and ignore Shift entirely - see
    // src/gui/KeyboardSteps.h.
    auto* knob = findDescendantByTitle<juce::Slider> (editor, "Mix");
    REQUIRE (knob != nullptr);

    knob->setValue (50.0, juce::sendNotificationSync);

    juce::Component& knobAsComponent = *knob;

    // Plain Right = 1% of the 100% range = 1%.
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (knob->getValue() == Catch::Approx (51.0).margin (1.0e-4));

    // Shift+Right = 0.1% of the range = 0.1%, snapped to the 0.01% grid.
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey,
                                                         juce::ModifierKeys::shiftModifier, 0)));
    CHECK (knob->getValue() == Catch::Approx (51.1).margin (1.0e-4));

    // Plain Left steps back down symmetrically.
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::leftKey)));
    CHECK (knob->getValue() == Catch::Approx (50.1).margin (1.0e-4));

    // PageDown = 10% of the range = 10%.
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::pageDownKey)));
    CHECK (knob->getValue() == Catch::Approx (40.1).margin (1.0e-4));

    // Home/End jump to the range extremes (WAI-ARIA slider pattern).
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
    CHECK (knob->getValue() == Catch::Approx (0.0).margin (1.0e-4));
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::endKey)));
    CHECK (knob->getValue() == Catch::Approx (100.0).margin (1.0e-4));
}

TEST_CASE ("Choice knobs step exactly one detent per arrow press and announce the new choice", "[gui][a11y]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    TriptychAudioProcessorEditor editor (processor);

    auto* globalPanel = findDescendantByTitle<basilica::gui::BusPanel> (editor, "Global");
    REQUIRE (globalPanel != nullptr);

    // Crossover Slope: 3 choices (12/24/48 dB/oct). A 1%-of-range coarse
    // step would collapse to zero after interval snapping - KeyboardSteps.h's
    // one-interval fallback must turn every arrow press into exactly one
    // detent instead of silently swallowing it.
    auto* knob = findDescendantByTitle<juce::Slider> (*globalPanel, "Slope");
    REQUIRE (knob != nullptr);

    juce::Component& knobAsComponent = *knob;

    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
    CHECK (knob->getValue() == Catch::Approx (0.0).margin (1.0e-6));

    const auto handler = createHandlerForTest (*knob);
    REQUIRE (handler != nullptr);
    auto* valueInterface = handler->getValueInterface();
    REQUIRE (valueInterface != nullptr);
    CHECK (valueInterface->getCurrentValueAsString() == "12 dB/oct");

    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (knob->getValue() == Catch::Approx (1.0).margin (1.0e-6));
    CHECK (valueInterface->getCurrentValueAsString() == "24 dB/oct");

    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::leftKey)));
    CHECK (knob->getValue() == Catch::Approx (0.0).margin (1.0e-6));

    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::endKey)));
    CHECK (knob->getValue() == Catch::Approx (2.0).margin (1.0e-6));
    CHECK (valueInterface->getCurrentValueAsString() == "48 dB/oct");
}

TEST_CASE ("Ctrl/Cmd-modified arrow presses are left to the host", "[gui][a11y]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    TriptychAudioProcessorEditor editor (processor);

    auto* knob = findDescendantByTitle<juce::Slider> (editor, "Mix");
    REQUIRE (knob != nullptr);

    knob->setValue (50.0, juce::sendNotificationSync);
    juce::Component& knobAsComponent = *knob;

    CHECK_FALSE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey,
                                                             juce::ModifierKeys::ctrlModifier, 0)));
    CHECK_FALSE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey,
                                                             juce::ModifierKeys::commandModifier, 0)));
    CHECK (knob->getValue() == Catch::Approx (50.0));
}

TEST_CASE ("Toggles expose title, checkable state, and real APVTS wiring", "[gui][a11y]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    TriptychAudioProcessorEditor editor (processor);

    struct Expectation
    {
        const char* panel;
        const char* title;
        const char* parameterId;
        float defaultRaw;
    };

    const Expectation expectations[] = {
        { "Low Band", "Gate", "lowGateEnabled", 0.0f },
        { "Low Band", "M/S", "lowMidSideEnabled", 0.0f },
        { "Mid Band", "Mute", "midMute", 0.0f },
        { "Mid Band", "Auto Rel", "midAutoRelease", 0.0f },
        { "High Band", "Solo", "highSolo", 0.0f },
        { "High Band", "Limiter", "highLimiterEnabled", 0.0f },
    };

    for (const auto& expectation : expectations)
    {
        auto* panel = findDescendantByTitle<basilica::gui::BusPanel> (editor, expectation.panel);
        REQUIRE (panel != nullptr);

        auto* toggle = findDescendantByTitle<juce::ToggleButton> (*panel, expectation.title);
        REQUIRE (toggle != nullptr);
        INFO ("toggle \"" << expectation.panel << " / " << expectation.title << "\"");

        CHECK (toggle->getTitle() == expectation.title);

        const auto handler = createHandlerForTest (*toggle);
        REQUIRE (handler != nullptr);

        // juce::ToggleButton's constructor calls setClickingTogglesState(true)
        // (JUCE 8.0.14 juce_ToggleButton.cpp), so the base Button handler
        // exposes checkable/checked state; its createAccessibilityHandler
        // reports AccessibilityRole::toggleButton (juce_ToggleButton.cpp:71),
        // so VoiceOver's rotor lists these as toggles, not plain buttons.
        CHECK (handler->getCurrentState().isCheckable());

        // Real APVTS wiring, never a decorative stub: flipping the toggle
        // through the state API (the same entry point mouse, Space/Return
        // and the attachment all funnel through) must move the parameter.
        auto* raw = processor.apvts.getRawParameterValue (expectation.parameterId);
        REQUIRE (raw != nullptr);
        CHECK (raw->load() == Catch::Approx (expectation.defaultRaw));

        toggle->setToggleState (expectation.defaultRaw < 0.5f, juce::sendNotificationSync);
        CHECK (raw->load() == Catch::Approx (expectation.defaultRaw < 0.5f ? 1.0f : 0.0f));

        toggle->setToggleState (expectation.defaultRaw >= 0.5f, juce::sendNotificationSync);
        CHECK (raw->load() == Catch::Approx (expectation.defaultRaw));
    }
}

TEST_CASE ("Each section is an accessibility focus container that does not trap Tab", "[gui][a11y]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    TriptychAudioProcessorEditor editor (processor);

    for (const auto* title : sectionPanelTitles)
    {
        auto* panel = findDescendantByTitle<basilica::gui::BusPanel> (editor, title);
        REQUIRE (panel != nullptr);
        INFO ("section panel \"" << title << "\"");

        // focusContainer (accessibility grouping: AT reads "Low Band,
        // Threshold")...
        CHECK (panel->isFocusContainer());

        // ...but NOT a keyboard focus container: JUCE 8.0.14's Tab
        // traversal looks up the nearest isKeyboardFocusContainer()
        // ancestor (juce_Component.cpp:2918), so setting that flag here
        // would trap Tab inside one section. setFocusContainerType(focusContainer)
        // sets only the accessibility-side flag (juce_Component.cpp:2879).
        CHECK_FALSE (panel->isKeyboardFocusContainer());

        // Reported to AT as a named group.
        const auto handler = createHandlerForTest (*panel);
        REQUIRE (handler != nullptr);
        CHECK (handler->getRole() == juce::AccessibilityRole::group);
    }

    // Every interactive control lives inside exactly one of the four
    // section panels (non-vacuous: the walk must find all 82).
    int controlsChecked = 0;

    const auto isInsideExactlyOnePanel = [&] (juce::Component& control)
    {
        int containingPanels = 0;

        for (auto* ancestor = control.getParentComponent(); ancestor != nullptr; ancestor = ancestor->getParentComponent())
            if (dynamic_cast<basilica::gui::BusPanel*> (ancestor) != nullptr)
                ++containingPanels;

        ++controlsChecked;
        CHECK (containingPanels == 1);
    };

    visitDescendants<juce::Slider> (editor, [&] (juce::Slider& s) { isInsideExactlyOnePanel (s); });
    visitDescendants<juce::ToggleButton> (editor, [&] (juce::ToggleButton& t) { isInsideExactlyOnePanel (t); });

    CHECK (controlsChecked == 82);
}

TEST_CASE ("Needle meters expose a read-only, unit-suffixed accessible value per band", "[gui][a11y]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    TriptychAudioProcessorEditor editor (processor);

    const char* meterTitles[] = { "Low band gain reduction meter",
                                  "Mid band gain reduction meter",
                                  "High band gain reduction meter" };

    int metersSeen = 0;

    for (const auto* title : meterTitles)
    {
        auto* meter = findDescendantByTitle<basilica::gui::NeedleMeter> (editor, title);
        REQUIRE (meter != nullptr);
        ++metersSeen;
        INFO ("meter \"" << title << "\"");

        // Display-only: never in the tab order, never eats mouse events
        // aimed at nearby controls.
        CHECK_FALSE (meter->getWantsKeyboardFocus());

        const auto handler = meter->createAccessibilityHandler();
        REQUIRE (handler != nullptr);

        auto* valueInterface = handler->getValueInterface();
        REQUIRE (valueInterface != nullptr);
        CHECK (valueInterface->isReadOnly());

        // On-demand value in dB, following the ballistic-smoothed reading.
        meter->setImmediateDbForPreview (6.4f);
        CHECK (valueInterface->getCurrentValueAsString() == "6.4 dB");
    }

    CHECK (metersSeen == 3);

    // ...and there are exactly three (one per band), no strays.
    int totalMeters = 0;
    visitDescendants<basilica::gui::NeedleMeter> (editor, [&] (basilica::gui::NeedleMeter&) { ++totalMeters; });
    CHECK (totalMeters == 3);
}
