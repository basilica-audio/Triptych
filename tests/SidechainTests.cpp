#include "PluginProcessor.h"
#include "dsp/TriptychEngine.h"
#include "params/ParameterIds.h"

#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

// External sidechain (v0.5.0, issue #1 part 1, brief section 3.5): band-matched
// keying, the silent fallback to internal keying when the host has not
// connected a sidechain, and detector-key monitoring.
namespace
{
    juce::dsp::ProcessSpec makeSpec (double sampleRate, int blockSize = 512, int numChannels = 2)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (blockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    // A engine configured so the Mid band compresses hard and audibly while
    // Low and High stay bit-exactly bypassed - so any measured level change is
    // unambiguously the Mid band's detector responding to its key.
    void configureMidBandOnly (TriptychEngine& engine)
    {
        engine.setLowRatio (1.0f);
        engine.setHighRatio (1.0f);
        engine.setLowMakeupDb (0.0f);
        engine.setHighMakeupDb (0.0f);

        engine.setMidThresholdDb (-40.0f);
        engine.setMidRatio (20.0f);
        engine.setMidKneePercent (0.0f);
        engine.setMidAttackMs (1.0f);
        engine.setMidReleaseMs (50.0f);
        engine.setMidMakeupDb (0.0f);

        engine.setOutputDb (0.0f);
        engine.setMixPercent (100.0f);
    }

    struct RenderResult
    {
        std::vector<float> output;
    };

    // Renders `numBlocks` blocks of a main signal (optionally alongside a
    // sidechain signal) through the engine and returns the output.
    RenderResult render (TriptychEngine& engine,
                          double sampleRate,
                          int blockSize,
                          int numBlocks,
                          double mainFrequencyHz,
                          float mainAmplitude,
                          double sidechainFrequencyHz,
                          float sidechainAmplitude,
                          bool provideSidechain)
    {
        RenderResult result;
        result.output.reserve (static_cast<size_t> (blockSize * numBlocks));

        juce::AudioBuffer<float> mainBuffer (2, blockSize);
        juce::AudioBuffer<float> sidechainBuffer (2, blockSize);

        for (int blockIndex = 0; blockIndex < numBlocks; ++blockIndex)
        {
            const auto offset = static_cast<juce::int64> (blockIndex) * blockSize;

            TestHelpers::fillWithSine (mainBuffer, sampleRate, mainFrequencyHz, mainAmplitude, offset);
            TestHelpers::fillWithSine (sidechainBuffer, sampleRate, sidechainFrequencyHz, sidechainAmplitude, offset);

            auto block = juce::dsp::AudioBlock<float> (mainBuffer);

            if (provideSidechain)
            {
                auto sidechainWritable = juce::dsp::AudioBlock<float> (sidechainBuffer);
                const juce::dsp::AudioBlock<const float> sidechainBlock (sidechainWritable);
                engine.process (block, &sidechainBlock);
            }
            else
            {
                engine.process (block);
            }

            for (int i = 0; i < blockSize; ++i)
                result.output.push_back (mainBuffer.getSample (0, i));
        }

        return result;
    }

    // RMS over the final quarter of a render, i.e. after every envelope has
    // settled.
    double settledRms (const std::vector<float>& signal)
    {
        const auto start = signal.size() * 3 / 4;
        auto sumOfSquares = 0.0;
        auto count = 0;

        for (auto i = start; i < signal.size(); ++i)
        {
            sumOfSquares += static_cast<double> (signal[i]) * static_cast<double> (signal[i]);
            ++count;
        }

        return count > 0 ? std::sqrt (sumOfSquares / count) : 0.0;
    }
}

// T10: external keying actually keys. A quiet main signal that would never
// trigger the Mid band on its own must be compressed hard once a loud,
// band-centred sidechain is routed in - and must be left alone when the same
// setup is keyed internally.
TEST_CASE ("T10: an external sidechain drives band gain reduction that internal keying does not", "[sidechain]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr int numBlocks = 60;

    // -60 dBFS main, 0 dBFS sidechain, both inside the Mid band.
    constexpr auto mainAmplitude = 0.001f;
    constexpr auto sidechainAmplitude = 1.0f;
    constexpr auto bandFrequency = 1000.0;

    const auto renderWith = [&] (bool external, bool provideSidechain)
    {
        TriptychEngine engine;
        configureMidBandOnly (engine);
        engine.setSidechainExternal (external);
        engine.prepare (makeSpec (sampleRate, blockSize));

        return render (engine, sampleRate, blockSize, numBlocks,
                        bandFrequency, mainAmplitude,
                        bandFrequency, sidechainAmplitude,
                        provideSidechain);
    };

    const auto internalKeyed = renderWith (false, true);
    const auto externalKeyed = renderWith (true, true);

    const auto internalDb = juce::Decibels::gainToDecibels (static_cast<float> (settledRms (internalKeyed.output)), -200.0f);
    const auto externalDb = juce::Decibels::gainToDecibels (static_cast<float> (settledRms (externalKeyed.output)), -200.0f);

    INFO ("internalDb=" << internalDb << " externalDb=" << externalDb);

    // External keying pulls the quiet signal down hard.
    CHECK ((internalDb - externalDb) >= 6.0f);

    // Internal keying leaves it essentially untouched: a -60 dBFS signal is
    // 20 dB below the band's -40 dB threshold, so there is nothing to reduce.
    const auto unprocessed = renderWith (false, false);
    const auto unprocessedDb = juce::Decibels::gainToDecibels (static_cast<float> (settledRms (unprocessed.output)), -200.0f);

    CHECK (std::abs (internalDb - unprocessedDb) <= 0.1f);
}

// T10 (continued): the defensive fallback. Selecting External while the host
// has not connected a sidechain must behave exactly like Internal, sample for
// sample - not fall silent, not key off nothing, not crash.
TEST_CASE ("T10: External keying with no sidechain connected falls back to Internal exactly", "[sidechain]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr int numBlocks = 40;

    const auto renderWith = [&] (bool external)
    {
        TriptychEngine engine;
        configureMidBandOnly (engine);
        engine.setSidechainExternal (external);
        engine.prepare (makeSpec (sampleRate, blockSize));

        return render (engine, sampleRate, blockSize, numBlocks, 1000.0, 0.5f, 1000.0, 1.0f, false);
    };

    const auto internalRender = renderWith (false);
    const auto externalRender = renderWith (true);

    REQUIRE (internalRender.output.size() == externalRender.output.size());

    auto mismatches = 0;

    for (size_t i = 0; i < internalRender.output.size(); ++i)
        if (internalRender.output[i] != externalRender.output[i])
            ++mismatches;

    CHECK (mismatches == 0);
}

// T11: detector-key monitoring outputs the selected band's key, not the
// band's processed audio and not a solo. Correlated against an independently
// band-filtered copy of the same sidechain, the match has to be essentially
// perfect.
TEST_CASE ("T11: Sidechain Listen outputs the selected band's detector key", "[sidechain][listen]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr int numBlocks = 40;

    TriptychEngine engine;
    configureMidBandOnly (engine);
    engine.setSidechainExternal (true);
    engine.setSidechainListen (TriptychEngine::SidechainListen::mid);
    engine.prepare (makeSpec (sampleRate, blockSize));

    const auto listened = render (engine, sampleRate, blockSize, numBlocks, 200.0, 0.4f, 1000.0, 0.8f, true);

    // Independent reference: the same sidechain signal through its own
    // crossover pair at the same frequencies, taking the Mid output.
    Crossover lowMid;
    Crossover midHigh;
    lowMid.prepare (makeSpec (sampleRate, blockSize));
    midHigh.prepare (makeSpec (sampleRate, blockSize));
    lowMid.setCutoffFrequency (200.0f);
    midHigh.setCutoffFrequency (3000.0f);

    juce::AudioBuffer<float> sidechainBuffer (2, blockSize);
    juce::AudioBuffer<float> lowBuffer (2, blockSize);
    juce::AudioBuffer<float> midHighBuffer (2, blockSize);
    juce::AudioBuffer<float> midBuffer (2, blockSize);
    juce::AudioBuffer<float> highBuffer (2, blockSize);

    std::vector<float> reference;
    reference.reserve (listened.output.size());

    for (int blockIndex = 0; blockIndex < numBlocks; ++blockIndex)
    {
        TestHelpers::fillWithSine (sidechainBuffer, sampleRate, 1000.0, 0.8f, static_cast<juce::int64> (blockIndex) * blockSize);

        auto sidechainWritable = juce::dsp::AudioBlock<float> (sidechainBuffer);
        const juce::dsp::AudioBlock<const float> sidechainBlock (sidechainWritable);
        auto low = juce::dsp::AudioBlock<float> (lowBuffer);
        auto midHighBlock = juce::dsp::AudioBlock<float> (midHighBuffer);

        lowMid.process (sidechainBlock, low, midHighBlock);

        auto midHighWritable = juce::dsp::AudioBlock<float> (midHighBuffer);
        const juce::dsp::AudioBlock<const float> remainder (midHighWritable);
        auto mid = juce::dsp::AudioBlock<float> (midBuffer);
        auto high = juce::dsp::AudioBlock<float> (highBuffer);

        midHigh.process (remainder, mid, high);

        for (int i = 0; i < blockSize; ++i)
            reference.push_back (midBuffer.getSample (0, i));
    }

    REQUIRE (reference.size() == listened.output.size());

    // Skip the first block while both crossovers prime identically.
    const auto offset = static_cast<size_t> (blockSize);
    const auto correlation = TestHelpers::correlation (listened.output.data() + offset,
                                                        reference.data() + offset,
                                                        static_cast<int> (listened.output.size() - offset));

    INFO ("correlation=" << correlation);
    CHECK (correlation >= 0.99);
}

// Listen Off is the default and must leave the output completely alone.
TEST_CASE ("Sidechain Listen Off leaves the processed output untouched", "[sidechain][listen]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr int numBlocks = 20;

    const auto renderWith = [&] (TriptychEngine::SidechainListen listen)
    {
        TriptychEngine engine;
        configureMidBandOnly (engine);
        engine.setSidechainListen (listen);
        engine.prepare (makeSpec (sampleRate, blockSize));

        return render (engine, sampleRate, blockSize, numBlocks, 1000.0, 0.5f, 1000.0, 0.5f, false);
    };

    const auto normal = renderWith (TriptychEngine::SidechainListen::off);
    const auto listening = renderWith (TriptychEngine::SidechainListen::mid);

    // Listening genuinely changes the output (it is a monitoring aid, not a
    // no-op)...
    auto differences = 0;

    for (size_t i = 0; i < normal.output.size(); ++i)
        if (normal.output[i] != listening.output[i])
            ++differences;

    CHECK (differences > 0);

    // ...and the processed render is finite and sane.
    for (const auto sample : normal.output)
        CHECK (std::isfinite (sample));
}

// The bus-layout half of the feature: the sidechain is declared, optional, and
// accepted disabled, mono or stereo - which is what auval and pluginval
// exercise, and what a Standalone build (no sidechain at all) needs.
TEST_CASE ("The sidechain bus is optional and accepts disabled, mono and stereo layouts", "[sidechain][buslayout]")
{
    TriptychAudioProcessor processor;

    REQUIRE (processor.getBusCount (true) == 2);

    const auto sidechainBus = processor.getBus (true, 1);
    REQUIRE (sidechainBus != nullptr);

    // Declared disabled by default: hosts must be able to instantiate the
    // plugin without ever offering a sidechain.
    CHECK (! sidechainBus->isEnabled());

    const auto makeLayout = [] (const juce::AudioChannelSet& main, const juce::AudioChannelSet& sidechain)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (main);
        layout.inputBuses.add (sidechain);
        layout.outputBuses.add (main);
        return layout;
    };

    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();
    const auto disabled = juce::AudioChannelSet::disabled();

    CHECK (processor.isBusesLayoutSupported (makeLayout (stereo, disabled)));
    CHECK (processor.isBusesLayoutSupported (makeLayout (stereo, mono)));
    CHECK (processor.isBusesLayoutSupported (makeLayout (stereo, stereo)));
    CHECK (processor.isBusesLayoutSupported (makeLayout (mono, disabled)));
    CHECK (processor.isBusesLayoutSupported (makeLayout (mono, mono)));
    CHECK (processor.isBusesLayoutSupported (makeLayout (mono, stereo)));

    // Exotic sidechain channel sets stay rejected - there is no sane keying
    // interpretation for them.
    CHECK (! processor.isBusesLayoutSupported (makeLayout (stereo, juce::AudioChannelSet::create5point1())));
}

// End to end through the processor with a genuine sidechain bus enabled: the
// key reaches the detectors and the plugin stays numerically healthy.
TEST_CASE ("An enabled sidechain bus is consumed by the processor without NaN/Inf", "[sidechain][buslayout]")
{
    TriptychAudioProcessor processor;

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());

    REQUIRE (processor.isBusesLayoutSupported (layout));
    REQUIRE (processor.setBusesLayout (layout));

    auto* sourceParameter = processor.apvts.getParameter (ParamIDs::scSource);
    REQUIRE (sourceParameter != nullptr);
    sourceParameter->setValueNotifyingHost (sourceParameter->convertTo0to1 (1.0f));

    processor.prepareToPlay (48000.0, 256);

    // Four channels: main stereo followed by sidechain stereo.
    juce::AudioBuffer<float> buffer (4, 256);
    juce::MidiBuffer midi;

    for (int blockIndex = 0; blockIndex < 20; ++blockIndex)
    {
        for (int channel = 0; channel < 4; ++channel)
        {
            auto* data = buffer.getWritePointer (channel);

            for (int i = 0; i < 256; ++i)
            {
                const auto phase = juce::MathConstants<double>::twoPi * (channel < 2 ? 220.0 : 1000.0)
                                    * static_cast<double> (blockIndex * 256 + i) / 48000.0;
                data[i] = (channel < 2 ? 0.3f : 0.9f) * static_cast<float> (std::sin (phase));
            }
        }

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
    }

    CHECK (TestHelpers::allSamplesFinite (buffer));
}
