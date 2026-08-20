#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "gui/BusPanel.h"
#include "gui/NeedleMeter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <vector>

// M3 vector-editor layout tests (issue #4). Unlike the photoreal siblings
// (whose EditorLayoutTests assert hand-measured pixel manifests against
// baked master renders), Triptych's geometry is COMPUTED from the layout
// constants + control tables in PluginEditor.cpp - so the manifest under
// test here is the real constructed component tree itself: containment, no
// overlap, and full parameter coverage. Any arithmetic slip in the layout
// constants shows up as a concrete clipped/colliding control here.
namespace
{
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

    juce::Rectangle<int> boundsInEditor (const juce::Component& component, const juce::Component& editor)
    {
        auto bounds = component.getBounds();

        for (const auto* ancestor = component.getParentComponent();
             ancestor != nullptr && ancestor != &editor;
             ancestor = ancestor->getParentComponent())
        {
            bounds += ancestor->getPosition();
        }

        return bounds;
    }
}

TEST_CASE ("Every automatable parameter has exactly one attached control", "[gui][layout]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    TriptychAudioProcessorEditor editor (processor);

    int sliders = 0, toggles = 0;
    visitDescendants<juce::Slider> (editor, [&] (juce::Slider&) { ++sliders; });
    visitDescendants<juce::ToggleButton> (editor, [&] (juce::ToggleButton&) { ++toggles; });

    // The APVTS carries 53 float + 10 choice + 19 bool parameters = 82
    // (ParameterIds.h: the 59 v0.1-v0.4 IDs plus the 23 v0.5.0 additions).
    // One knob per float/choice parameter, one toggle per bool parameter -
    // no parameter may be left off the M3 surface, and no control may exist
    // without a parameter.
    CHECK ((int) processor.getParameters().size() == 82);
    CHECK (sliders + toggles == (int) processor.getParameters().size());
    CHECK (sliders == 63);
    CHECK (toggles == 19);
}

TEST_CASE ("Moving a knob moves its parameter - one wiring spot check per section", "[gui][layout]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    TriptychAudioProcessorEditor editor (processor);

    struct Expectation
    {
        const char* panel;      // labels repeat across band columns, so
        const char* title;      // every lookup is panel-scoped
        const char* parameterId;
        double sliderValue;     // a legal, non-default value
        float expectedRaw;      // denormalised parameter value afterwards
    };

    const Expectation expectations[] = {
        { "Global", "Mix", "mix", 60.0, 60.0f },
        { "Global", "Output", "output", 3.0, 3.0f },
        { "Low Band", "Threshold", "lowThreshold", -30.0, -30.0f },
        { "Mid Band", "Knee", "midKnee", 25.0, 25.0f },
        { "High Band", "Makeup", "highMakeup", 6.0, 6.0f },
        { "High Band", "Hyst", "highGateHysteresis", 4.0, 4.0f },
    };

    for (const auto& expectation : expectations)
    {
        auto* panel = findDescendantByTitle<basilica::gui::BusPanel> (editor, expectation.panel);
        REQUIRE (panel != nullptr);

        auto* knob = findDescendantByTitle<juce::Slider> (*panel, expectation.title);
        REQUIRE (knob != nullptr);
        INFO ("knob \"" << expectation.panel << " / " << expectation.title
              << "\" -> " << expectation.parameterId);

        auto* raw = processor.apvts.getRawParameterValue (expectation.parameterId);
        REQUIRE (raw != nullptr);

        knob->setValue (expectation.sliderValue, juce::sendNotificationSync);
        CHECK (raw->load() == Catch::Approx (expectation.expectedRaw).margin (1.0e-4));
    }
}

TEST_CASE ("All controls, labels and meters stay inside their panel; panels stay inside the editor", "[gui][layout]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    TriptychAudioProcessorEditor editor (processor);

    const auto editorBounds = editor.getLocalBounds();
    CHECK (editorBounds.getWidth() > 0);
    CHECK (editorBounds.getHeight() > 0);

    int panelsSeen = 0;

    visitDescendants<basilica::gui::BusPanel> (editor, [&] (basilica::gui::BusPanel& panel)
    {
        ++panelsSeen;
        INFO ("panel \"" << panel.getTitle().toStdString() << "\" bounds "
              << panel.getBounds().toString().toStdString());
        CHECK (editorBounds.contains (panel.getBounds()));

        // Every direct child (knobs incl. their value boxes, attached
        // labels, toggles, the meter) must be fully inside the panel.
        for (int i = 0; i < panel.getNumChildComponents(); ++i)
        {
            const auto* child = panel.getChildComponent (i);
            INFO ("child \"" << child->getName().toStdString() << "\" bounds "
                  << child->getBounds().toString().toStdString()
                  << " in panel \"" << panel.getTitle().toStdString() << "\" "
                  << panel.getLocalBounds().toString().toStdString());
            CHECK (panel.getLocalBounds().contains (child->getBounds()));
        }
    });

    CHECK (panelsSeen == 4);
}

TEST_CASE ("No two interactive controls or meters overlap", "[gui][layout]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    TriptychAudioProcessorEditor editor (processor);

    struct Entry
    {
        juce::String name;
        juce::Rectangle<int> bounds;
    };

    std::vector<Entry> entries;

    const auto collect = [&] (juce::Component& component)
    {
        entries.push_back ({ component.getTitle(), boundsInEditor (component, editor) });
    };

    visitDescendants<juce::Slider> (editor, [&] (juce::Slider& s) { collect (s); });
    visitDescendants<juce::ToggleButton> (editor, [&] (juce::ToggleButton& t) { collect (t); });
    visitDescendants<basilica::gui::NeedleMeter> (editor, [&] (basilica::gui::NeedleMeter& m) { collect (m); });

    // 63 knobs + 19 toggles + 3 meters - the pairwise scan below must not
    // pass vacuously on an empty collection.
    REQUIRE (entries.size() == 85);

    for (size_t i = 0; i < entries.size(); ++i)
    {
        for (size_t j = i + 1; j < entries.size(); ++j)
        {
            INFO ("\"" << entries[i].name.toStdString() << "\" " << entries[i].bounds.toString().toStdString()
                  << " vs \"" << entries[j].name.toStdString() << "\" " << entries[j].bounds.toString().toStdString());
            CHECK_FALSE (entries[i].bounds.intersects (entries[j].bounds));
        }
    }
}

TEST_CASE ("Panels do not overlap each other or the preset bar", "[gui][layout]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    TriptychAudioProcessorEditor editor (processor);

    std::vector<juce::Rectangle<int>> panelBounds;

    visitDescendants<basilica::gui::BusPanel> (editor, [&] (basilica::gui::BusPanel& panel)
    {
        panelBounds.push_back (panel.getBounds());
    });

    REQUIRE (panelBounds.size() == 4);

    for (size_t i = 0; i < panelBounds.size(); ++i)
        for (size_t j = i + 1; j < panelBounds.size(); ++j)
            CHECK_FALSE (panelBounds[i].intersects (panelBounds[j]));

    // The preset bar band sits above all panels.
    juce::Component* presetBar = nullptr;

    for (int i = 0; i < editor.getNumChildComponents(); ++i)
        if (dynamic_cast<basilica::presets::PresetBar*> (editor.getChildComponent (i)) != nullptr)
            presetBar = editor.getChildComponent (i);

    REQUIRE (presetBar != nullptr);

    for (const auto& bounds : panelBounds)
        CHECK_FALSE (presetBar->getBounds().intersects (bounds));
}

TEST_CASE ("Every knob's visible label text matches its accessible title (label-in-name)", "[gui][layout][a11y]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    TriptychAudioProcessorEditor editor (processor);

    // WCAG 2.5.3 Label in Name: the label painted next to a knob must be
    // the same string AT users hear as the control's name - a mismatch
    // breaks voice-control users ("click Threshold" targeting a control
    // whose accessible name is something else).
    int labelledKnobs = 0;

    visitDescendants<juce::Label> (editor, [&] (juce::Label& label)
    {
        if (auto* attached = label.getAttachedComponent())
        {
            if (auto* slider = dynamic_cast<juce::Slider*> (attached))
            {
                ++labelledKnobs;
                INFO ("label \"" << label.getText().toStdString() << "\" for knob \""
                      << slider->getTitle().toStdString() << "\"");
                CHECK (label.getText() == slider->getTitle());
            }
        }
    });

    // Every one of the 63 knobs carries an attached, matching label.
    CHECK (labelledKnobs == 63);
}
