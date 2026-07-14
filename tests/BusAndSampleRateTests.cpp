#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

// Broadens coverage beyond the original v0.1 suite per the M1 "Broaden test
// coverage" issue: sample-rate sweeps (44.1-192 kHz), mono/stereo bus
// configurations, and long-run NaN/Inf stability. Follows the same pattern
// as sibling plugin overture's tests/BusAndSampleRateTests.cpp.
namespace
{
    void setParam (TriptychAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    void setModerateCompression (TriptychAudioProcessor& processor)
    {
        setParam (processor, ParamIDs::lowMidSplit, 250.0f);
        setParam (processor, ParamIDs::midHighSplit, 2500.0f);

        for (const auto* thresholdId : { ParamIDs::lowThreshold, ParamIDs::midThreshold, ParamIDs::highThreshold })
            setParam (processor, thresholdId, -24.0f);

        for (const auto* ratioId : { ParamIDs::lowRatio, ParamIDs::midRatio, ParamIDs::highRatio })
            setParam (processor, ratioId, 4.0f);
    }
}

TEST_CASE ("Sample-rate sweep 44.1-192 kHz: finite output and zero latency at every rate", "[robustness][samplerate]")
{
    static constexpr double sampleRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

    for (const auto sampleRate : sampleRates)
    {
        TriptychAudioProcessor processor;
        processor.prepareToPlay (sampleRate, 256);

        // Both the LR4 crossovers and juce::dsp::Compressor are minimum-
        // phase/causal (see TriptychEngine.h) - latency must stay exactly 0
        // regardless of sample rate.
        CHECK (processor.getLatencySamples() == 0);

        setModerateCompression (processor);

        juce::AudioBuffer<float> buffer (2, 256);
        TestHelpers::fillWithSine (buffer, sampleRate, 1000.0, 0.7f);

        juce::MidiBuffer midi;

        for (int block = 0; block < 4; ++block)
            CHECK_NOTHROW (processor.processBlock (buffer, midi));

        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Sample-rate change mid-session (prepareToPlay called again) stays finite", "[robustness][samplerate]")
{
    TriptychAudioProcessor processor;
    juce::MidiBuffer midi;

    static constexpr double sampleRates[] = { 44100.0, 192000.0, 48000.0, 96000.0 };

    for (const auto sampleRate : sampleRates)
    {
        processor.prepareToPlay (sampleRate, 512);
        setModerateCompression (processor);

        juce::AudioBuffer<float> buffer (2, 512);
        TestHelpers::fillWithSine (buffer, sampleRate, 220.0, 0.6f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Mono bus layout is supported and processes without NaN/Inf", "[robustness][buslayout]")
{
    TriptychAudioProcessor processor;

    juce::AudioProcessor::BusesLayout monoLayout;
    monoLayout.inputBuses.add (juce::AudioChannelSet::mono());
    monoLayout.outputBuses.add (juce::AudioChannelSet::mono());

    REQUIRE (processor.isBusesLayoutSupported (monoLayout));
    REQUIRE (processor.setBusesLayout (monoLayout));

    processor.prepareToPlay (48000.0, 256);
    setModerateCompression (processor);

    juce::AudioBuffer<float> buffer (1, 256);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.8f);

    juce::MidiBuffer midi;

    for (int block = 0; block < 4; ++block)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Stereo bus layout is supported (explicit isBusesLayoutSupported check)", "[robustness][buslayout]")
{
    TriptychAudioProcessor processor;

    juce::AudioProcessor::BusesLayout stereoLayout;
    stereoLayout.inputBuses.add (juce::AudioChannelSet::stereo());
    stereoLayout.outputBuses.add (juce::AudioChannelSet::stereo());

    CHECK (processor.isBusesLayoutSupported (stereoLayout));
}

TEST_CASE ("Mismatched in/out channel-set bus layouts are rejected", "[robustness][buslayout]")
{
    TriptychAudioProcessor processor;

    juce::AudioProcessor::BusesLayout mismatchedLayout;
    mismatchedLayout.inputBuses.add (juce::AudioChannelSet::mono());
    mismatchedLayout.outputBuses.add (juce::AudioChannelSet::stereo());

    CHECK_FALSE (processor.isBusesLayoutSupported (mismatchedLayout));
}

TEST_CASE ("Unsupported multichannel bus layout is rejected", "[robustness][buslayout]")
{
    TriptychAudioProcessor processor;

    juce::AudioProcessor::BusesLayout quadLayout;
    quadLayout.inputBuses.add (juce::AudioChannelSet::quadraphonic());
    quadLayout.outputBuses.add (juce::AudioChannelSet::quadraphonic());

    CHECK_FALSE (processor.isBusesLayoutSupported (quadLayout));
}

TEST_CASE ("Long-run processing (many blocks, several seconds of audio) produces no NaN/Inf drift", "[robustness][longrun]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setModerateCompression (processor);
    setParam (processor, ParamIDs::lowMakeup, 6.0f);
    setParam (processor, ParamIDs::midMakeup, 6.0f);
    setParam (processor, ParamIDs::highMakeup, 6.0f);
    setParam (processor, ParamIDs::output, 3.0f);

    juce::MidiBuffer midi;

    // 500 blocks @ 512 samples/48kHz ~= 5.3 seconds of continuous audio -
    // long enough to reveal slow-building filter-state or smoother drift
    // while staying comfortably under a minute even on Debug/Windows CI.
    constexpr int numBlocks = 500;

    for (int block = 0; block < numBlocks; ++block)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        TestHelpers::fillWithSine (buffer, 48000.0, 110.0, 0.75f, static_cast<juce::int64> (block) * 512);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        REQUIRE (TestHelpers::allSamplesFinite (buffer));
        REQUIRE (TestHelpers::peakAbsolute (buffer) < 100.0f);
    }
}
