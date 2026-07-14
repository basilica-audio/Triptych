#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIds.h"
#include "params/ParameterLayout.h"

//==============================================================================
TriptychAudioProcessor::TriptychAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    lowMidSplitHz = apvts.getRawParameterValue (ParamIDs::lowMidSplit);
    midHighSplitHz = apvts.getRawParameterValue (ParamIDs::midHighSplit);

    lowThresholdDb = apvts.getRawParameterValue (ParamIDs::lowThreshold);
    lowRatio = apvts.getRawParameterValue (ParamIDs::lowRatio);
    lowAttackMs = apvts.getRawParameterValue (ParamIDs::lowAttack);
    lowReleaseMs = apvts.getRawParameterValue (ParamIDs::lowRelease);
    lowMakeupDb = apvts.getRawParameterValue (ParamIDs::lowMakeup);

    midThresholdDb = apvts.getRawParameterValue (ParamIDs::midThreshold);
    midRatio = apvts.getRawParameterValue (ParamIDs::midRatio);
    midAttackMs = apvts.getRawParameterValue (ParamIDs::midAttack);
    midReleaseMs = apvts.getRawParameterValue (ParamIDs::midRelease);
    midMakeupDb = apvts.getRawParameterValue (ParamIDs::midMakeup);

    highThresholdDb = apvts.getRawParameterValue (ParamIDs::highThreshold);
    highRatio = apvts.getRawParameterValue (ParamIDs::highRatio);
    highAttackMs = apvts.getRawParameterValue (ParamIDs::highAttack);
    highReleaseMs = apvts.getRawParameterValue (ParamIDs::highRelease);
    highMakeupDb = apvts.getRawParameterValue (ParamIDs::highMakeup);

    outputDb = apvts.getRawParameterValue (ParamIDs::output);

    jassert (lowMidSplitHz != nullptr);
    jassert (midHighSplitHz != nullptr);
    jassert (lowThresholdDb != nullptr);
    jassert (lowRatio != nullptr);
    jassert (lowAttackMs != nullptr);
    jassert (lowReleaseMs != nullptr);
    jassert (lowMakeupDb != nullptr);
    jassert (midThresholdDb != nullptr);
    jassert (midRatio != nullptr);
    jassert (midAttackMs != nullptr);
    jassert (midReleaseMs != nullptr);
    jassert (midMakeupDb != nullptr);
    jassert (highThresholdDb != nullptr);
    jassert (highRatio != nullptr);
    jassert (highAttackMs != nullptr);
    jassert (highReleaseMs != nullptr);
    jassert (highMakeupDb != nullptr);
    jassert (outputDb != nullptr);
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
    engine.setLowAttackMs (lowAttackMs->load (std::memory_order_relaxed));
    engine.setLowReleaseMs (lowReleaseMs->load (std::memory_order_relaxed));
    engine.setLowMakeupDb (lowMakeupDb->load (std::memory_order_relaxed));

    engine.setMidThresholdDb (midThresholdDb->load (std::memory_order_relaxed));
    engine.setMidRatio (midRatio->load (std::memory_order_relaxed));
    engine.setMidAttackMs (midAttackMs->load (std::memory_order_relaxed));
    engine.setMidReleaseMs (midReleaseMs->load (std::memory_order_relaxed));
    engine.setMidMakeupDb (midMakeupDb->load (std::memory_order_relaxed));

    engine.setHighThresholdDb (highThresholdDb->load (std::memory_order_relaxed));
    engine.setHighRatio (highRatio->load (std::memory_order_relaxed));
    engine.setHighAttackMs (highAttackMs->load (std::memory_order_relaxed));
    engine.setHighReleaseMs (highReleaseMs->load (std::memory_order_relaxed));
    engine.setHighMakeupDb (highMakeupDb->load (std::memory_order_relaxed));

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
