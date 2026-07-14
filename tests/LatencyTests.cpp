#include "PluginProcessor.h"
#include "dsp/TriptychEngine.h"

#include <catch2/catch_test_macros.hpp>

// Both the LR4 crossovers (minimum-phase IIR) and juce::dsp::Compressor (a
// causal ballistics-filter envelope follower with no lookahead) add zero
// latency, so Triptych is expected to report exactly 0 samples at all
// times - unlike, e.g. Overture's oversampled clipper, there is no dry-path
// delay compensation to verify here.
TEST_CASE ("getLatencySamples() reports zero latency, before and after prepareToPlay", "[latency]")
{
    TriptychAudioProcessor processor;

    CHECK (processor.getLatencySamples() == 0);

    processor.prepareToPlay (48000.0, 512);

    CHECK (processor.getLatencySamples() == 0);
    CHECK (TriptychEngine::getLatencySamples() == 0);
}

TEST_CASE ("Latency stays zero across sample-rate and block-size changes", "[latency]")
{
    TriptychAudioProcessor processor;

    processor.prepareToPlay (44100.0, 256);
    CHECK (processor.getLatencySamples() == 0);

    processor.prepareToPlay (96000.0, 1024);
    CHECK (processor.getLatencySamples() == 0);

    processor.prepareToPlay (192000.0, 32);
    CHECK (processor.getLatencySamples() == 0);
}
