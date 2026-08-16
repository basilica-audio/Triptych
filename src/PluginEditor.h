#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "presets/PresetBar.h"

#include "dsp/GainReductionMeter.h"

class TriptychAudioProcessor;

// A simple, functional v0.1/v0.2 editor: one rotary slider per parameter,
// bound to the APVTS via SliderAttachment, arranged as a top strip (M2
// preset bar, then crossover splits + master output) above three per-band
// columns (Low/Mid/High), each column holding Mute/Solo toggles above
// Threshold/Ratio/Knee/Attack/Release/Makeup knobs in signal-flow order. The
// High column additionally carries the M1 high-band limiter option (an
// enable toggle + threshold knob). A custom vector-drawn GUI is a later
// milestone; this is deliberately plain but fully wired and usable.
class TriptychAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit TriptychAudioProcessorEditor (TriptychAudioProcessor& processorToEdit);
    ~TriptychAudioProcessorEditor() override;

    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

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

    // JUCE 8.0.14 gotcha: ComboBoxAttachment does NOT populate the box's
    // items - it only syncs the selected index. configureChoice() below fills
    // them from the parameter's own choice list before attaching.
    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboBoxAttachment> attachment;
    };

    // A thin vertical gain-reduction bar, one per band column (v0.5.0). Reads
    // the processor's relaxed GR atomics on a 30 Hz timer; peak-hold decay is
    // handled here rather than on the audio thread.
    struct GainReductionBar final : public juce::Component
    {
        void paint (juce::Graphics& g) override;

        float currentDb = 0.0f;
        float heldDb = 0.0f;
    };

    // One band's Mute/Solo pair plus its six compression knobs (Knee added
    // in v0.2.0) and Range enable/amount (added in v0.3.0), in signal-flow
    // order.
    struct BandControls
    {
        Toggle mute;
        Toggle solo;
        Knob threshold;
        Knob ratio;
        Knob knee;
        Knob attack;
        Knob release;
        Knob makeup;
        Toggle rangeEnabled;
        Knob range;
        Toggle gateEnabled;
        Knob gateThreshold;
        Knob gateRatio;
        Knob gateAttack;
        Knob gateRelease;
        Toggle midSideEnabled;
        Knob sideThreshold;
        Knob sideRatio;

        // v0.5.0 detector + gate-shaping controls.
        Choice detectorMode;
        Toggle autoRelease;
        Choice character;
        Knob stereoLink;
        Knob gateHold;
        Knob gateHysteresis;

        GainReductionBar gainReductionBar;
    };

    void configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText);
    void configureToggle (Toggle& toggle, const juce::String& parameterId, const juce::String& labelText);
    void configureChoice (Choice& choice, const juce::String& parameterId, const juce::String& labelText);
    void configureBandLabel (juce::Label& label, const juce::String& text);
    void configureBandExtras (BandControls& controls,
                               const juce::String& detectorModeId,
                               const juce::String& autoReleaseId,
                               const juce::String& characterId,
                               const juce::String& stereoLinkId,
                               const juce::String& gateHoldId,
                               const juce::String& gateHysteresisId);

    void timerCallback() override;

    TriptychAudioProcessor& audioProcessor;

    // M2 preset system (src/presets/PresetBar.h) - a horizontal strip
    // docked at the top of the editor. Constructed after the localisation
    // frame is installed (see the constructor) so its TRANS()'d strings
    // (and any of its own dialogs opened later) pick up the right language
    // from the very first paint.
    basilica::presets::PresetBar presetBar;

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

    // v0.5.0 global controls, added to the top strip.
    Choice sidechainSourceChoice;
    Choice sidechainListenChoice;
    Choice crossoverSlopeChoice;
    Choice lookaheadChoice;
    Knob mixKnob;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriptychAudioProcessorEditor)
};
