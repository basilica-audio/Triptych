#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "presets/Localisation.h"

#include <BinaryData.h>

namespace
{
    constexpr int knobSize = 70;
    constexpr int textBoxHeight = 18;
    constexpr int labelHeight = 18;
    constexpr int bandLabelHeight = 22;
    constexpr int toggleRowHeight = 26;
    constexpr int margin = 16;
    constexpr int numColumns = 3;
    constexpr int rowHeight = labelHeight + knobSize + textBoxHeight;
    constexpr int presetBarHeight = 28;
    constexpr int numBandKnobRows = 6; // Threshold/Ratio/Knee/Attack/Release/Makeup (Knee added in v0.2.0)

    constexpr int editorWidth = margin * 2 + numColumns * knobSize + (numColumns + 1) * margin;

    // Preset bar + top strip + band labels + Mute/Solo row + band knob rows
    // + the High-band limiter option's own toggle row and threshold-knob row.
    constexpr int editorHeight = margin + presetBarHeight + margin + rowHeight + margin + bandLabelHeight + toggleRowHeight
                                  + numBandKnobRows * rowHeight + margin + toggleRowHeight + rowHeight + margin;

    // M2 i18n frame (.scaffold/specs/preset-system-m2.md): selects German
    // (resources/i18n/de.txt) or falls through to English, once, at editor
    // construction - see Localisation.h's docs. `presetBar` is a member
    // initialised via the constructor's initialiser list, and its own
    // constructor already calls TRANS() on every button label - member
    // initialisers run in declaration order regardless of the order
    // they're written in, so this helper (called from presetBar's own
    // initialiser expression below) is what actually guarantees
    // installLocalisation() runs before presetBar exists, not an
    // installLocalisation() call in the constructor *body*, which would run
    // too late.
    basilica::presets::PresetManager& initLocalisationThenGetPresetManager (TriptychAudioProcessor& processor)
    {
        basilica::presets::installLocalisation (BinaryData::de_txt, BinaryData::de_txtSize);
        return processor.presetManager;
    }
}

TriptychAudioProcessorEditor::TriptychAudioProcessorEditor (TriptychAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor (&processorToEdit),
      audioProcessor (processorToEdit),
      presetBar (initLocalisationThenGetPresetManager (processorToEdit))
{
    addAndMakeVisible (presetBar);

    configureKnob (lowMidSplitKnob, ParamIDs::lowMidSplit, "Low/Mid");
    configureKnob (midHighSplitKnob, ParamIDs::midHighSplit, "Mid/High");
    configureKnob (outputKnob, ParamIDs::output, "Output");

    configureBandLabel (lowBandLabel, "Low");
    configureBandLabel (midBandLabel, "Mid");
    configureBandLabel (highBandLabel, "High");

    configureToggle (lowControls.mute, ParamIDs::lowMute, "Mute");
    configureToggle (lowControls.solo, ParamIDs::lowSolo, "Solo");
    configureKnob (lowControls.threshold, ParamIDs::lowThreshold, "Threshold");
    configureKnob (lowControls.ratio, ParamIDs::lowRatio, "Ratio");
    configureKnob (lowControls.knee, ParamIDs::lowKnee, "Knee");
    configureKnob (lowControls.attack, ParamIDs::lowAttack, "Attack");
    configureKnob (lowControls.release, ParamIDs::lowRelease, "Release");
    configureKnob (lowControls.makeup, ParamIDs::lowMakeup, "Makeup");

    configureToggle (midControls.mute, ParamIDs::midMute, "Mute");
    configureToggle (midControls.solo, ParamIDs::midSolo, "Solo");
    configureKnob (midControls.threshold, ParamIDs::midThreshold, "Threshold");
    configureKnob (midControls.ratio, ParamIDs::midRatio, "Ratio");
    configureKnob (midControls.knee, ParamIDs::midKnee, "Knee");
    configureKnob (midControls.attack, ParamIDs::midAttack, "Attack");
    configureKnob (midControls.release, ParamIDs::midRelease, "Release");
    configureKnob (midControls.makeup, ParamIDs::midMakeup, "Makeup");

    configureToggle (highControls.mute, ParamIDs::highMute, "Mute");
    configureToggle (highControls.solo, ParamIDs::highSolo, "Solo");
    configureKnob (highControls.threshold, ParamIDs::highThreshold, "Threshold");
    configureKnob (highControls.ratio, ParamIDs::highRatio, "Ratio");
    configureKnob (highControls.knee, ParamIDs::highKnee, "Knee");
    configureKnob (highControls.attack, ParamIDs::highAttack, "Attack");
    configureKnob (highControls.release, ParamIDs::highRelease, "Release");
    configureKnob (highControls.makeup, ParamIDs::highMakeup, "Makeup");

    configureToggle (highLimiterEnabledToggle, ParamIDs::highLimiterEnabled, "Limiter");
    configureKnob (highLimiterThresholdKnob, ParamIDs::highLimiterThreshold, "Lim. Thresh");

    setResizable (false, false);
    setSize (editorWidth, editorHeight);
}

TriptychAudioProcessorEditor::~TriptychAudioProcessorEditor() = default;

void TriptychAudioProcessorEditor::configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobSize, textBoxHeight);
    addAndMakeVisible (knob.slider);

    // Deliberately not TRANS()'d - core/DSP parameter names stay English
    // everywhere per the M2 i18n frame's scope (.scaffold/specs/
    // preset-system-m2.md's "NEVER translate core/DSP terminology" rule).
    knob.label.setText (labelText, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    // false => label sits above the slider it tracks; JUCE repositions it
    // automatically whenever the slider's bounds change, so resized() only
    // needs to place the sliders themselves.
    knob.label.attachToComponent (&knob.slider, false);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, parameterId, knob.slider);
}

void TriptychAudioProcessorEditor::configureToggle (Toggle& toggle, const juce::String& parameterId, const juce::String& labelText)
{
    // Deliberately not TRANS()'d - see configureKnob()'s comment above.
    toggle.button.setButtonText (labelText);
    addAndMakeVisible (toggle.button);

    toggle.attachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, parameterId, toggle.button);
}

void TriptychAudioProcessorEditor::configureBandLabel (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    addAndMakeVisible (label);
}

void TriptychAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (margin);

    presetBar.setBounds (bounds.removeFromTop (presetBarHeight));
    bounds.removeFromTop (margin);

    // Top strip: Low/Mid split, Mid/High split, Output - one knob per
    // column slot so the grid below lines up underneath it.
    auto topRow = bounds.removeFromTop (rowHeight);
    const auto slotWidth = topRow.getWidth() / numColumns;

    lowMidSplitKnob.slider.setBounds (topRow.removeFromLeft (slotWidth).reduced (margin / 2, 0));
    midHighSplitKnob.slider.setBounds (topRow.removeFromLeft (slotWidth).reduced (margin / 2, 0));
    outputKnob.slider.setBounds (topRow.removeFromLeft (slotWidth).reduced (margin / 2, 0));

    bounds.removeFromTop (margin);

    auto bandLabelRow = bounds.removeFromTop (bandLabelHeight);
    const auto bandSlotWidth = bandLabelRow.getWidth() / numColumns;

    lowBandLabel.setBounds (bandLabelRow.removeFromLeft (bandSlotWidth));
    midBandLabel.setBounds (bandLabelRow.removeFromLeft (bandSlotWidth));
    highBandLabel.setBounds (bandLabelRow.removeFromLeft (bandSlotWidth));

    // Mute/Solo row: one slot per band, each split into a left (Mute) and
    // right (Solo) half.
    auto muteSoloRow = bounds.removeFromTop (toggleRowHeight);

    const auto placeMuteSolo = [] (juce::Rectangle<int> slot, Toggle& mute, Toggle& solo)
    {
        const auto half = slot.getWidth() / 2;
        mute.button.setBounds (slot.removeFromLeft (half).reduced (margin / 4, 2));
        solo.button.setBounds (slot.reduced (margin / 4, 2));
    };

    placeMuteSolo (muteSoloRow.removeFromLeft (bandSlotWidth), lowControls.mute, lowControls.solo);
    placeMuteSolo (muteSoloRow.removeFromLeft (bandSlotWidth), midControls.mute, midControls.solo);
    placeMuteSolo (muteSoloRow, highControls.mute, highControls.solo);

    auto lowColumn = bounds.removeFromLeft (bandSlotWidth);
    auto midColumn = bounds.removeFromLeft (bandSlotWidth);
    auto highColumn = bounds;

    for (auto* knob : { &lowControls.threshold, &lowControls.ratio, &lowControls.knee, &lowControls.attack, &lowControls.release, &lowControls.makeup })
        knob->slider.setBounds (lowColumn.removeFromTop (rowHeight).reduced (margin / 2, 0));

    for (auto* knob : { &midControls.threshold, &midControls.ratio, &midControls.knee, &midControls.attack, &midControls.release, &midControls.makeup })
        knob->slider.setBounds (midColumn.removeFromTop (rowHeight).reduced (margin / 2, 0));

    for (auto* knob : { &highControls.threshold, &highControls.ratio, &highControls.knee, &highControls.attack, &highControls.release, &highControls.makeup })
        knob->slider.setBounds (highColumn.removeFromTop (rowHeight).reduced (margin / 2, 0));

    // High-band limiter option (M1): High column only, below its six
    // compression knobs.
    highLimiterEnabledToggle.button.setBounds (highColumn.removeFromTop (toggleRowHeight).reduced (margin / 2, 2));
    highLimiterThresholdKnob.slider.setBounds (highColumn.removeFromTop (rowHeight).reduced (margin / 2, 0));
}
