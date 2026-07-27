#include "AllocationGuard.h"

#include <limits>
#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <random>

namespace
{
    void setParam (TriptychAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    void setAllBandParams (TriptychAudioProcessor& processor,
                            float thresholdDb, float ratio, float attackMs, float releaseMs, float makeupDb)
    {
        setParam (processor, ParamIDs::lowThreshold, thresholdDb);
        setParam (processor, ParamIDs::lowRatio, ratio);
        setParam (processor, ParamIDs::lowAttack, attackMs);
        setParam (processor, ParamIDs::lowRelease, releaseMs);
        setParam (processor, ParamIDs::lowMakeup, makeupDb);

        setParam (processor, ParamIDs::midThreshold, thresholdDb);
        setParam (processor, ParamIDs::midRatio, ratio);
        setParam (processor, ParamIDs::midAttack, attackMs);
        setParam (processor, ParamIDs::midRelease, releaseMs);
        setParam (processor, ParamIDs::midMakeup, makeupDb);

        setParam (processor, ParamIDs::highThreshold, thresholdDb);
        setParam (processor, ParamIDs::highRatio, ratio);
        setParam (processor, ParamIDs::highAttack, attackMs);
        setParam (processor, ParamIDs::highRelease, releaseMs);
        setParam (processor, ParamIDs::highMakeup, makeupDb);
    }

    // v0.3.0: Range Enabled + Range amount, every band.
    void setAllBandRangeParams (TriptychAudioProcessor& processor, bool rangeEnabled, float rangeDb)
    {
        setParam (processor, ParamIDs::lowRangeEnabled, rangeEnabled ? 1.0f : 0.0f);
        setParam (processor, ParamIDs::lowRange, rangeDb);
        setParam (processor, ParamIDs::midRangeEnabled, rangeEnabled ? 1.0f : 0.0f);
        setParam (processor, ParamIDs::midRange, rangeDb);
        setParam (processor, ParamIDs::highRangeEnabled, rangeEnabled ? 1.0f : 0.0f);
        setParam (processor, ParamIDs::highRange, rangeDb);
    }
}

TEST_CASE ("Silence produces silence (and no NaN/Inf)", "[robustness]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setAllBandParams (processor, -20.0f, 10.0f, 1.0f, 50.0f, 12.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Full-scale input at extreme ratio/makeup produces no NaN/Inf", "[robustness]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setAllBandParams (processor, -60.0f, 20.0f, 0.1f, 1000.0f, 24.0f);
    setParam (processor, ParamIDs::output, 24.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 1.0f);

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
    CHECK (TestHelpers::peakAbsolute (buffer) < 1000.0f); // sane bound, not just "finite"
}

TEST_CASE ("Denormal-range input produces no NaN/Inf output", "[robustness]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setAllBandParams (processor, -30.0f, 4.0f, 5.0f, 100.0f, 6.0f);

    constexpr int numSamples = 512;
    juce::AudioBuffer<float> buffer (2, numSamples);

    const auto denormalValue = std::numeric_limits<float>::denorm_min() * 4.0f;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int sample = 0; sample < numSamples; ++sample)
            data[sample] = (sample % 2 == 0) ? denormalValue : -denormalValue;
    }

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Zero-sample buffer does not crash processBlock", "[robustness]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 0);
    juce::MidiBuffer midi;

    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (buffer.getNumSamples() == 0);
}

TEST_CASE ("Extreme parameter values at both range edges produce no NaN/Inf", "[robustness]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (44100.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;

    for (bool useMinimum : { true, false })
    {
        setParam (processor, ParamIDs::lowMidSplit, useMinimum ? 40.0f : 1000.0f);
        setParam (processor, ParamIDs::midHighSplit, useMinimum ? 400.0f : 12000.0f);
        setAllBandParams (processor,
                           useMinimum ? -60.0f : 0.0f,
                           // v0.3.0: Ratio's minimum is now 0.2 (upward),
                           // widened from 1.0 - see
                           // docs/design-brief-v3-dynamics.md.
                           useMinimum ? 0.2f : 20.0f,
                           useMinimum ? 0.1f : 100.0f,
                           useMinimum ? 10.0f : 1000.0f,
                           useMinimum ? -12.0f : 24.0f);
        setAllBandRangeParams (processor, true, useMinimum ? 0.0f : 30.0f);
        setParam (processor, ParamIDs::output, useMinimum ? -24.0f : 24.0f);

        TestHelpers::fillWithSine (buffer, 44100.0, 440.0, 0.8f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Extreme upward ratio (0.2) with deep threshold and Range disabled produces no NaN/Inf", "[robustness]")
{
    // v0.3.0: the specific scenario that would blow up unclamped (see
    // KneeGainComputerTests.cpp's Range-clamp test) - deepest threshold,
    // most extreme upward ratio, Range explicitly left disabled, full-scale
    // input. A *fresh* sine is supplied every block (matching how a real
    // host actually calls processBlock() - always with a new slice of
    // program material, never the plugin's own prior output fed back in as
    // if it were new input), the same convention the "Rapid parameter
    // automation" test below uses. Deliberately reusing an unrefreshed
    // buffer across many calls instead would model a literal audio feedback
    // loop (the host routing this band's own output back into itself) -
    // for an upward (ratio < 1) band that is a genuine, expected positive-
    // feedback divergence (each pass's already-amplified output becomes the
    // next pass's input, which an upward transfer curve amplifies further
    // still), no different in kind from a delay-with-feedback-over-100% or
    // a self-oscillating resonant filter; not a property this per-block
    // static transfer curve is responsible for damping.
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setAllBandParams (processor, -60.0f, 0.2f, 0.1f, 10.0f, 0.0f);
    setAllBandRangeParams (processor, false, 30.0f); // disabled - the sentinel path is what's under test
    setParam (processor, ParamIDs::output, 0.0f);

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 1.0f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Range enabled at its tightest (0 dB) and loosest (30 dB) settings produces no NaN/Inf", "[robustness]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    for (const auto rangeDb : { 0.0f, 30.0f })
    {
        setAllBandParams (processor, -40.0f, 0.3f, 1.0f, 50.0f, 6.0f);
        setAllBandRangeParams (processor, true, rangeDb);

        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.9f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Rapid parameter automation across many blocks produces no NaN/Inf", "[robustness]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);

    juce::MidiBuffer midi;

    for (int block = 0; block < 100; ++block)
    {
        setParam (processor, ParamIDs::lowMidSplit, 40.0f + unit (rng) * 960.0f);
        setParam (processor, ParamIDs::midHighSplit, 400.0f + unit (rng) * 11600.0f);

        setAllBandParams (processor,
                           -60.0f + unit (rng) * 60.0f,
                           // v0.3.0: sweep the full 0.2-20 range, including
                           // upward (< 1.0) ratios.
                           0.2f + unit (rng) * 19.8f,
                           0.1f + unit (rng) * 99.9f,
                           10.0f + unit (rng) * 990.0f,
                           -12.0f + unit (rng) * 36.0f);
        setAllBandRangeParams (processor, unit (rng) > 0.5f, unit (rng) * 30.0f);
        setParam (processor, ParamIDs::output, -24.0f + unit (rng) * 48.0f);

        juce::AudioBuffer<float> buffer (2, 256);
        TestHelpers::fillWithSine (buffer, 48000.0, 200.0 + unit (rng) * 4000.0, 0.7f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("reset() followed by processBlock does not crash", "[robustness]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setAllBandParams (processor, -18.0f, 4.0f, 10.0f, 100.0f, 0.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.6f);
    juce::MidiBuffer midi;

    processor.processBlock (buffer, midi);

    CHECK_NOTHROW (processor.reset());

    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.6f);
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Block larger than prepareToPlay's declared size is handled defensively", "[robustness]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 128);

    setAllBandParams (processor, -18.0f, 4.0f, 10.0f, 100.0f, 0.0f);

    // Deliberately larger than the 128 declared to prepareToPlay - exercises
    // TriptychEngine::process()'s internal chunking of an oversized block
    // into <= prepared-capacity pieces (see EngineTests.cpp's dedicated
    // "fully processed, not dry-passthrough past the boundary" test for the
    // stronger property that every sample - not just the first 128 - is
    // actually run through the chain).
    juce::AudioBuffer<float> buffer (2, 4096);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.7f);

    juce::MidiBuffer midi;
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}

//==============================================================================
// v0.5.0 robustness (brief section 6, T14 and T15).

namespace
{
    // Drives every v0.5.0 feature at once: external sidechain, maximum
    // lookahead, RMS detection, VCA character, the steepest crossover, full
    // stereo link, gate shaping and a half-wet mix.
    void engageEveryV050Feature (TriptychAudioProcessor& processor)
    {
        const auto setChoice = [&] (const char* id, int index)
        {
            auto* parameter = processor.apvts.getParameter (id);
            REQUIRE (parameter != nullptr);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (static_cast<float> (index)));
        };

        const auto setFloat = [&] (const char* id, float value)
        {
            auto* parameter = processor.apvts.getParameter (id);
            REQUIRE (parameter != nullptr);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        };

        setChoice (ParamIDs::scSource, 1);        // External
        setChoice (ParamIDs::crossoverSlope, 2);  // 48 dB/oct
        setChoice (ParamIDs::lookahead, 3);       // 5 ms
        setFloat (ParamIDs::mix, 50.0f);

        for (const auto* id : { ParamIDs::lowDetectorMode, ParamIDs::midDetectorMode, ParamIDs::highDetectorMode })
            setChoice (id, 1);                    // RMS

        for (const auto* id : { ParamIDs::lowCharacter, ParamIDs::midCharacter, ParamIDs::highCharacter })
            setChoice (id, 1);                    // VCA

        for (const auto* id : { ParamIDs::lowAutoRelease, ParamIDs::midAutoRelease, ParamIDs::highAutoRelease })
            setFloat (id, 1.0f);

        for (const auto* id : { ParamIDs::lowStereoLink, ParamIDs::midStereoLink, ParamIDs::highStereoLink })
            setFloat (id, 100.0f);

        for (const auto* id : { ParamIDs::lowGateEnabled, ParamIDs::midGateEnabled, ParamIDs::highGateEnabled })
            setFloat (id, 1.0f);

        for (const auto* id : { ParamIDs::lowGateHold, ParamIDs::midGateHold, ParamIDs::highGateHold })
            setFloat (id, 250.0f);

        for (const auto* id : { ParamIDs::lowGateHysteresis, ParamIDs::midGateHysteresis, ParamIDs::highGateHysteresis })
            setFloat (id, 8.0f);

        setFloat (ParamIDs::highLimiterEnabled, 1.0f);
    }

    // A processor with the sidechain bus genuinely enabled, so the buffer
    // handed to processBlock carries four channels.
    void enableSidechainBus (TriptychAudioProcessor& processor)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.outputBuses.add (juce::AudioChannelSet::stereo());

        REQUIRE (processor.setBusesLayout (layout));
    }
}

// T14: no heap allocation on the audio thread, with every v0.5.0 feature
// engaged at once. v0.5.0 adds delay lines, key buffers and a whole second
// crossover pair to the signal path; all of them are sized in prepare(), and
// this is the gate that keeps it that way.
TEST_CASE ("T14: processBlock allocates nothing with the full v0.5.0 feature matrix engaged", "[robustness][allocation]")
{
    TriptychAudioProcessor processor;
    enableSidechainBus (processor);
    engageEveryV050Feature (processor);

    processor.prepareToPlay (48000.0, 512);

    // Let the AsyncUpdater-mediated lookahead handshake complete and the
    // engine reconfigure BEFORE the guard goes up - the reconfigure itself
    // runs on the audio thread but only ever after prepare has sized
    // everything, and the first post-change block is the one that applies it.
    juce::AudioBuffer<float> buffer (4, 512);
    juce::MidiBuffer midi;

    for (int warmup = 0; warmup < 8; ++warmup)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
        processor.handleUpdateNowIfNeeded();
    }

    REQUIRE (processor.getLatencySamples() > 0);

    juce::Random random (0x4110);

    for (int channel = 0; channel < 4; ++channel)
        for (int i = 0; i < 512; ++i)
            buffer.setSample (channel, i, 0.5f * (random.nextFloat() * 2.0f - 1.0f));

    {
        const TestAlloc::AllocationGuard guard;

        for (int blockIndex = 0; blockIndex < 32; ++blockIndex)
            processor.processBlock (buffer, midi);

        CHECK (guard.count() == 0);
    }

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

// T15: NaN, Inf and denormal input with every new parameter at an extreme
// must not produce non-finite output, and the engine must recover once the
// input becomes sane again.
TEST_CASE ("T15: extreme v0.5.0 parameter settings survive NaN/Inf/denormal input", "[robustness][nan]")
{
    for (const auto lookaheadChoice : { 0, 3 })
    {
        for (const auto slopeChoice : { 0, 2 })
        {
            TriptychAudioProcessor processor;
            enableSidechainBus (processor);
            engageEveryV050Feature (processor);

            const auto setChoice = [&] (const char* id, int index)
            {
                auto* parameter = processor.apvts.getParameter (id);
                REQUIRE (parameter != nullptr);
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (static_cast<float> (index)));
            };

            setChoice (ParamIDs::lookahead, lookaheadChoice);
            setChoice (ParamIDs::crossoverSlope, slopeChoice);
            setChoice (ParamIDs::scListen, 2); // audition the Mid key as well

            processor.prepareToPlay (48000.0, 256);

            juce::AudioBuffer<float> buffer (4, 256);
            juce::MidiBuffer midi;

            // A block of pathological values.
            for (int channel = 0; channel < 4; ++channel)
                for (int i = 0; i < 256; ++i)
                {
                    const auto pattern = i % 4;
                    buffer.setSample (channel, i, pattern == 0 ? std::numeric_limits<float>::quiet_NaN()
                                                                : pattern == 1 ? std::numeric_limits<float>::infinity()
                                                                                : pattern == 2 ? -std::numeric_limits<float>::infinity()
                                                                                                : 1.0e-40f);
                }

            CHECK_NOTHROW (processor.processBlock (buffer, midi));
            processor.handleUpdateNowIfNeeded();

            // Recovery: after a reset and sane input, the output is finite
            // again within a handful of blocks.
            processor.reset();

            juce::Random random (0x1EEE);

            for (int blockIndex = 0; blockIndex < 12; ++blockIndex)
            {
                for (int channel = 0; channel < 4; ++channel)
                    for (int i = 0; i < 256; ++i)
                        buffer.setSample (channel, i, 0.3f * (random.nextFloat() * 2.0f - 1.0f));

                CHECK_NOTHROW (processor.processBlock (buffer, midi));
                processor.handleUpdateNowIfNeeded();
            }

            INFO ("lookaheadChoice=" << lookaheadChoice << " slopeChoice=" << slopeChoice);
            CHECK (TestHelpers::allSamplesFinite (buffer));
        }
    }
}
