#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class TriptychAudioProcessor;

// A simple, functional v0.1 editor: one rotary slider per parameter, bound
// to the APVTS via SliderAttachment, arranged as a top strip (crossover
// splits + master output) above three per-band columns (Low/Mid/High), each
// column holding Threshold/Ratio/Attack/Release/Makeup in signal-flow order.
// A custom vector-drawn GUI is a later milestone; this is deliberately plain
// but fully wired and usable.
class TriptychAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit TriptychAudioProcessorEditor (TriptychAudioProcessor& processorToEdit);
    ~TriptychAudioProcessorEditor() override;

    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    // One band's five knobs, in signal-flow order.
    struct BandKnobs
    {
        Knob threshold;
        Knob ratio;
        Knob attack;
        Knob release;
        Knob makeup;
    };

    void configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText);
    void configureBandLabel (juce::Label& label, const juce::String& text);

    TriptychAudioProcessor& audioProcessor;

    // Top strip: crossover splits + master output.
    Knob lowMidSplitKnob;
    Knob midHighSplitKnob;
    Knob outputKnob;

    juce::Label lowBandLabel;
    juce::Label midBandLabel;
    juce::Label highBandLabel;

    BandKnobs lowKnobs;
    BandKnobs midKnobs;
    BandKnobs highKnobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriptychAudioProcessorEditor)
};
