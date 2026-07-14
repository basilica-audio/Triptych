#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "params/ParameterIds.h"

namespace
{
    constexpr int knobSize = 70;
    constexpr int textBoxHeight = 18;
    constexpr int labelHeight = 18;
    constexpr int bandLabelHeight = 22;
    constexpr int margin = 16;
    constexpr int numColumns = 3;
    constexpr int rowHeight = labelHeight + knobSize + textBoxHeight;

    constexpr int editorWidth = margin * 2 + numColumns * knobSize + (numColumns + 1) * margin;
    constexpr int editorHeight = margin + rowHeight + margin + bandLabelHeight + 5 * rowHeight + margin;
}

TriptychAudioProcessorEditor::TriptychAudioProcessorEditor (TriptychAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor (&processorToEdit),
      audioProcessor (processorToEdit)
{
    configureKnob (lowMidSplitKnob, ParamIDs::lowMidSplit, "Low/Mid");
    configureKnob (midHighSplitKnob, ParamIDs::midHighSplit, "Mid/High");
    configureKnob (outputKnob, ParamIDs::output, "Output");

    configureBandLabel (lowBandLabel, "Low");
    configureBandLabel (midBandLabel, "Mid");
    configureBandLabel (highBandLabel, "High");

    configureKnob (lowKnobs.threshold, ParamIDs::lowThreshold, "Threshold");
    configureKnob (lowKnobs.ratio, ParamIDs::lowRatio, "Ratio");
    configureKnob (lowKnobs.attack, ParamIDs::lowAttack, "Attack");
    configureKnob (lowKnobs.release, ParamIDs::lowRelease, "Release");
    configureKnob (lowKnobs.makeup, ParamIDs::lowMakeup, "Makeup");

    configureKnob (midKnobs.threshold, ParamIDs::midThreshold, "Threshold");
    configureKnob (midKnobs.ratio, ParamIDs::midRatio, "Ratio");
    configureKnob (midKnobs.attack, ParamIDs::midAttack, "Attack");
    configureKnob (midKnobs.release, ParamIDs::midRelease, "Release");
    configureKnob (midKnobs.makeup, ParamIDs::midMakeup, "Makeup");

    configureKnob (highKnobs.threshold, ParamIDs::highThreshold, "Threshold");
    configureKnob (highKnobs.ratio, ParamIDs::highRatio, "Ratio");
    configureKnob (highKnobs.attack, ParamIDs::highAttack, "Attack");
    configureKnob (highKnobs.release, ParamIDs::highRelease, "Release");
    configureKnob (highKnobs.makeup, ParamIDs::highMakeup, "Makeup");

    setResizable (false, false);
    setSize (editorWidth, editorHeight);
}

TriptychAudioProcessorEditor::~TriptychAudioProcessorEditor() = default;

void TriptychAudioProcessorEditor::configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobSize, textBoxHeight);
    addAndMakeVisible (knob.slider);

    knob.label.setText (labelText, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    // false => label sits above the slider it tracks; JUCE repositions it
    // automatically whenever the slider's bounds change, so resized() only
    // needs to place the sliders themselves.
    knob.label.attachToComponent (&knob.slider, false);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, parameterId, knob.slider);
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

    auto lowColumn = bounds.removeFromLeft (bandSlotWidth);
    auto midColumn = bounds.removeFromLeft (bandSlotWidth);
    auto highColumn = bounds;

    for (auto* knob : { &lowKnobs.threshold, &lowKnobs.ratio, &lowKnobs.attack, &lowKnobs.release, &lowKnobs.makeup })
        knob->slider.setBounds (lowColumn.removeFromTop (rowHeight).reduced (margin / 2, 0));

    for (auto* knob : { &midKnobs.threshold, &midKnobs.ratio, &midKnobs.attack, &midKnobs.release, &midKnobs.makeup })
        knob->slider.setBounds (midColumn.removeFromTop (rowHeight).reduced (margin / 2, 0));

    for (auto* knob : { &highKnobs.threshold, &highKnobs.ratio, &highKnobs.attack, &highKnobs.release, &highKnobs.makeup })
        knob->slider.setBounds (highColumn.removeFromTop (rowHeight).reduced (margin / 2, 0));
}
