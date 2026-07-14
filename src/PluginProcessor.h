#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/TriptychEngine.h"

// Triptych: a 3-band multiband compressor for dense metal mixes. Signal flow
// lives in TriptychEngine (src/dsp) so it stays unit-testable independent of
// this AudioProcessor; this class is just APVTS + host plumbing around it.
class TriptychAudioProcessor final : public juce::AudioProcessor
{
public:
    TriptychAudioProcessor();
    ~TriptychAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

private:
    TriptychEngine engine;

    // Raw atomic pointers into the APVTS-managed parameter values, resolved
    // once at construction time so processBlock() never has to search for
    // them (no allocation/locks on the audio thread).
    std::atomic<float>* lowMidSplitHz = nullptr;
    std::atomic<float>* midHighSplitHz = nullptr;

    std::atomic<float>* lowThresholdDb = nullptr;
    std::atomic<float>* lowRatio = nullptr;
    std::atomic<float>* lowAttackMs = nullptr;
    std::atomic<float>* lowReleaseMs = nullptr;
    std::atomic<float>* lowMakeupDb = nullptr;

    std::atomic<float>* midThresholdDb = nullptr;
    std::atomic<float>* midRatio = nullptr;
    std::atomic<float>* midAttackMs = nullptr;
    std::atomic<float>* midReleaseMs = nullptr;
    std::atomic<float>* midMakeupDb = nullptr;

    std::atomic<float>* highThresholdDb = nullptr;
    std::atomic<float>* highRatio = nullptr;
    std::atomic<float>* highAttackMs = nullptr;
    std::atomic<float>* highReleaseMs = nullptr;
    std::atomic<float>* highMakeupDb = nullptr;

    // Per-band Mute/Solo (M1). AudioParameterBool's raw APVTS value is
    // 0.0f/1.0f, thresholded at 0.5f in pushParametersToEngine() - the same
    // atomic-read pattern used for every other parameter here.
    std::atomic<float>* lowMuteOn = nullptr;
    std::atomic<float>* lowSoloOn = nullptr;
    std::atomic<float>* midMuteOn = nullptr;
    std::atomic<float>* midSoloOn = nullptr;
    std::atomic<float>* highMuteOn = nullptr;
    std::atomic<float>* highSoloOn = nullptr;

    // High-band limiter option (M1).
    std::atomic<float>* highLimiterEnabledOn = nullptr;
    std::atomic<float>* highLimiterThresholdDb = nullptr;

    std::atomic<float>* outputDb = nullptr;

    // Reads every APVTS atomic and pushes the current values into `engine`.
    // Called both from prepareToPlay() (so the first block after prepare
    // already reflects the host/session's actual parameter values) and from
    // every processBlock() call.
    void pushParametersToEngine();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriptychAudioProcessor)
};
