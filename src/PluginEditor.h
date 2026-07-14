#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class TriptychAudioProcessor;

// A simple, functional v0.1 editor: one rotary slider per parameter, bound
// to the APVTS via SliderAttachment, arranged as a top strip (crossover
// splits + master output) above three per-band columns (Low/Mid/High), each
// column holding Mute/Solo toggles above Threshold/Ratio/Attack/Release/
// Makeup knobs in signal-flow order. The High column additionally carries
// the M1 high-band limiter option (an enable toggle + threshold knob). A
// custom vector-drawn GUI is a later milestone; this is deliberately plain
// but fully wired and usable.
class TriptychAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit TriptychAudioProcessorEditor (TriptychAudioProcessor& processorToEdit);
    ~TriptychAudioProcessorEditor() override;

    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<ButtonAttachment> attachment;
    };

    // One band's Mute/Solo pair plus its five compression knobs, in
    // signal-flow order.
    struct BandControls
    {
        Toggle mute;
        Toggle solo;
        Knob threshold;
        Knob ratio;
        Knob attack;
        Knob release;
        Knob makeup;
    };

    void configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText);
    void configureToggle (Toggle& toggle, const juce::String& parameterId, const juce::String& labelText);
    void configureBandLabel (juce::Label& label, const juce::String& text);

    TriptychAudioProcessor& audioProcessor;

    // Top strip: crossover splits + master output.
    Knob lowMidSplitKnob;
    Knob midHighSplitKnob;
    Knob outputKnob;

    juce::Label lowBandLabel;
    juce::Label midBandLabel;
    juce::Label highBandLabel;

    BandControls lowControls;
    BandControls midControls;
    BandControls highControls;

    // High-band limiter option (M1) - High column only.
    Toggle highLimiterEnabledToggle;
    Knob highLimiterThresholdKnob;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriptychAudioProcessorEditor)
};
