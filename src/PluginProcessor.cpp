#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIds.h"
#include "params/ParameterLayout.h"

#include <BinaryData.h>

namespace
{
    // The small, Triptych-specific config surface PresetManager needs (see
    // src/presets/PresetManager.h's class docs) - everything else about the
    // preset system is fully generic and portable to sibling plugins (see
    // docs/preset-system-notes.md in basilica-audio/nave, the pilot
    // implementation this was copied from).
    basilica::presets::PresetManagerConfig makePresetManagerConfig()
    {
        // JucePlugin_CFBundleIdentifier expands to a raw (unquoted) token
        // sequence, not a string literal - JUCE_STRINGIFY() is the
        // documented way to turn it into one. This is always
        // "com.yvesvogl.triptych" here (BUNDLE_ID in CMakeLists.txt),
        // matching the "plugin" field baked into every presets/factory/*.json
        // file.
        basilica::presets::PresetManagerConfig config;
        config.pluginId = JUCE_STRINGIFY (JucePlugin_CFBundleIdentifier);
        config.pluginName = JucePlugin_Name;
        config.manufacturerName = "Yves Vogl";
        config.pluginVersion = JucePlugin_VersionString;
        // userPresetsDirectoryOverrideForTests intentionally left
        // default-constructed (empty) - production instances always use the
        // real platform-standard preset location (see PresetManager.h).
        return config;
    }

    // BinaryData symbol names are derived from the presets/factory/*.json
    // file names passed to juce_add_binary_data() in CMakeLists.txt (dots
    // become underscores) - this list must stay in sync with that SOURCES
    // list. Order here only affects factory-preset iteration order before
    // getAllPresets() re-sorts alphabetically, so it isn't otherwise
    // significant.
    std::vector<basilica::presets::FactoryPresetAsset> makeFactoryPresetAssets()
    {
        return {
            { BinaryData::default_json, BinaryData::default_jsonSize },
            { BinaryData::densityGlue_json, BinaryData::densityGlue_jsonSize },
            { BinaryData::peakControl_json, BinaryData::peakControl_jsonSize },
            { BinaryData::lowEndTighten_json, BinaryData::lowEndTighten_jsonSize },
            { BinaryData::deHarshHighs_json, BinaryData::deHarshHighs_jsonSize },
            { BinaryData::masteringSafetyCeiling_json, BinaryData::masteringSafetyCeiling_jsonSize },
            { BinaryData::parallelStyleDensity_json, BinaryData::parallelStyleDensity_jsonSize },
            { BinaryData::hardLimiterCeiling_json, BinaryData::hardLimiterCeiling_jsonSize },
        };
    }
}

//==============================================================================
TriptychAudioProcessor::TriptychAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout()),
      presetManager (apvts, makePresetManagerConfig(), makeFactoryPresetAssets())
{
    lowMidSplitHz = apvts.getRawParameterValue (ParamIDs::lowMidSplit);
    midHighSplitHz = apvts.getRawParameterValue (ParamIDs::midHighSplit);

    lowThresholdDb = apvts.getRawParameterValue (ParamIDs::lowThreshold);
    lowRatio = apvts.getRawParameterValue (ParamIDs::lowRatio);
    lowKneePercent = apvts.getRawParameterValue (ParamIDs::lowKnee);
    lowAttackMs = apvts.getRawParameterValue (ParamIDs::lowAttack);
    lowReleaseMs = apvts.getRawParameterValue (ParamIDs::lowRelease);
    lowMakeupDb = apvts.getRawParameterValue (ParamIDs::lowMakeup);
    lowRangeEnabledOn = apvts.getRawParameterValue (ParamIDs::lowRangeEnabled);
    lowRangeDb = apvts.getRawParameterValue (ParamIDs::lowRange);

    lowMidSideEnabledOn = apvts.getRawParameterValue (ParamIDs::lowMidSideEnabled);
    lowSideThresholdDb = apvts.getRawParameterValue (ParamIDs::lowSideThreshold);
    lowSideRatio = apvts.getRawParameterValue (ParamIDs::lowSideRatio);

    midThresholdDb = apvts.getRawParameterValue (ParamIDs::midThreshold);
    midRatio = apvts.getRawParameterValue (ParamIDs::midRatio);
    midKneePercent = apvts.getRawParameterValue (ParamIDs::midKnee);
    midAttackMs = apvts.getRawParameterValue (ParamIDs::midAttack);
    midReleaseMs = apvts.getRawParameterValue (ParamIDs::midRelease);
    midMakeupDb = apvts.getRawParameterValue (ParamIDs::midMakeup);
    midRangeEnabledOn = apvts.getRawParameterValue (ParamIDs::midRangeEnabled);
    midRangeDb = apvts.getRawParameterValue (ParamIDs::midRange);

    midMidSideEnabledOn = apvts.getRawParameterValue (ParamIDs::midMidSideEnabled);
    midSideThresholdDb = apvts.getRawParameterValue (ParamIDs::midSideThreshold);
    midSideRatio = apvts.getRawParameterValue (ParamIDs::midSideRatio);

    highThresholdDb = apvts.getRawParameterValue (ParamIDs::highThreshold);
    highRatio = apvts.getRawParameterValue (ParamIDs::highRatio);
    highKneePercent = apvts.getRawParameterValue (ParamIDs::highKnee);
    highAttackMs = apvts.getRawParameterValue (ParamIDs::highAttack);
    highReleaseMs = apvts.getRawParameterValue (ParamIDs::highRelease);
    highMakeupDb = apvts.getRawParameterValue (ParamIDs::highMakeup);
    highRangeEnabledOn = apvts.getRawParameterValue (ParamIDs::highRangeEnabled);
    highRangeDb = apvts.getRawParameterValue (ParamIDs::highRange);

    highMidSideEnabledOn = apvts.getRawParameterValue (ParamIDs::highMidSideEnabled);
    highSideThresholdDb = apvts.getRawParameterValue (ParamIDs::highSideThreshold);
    highSideRatio = apvts.getRawParameterValue (ParamIDs::highSideRatio);

    lowMuteOn = apvts.getRawParameterValue (ParamIDs::lowMute);
    lowSoloOn = apvts.getRawParameterValue (ParamIDs::lowSolo);
    midMuteOn = apvts.getRawParameterValue (ParamIDs::midMute);
    midSoloOn = apvts.getRawParameterValue (ParamIDs::midSolo);
    highMuteOn = apvts.getRawParameterValue (ParamIDs::highMute);
    highSoloOn = apvts.getRawParameterValue (ParamIDs::highSolo);

    highLimiterEnabledOn = apvts.getRawParameterValue (ParamIDs::highLimiterEnabled);
    highLimiterThresholdDb = apvts.getRawParameterValue (ParamIDs::highLimiterThreshold);

    outputDb = apvts.getRawParameterValue (ParamIDs::output);

    jassert (lowMidSplitHz != nullptr);
    jassert (midHighSplitHz != nullptr);
    jassert (lowThresholdDb != nullptr);
    jassert (lowRatio != nullptr);
    jassert (lowKneePercent != nullptr);
    jassert (lowAttackMs != nullptr);
    jassert (lowReleaseMs != nullptr);
    jassert (lowMakeupDb != nullptr);
    jassert (lowRangeEnabledOn != nullptr);
    jassert (lowRangeDb != nullptr);
    jassert (lowMidSideEnabledOn != nullptr);
    jassert (lowSideThresholdDb != nullptr);
    jassert (lowSideRatio != nullptr);
    jassert (midThresholdDb != nullptr);
    jassert (midRatio != nullptr);
    jassert (midKneePercent != nullptr);
    jassert (midAttackMs != nullptr);
    jassert (midReleaseMs != nullptr);
    jassert (midMakeupDb != nullptr);
    jassert (midRangeEnabledOn != nullptr);
    jassert (midRangeDb != nullptr);
    jassert (midMidSideEnabledOn != nullptr);
    jassert (midSideThresholdDb != nullptr);
    jassert (midSideRatio != nullptr);
    jassert (highThresholdDb != nullptr);
    jassert (highRatio != nullptr);
    jassert (highKneePercent != nullptr);
    jassert (highAttackMs != nullptr);
    jassert (highReleaseMs != nullptr);
    jassert (highMakeupDb != nullptr);
    jassert (highRangeEnabledOn != nullptr);
    jassert (highRangeDb != nullptr);
    jassert (highMidSideEnabledOn != nullptr);
    jassert (highSideThresholdDb != nullptr);
    jassert (highSideRatio != nullptr);
    jassert (lowMuteOn != nullptr);
    jassert (lowSoloOn != nullptr);
    jassert (midMuteOn != nullptr);
    jassert (midSoloOn != nullptr);
    jassert (highMuteOn != nullptr);
    jassert (highSoloOn != nullptr);
    jassert (highLimiterEnabledOn != nullptr);
    jassert (highLimiterThresholdDb != nullptr);
    jassert (outputDb != nullptr);

    // M2 default resolution: user "Default" preset > factory "Default"
    // preset > the ParameterLayout defaults apvts was just constructed
    // with above (see PresetManager::applyStartupDefault()'s docs).
    presetManager.applyStartupDefault();
}

TriptychAudioProcessor::~TriptychAudioProcessor() = default;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout TriptychAudioProcessor::createParameterLayout()
{
    return trpt::createParameterLayout();
}

//==============================================================================
const juce::String TriptychAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool TriptychAudioProcessor::acceptsMidi() const
{
    return false;
}

bool TriptychAudioProcessor::producesMidi() const
{
    return false;
}

bool TriptychAudioProcessor::isMidiEffect() const
{
    return false;
}

double TriptychAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int TriptychAudioProcessor::getNumPrograms()
{
    return 1;
}

int TriptychAudioProcessor::getCurrentProgram()
{
    return 0;
}

void TriptychAudioProcessor::setCurrentProgram (int)
{
}

const juce::String TriptychAudioProcessor::getProgramName (int)
{
    return {};
}

void TriptychAudioProcessor::changeProgramName (int, const juce::String&)
{
}

//==============================================================================
void TriptychAudioProcessor::pushParametersToEngine()
{
    engine.setLowMidSplitHz (lowMidSplitHz->load (std::memory_order_relaxed));
    engine.setMidHighSplitHz (midHighSplitHz->load (std::memory_order_relaxed));

    engine.setLowThresholdDb (lowThresholdDb->load (std::memory_order_relaxed));
    engine.setLowRatio (lowRatio->load (std::memory_order_relaxed));
    engine.setLowKneePercent (lowKneePercent->load (std::memory_order_relaxed));
    engine.setLowAttackMs (lowAttackMs->load (std::memory_order_relaxed));
    engine.setLowReleaseMs (lowReleaseMs->load (std::memory_order_relaxed));
    engine.setLowMakeupDb (lowMakeupDb->load (std::memory_order_relaxed));
    engine.setLowRangeEnabled (lowRangeEnabledOn->load (std::memory_order_relaxed) > 0.5f);
    engine.setLowRangeDb (lowRangeDb->load (std::memory_order_relaxed));

    engine.setLowMidSideEnabled (lowMidSideEnabledOn->load (std::memory_order_relaxed) > 0.5f);
    engine.setLowSideThresholdDb (lowSideThresholdDb->load (std::memory_order_relaxed));
    engine.setLowSideRatio (lowSideRatio->load (std::memory_order_relaxed));

    engine.setMidThresholdDb (midThresholdDb->load (std::memory_order_relaxed));
    engine.setMidRatio (midRatio->load (std::memory_order_relaxed));
    engine.setMidKneePercent (midKneePercent->load (std::memory_order_relaxed));
    engine.setMidAttackMs (midAttackMs->load (std::memory_order_relaxed));
    engine.setMidReleaseMs (midReleaseMs->load (std::memory_order_relaxed));
    engine.setMidMakeupDb (midMakeupDb->load (std::memory_order_relaxed));
    engine.setMidRangeEnabled (midRangeEnabledOn->load (std::memory_order_relaxed) > 0.5f);
    engine.setMidRangeDb (midRangeDb->load (std::memory_order_relaxed));

    engine.setMidMidSideEnabled (midMidSideEnabledOn->load (std::memory_order_relaxed) > 0.5f);
    engine.setMidSideThresholdDb (midSideThresholdDb->load (std::memory_order_relaxed));
    engine.setMidSideRatio (midSideRatio->load (std::memory_order_relaxed));

    engine.setHighThresholdDb (highThresholdDb->load (std::memory_order_relaxed));
    engine.setHighRatio (highRatio->load (std::memory_order_relaxed));
    engine.setHighKneePercent (highKneePercent->load (std::memory_order_relaxed));
    engine.setHighAttackMs (highAttackMs->load (std::memory_order_relaxed));
    engine.setHighReleaseMs (highReleaseMs->load (std::memory_order_relaxed));
    engine.setHighMakeupDb (highMakeupDb->load (std::memory_order_relaxed));
    engine.setHighRangeEnabled (highRangeEnabledOn->load (std::memory_order_relaxed) > 0.5f);
    engine.setHighRangeDb (highRangeDb->load (std::memory_order_relaxed));

    engine.setHighMidSideEnabled (highMidSideEnabledOn->load (std::memory_order_relaxed) > 0.5f);
    engine.setHighSideThresholdDb (highSideThresholdDb->load (std::memory_order_relaxed));
    engine.setHighSideRatio (highSideRatio->load (std::memory_order_relaxed));

    engine.setLowMute (lowMuteOn->load (std::memory_order_relaxed) > 0.5f);
    engine.setLowSolo (lowSoloOn->load (std::memory_order_relaxed) > 0.5f);
    engine.setMidMute (midMuteOn->load (std::memory_order_relaxed) > 0.5f);
    engine.setMidSolo (midSoloOn->load (std::memory_order_relaxed) > 0.5f);
    engine.setHighMute (highMuteOn->load (std::memory_order_relaxed) > 0.5f);
    engine.setHighSolo (highSoloOn->load (std::memory_order_relaxed) > 0.5f);

    engine.setHighLimiterEnabled (highLimiterEnabledOn->load (std::memory_order_relaxed) > 0.5f);
    engine.setHighLimiterThresholdDb (highLimiterThresholdDb->load (std::memory_order_relaxed));

    engine.setOutputDb (outputDb->load (std::memory_order_relaxed));
}

//==============================================================================
void TriptychAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32> (getTotalNumOutputChannels());

    // Seed the engine's parameters from the current APVTS state before
    // prepare() primes the crossover/compressor coefficients, so the very
    // first block after prepareToPlay() already reflects the host/session's
    // actual parameter values rather than the engine's built-in defaults.
    pushParametersToEngine();

    engine.prepare (spec);

    // The LR4 crossovers and juce::dsp::Compressor are both minimum-phase/
    // causal with no lookahead, so Triptych never adds latency.
    setLatencySamples (engine.getLatencySamples());
}

void TriptychAudioProcessor::releaseResources()
{
}

void TriptychAudioProcessor::reset()
{
    engine.reset();
}

bool TriptychAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();

    const auto mainOut = layouts.getMainOutputChannelSet();
    const auto mainIn = layouts.getMainInputChannelSet();

    if (mainOut != mono && mainOut != stereo)
        return false;

    if (mainOut != mainIn)
        return false;

    return true;
}

void TriptychAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Buses are constrained to in == out (mono or stereo), so this is
    // normally a no-op, but it's cheap insurance against stray channels.
    for (auto channel = totalNumInputChannels; channel < totalNumOutputChannels; ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    pushParametersToEngine();

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);
}

//==============================================================================
bool TriptychAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* TriptychAudioProcessor::createEditor()
{
    return new TriptychAudioProcessorEditor (*this);
}

//==============================================================================
void TriptychAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();
    const std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void TriptychAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TriptychAudioProcessor();
}
