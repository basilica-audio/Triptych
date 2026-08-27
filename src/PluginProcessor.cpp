#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
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
        config.manufacturerName = "Basilica Audio";
        // Presets saved before the suite adopted its trading name still live
        // under the old folder. PresetManager copies them across on first
        // construction - copies, never moves, so an older build still finds
        // its own. See docs/branding.md.
        config.legacyManufacturerName = "Yves Vogl";
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
            { BinaryData::glueMaster_json, BinaryData::glueMaster_jsonSize },
        };
    }
}

//==============================================================================
TriptychAudioProcessor::TriptychAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          // v0.5.0 external sidechain (issue #1, part 1):
                          // declared disabled by default, because AU hosts
                          // routinely instantiate without it and the plugin
                          // must never *require* a sidechain to load.
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
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

    lowGateEnabledOn = apvts.getRawParameterValue (ParamIDs::lowGateEnabled);
    lowGateThresholdDb = apvts.getRawParameterValue (ParamIDs::lowGateThreshold);
    lowGateRatio = apvts.getRawParameterValue (ParamIDs::lowGateRatio);
    lowGateAttackMs = apvts.getRawParameterValue (ParamIDs::lowGateAttack);
    lowGateReleaseMs = apvts.getRawParameterValue (ParamIDs::lowGateRelease);
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

    midGateEnabledOn = apvts.getRawParameterValue (ParamIDs::midGateEnabled);
    midGateThresholdDb = apvts.getRawParameterValue (ParamIDs::midGateThreshold);
    midGateRatio = apvts.getRawParameterValue (ParamIDs::midGateRatio);
    midGateAttackMs = apvts.getRawParameterValue (ParamIDs::midGateAttack);
    midGateReleaseMs = apvts.getRawParameterValue (ParamIDs::midGateRelease);
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

    highGateEnabledOn = apvts.getRawParameterValue (ParamIDs::highGateEnabled);
    highGateThresholdDb = apvts.getRawParameterValue (ParamIDs::highGateThreshold);
    highGateRatio = apvts.getRawParameterValue (ParamIDs::highGateRatio);
    highGateAttackMs = apvts.getRawParameterValue (ParamIDs::highGateAttack);
    highGateReleaseMs = apvts.getRawParameterValue (ParamIDs::highGateRelease);
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

    scSourceChoice = apvts.getRawParameterValue (ParamIDs::scSource);
    scListenChoice = apvts.getRawParameterValue (ParamIDs::scListen);
    crossoverSlopeChoice = apvts.getRawParameterValue (ParamIDs::crossoverSlope);
    lookaheadChoice = apvts.getRawParameterValue (ParamIDs::lookahead);
    mixPercent = apvts.getRawParameterValue (ParamIDs::mix);

    lowDetectorModeChoice = apvts.getRawParameterValue (ParamIDs::lowDetectorMode);
    lowAutoReleaseOn = apvts.getRawParameterValue (ParamIDs::lowAutoRelease);
    lowCharacterChoice = apvts.getRawParameterValue (ParamIDs::lowCharacter);
    lowStereoLinkPercent = apvts.getRawParameterValue (ParamIDs::lowStereoLink);
    lowGateHoldMs = apvts.getRawParameterValue (ParamIDs::lowGateHold);
    lowGateHysteresisDb = apvts.getRawParameterValue (ParamIDs::lowGateHysteresis);

    midDetectorModeChoice = apvts.getRawParameterValue (ParamIDs::midDetectorMode);
    midAutoReleaseOn = apvts.getRawParameterValue (ParamIDs::midAutoRelease);
    midCharacterChoice = apvts.getRawParameterValue (ParamIDs::midCharacter);
    midStereoLinkPercent = apvts.getRawParameterValue (ParamIDs::midStereoLink);
    midGateHoldMs = apvts.getRawParameterValue (ParamIDs::midGateHold);
    midGateHysteresisDb = apvts.getRawParameterValue (ParamIDs::midGateHysteresis);

    highDetectorModeChoice = apvts.getRawParameterValue (ParamIDs::highDetectorMode);
    highAutoReleaseOn = apvts.getRawParameterValue (ParamIDs::highAutoRelease);
    highCharacterChoice = apvts.getRawParameterValue (ParamIDs::highCharacter);
    highStereoLinkPercent = apvts.getRawParameterValue (ParamIDs::highStereoLink);
    highGateHoldMs = apvts.getRawParameterValue (ParamIDs::highGateHold);
    highGateHysteresisDb = apvts.getRawParameterValue (ParamIDs::highGateHysteresis);

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
    jassert (lowGateEnabledOn != nullptr);
    jassert (lowGateThresholdDb != nullptr);
    jassert (lowGateRatio != nullptr);
    jassert (lowGateAttackMs != nullptr);
    jassert (lowGateReleaseMs != nullptr);
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
    jassert (midGateEnabledOn != nullptr);
    jassert (midGateThresholdDb != nullptr);
    jassert (midGateRatio != nullptr);
    jassert (midGateAttackMs != nullptr);
    jassert (midGateReleaseMs != nullptr);
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
    jassert (highGateEnabledOn != nullptr);
    jassert (highGateThresholdDb != nullptr);
    jassert (highGateRatio != nullptr);
    jassert (highGateAttackMs != nullptr);
    jassert (highGateReleaseMs != nullptr);
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
    jassert (scSourceChoice != nullptr);
    jassert (scListenChoice != nullptr);
    jassert (crossoverSlopeChoice != nullptr);
    jassert (lookaheadChoice != nullptr);
    jassert (mixPercent != nullptr);
    jassert (lowDetectorModeChoice != nullptr);
    jassert (lowAutoReleaseOn != nullptr);
    jassert (lowCharacterChoice != nullptr);
    jassert (lowStereoLinkPercent != nullptr);
    jassert (lowGateHoldMs != nullptr);
    jassert (lowGateHysteresisDb != nullptr);
    jassert (midDetectorModeChoice != nullptr);
    jassert (midAutoReleaseOn != nullptr);
    jassert (midCharacterChoice != nullptr);
    jassert (midStereoLinkPercent != nullptr);
    jassert (midGateHoldMs != nullptr);
    jassert (midGateHysteresisDb != nullptr);
    jassert (highDetectorModeChoice != nullptr);
    jassert (highAutoReleaseOn != nullptr);
    jassert (highCharacterChoice != nullptr);
    jassert (highStereoLinkPercent != nullptr);
    jassert (highGateHoldMs != nullptr);
    jassert (highGateHysteresisDb != nullptr);

    // M2 default resolution: user "Default" preset > factory "Default"
    // preset > the ParameterLayout defaults apvts was just constructed
    // with above (see PresetManager::applyStartupDefault()'s docs).
    presetManager.applyStartupDefault();
}

TriptychAudioProcessor::~TriptychAudioProcessor()
{
    cancelPendingUpdate();
}

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

    engine.setLowGateEnabled (lowGateEnabledOn->load (std::memory_order_relaxed) > 0.5f);
    engine.setLowGateThresholdDb (lowGateThresholdDb->load (std::memory_order_relaxed));
    engine.setLowGateRatio (lowGateRatio->load (std::memory_order_relaxed));
    engine.setLowGateAttackMs (lowGateAttackMs->load (std::memory_order_relaxed));
    engine.setLowGateReleaseMs (lowGateReleaseMs->load (std::memory_order_relaxed));
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

    engine.setMidGateEnabled (midGateEnabledOn->load (std::memory_order_relaxed) > 0.5f);
    engine.setMidGateThresholdDb (midGateThresholdDb->load (std::memory_order_relaxed));
    engine.setMidGateRatio (midGateRatio->load (std::memory_order_relaxed));
    engine.setMidGateAttackMs (midGateAttackMs->load (std::memory_order_relaxed));
    engine.setMidGateReleaseMs (midGateReleaseMs->load (std::memory_order_relaxed));
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

    engine.setHighGateEnabled (highGateEnabledOn->load (std::memory_order_relaxed) > 0.5f);
    engine.setHighGateThresholdDb (highGateThresholdDb->load (std::memory_order_relaxed));
    engine.setHighGateRatio (highGateRatio->load (std::memory_order_relaxed));
    engine.setHighGateAttackMs (highGateAttackMs->load (std::memory_order_relaxed));
    engine.setHighGateReleaseMs (highGateReleaseMs->load (std::memory_order_relaxed));
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

    //==========================================================================
    // v0.5.0.
    const auto readChoice = [] (const std::atomic<float>* value)
    {
        return static_cast<int> (std::lround (value->load (std::memory_order_relaxed)));
    };

    engine.setSidechainExternal (readChoice (scSourceChoice) == 1);
    engine.setSidechainListen (static_cast<TriptychEngine::SidechainListen> (juce::jlimit (0, 3, readChoice (scListenChoice))));

    engine.setCrossoverSlope (static_cast<Crossover::Slope> (juce::jlimit (0, 2, readChoice (crossoverSlopeChoice))));
    engine.setMixPercent (mixPercent->load (std::memory_order_relaxed));

    const auto toLaw = [&readChoice] (const std::atomic<float>* value)
    {
        return readChoice (value) == 1 ? Detector::Law::rms : Detector::Law::peak;
    };

    const auto toCharacter = [&readChoice] (const std::atomic<float>* value)
    {
        return readChoice (value) == 1 ? Detector::Character::vca : Detector::Character::clean;
    };

    engine.setLowDetectorLaw (toLaw (lowDetectorModeChoice));
    engine.setLowDetectorCharacter (toCharacter (lowCharacterChoice));
    engine.setLowAutoReleaseEnabled (lowAutoReleaseOn->load (std::memory_order_relaxed) > 0.5f);
    engine.setLowStereoLinkPercent (lowStereoLinkPercent->load (std::memory_order_relaxed));
    engine.setLowGateHoldMs (lowGateHoldMs->load (std::memory_order_relaxed));
    engine.setLowGateHysteresisDb (lowGateHysteresisDb->load (std::memory_order_relaxed));

    engine.setMidDetectorLaw (toLaw (midDetectorModeChoice));
    engine.setMidDetectorCharacter (toCharacter (midCharacterChoice));
    engine.setMidAutoReleaseEnabled (midAutoReleaseOn->load (std::memory_order_relaxed) > 0.5f);
    engine.setMidStereoLinkPercent (midStereoLinkPercent->load (std::memory_order_relaxed));
    engine.setMidGateHoldMs (midGateHoldMs->load (std::memory_order_relaxed));
    engine.setMidGateHysteresisDb (midGateHysteresisDb->load (std::memory_order_relaxed));

    engine.setHighDetectorLaw (toLaw (highDetectorModeChoice));
    engine.setHighDetectorCharacter (toCharacter (highCharacterChoice));
    engine.setHighAutoReleaseEnabled (highAutoReleaseOn->load (std::memory_order_relaxed) > 0.5f);
    engine.setHighStereoLinkPercent (highStereoLinkPercent->load (std::memory_order_relaxed));
    engine.setHighGateHoldMs (highGateHoldMs->load (std::memory_order_relaxed));
    engine.setHighGateHysteresisDb (highGateHysteresisDb->load (std::memory_order_relaxed));
}

int TriptychAudioProcessor::resolveLookaheadSamples() const noexcept
{
    const auto choice = juce::jlimit (0, 3, static_cast<int> (std::lround (lookaheadChoice->load (std::memory_order_relaxed))));
    const auto seconds = lookaheadSecondsForChoice (choice);

    // Integer by construction: hosts accept whole samples of PDC only.
    return static_cast<int> (std::lround (static_cast<double> (seconds) * preparedSampleRate.load (std::memory_order_relaxed)));
}

void TriptychAudioProcessor::handleAsyncUpdate()
{
    // Always the real JUCE message thread (juce::AsyncUpdater's own
    // contract). prepareToPlay() below can run on a different, host-chosen
    // thread and touches the exact same state (requestedLookaheadSamples/
    // reportedLookaheadSamples, and setLatencySamples() itself), so both are
    // serialised through asyncHandshakeMutex - never taken by processBlock()
    // - to structurally rule out the two entry points racing (see
    // PluginProcessor.h's comment on asyncHandshakeMutex and
    // tests/CrossThreadReprepareTests.cpp for the full writeup and
    // ThreadSanitizer confirmation that this race is real, not just
    // theoretical).
    const std::scoped_lock lock (asyncHandshakeMutex);

    // Tell the host first. Only after the host knows does the audio thread
    // reconfigure the engine (see processBlock), so the engine never runs at
    // a latency the host has not been told about.
    const auto requested = requestedLookaheadSamples.load (std::memory_order_relaxed);

    setLatencySamples (requested);
    reportedLookaheadSamples.store (requested, std::memory_order_release);
}

//==============================================================================
void TriptychAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedSampleRate.store (sampleRate, std::memory_order_relaxed);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32> (juce::jmax (1, getMainBusNumOutputChannels()));

    // Seed the engine's parameters from the current APVTS state before
    // prepare() primes the crossover/compressor coefficients, so the very
    // first block after prepareToPlay() already reflects the host/session's
    // actual parameter values rather than the engine's built-in defaults.
    pushParametersToEngine();

    // Lookahead is resolved here, before prepare() sizes and primes
    // everything, so the very first block already runs at the latency the
    // host is about to be told about. This is called by the host on
    // whatever thread it chooses - the VST3/AU contract guarantees only
    // that it is not the audio thread, NOT that it is JUCE's own message
    // thread - so the requestedLookaheadSamples/reportedLookaheadSamples
    // read-modify-write and the setLatencySamples() call below are
    // serialised against handleAsyncUpdate() through asyncHandshakeMutex
    // (see that method's comment and PluginProcessor.h).
    const auto resolvedLookahead = resolveLookaheadSamples();
    appliedLookaheadSamples.store (resolvedLookahead, std::memory_order_relaxed);
    engine.setLookaheadSamples (resolvedLookahead);

    {
        const std::scoped_lock lock (asyncHandshakeMutex);
        requestedLookaheadSamples.store (resolvedLookahead, std::memory_order_relaxed);
        reportedLookaheadSamples.store (resolvedLookahead, std::memory_order_release);
    }

    engine.prepare (spec);

    // Zero while lookahead is Off - the v0.1-v0.4 invariant every existing
    // session relies on. Otherwise exactly the lookahead length in samples;
    // the LR crossovers (minimum-phase IIR) and the causal, ballistics-driven
    // gain computers add nothing on top of it. Serialised against
    // handleAsyncUpdate()'s own setLatencySamples() call for the same reason
    // as above - both write juce::AudioProcessor's own non-atomic internal
    // latencySamples member.
    {
        const std::scoped_lock lock (asyncHandshakeMutex);
        setLatencySamples (engine.getLatencySamples());
    }
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

    // v0.5.0 sidechain bus: accepted disabled, mono or stereo, and never
    // required. AU hosts frequently present it disabled (auval exercises
    // exactly that), and Standalone has no sidechain at all - so the only
    // rejected states are exotic channel sets we would have no sane keying
    // interpretation for.
    if (layouts.inputBuses.size() > 1)
    {
        const auto sidechain = layouts.getChannelSet (true, 1);

        if (! sidechain.isDisabled() && sidechain != mono && sidechain != stereo)
            return false;
    }

    return true;
}

void TriptychAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // v0.5.0: with a sidechain bus in the layout, `buffer` is no longer just
    // the main bus, so every index into it has to be main-bus-relative.
    auto mainBuffer = getBusBuffer (buffer, false, 0);

    const auto mainInputChannels = getMainBusNumInputChannels();
    const auto mainOutputChannels = getMainBusNumOutputChannels();

    // Buses are constrained to in == out (mono or stereo), so this is
    // normally a no-op, but it's cheap insurance against stray channels.
    for (auto channel = mainInputChannels; channel < mainOutputChannels; ++channel)
        mainBuffer.clear (channel, 0, mainBuffer.getNumSamples());

    pushParametersToEngine();

    // Lookahead handshake: publish what the parameter asks for, let the
    // AsyncUpdater tell the host on the message thread, and only reconfigure
    // once the host has actually been told. Until then the engine keeps
    // running at the latency currently reported, which is what makes the
    // switch safe mid-playback.
    const auto desiredLookahead = resolveLookaheadSamples();

    if (desiredLookahead != appliedLookaheadSamples.load (std::memory_order_relaxed))
    {
        if (reportedLookaheadSamples.load (std::memory_order_acquire) == desiredLookahead)
        {
            appliedLookaheadSamples.store (desiredLookahead, std::memory_order_relaxed);
            engine.setLookaheadSamples (desiredLookahead);
            engine.reset();
        }
        else if (requestedLookaheadSamples.exchange (desiredLookahead, std::memory_order_relaxed) != desiredLookahead)
        {
            triggerAsyncUpdate();
        }
    }

    juce::dsp::AudioBlock<float> block (mainBuffer);

    const auto sidechainBus = getBus (true, 1);

    if (sidechainBus != nullptr && sidechainBus->isEnabled())
    {
        auto sidechainBuffer = getBusBuffer (buffer, true, 1);

        if (sidechainBuffer.getNumChannels() > 0)
        {
            const juce::dsp::AudioBlock<const float> sidechainBlock (sidechainBuffer);
            engine.process (block, &sidechainBlock);
            return;
        }
    }

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
    auto state = apvts.copyState();

    // v0.5.0 state schema versioning: stamp the schema the tree was written
    // with, so a future release can migrate deliberately instead of relying
    // purely on replaceState()'s tolerance. 4 -> 5 needs no transform (the
    // twenty-three additions are purely additive and neutral), but the hook
    // now exists - an absent attribute means v0.4.0 or older.
    state.setProperty (stateVersionProperty, stateSchemaVersion, nullptr);

    const std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void TriptychAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState == nullptr || ! xmlState->hasTagName (apvts.state.getType()))
        return;

    const auto incoming = juce::ValueTree::fromXml (*xmlState);

    // Schema resolution. A tree without the attribute was written by v0.4.0
    // or older; a v0.4.0-shaped tree simply lacks the twenty-three v0.5.0 IDs
    // and APVTS::replaceState() leaves each of them at its (neutral)
    // constructor default - the proven tolerant-migration mechanism this
    // plugin has used since v0.2.0. No 4 -> 5 transform is needed.
    const auto incomingVersion = static_cast<int> (incoming.getProperty (stateVersionProperty, 4));
    juce::ignoreUnused (incomingVersion);

    apvts.replaceState (incoming);
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TriptychAudioProcessor();
}
