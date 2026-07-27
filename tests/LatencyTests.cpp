#include "PluginProcessor.h"
#include "dsp/TriptychEngine.h"
#include "params/ParameterIds.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace
{
    // Drives the Lookahead choice parameter to one of its four options and
    // lets the AsyncUpdater-mediated latency handshake settle. The processor
    // deliberately refuses to reconfigure until the host has been told about
    // the new latency on the message thread (see
    // TriptychAudioProcessor::processBlock), so a test has to pump the
    // message queue between blocks - exactly what a host does.
    void selectLookahead (TriptychAudioProcessor& processor, int choiceIndex, double sampleRate, int blockSize)
    {
        auto* parameter = processor.apvts.getParameter (ParamIDs::lookahead);
        REQUIRE (parameter != nullptr);

        parameter->setValueNotifyingHost (parameter->convertTo0to1 (static_cast<float> (choiceIndex)));

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        // Three rounds is more than enough: one block publishes the request,
        // the message-thread dispatch reports it, the next block applies it.
        for (int round = 0; round < 3; ++round)
        {
            buffer.clear();
            processor.processBlock (buffer, midi);

            // Stand in for the host's message loop: the processor only
            // reconfigures once setLatencySamples() has actually run.
            processor.handleUpdateNowIfNeeded();
        }

        juce::ignoreUnused (sampleRate);
    }

    int expectedLatencySamples (int choiceIndex, double sampleRate)
    {
        return static_cast<int> (std::lround (static_cast<double> (TriptychAudioProcessor::lookaheadSecondsForChoice (choiceIndex)) * sampleRate));
    }
}

// The v0.1-v0.4 invariant, deliberately retained rather than weakened: with
// Lookahead at its default Off, both the LR crossovers (minimum-phase IIR)
// and the causal, ballistics-driven gain computers add zero latency, so
// Triptych still reports exactly 0 samples. Every session saved before
// v0.5.0 has Lookahead Off (the parameter did not exist), so every one of
// them keeps its zero-latency behaviour.
TEST_CASE ("getLatencySamples() reports zero latency at the default Lookahead Off, before and after prepareToPlay", "[latency]")
{
    TriptychAudioProcessor processor;

    CHECK (processor.getLatencySamples() == 0);

    processor.prepareToPlay (48000.0, 512);

    CHECK (processor.getLatencySamples() == 0);
    CHECK (processor.getLatencySamples() == 0);
}

TEST_CASE ("Latency stays zero across sample-rate and block-size changes while Lookahead is Off", "[latency]")
{
    TriptychAudioProcessor processor;

    processor.prepareToPlay (44100.0, 256);
    CHECK (processor.getLatencySamples() == 0);

    processor.prepareToPlay (96000.0, 1024);
    CHECK (processor.getLatencySamples() == 0);

    processor.prepareToPlay (192000.0, 32);
    CHECK (processor.getLatencySamples() == 0);
}

// T12: with lookahead engaged the reported latency is exactly
// round (lookaheadSeconds * fs) - an integer by construction, because hosts
// only accept whole samples of delay compensation.
TEST_CASE ("T12: reported latency equals the exact lookahead sample count at every rate", "[latency][lookahead]")
{
    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        TriptychAudioProcessor processor;
        processor.prepareToPlay (sampleRate, 512);

        REQUIRE (processor.getLatencySamples() == 0);

        for (int choice = 1; choice <= 3; ++choice)
        {
            selectLookahead (processor, choice, sampleRate, 512);

            const auto expected = expectedLatencySamples (choice, sampleRate);

            CHECK (expected > 0);
            CHECK (processor.getLatencySamples() == expected);
        }

        // Back to Off restores the zero-latency invariant exactly.
        selectLookahead (processor, 0, sampleRate, 512);
        CHECK (processor.getLatencySamples() == 0);
    }
}

// T12 (continued): the reported latency is a property of the lookahead choice
// and the sample rate only - never of the host's block size.
TEST_CASE ("T12: reported latency is stable across block sizes", "[latency][lookahead]")
{
    constexpr auto sampleRate = 48000.0;

    for (const auto blockSize : { 1, 64, 513, 4096 })
    {
        TriptychAudioProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);

        selectLookahead (processor, 2, sampleRate, blockSize);

        CHECK (processor.getLatencySamples() == expectedLatencySamples (2, sampleRate));
    }
}

// The engine's own accessor tracks the same value, so an engine driven
// directly by a test (rather than through the processor) reports honestly too.
TEST_CASE ("TriptychEngine::getLatencySamples() follows the configured lookahead", "[latency][lookahead]")
{
    TriptychEngine engine;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 512;
    spec.numChannels = 2;

    engine.prepare (spec);
    CHECK (engine.getLatencySamples() == 0);

    engine.setLookaheadSamples (144);
    CHECK (engine.getLatencySamples() == 144);

    engine.setLookaheadSamples (0);
    CHECK (engine.getLatencySamples() == 0);
}
