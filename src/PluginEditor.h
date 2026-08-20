#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

#include "gui/BasilicaLookAndFeel.h"
#include "gui/BusPanel.h"
#include "gui/NeedleMeter.h"
#include "gui/PointerKnob.h"
#include "presets/PresetBar.h"

class TriptychAudioProcessor;

// M3 custom vector editor + accessible parameter surface (issue #4), ported
// from Miserere's merged M3 implementation (basilica-audio/miserere PR #31).
//
// Everything is drawn at runtime by BasilicaLookAndFeel / the src/gui
// components - no photoreal PNG assets exist in this plugin (unlike the
// filmstrip/faceplate siblings): pointer knobs with engraved scale rings,
// lamp toggles, and one vector gain-reduction needle meter per band (fed by
// the engine's per-band GainReductionMeter telemetry), grouped into one
// BusPanel per processing section in signal-flow order: a Global strip
// (crossovers, slope, lookahead, sidechain, mix, output) above the three
// band columns (Low / Mid / High, side by side - the classic multiband
// view).
//
// FOCUS ORDER CONTRACT (WCAG 2.4.3, suite-wide convention): JUCE's default
// traverser walks children in z-order, which equals CREATION order - the
// constructor therefore creates every control in signal-flow/reading order
// (preset bar first, then panel by panel, left-to-right within each row),
// and nothing may reorder children afterwards. Each BusPanel is an
// accessibility focus container (NOT a keyboard focus container - see
// BusPanel.h), so screen readers hear "Low Band, Threshold" while Tab still
// walks the whole editor.
//
// Controls are built data-driven from ID/label tables (see the .cpp) - all
// float AND choice parameters are PointerKnobs (choice knobs snap to their
// integer detents and announce the choice NAME - the interim editor's
// ComboBox selectors are gone), bool parameters are real juce::ToggleButtons
// (focusable and Space/Enter-operable out of the box, reported as toggle
// buttons by AT).
class TriptychAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit TriptychAudioProcessorEditor (TriptychAudioProcessor& processorToEdit);
    ~TriptychAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        basilica::gui::PointerKnob slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<ButtonAttachment> attachment;
    };

    // One section faceplate: the BusPanel component plus its control rows
    // (each row a left-to-right list of the controls laid out in it) and an
    // optional gain-reduction needle meter. Unlike the sibling editors'
    // right-hand meter bay, Triptych's band columns carry their meter as a
    // full-width strip directly under the panel header, above the control
    // rows - the columns are tall and narrow, so a side bay would waste a
    // third of each column's width.
    struct Panel
    {
        std::unique_ptr<basilica::gui::BusPanel> component;
        std::vector<std::vector<juce::Component*>> rows;
        basilica::gui::NeedleMeter* meter = nullptr; // owned via `meters`
    };

    Panel& addPanel (const juce::String& sectionTitle);
    void addRow (Panel& panel);
    Knob& addKnob (Panel& panel, const char* parameterId, const juce::String& labelText);
    Toggle& addToggle (Panel& panel, const char* parameterId, const juce::String& labelText);
    basilica::gui::NeedleMeter& addMeter (Panel& panel, const juce::String& accessibleTitle,
                                          const juce::String& faceLegend);

    // Builds one band column's full control surface (shared by Low/Mid/High;
    // the High band additionally appends its limiter row afterwards).
    struct BandParameterIds;
    Panel& addBandPanel (const juce::String& title, const BandParameterIds& ids,
                         const juce::String& meterTitle, const juce::String& meterLegend);

    void timerCallback() override;

    static int slotWidthFor (const juce::Component& control) noexcept;
    static int rowWidth (const std::vector<juce::Component*>& row) noexcept;
    int panelRequiredWidth (const Panel& panel) const noexcept;
    int panelRequiredHeight (const Panel& panel) const noexcept;

    TriptychAudioProcessor& audioProcessor;

    // Must be constructed before any child that paints with it and
    // installed on `this` so it propagates to every child (including the
    // preset bar's stock buttons/menus/dialogs).
    basilica::gui::BasilicaLookAndFeel lookAndFeel;

    // M2 preset system - constructed after the localisation frame is
    // installed (see the constructor) so its TRANS()'d strings pick up the
    // right language from the very first paint.
    basilica::presets::PresetBar presetBar;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Toggle>> toggles;
    std::vector<std::unique_ptr<basilica::gui::NeedleMeter>> meters;
    std::vector<std::unique_ptr<Panel>> panels;

    // Signal-flow panels, kept as raw pointers into `panels` for layout:
    // the Global strip spans the top; the three band columns share the band
    // below it.
    Panel* globalPanel = nullptr;
    Panel* lowPanel = nullptr;
    Panel* midPanel = nullptr;
    Panel* highPanel = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriptychAudioProcessorEditor)
};
