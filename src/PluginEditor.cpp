#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "presets/Localisation.h"

#include <BinaryData.h>

#include <algorithm>

namespace
{
    // ----- M3 vector-editor layout metrics (issue #4) ---------------------
    // All values are design constants, not measurements of pre-rendered
    // art (there is none): the editor computes its own size from these plus
    // the control tables in the constructor, and tests/gui/EditorLayoutTests.cpp
    // asserts the resulting geometry (containment, no overlap) on the real
    // component tree, so a change here can never silently clip a control.
    constexpr int outerMargin = 10;
    constexpr int presetBarHeight = 30;
    constexpr int bandGap = 8;

    constexpr int panelPadding = 10;
    constexpr int panelBottomPadding = 8;
    constexpr int rowGap = 8;

    // A knob slot: attached label above (JUCE 8.0.14 Label::
    // componentMovedOrResized sizes an above-attached label to
    // borderTopAndBottom + 6 + fontHeight ~ 22 px for the 14 px suite
    // serif, so 24 reserved keeps it clear of the row above), then the
    // rotary area, then the value box baked into the slider's own bounds.
    constexpr int labelHeight = 24;
    constexpr int knobSize = 60;
    constexpr int textBoxHeight = 16;
    constexpr int knobSlotWidth = 80;
    constexpr int toggleSlotWidth = 70;
    constexpr int toggleHeight = 32;
    constexpr int slotGap = 6; // trimmed off the right of every slot
    constexpr int rowHeight = labelHeight + knobSize + textBoxHeight;

    // Band-column meter strip (under the panel header, above the rows).
    constexpr int meterStripHeight = 96;
    constexpr int meterWidth = 134;

    // M2 i18n frame: selects German (resources/i18n/de.txt) or falls
    // through to English, once, at editor construction - see
    // Localisation.h's docs. `presetBar` is a member initialised via the
    // constructor's initialiser list, and its own constructor already calls
    // TRANS() on every button label - member initialisers run in
    // declaration order, so this helper (called from presetBar's own
    // initialiser expression below) is what guarantees installLocalisation()
    // runs before presetBar exists, not a call in the constructor *body*,
    // which would run too late.
    basilica::presets::PresetManager& initLocalisationThenGetPresetManager (TriptychAudioProcessor& processor)
    {
        basilica::presets::installLocalisation (BinaryData::de_txt, BinaryData::de_txtSize);
        return processor.presetManager;
    }
}

// The full per-band ID set, so Low/Mid/High share one construction routine
// and can never drift structurally apart.
struct TriptychAudioProcessorEditor::BandParameterIds
{
    const char* threshold;
    const char* ratio;
    const char* knee;
    const char* attack;
    const char* release;
    const char* makeup;
    const char* stereoLink;
    const char* range;
    const char* detectorMode;
    const char* character;
    const char* autoRelease;
    const char* rangeEnabled;
    const char* gateEnabled;
    const char* gateThreshold;
    const char* gateRatio;
    const char* gateAttack;
    const char* gateRelease;
    const char* gateHold;
    const char* gateHysteresis;
    const char* midSideEnabled;
    const char* sideThreshold;
    const char* sideRatio;
    const char* mute;
    const char* solo;
};

TriptychAudioProcessorEditor::TriptychAudioProcessorEditor (TriptychAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor (&processorToEdit),
      audioProcessor (processorToEdit),
      presetBar (initLocalisationThenGetPresetManager (processorToEdit))
{
    // Propagates to every child, including the preset bar's stock buttons
    // and any menus/dialogs they open.
    setLookAndFeel (&lookAndFeel);

    // FOCUS ORDER (WCAG 2.4.3): children are created and added in signal-
    // flow/reading order - preset bar, then the Global strip, then the Low,
    // Mid and High band columns, left-to-right within each row. JUCE's
    // default traverser follows this creation order; do not reorder.
    addAndMakeVisible (presetBar);

    // --- Global: crossovers, engine options, output ------------------------
    auto& global = addPanel ("Global");
    globalPanel = &global;
    addKnob (global, ParamIDs::lowMidSplit, "Low/Mid");
    addKnob (global, ParamIDs::midHighSplit, "Mid/High");
    addKnob (global, ParamIDs::crossoverSlope, "Slope");
    addKnob (global, ParamIDs::lookahead, "Lookahead");
    addKnob (global, ParamIDs::scSource, "Sidechain");
    addKnob (global, ParamIDs::scListen, "Listen");
    addKnob (global, ParamIDs::mix, "Mix");
    addKnob (global, ParamIDs::output, "Output");

    // --- The three band columns, in spectrum order -------------------------
    const BandParameterIds lowIds {
        ParamIDs::lowThreshold, ParamIDs::lowRatio, ParamIDs::lowKnee,
        ParamIDs::lowAttack, ParamIDs::lowRelease, ParamIDs::lowMakeup,
        ParamIDs::lowStereoLink, ParamIDs::lowRange,
        ParamIDs::lowDetectorMode, ParamIDs::lowCharacter, ParamIDs::lowAutoRelease,
        ParamIDs::lowRangeEnabled,
        ParamIDs::lowGateEnabled, ParamIDs::lowGateThreshold, ParamIDs::lowGateRatio,
        ParamIDs::lowGateAttack, ParamIDs::lowGateRelease, ParamIDs::lowGateHold,
        ParamIDs::lowGateHysteresis,
        ParamIDs::lowMidSideEnabled, ParamIDs::lowSideThreshold, ParamIDs::lowSideRatio,
        ParamIDs::lowMute, ParamIDs::lowSolo
    };

    const BandParameterIds midIds {
        ParamIDs::midThreshold, ParamIDs::midRatio, ParamIDs::midKnee,
        ParamIDs::midAttack, ParamIDs::midRelease, ParamIDs::midMakeup,
        ParamIDs::midStereoLink, ParamIDs::midRange,
        ParamIDs::midDetectorMode, ParamIDs::midCharacter, ParamIDs::midAutoRelease,
        ParamIDs::midRangeEnabled,
        ParamIDs::midGateEnabled, ParamIDs::midGateThreshold, ParamIDs::midGateRatio,
        ParamIDs::midGateAttack, ParamIDs::midGateRelease, ParamIDs::midGateHold,
        ParamIDs::midGateHysteresis,
        ParamIDs::midMidSideEnabled, ParamIDs::midSideThreshold, ParamIDs::midSideRatio,
        ParamIDs::midMute, ParamIDs::midSolo
    };

    const BandParameterIds highIds {
        ParamIDs::highThreshold, ParamIDs::highRatio, ParamIDs::highKnee,
        ParamIDs::highAttack, ParamIDs::highRelease, ParamIDs::highMakeup,
        ParamIDs::highStereoLink, ParamIDs::highRange,
        ParamIDs::highDetectorMode, ParamIDs::highCharacter, ParamIDs::highAutoRelease,
        ParamIDs::highRangeEnabled,
        ParamIDs::highGateEnabled, ParamIDs::highGateThreshold, ParamIDs::highGateRatio,
        ParamIDs::highGateAttack, ParamIDs::highGateRelease, ParamIDs::highGateHold,
        ParamIDs::highGateHysteresis,
        ParamIDs::highMidSideEnabled, ParamIDs::highSideThreshold, ParamIDs::highSideRatio,
        ParamIDs::highMute, ParamIDs::highSolo
    };

    lowPanel = &addBandPanel ("Low Band", lowIds, "Low band gain reduction meter", "LOW");
    midPanel = &addBandPanel ("Mid Band", midIds, "Mid band gain reduction meter", "MID");
    highPanel = &addBandPanel ("High Band", highIds, "High band gain reduction meter", "HIGH");

    // The high band's limiter option appends one extra row to its column.
    addRow (*highPanel);
    addToggle (*highPanel, ParamIDs::highLimiterEnabled, "Limiter");
    addKnob (*highPanel, ParamIDs::highLimiterThreshold, "Lim Thr");

    // --- Size: computed from the control tables above ---------------------
    const auto bandsWidth = panelRequiredWidth (*lowPanel) + bandGap
                          + panelRequiredWidth (*midPanel) + bandGap
                          + panelRequiredWidth (*highPanel);
    const auto contentWidth = std::max (panelRequiredWidth (global), bandsWidth);

    const auto contentHeight = presetBarHeight + bandGap
                             + panelRequiredHeight (global) + bandGap
                             + std::max ({ panelRequiredHeight (*lowPanel),
                                           panelRequiredHeight (*midPanel),
                                           panelRequiredHeight (*highPanel) });

    setResizable (false, false);
    setSize (outerMargin * 2 + contentWidth, outerMargin * 2 + contentHeight);

    // GR meter polling: ~30 Hz GUI-thread timer feeding the ballistic
    // needles; the engine's GainReductionMeter is relaxed atomics, so this
    // never touches the audio thread.
    startTimerHz (30);
}

TriptychAudioProcessorEditor::~TriptychAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

TriptychAudioProcessorEditor::Panel& TriptychAudioProcessorEditor::addPanel (const juce::String& sectionTitle)
{
    auto panel = std::make_unique<Panel>();
    panel->component = std::make_unique<basilica::gui::BusPanel> (sectionTitle);
    panel->rows.emplace_back();

    addAndMakeVisible (*panel->component);

    panels.push_back (std::move (panel));
    return *panels.back();
}

void TriptychAudioProcessorEditor::addRow (Panel& panel)
{
    panel.rows.emplace_back();
}

TriptychAudioProcessorEditor::Knob& TriptychAudioProcessorEditor::addKnob (Panel& panel, const char* parameterId,
                                                                           const juce::String& labelText)
{
    auto knob = std::make_unique<Knob>();

    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobSlotWidth - slotGap, textBoxHeight);
    knob->slider.setTitle (labelText);
    knob->slider.setName (labelText);
    panel.component->addAndMakeVisible (knob->slider);

    knob->label.setText (labelText, juce::dontSendNotification);
    knob->label.setJustificationType (juce::Justification::centred);
    knob->label.attachToComponent (&knob->slider, false); // above; auto-repositions with the slider
    panel.component->addAndMakeVisible (knob->label);

    // SliderAttachment MUST be constructed before the textFromValueFunction
    // override below, not after: JUCE 8.0.14's SliderParameterAttachment
    // constructor (juce_ParameterAttachments.cpp:128) itself assigns
    // `slider.textFromValueFunction` as part of wiring the attachment -
    // setting our own function BEFORE this point would be silently
    // clobbered the moment the attachment is created.
    knob->attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, parameterId, knob->slider);

    if (auto* param = audioProcessor.apvts.getParameter (parameterId))
    {
        // A-02 pattern: unit-carrying parameters declare their unit via
        // .withLabel() in ParameterLayout.cpp (dB/:1/%/ms/Hz) - feed it
        // into both the value box and the accessibility value string.
        // Choice parameters have an empty label and getText() already
        // returns the choice NAME, so this is a no-op suffix for them.
        knob->slider.textFromValueFunction = [param] (double v)
        {
            const auto text = param->getText (param->convertTo0to1 ((float) v), 0);
            const auto unit = param->getLabel();
            return unit.isEmpty() ? text : text + " " + unit;
        };
        knob->slider.updateText();
    }

    panel.rows.back().push_back (&knob->slider);
    knobs.push_back (std::move (knob));
    return *knobs.back();
}

TriptychAudioProcessorEditor::Toggle& TriptychAudioProcessorEditor::addToggle (Panel& panel, const char* parameterId,
                                                                               const juce::String& labelText)
{
    auto toggle = std::make_unique<Toggle>();

    // Real juce::ToggleButton on purpose: focusable and Space/Enter-
    // operable by default, and its createAccessibilityHandler() reports
    // AccessibilityRole::toggleButton (JUCE 8.0.14 juce_ToggleButton.cpp:71)
    // so it lands in the VoiceOver rotor as a toggle, not a plain button.
    toggle->button.setButtonText (labelText);
    toggle->button.setTitle (labelText);
    toggle->button.setName (labelText);
    panel.component->addAndMakeVisible (toggle->button);

    toggle->attachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, parameterId, toggle->button);

    panel.rows.back().push_back (&toggle->button);
    toggles.push_back (std::move (toggle));
    return *toggles.back();
}

basilica::gui::NeedleMeter& TriptychAudioProcessorEditor::addMeter (Panel& panel, const juce::String& accessibleTitle,
                                                                    const juce::String& faceLegend)
{
    auto meter = std::make_unique<basilica::gui::NeedleMeter> (accessibleTitle, faceLegend);
    panel.component->addAndMakeVisible (*meter);
    panel.meter = meter.get();

    meters.push_back (std::move (meter));
    return *meters.back();
}

TriptychAudioProcessorEditor::Panel& TriptychAudioProcessorEditor::addBandPanel (const juce::String& title,
                                                                                 const BandParameterIds& ids,
                                                                                 const juce::String& meterTitle,
                                                                                 const juce::String& meterLegend)
{
    auto& panel = addPanel (title);

    addMeter (panel, meterTitle, meterLegend);

    // Row 1+2: the compressor proper.
    addKnob (panel, ids.threshold, "Threshold");
    addKnob (panel, ids.ratio, "Ratio");
    addKnob (panel, ids.knee, "Knee");
    addKnob (panel, ids.attack, "Attack");

    addRow (panel);
    addKnob (panel, ids.release, "Release");
    addKnob (panel, ids.makeup, "Makeup");
    addKnob (panel, ids.stereoLink, "Link");
    addKnob (panel, ids.range, "Range");

    // Row 3: detector behaviour + the Range clamp's enable.
    addRow (panel);
    addKnob (panel, ids.detectorMode, "Detector");
    addKnob (panel, ids.character, "Character");
    addToggle (panel, ids.autoRelease, "Auto Rel");
    addToggle (panel, ids.rangeEnabled, "Range On");

    // Row 4+5: the gate/expander stage.
    addRow (panel);
    addToggle (panel, ids.gateEnabled, "Gate");
    addKnob (panel, ids.gateThreshold, "Gate Thr");
    addKnob (panel, ids.gateRatio, "Gate Ratio");
    addKnob (panel, ids.gateAttack, "Gate Att");

    addRow (panel);
    addKnob (panel, ids.gateRelease, "Gate Rel");
    addKnob (panel, ids.gateHold, "Hold");
    addKnob (panel, ids.gateHysteresis, "Hyst");
    addToggle (panel, ids.midSideEnabled, "M/S");

    // Row 6: the M/S Side leg + the band's mix-bus switches.
    addRow (panel);
    addKnob (panel, ids.sideThreshold, "Side Thr");
    addKnob (panel, ids.sideRatio, "Side Ratio");
    addToggle (panel, ids.mute, "Mute");
    addToggle (panel, ids.solo, "Solo");

    return panel;
}

void TriptychAudioProcessorEditor::timerCallback()
{
    const auto& meter = audioProcessor.getGainReductionMeter();

    // The engine publishes per-band compressor and gate gains as dB <= 0
    // (see src/dsp/GainReductionMeter.h); both are attenuations of the same
    // band, so the needle shows their sum, converted to the meter's
    // positive-dB-of-reduction convention.
    const auto totalReductionDb = [] (const trpt::BandGainReduction& source)
    {
        return juce::jmax (0.0f, -(source.loadCompressorDb() + source.loadGateDb()));
    };

    if (lowPanel != nullptr && lowPanel->meter != nullptr)
        lowPanel->meter->setTargetDb (totalReductionDb (meter.low));

    if (midPanel != nullptr && midPanel->meter != nullptr)
        midPanel->meter->setTargetDb (totalReductionDb (meter.mid));

    if (highPanel != nullptr && highPanel->meter != nullptr)
        highPanel->meter->setTargetDb (totalReductionDb (meter.high));

    constexpr float dtSeconds = 1.0f / 30.0f;

    for (auto& needle : meters)
        needle->tick (dtSeconds);
}

int TriptychAudioProcessorEditor::slotWidthFor (const juce::Component& control) noexcept
{
    return dynamic_cast<const juce::Slider*> (&control) != nullptr ? knobSlotWidth : toggleSlotWidth;
}

int TriptychAudioProcessorEditor::rowWidth (const std::vector<juce::Component*>& row) noexcept
{
    int width = 0;

    for (const auto* control : row)
        width += slotWidthFor (*control);

    return width;
}

int TriptychAudioProcessorEditor::panelRequiredWidth (const Panel& panel) const noexcept
{
    int widest = panel.meter != nullptr ? meterWidth : 0;

    for (const auto& row : panel.rows)
        widest = std::max (widest, rowWidth (row));

    return panelPadding * 2 + widest;
}

int TriptychAudioProcessorEditor::panelRequiredHeight (const Panel& panel) const noexcept
{
    const auto numRows = (int) panel.rows.size();
    return basilica::gui::BusPanel::headerHeight
         + (panel.meter != nullptr ? meterStripHeight + rowGap : 0)
         + numRows * rowHeight + (numRows - 1) * rowGap
         + panelBottomPadding;
}

void TriptychAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (basilica::gui::BasilicaLookAndFeel::getEditorBackgroundColour());
}

void TriptychAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (outerMargin);

    presetBar.setBounds (bounds.removeFromTop (presetBarHeight));
    bounds.removeFromTop (bandGap);

    const auto layoutPanel = [] (Panel& panel, juce::Rectangle<int> area)
    {
        panel.component->setBounds (area);

        auto content = panel.component->getLocalBounds().reduced (panelPadding, 0);
        content.removeFromTop (basilica::gui::BusPanel::headerHeight);

        if (panel.meter != nullptr)
        {
            // Full-width meter strip under the header (see Panel's docs).
            auto strip = content.removeFromTop (meterStripHeight);
            panel.meter->setBounds (juce::Rectangle<int> (meterWidth, meterStripHeight)
                                        .withCentre (strip.getCentre()));
            content.removeFromTop (rowGap);
        }

        for (auto& row : panel.rows)
        {
            auto rowArea = content.removeFromTop (rowHeight);
            rowArea.removeFromTop (labelHeight); // attached labels position themselves here

            for (auto* control : row)
            {
                auto slot = rowArea.removeFromLeft (slotWidthFor (*control)).withTrimmedRight (slotGap);

                if (dynamic_cast<juce::Slider*> (control) != nullptr)
                    control->setBounds (slot.withHeight (knobSize + textBoxHeight));
                else
                    control->setBounds (slot.withSizeKeepingCentre (slot.getWidth(), toggleHeight)
                                            .withY (rowArea.getY() + (knobSize - toggleHeight) / 2));
            }

            content.removeFromTop (rowGap);
        }
    };

    layoutPanel (*globalPanel, bounds.removeFromTop (panelRequiredHeight (*globalPanel)));
    bounds.removeFromTop (bandGap);

    // The three band columns share the remaining band, each at its own
    // required width/height, top-aligned (the High column is one row taller
    // than its siblings - the limiter row).
    auto columnBand = bounds.removeFromTop (std::max ({ panelRequiredHeight (*lowPanel),
                                                        panelRequiredHeight (*midPanel),
                                                        panelRequiredHeight (*highPanel) }));

    for (auto* panel : { lowPanel, midPanel, highPanel })
    {
        auto column = columnBand.removeFromLeft (panelRequiredWidth (*panel))
                          .withHeight (panelRequiredHeight (*panel));
        layoutPanel (*panel, column);
        columnBand.removeFromLeft (bandGap);
    }
}
