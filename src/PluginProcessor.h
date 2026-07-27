#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/TriptychEngine.h"
#include "presets/PresetManager.h"

// Triptych: a 3-band multiband compressor for dense metal mixes. Signal flow
// lives in TriptychEngine (src/dsp) so it stays unit-testable independent of
// this AudioProcessor; this class is just APVTS + host plumbing around it.
class TriptychAudioProcessor final : public juce::AudioProcessor,
                                      public juce::AsyncUpdater
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

    // Per-band gain-reduction telemetry for the editor's GR bars (v0.5.0).
    const trpt::GainReductionMeter& getGainReductionMeter() const noexcept { return engine.getGainReductionMeter(); }

    // The lookahead lengths, in seconds, behind the Lookahead choice
    // parameter's four options. Public so tests can derive the expected
    // reported latency without duplicating the table.
    static constexpr float lookaheadSecondsForChoice (int choiceIndex) noexcept
    {
        return choiceIndex == 1 ? 0.0015f
                                : choiceIndex == 2 ? 0.003f
                                                    : choiceIndex == 3 ? 0.005f
                                                                        : 0.0f;
    }

    // State schema version stamped onto the APVTS root by getStateInformation
    // and read back by setStateInformation (absent means v0.4.0 or older).
    static constexpr int stateSchemaVersion = 5;
    static constexpr const char* stateVersionProperty = "stateVersion";

    juce::AudioProcessorValueTreeState apvts;

    // M2 preset system (.scaffold/specs/preset-system-m2.md,
    // src/presets/PresetManager.h). Constructed after apvts (its
    // constructor registers APVTS parameter listeners) and public so
    // TriptychAudioProcessorEditor's PresetBar can talk to it directly - the
    // same "processor owns it, editor references it" pattern apvts itself
    // already uses.
    basilica::presets::PresetManager presetManager;

private:
    TriptychEngine engine;

    // Raw atomic pointers into the APVTS-managed parameter values, resolved
    // once at construction time so processBlock() never has to search for
    // them (no allocation/locks on the audio thread).
    std::atomic<float>* lowMidSplitHz = nullptr;
    std::atomic<float>* midHighSplitHz = nullptr;

    std::atomic<float>* lowThresholdDb = nullptr;
    std::atomic<float>* lowRatio = nullptr;
    std::atomic<float>* lowKneePercent = nullptr;
    std::atomic<float>* lowAttackMs = nullptr;
    std::atomic<float>* lowReleaseMs = nullptr;
    std::atomic<float>* lowMakeupDb = nullptr;
    std::atomic<float>* lowRangeEnabledOn = nullptr;
    std::atomic<float>* lowRangeDb = nullptr;

    // Downward expansion / gating (v0.4.0, issue #25).
    std::atomic<float>* lowGateEnabledOn = nullptr;
    std::atomic<float>* lowGateThresholdDb = nullptr;
    std::atomic<float>* lowGateRatio = nullptr;
    std::atomic<float>* lowGateAttackMs = nullptr;
    std::atomic<float>* lowGateReleaseMs = nullptr;
    // Per-band Mid/Side processing (v0.4.0, issue #24).
    std::atomic<float>* lowMidSideEnabledOn = nullptr;
    std::atomic<float>* lowSideThresholdDb = nullptr;
    std::atomic<float>* lowSideRatio = nullptr;

    std::atomic<float>* midThresholdDb = nullptr;
    std::atomic<float>* midRatio = nullptr;
    std::atomic<float>* midKneePercent = nullptr;
    std::atomic<float>* midAttackMs = nullptr;
    std::atomic<float>* midReleaseMs = nullptr;
    std::atomic<float>* midMakeupDb = nullptr;
    std::atomic<float>* midRangeEnabledOn = nullptr;
    std::atomic<float>* midRangeDb = nullptr;

    std::atomic<float>* midGateEnabledOn = nullptr;
    std::atomic<float>* midGateThresholdDb = nullptr;
    std::atomic<float>* midGateRatio = nullptr;
    std::atomic<float>* midGateAttackMs = nullptr;
    std::atomic<float>* midGateReleaseMs = nullptr;
    std::atomic<float>* midMidSideEnabledOn = nullptr;
    std::atomic<float>* midSideThresholdDb = nullptr;
    std::atomic<float>* midSideRatio = nullptr;

    std::atomic<float>* highThresholdDb = nullptr;
    std::atomic<float>* highRatio = nullptr;
    std::atomic<float>* highKneePercent = nullptr;
    std::atomic<float>* highAttackMs = nullptr;
    std::atomic<float>* highReleaseMs = nullptr;
    std::atomic<float>* highMakeupDb = nullptr;
    std::atomic<float>* highRangeEnabledOn = nullptr;
    std::atomic<float>* highRangeDb = nullptr;

    std::atomic<float>* highGateEnabledOn = nullptr;
    std::atomic<float>* highGateThresholdDb = nullptr;
    std::atomic<float>* highGateRatio = nullptr;
    std::atomic<float>* highGateAttackMs = nullptr;
    std::atomic<float>* highGateReleaseMs = nullptr;
    std::atomic<float>* highMidSideEnabledOn = nullptr;
    std::atomic<float>* highSideThresholdDb = nullptr;
    std::atomic<float>* highSideRatio = nullptr;

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

    //==========================================================================
    // v0.5.0 parameters.
    std::atomic<float>* scSourceChoice = nullptr;
    std::atomic<float>* scListenChoice = nullptr;
    std::atomic<float>* crossoverSlopeChoice = nullptr;
    std::atomic<float>* lookaheadChoice = nullptr;
    std::atomic<float>* mixPercent = nullptr;

    std::atomic<float>* lowDetectorModeChoice = nullptr;
    std::atomic<float>* lowAutoReleaseOn = nullptr;
    std::atomic<float>* lowCharacterChoice = nullptr;
    std::atomic<float>* lowStereoLinkPercent = nullptr;
    std::atomic<float>* lowGateHoldMs = nullptr;
    std::atomic<float>* lowGateHysteresisDb = nullptr;

    std::atomic<float>* midDetectorModeChoice = nullptr;
    std::atomic<float>* midAutoReleaseOn = nullptr;
    std::atomic<float>* midCharacterChoice = nullptr;
    std::atomic<float>* midStereoLinkPercent = nullptr;
    std::atomic<float>* midGateHoldMs = nullptr;
    std::atomic<float>* midGateHysteresisDb = nullptr;

    std::atomic<float>* highDetectorModeChoice = nullptr;
    std::atomic<float>* highAutoReleaseOn = nullptr;
    std::atomic<float>* highCharacterChoice = nullptr;
    std::atomic<float>* highStereoLinkPercent = nullptr;
    std::atomic<float>* highGateHoldMs = nullptr;
    std::atomic<float>* highGateHysteresisDb = nullptr;

    // Lookahead is the one parameter that changes the plugin's reported
    // latency, so it is NOT applied straight from the audio thread. The audio
    // thread publishes what it wants, an AsyncUpdater tells the host on the
    // message thread, and only once the host has been told does the audio
    // thread actually reconfigure the engine - so the engine always processes
    // at exactly the latency the host currently believes in.
    // The rate prepareToPlay() was last called with. Deliberately NOT
    // AudioProcessor::getSampleRate(), which is only populated by the host
    // wrapper's setRateAndBufferSizeDetails() call and therefore reads 0 for
    // a processor driven directly (as every unit test does).
    double preparedSampleRate = 44100.0;

    std::atomic<int> requestedLookaheadSamples { 0 };
    std::atomic<int> reportedLookaheadSamples { 0 };
    int appliedLookaheadSamples = 0;

    void handleAsyncUpdate() override;

    // Resolves the Lookahead choice to a sample count at the current rate.
    int resolveLookaheadSamples() const noexcept;

    // Reads every APVTS atomic and pushes the current values into `engine`.
    // Called both from prepareToPlay() (so the first block after prepare
    // already reflects the host/session's actual parameter values) and from
    // every processBlock() call.
    void pushParametersToEngine();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriptychAudioProcessor)
};
