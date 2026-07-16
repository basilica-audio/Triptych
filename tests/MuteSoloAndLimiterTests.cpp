#include "PluginProcessor.h"
#include "dsp/TriptychEngine.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

// Coverage for the two M1 "Complete and refine the DSP" additions: per-band
// Mute/Solo (resolved at the summing stage in TriptychEngine) and the
// High-band limiter option (an opt-in juce::dsp::Limiter stage inside
// BandCompressor). See docs/architecture.md for the full design writeup.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 8192;
    constexpr int settleSamples = 2048;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    // Configures a 3-band split with all bands bypassed (ratio 1:1, makeup
    // 0 dB - an exact identity, see BandCompressor.h) so any level change
    // measured afterwards can only come from Mute/Solo, not from
    // compression.
    void bypassAllBandsForMuteSoloTest (TriptychEngine& engine)
    {
        engine.setLowMidSplitHz (250.0f);
        engine.setMidHighSplitHz (2500.0f);

        engine.setLowRatio (1.0f);
        engine.setLowMakeupDb (0.0f);
        engine.setMidRatio (1.0f);
        engine.setMidMakeupDb (0.0f);
        engine.setHighRatio (1.0f);
        engine.setHighMakeupDb (0.0f);
        engine.setOutputDb (0.0f);
    }

    double tailRms (const juce::AudioBuffer<float>& buffer)
    {
        double sumOfSquares = 0.0;
        int counted = 0;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int i = settleSamples; i < buffer.getNumSamples(); ++i)
            {
                sumOfSquares += static_cast<double> (data[i]) * static_cast<double> (data[i]);
                ++counted;
            }
        }

        return counted > 0 ? std::sqrt (sumOfSquares / static_cast<double> (counted)) : 0.0;
    }

    void setParam (TriptychAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }
}

namespace
{
    // Runs the standard Low-band probe tone through an engine configured by
    // `configure`, returning the output's settled-tail RMS. Shared by the
    // Mute/Solo isolation tests below, which compare this against an
    // unmuted/unsoloed reference rather than asserting near-absolute
    // silence - an LR4 crossover's -24 dB/octave rolloff means a
    // muted/non-soloed band's own filter path still leaks a measurable
    // (if strongly attenuated) amount of an in-band probe tone into
    // neighbouring bands; the property actually under test is that
    // Mute/Solo produce a large, deliberate level drop relative to the
    // normal reconstruction, not literal digital silence.
    template <typename Configure>
    double measureLowBandProbeRms (Configure&& configure)
    {
        TriptychEngine engine;
        bypassAllBandsForMuteSoloTest (engine);
        configure (engine);

        const auto spec = makeTestSpec (1);
        engine.prepare (spec);

        juce::AudioBuffer<float> buffer (1, testBlockSize);
        TestHelpers::fillWithSine (buffer, testSampleRate, 100.0, 0.8f); // well inside the Low band

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);

        CHECK (TestHelpers::allSamplesFinite (buffer));
        return tailRms (buffer);
    }
}

TEST_CASE ("Mute: a muted band contributes nothing to the sum", "[dsp][engine][mute]")
{
    const auto referenceRms = measureLowBandProbeRms ([] (TriptychEngine&) {});
    const auto mutedRms = measureLowBandProbeRms ([] (TriptychEngine& engine) { engine.setLowMute (true); });

    REQUIRE (referenceRms > 0.0);
    // Muting Low removes its full-amplitude contribution; only the other
    // (unmuted) bands' cross-crossover leakage of the Low-band probe tone
    // remains, a large and deliberate drop even though the LR4 crossover's
    // own rolloff means it isn't literal silence (see the helper's comment
    // above).
    CHECK (juce::Decibels::gainToDecibels (mutedRms / referenceRms) < -20.0);
}

TEST_CASE ("Solo: only the soloed band reaches the sum, even though other bands are unmuted", "[dsp][engine][solo]")
{
    const auto referenceRms = measureLowBandProbeRms ([] (TriptychEngine&) {});
    const auto midSoloedRms = measureLowBandProbeRms ([] (TriptychEngine& engine) { engine.setMidSolo (true); });

    REQUIRE (referenceRms > 0.0);
    // Soloing Mid isolates away from Low (which is not soloed, even though
    // it isn't muted) for a probe tone that lives entirely in the Low band -
    // same large-drop reasoning as the Mute test above.
    CHECK (juce::Decibels::gainToDecibels (midSoloedRms / referenceRms) < -20.0);
}

TEST_CASE ("Solo: the soloed band's own signal passes through unattenuated", "[dsp][engine][solo]")
{
    TriptychEngine engine;
    bypassAllBandsForMuteSoloTest (engine);
    engine.setLowSolo (true);

    const auto spec = makeTestSpec (1);
    engine.prepare (spec);

    juce::AudioBuffer<float> reference (1, testBlockSize);
    TestHelpers::fillWithSine (reference, testSampleRate, 100.0, 0.8f); // well inside the Low band

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    engine.process (block);

    const auto inputRms = tailRms (reference);
    const auto outputRms = tailRms (processed);

    REQUIRE (inputRms > 0.0);
    // Soloing the band the probe tone actually lives in should reconstruct
    // it just as flat as the full-band null test (all three bands active) -
    // Solo must not itself attenuate the one band it isolates.
    CHECK (juce::Decibels::gainToDecibels (outputRms / inputRms) == Catch::Approx (0.0).margin (0.5));
}

TEST_CASE ("Mute: toggling mid-playback is smoothed, not an instantaneous per-block gain jump (issue #13)", "[dsp][engine][mute][regression]")
{
    // Regression coverage for issue #13: TriptychEngine::process() used to
    // resolve Mute/Solo to a bare 0.0f/1.0f multiplier applied uniformly
    // across an entire block, so toggling mid-playback produced a hard step
    // discontinuity at whichever sample happened to land on the block
    // boundary - audible as a click. This is a distinct failure mode from
    // envelope continuity (already covered above): even with each band's
    // compressor/limiter running continuously underneath, the gain *step*
    // itself was never smoothed.
    TriptychEngine engine;
    bypassAllBandsForMuteSoloTest (engine);

    constexpr int blockSize = 64;
    const auto spec = makeTestSpec (1);
    engine.prepare (spec);

    juce::int64 samplePosition = 0;
    float previousLastSample = 0.0f;
    float maxAbsoluteStepDelta = 0.0f;

    for (int b = 0; b < 20; ++b)
    {
        // Toggle Low mute on mid-stream, while a steady tone is flowing -
        // the exact "click on toggle mid-playback" scenario from the issue
        // title.
        if (b == 10)
            engine.setLowMute (true);

        juce::AudioBuffer<float> buffer (1, blockSize);
        TestHelpers::fillWithSine (buffer, testSampleRate, 100.0, 0.8f, samplePosition); // well inside the Low band

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);

        const auto* data = buffer.getReadPointer (0);

        // The delta across the sample pair straddling this block's boundary
        // with the previous one - exactly where an unsmoothed toggle shows
        // up (the mute/solo gate is resolved once at the top of each block).
        if (b > 0)
            maxAbsoluteStepDelta = std::max (maxAbsoluteStepDelta, std::abs (data[0] - previousLastSample));

        // Also track every intra-block consecutive-sample delta, so the fix
        // can't just move the discontinuity from sample 0 to sample 1.
        for (int i = 1; i < blockSize; ++i)
            maxAbsoluteStepDelta = std::max (maxAbsoluteStepDelta, std::abs (data[i] - data[i - 1]));

        previousLastSample = data[blockSize - 1];
        samplePosition += blockSize;
    }

    // A 100 Hz, 0.8-amplitude sine at 48 kHz has a maximum sample-to-sample
    // delta on the order of 2*pi*100/48000 * 0.8 ~= 0.0105 from the
    // waveform's own slope alone - nowhere near the ~0.8 (full amplitude)
    // single-sample jump an unsmoothed Mute toggle produces. This margin
    // comfortably separates "smoothed ramp" from "hard step" without being
    // tight enough to fail on the waveform's own natural slope.
    CHECK (maxAbsoluteStepDelta < 0.05f);
}

TEST_CASE ("Mute always wins over Solo on the same band", "[dsp][engine][mute][solo]")
{
    TriptychEngine engine;
    bypassAllBandsForMuteSoloTest (engine);
    engine.setLowSolo (true);
    engine.setLowMute (true); // Mute + Solo on the same band: console convention is Mute wins.

    const auto spec = makeTestSpec (1);
    engine.prepare (spec);

    juce::AudioBuffer<float> buffer (1, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, 100.0, 0.8f);

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    CHECK (tailRms (buffer) < 1.0e-4);
    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("High limiter disabled: hard-clipping headroom is not enforced (baseline for the next test)", "[dsp][limiter]")
{
    TriptychEngine engine;
    engine.setLowMidSplitHz (250.0f);
    engine.setMidHighSplitHz (2500.0f);
    engine.setLowRatio (1.0f);
    engine.setMidRatio (1.0f);
    engine.setHighRatio (1.0f);
    engine.setHighMakeupDb (20.0f); // deliberately drive the High band well over 0 dBFS
    engine.setHighLimiterEnabled (false);
    engine.setOutputDb (0.0f);

    const auto spec = makeTestSpec (1);
    engine.prepare (spec);

    juce::AudioBuffer<float> buffer (1, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, 8000.0, 0.9f); // well inside the High band

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    CHECK (TestHelpers::allSamplesFinite (buffer));
    // +20 dB makeup on a 0.9-amplitude tone is far over 0 dBFS - confirms
    // the scenario actually exercises the limiter's job in the next test
    // rather than already being safe by coincidence.
    CHECK (TestHelpers::peakAbsolute (buffer) > 1.0f);
}

TEST_CASE ("High limiter enabled: output never exceeds 0 dBFS even when driven hard", "[dsp][limiter]")
{
    TriptychEngine engine;
    engine.setLowMidSplitHz (250.0f);
    engine.setMidHighSplitHz (2500.0f);
    engine.setLowRatio (1.0f);
    engine.setMidRatio (1.0f);
    engine.setHighRatio (1.0f);
    engine.setHighMakeupDb (20.0f); // same over-0dBFS drive as the baseline test above
    engine.setHighLimiterEnabled (true);
    engine.setHighLimiterThresholdDb (-3.0f);
    engine.setOutputDb (0.0f);

    const auto spec = makeTestSpec (1);
    engine.prepare (spec);

    juce::AudioBuffer<float> buffer (1, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, 8000.0, 0.9f);

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    CHECK (TestHelpers::allSamplesFinite (buffer));
    // juce::dsp::Limiter's second stage always hard-clips at +-1.0 (0 dBFS)
    // regardless of threshold/makeup (JUCE 8.0.14,
    // juce_dsp/widgets/juce_Limiter.cpp) - this is the safety contract the
    // High-band limiter option exists to provide.
    CHECK (TestHelpers::peakAbsolute (buffer) <= 1.0f + 1.0e-4f);
}

TEST_CASE ("High limiter: peak output stays within 0 dBFS across the full threshold range", "[dsp][limiter]")
{
    // juce::dsp::Limiter (JUCE 8.0.14) is a loudness-style limiter: its
    // internal makeup gain (outputVolume, derived from the user threshold)
    // actually *increases* as the threshold is pulled down, so RMS level is
    // not simply monotonic with threshold - only the final hard clip at
    // +-1.0 is an unconditional guarantee regardless of threshold. That is
    // exactly the safety property the High-band limiter option exists to
    // provide, so this test sweeps the full threshold range and checks it
    // holds at every point instead of asserting a level relationship that
    // doesn't actually hold.
    static constexpr float thresholdsDb[] = { 0.0f, -3.0f, -6.0f, -12.0f, -18.0f, -24.0f };

    for (const auto thresholdDb : thresholdsDb)
    {
        INFO ("threshold = " << thresholdDb << " dB");

        TriptychEngine engine;
        engine.setLowMidSplitHz (250.0f);
        engine.setMidHighSplitHz (2500.0f);
        engine.setLowRatio (1.0f);
        engine.setMidRatio (1.0f);
        engine.setHighRatio (1.0f);
        engine.setHighMakeupDb (20.0f);
        engine.setHighLimiterEnabled (true);
        engine.setHighLimiterThresholdDb (thresholdDb);
        engine.setOutputDb (0.0f);

        const auto spec = makeTestSpec (1);
        engine.prepare (spec);

        juce::AudioBuffer<float> buffer (1, testBlockSize);
        TestHelpers::fillWithSine (buffer, testSampleRate, 8000.0, 0.9f);

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);

        CHECK (TestHelpers::allSamplesFinite (buffer));
        CHECK (TestHelpers::peakAbsolute (buffer) <= 1.0f + 1.0e-4f);
    }
}

TEST_CASE ("High limiter: no NaN/Inf across a denormal-range sweep", "[dsp][limiter][robustness]")
{
    TriptychEngine engine;
    engine.setHighLimiterEnabled (true);
    engine.setHighLimiterThresholdDb (-6.0f);
    engine.setHighMakeupDb (24.0f);
    engine.setHighRatio (20.0f);
    engine.setHighThresholdDb (-60.0f);

    const auto spec = makeTestSpec (2);
    engine.prepare (spec);

    constexpr int numSamples = testBlockSize;
    juce::AudioBuffer<float> buffer (2, numSamples);

    const auto denormalValue = std::numeric_limits<float>::denorm_min() * 4.0f;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int sample = 0; sample < numSamples; ++sample)
            data[sample] = (sample % 2 == 0) ? denormalValue : -denormalValue;
    }

    juce::dsp::AudioBlock<float> block (buffer);
    CHECK_NOTHROW (engine.process (block));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Rapid Mute/Solo/Limiter automation across many blocks produces no NaN/Inf", "[robustness][automation][mute][solo][limiter]")
{
    TriptychAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    juce::MidiBuffer midi;

    for (int block = 0; block < 60; ++block)
    {
        setParam (processor, ParamIDs::lowMute, (block % 7 == 0) ? 1.0f : 0.0f);
        setParam (processor, ParamIDs::midSolo, (block % 5 == 0) ? 1.0f : 0.0f);
        setParam (processor, ParamIDs::highMute, (block % 11 == 0) ? 1.0f : 0.0f);
        setParam (processor, ParamIDs::highSolo, (block % 9 == 0) ? 1.0f : 0.0f);
        setParam (processor, ParamIDs::highLimiterEnabled, (block % 3 == 0) ? 1.0f : 0.0f);
        setParam (processor, ParamIDs::highLimiterThreshold, -24.0f + static_cast<float> (block % 24));

        juce::AudioBuffer<float> buffer (2, 256);
        TestHelpers::fillWithSine (buffer, 48000.0, 500.0 + static_cast<double> (block) * 50.0, 0.8f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}
