#include "PluginProcessor.h"
#include "dsp/BandCompressor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// Standing regression guarantees from docs/design-brief.md's "Guarantees &
// tests" section that are properties of the v0.2.0 per-band voicing itself
// (not the Knee stage - see tests/KneeGainComputerTests.cpp /
// tests/BandCompressorTests.cpp for that) - guarantees #4 and #5.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 8192;

    // Reads a float parameter's declared ParameterLayout default in real
    // (non-normalised) units, so this test tracks whatever ParameterLayout.cpp
    // actually ships rather than duplicating magic numbers that could drift
    // out of sync with it.
    float defaultOf (juce::AudioProcessorValueTreeState& apvts, const char* id)
    {
        auto* param = apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param->convertFrom0to1 (param->getDefaultValue());
    }

    double measureTailGainReductionDb (BandCompressor& band, float amplitude)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = 1;
        band.prepare (spec);

        juce::AudioBuffer<float> reference (1, testBlockSize);
        TestHelpers::fillWithSine (reference, testSampleRate, 500.0, amplitude);

        juce::AudioBuffer<float> processed;
        processed.makeCopyOf (reference);

        juce::dsp::AudioBlock<float> block (processed);
        band.process (block);

        constexpr int settleSamples = testBlockSize / 2;
        const auto* refData = reference.getReadPointer (0);
        const auto* outData = processed.getReadPointer (0);

        double sumOfSquaresIn = 0.0;
        double sumOfSquaresOut = 0.0;

        for (int i = settleSamples; i < testBlockSize; ++i)
        {
            sumOfSquaresIn += static_cast<double> (refData[i]) * static_cast<double> (refData[i]);
            sumOfSquaresOut += static_cast<double> (outData[i]) * static_cast<double> (outData[i]);
        }

        const auto count = static_cast<double> (testBlockSize - settleSamples);
        const auto inputRms = std::sqrt (sumOfSquaresIn / count);
        const auto outputRms = std::sqrt (sumOfSquaresOut / count);

        REQUIRE (inputRms > 0.0);
        return juce::Decibels::gainToDecibels (outputRms / inputRms);
    }

    // Feeds a DC step (silence -> constant amplitude) through a freshly
    // configured BandCompressor and measures the sample index at which the
    // output crosses 63.2% of the way from its immediate post-step value to
    // its settled steady-state value - the standard exponential-step-
    // response settling metric, used here as a comparative (not absolute)
    // measure of attack speed across bands. Hard knee (0%) and a very long
    // release keep the release stage from ever engaging during the pure
    // attack phase under test (the ballistics filter's own coefficient
    // selection - attack while rising, release while falling - already
    // guarantees this structurally; the long release is extra insurance).
    int measureAttackSettlingSamples (float attackMs)
    {
        BandCompressor band;
        band.setThresholdDb (-20.0f);
        band.setRatio (8.0f);
        band.setKneePercent (0.0f);
        band.setAttackMs (attackMs);
        band.setReleaseMs (1000.0f);
        band.setMakeupDb (0.0f);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = 1;
        band.prepare (spec);

        juce::AudioBuffer<float> buffer (1, testBlockSize);
        auto* data = buffer.getWritePointer (0);

        constexpr float amplitude = 0.9f; // well above the -20 dB threshold
        for (int i = 0; i < testBlockSize; ++i)
            data[i] = amplitude;

        juce::dsp::AudioBlock<float> block (buffer);
        band.process (block);

        const auto initialValue = data[0];
        const auto finalValue = data[testBlockSize - 1];
        const auto target = initialValue - 0.632f * (initialValue - finalValue);

        for (int i = 0; i < testBlockSize; ++i)
            if (data[i] <= target)
                return i;

        return testBlockSize; // did not settle within the measured window
    }

    // Symmetric release-side measurement: settles into deep compression at a
    // loud DC level (fast, identical attack across all callers so only
    // release differs), then steps down to a quieter-but-still-above-
    // threshold DC level and measures the sample index at which gain
    // recovery crosses 63.2% of the way from the (still hot) transition
    // value to the new, lighter steady-state value. The second level stays
    // non-zero (never true silence) so output amplitude keeps tracking the
    // underlying gain trajectory throughout - see the comment on why a
    // silent second phase would make this unmeasurable.
    int measureReleaseSettlingSamples (float releaseMs)
    {
        BandCompressor band;
        band.setThresholdDb (-20.0f); // 0.1 linear
        band.setRatio (8.0f);
        band.setKneePercent (0.0f);
        band.setAttackMs (0.5f);
        band.setReleaseMs (releaseMs);
        band.setMakeupDb (0.0f);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = 1;
        band.prepare (spec);

        constexpr int settlePhaseSamples = 4096;
        constexpr int releasePhaseSamples = testBlockSize - settlePhaseSamples;
        constexpr float loudAmplitude = 0.9f;
        constexpr float quietAmplitude = 0.15f; // just above the 0.1 linear threshold

        juce::AudioBuffer<float> buffer (1, testBlockSize);
        auto* data = buffer.getWritePointer (0);

        for (int i = 0; i < settlePhaseSamples; ++i)
            data[i] = loudAmplitude;

        for (int i = settlePhaseSamples; i < testBlockSize; ++i)
            data[i] = quietAmplitude;

        juce::dsp::AudioBlock<float> block (buffer);
        band.process (block);

        const auto transitionValue = data[settlePhaseSamples];
        const auto finalValue = data[testBlockSize - 1];
        const auto target = transitionValue + 0.632f * (finalValue - transitionValue);

        for (int i = 0; i < releasePhaseSamples; ++i)
            if (data[settlePhaseSamples + i] >= target)
                return i;

        return releasePhaseSamples; // did not settle within the measured window
    }
}

TEST_CASE ("Voicing: v0.2.0 per-band defaults produce measurably different gain reduction (design-brief.md guarantee #4)", "[voicing][regression]")
{
    TriptychAudioProcessor processor; // used purely to read back ParameterLayout defaults

    BandCompressor low, mid, high;

    low.setThresholdDb (defaultOf (processor.apvts, ParamIDs::lowThreshold));
    low.setRatio (defaultOf (processor.apvts, ParamIDs::lowRatio));
    low.setKneePercent (defaultOf (processor.apvts, ParamIDs::lowKnee));
    low.setAttackMs (defaultOf (processor.apvts, ParamIDs::lowAttack));
    low.setReleaseMs (defaultOf (processor.apvts, ParamIDs::lowRelease));
    low.setMakeupDb (0.0f);

    mid.setThresholdDb (defaultOf (processor.apvts, ParamIDs::midThreshold));
    mid.setRatio (defaultOf (processor.apvts, ParamIDs::midRatio));
    mid.setKneePercent (defaultOf (processor.apvts, ParamIDs::midKnee));
    mid.setAttackMs (defaultOf (processor.apvts, ParamIDs::midAttack));
    mid.setReleaseMs (defaultOf (processor.apvts, ParamIDs::midRelease));
    mid.setMakeupDb (0.0f);

    high.setThresholdDb (defaultOf (processor.apvts, ParamIDs::highThreshold));
    high.setRatio (defaultOf (processor.apvts, ParamIDs::highRatio));
    high.setKneePercent (defaultOf (processor.apvts, ParamIDs::highKnee));
    high.setAttackMs (defaultOf (processor.apvts, ParamIDs::highAttack));
    high.setReleaseMs (defaultOf (processor.apvts, ParamIDs::highRelease));
    high.setMakeupDb (0.0f);

    // -6 dBFS (0.5 amplitude): comfortably above every band's own threshold
    // (Low -24 dB, Mid -30 dB, High -20 dB), so all three measurably
    // compress - identical program material into each, per the guarantee.
    constexpr float amplitude = 0.5f;

    const auto lowGrDb = measureTailGainReductionDb (low, amplitude);
    const auto midGrDb = measureTailGainReductionDb (mid, amplitude);
    const auto highGrDb = measureTailGainReductionDb (high, amplitude);

    INFO ("lowGrDb=" << lowGrDb << " midGrDb=" << midGrDb << " highGrDb=" << highGrDb);

    // Regression guard against a future refactor silently re-uniforming the
    // per-band defaults: none of the three measured gain-reduction amounts
    // may coincide.
    CHECK (lowGrDb != Catch::Approx (midGrDb).margin (0.1));
    CHECK (midGrDb != Catch::Approx (highGrDb).margin (0.1));
    CHECK (lowGrDb != Catch::Approx (highGrDb).margin (0.1));
}

TEST_CASE ("Voicing: attack settling time follows lowAttack > midAttack > highAttack (design-brief.md guarantee #5)", "[voicing][regression]")
{
    TriptychAudioProcessor processor;

    const auto lowAttackMs = defaultOf (processor.apvts, ParamIDs::lowAttack);
    const auto midAttackMs = defaultOf (processor.apvts, ParamIDs::midAttack);
    const auto highAttackMs = defaultOf (processor.apvts, ParamIDs::highAttack);

    // The defaults themselves must already encode the documented ordering
    // (see docs/research-notes.md's Sound on Sound per-band ballistics
    // reasoning) - checked directly, not just via the settling-time proxy
    // below.
    REQUIRE (lowAttackMs > midAttackMs);
    REQUIRE (midAttackMs > highAttackMs);

    const auto lowSettling = measureAttackSettlingSamples (lowAttackMs);
    const auto midSettling = measureAttackSettlingSamples (midAttackMs);
    const auto highSettling = measureAttackSettlingSamples (highAttackMs);

    INFO ("lowSettling=" << lowSettling << " midSettling=" << midSettling << " highSettling=" << highSettling);

    CHECK (lowSettling > midSettling);
    CHECK (midSettling > highSettling);
}

// v0.4.0 (issue #25): the gate's own Attack/Release defaults follow the same
// per-band ordering invariant as the compressor's own (see the two tests
// above) - a direct default-value check, not a settling-time proxy (the gate
// stage's ballistics are exercised via BandCompressorTests.cpp's dedicated
// gate tests instead).
TEST_CASE ("Voicing: Gate Attack/Release defaults follow lowGate > midGate > highGate (issue #25)", "[voicing][gate][regression]")
{
    TriptychAudioProcessor processor;

    const auto lowGateAttackMs = defaultOf (processor.apvts, ParamIDs::lowGateAttack);
    const auto midGateAttackMs = defaultOf (processor.apvts, ParamIDs::midGateAttack);
    const auto highGateAttackMs = defaultOf (processor.apvts, ParamIDs::highGateAttack);

    CHECK (lowGateAttackMs > midGateAttackMs);
    CHECK (midGateAttackMs > highGateAttackMs);

    const auto lowGateReleaseMs = defaultOf (processor.apvts, ParamIDs::lowGateRelease);
    const auto midGateReleaseMs = defaultOf (processor.apvts, ParamIDs::midGateRelease);
    const auto highGateReleaseMs = defaultOf (processor.apvts, ParamIDs::highGateRelease);

    CHECK (lowGateReleaseMs > midGateReleaseMs);
    CHECK (midGateReleaseMs > highGateReleaseMs);

    // Gate Threshold defaults sit well below each band's own compressor
    // Threshold default (issue #25's own recommendation - "well below the
    // compressor's own Threshold").
    const auto lowGateThresholdDb = defaultOf (processor.apvts, ParamIDs::lowGateThreshold);
    const auto midGateThresholdDb = defaultOf (processor.apvts, ParamIDs::midGateThreshold);
    const auto highGateThresholdDb = defaultOf (processor.apvts, ParamIDs::highGateThreshold);

    CHECK (lowGateThresholdDb < defaultOf (processor.apvts, ParamIDs::lowThreshold));
    CHECK (midGateThresholdDb < defaultOf (processor.apvts, ParamIDs::midThreshold));
    CHECK (highGateThresholdDb < defaultOf (processor.apvts, ParamIDs::highThreshold));
}

TEST_CASE ("Voicing: release settling time follows lowRelease > midRelease > highRelease (design-brief.md guarantee #5)", "[voicing][regression]")
{
    TriptychAudioProcessor processor;

    const auto lowReleaseMs = defaultOf (processor.apvts, ParamIDs::lowRelease);
    const auto midReleaseMs = defaultOf (processor.apvts, ParamIDs::midRelease);
    const auto highReleaseMs = defaultOf (processor.apvts, ParamIDs::highRelease);

    REQUIRE (lowReleaseMs > midReleaseMs);
    REQUIRE (midReleaseMs > highReleaseMs);

    const auto lowSettling = measureReleaseSettlingSamples (lowReleaseMs);
    const auto midSettling = measureReleaseSettlingSamples (midReleaseMs);
    const auto highSettling = measureReleaseSettlingSamples (highReleaseMs);

    INFO ("lowSettling=" << lowSettling << " midSettling=" << midSettling << " highSettling=" << highSettling);

    CHECK (lowSettling > midSettling);
    CHECK (midSettling > highSettling);
}
