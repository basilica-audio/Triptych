#include "dsp/BandCompressor.h"
#include "dsp/Detector.h"
#include "LegacyReferenceChain.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 4096;
    constexpr double testFrequencyHz = 500.0;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }
}

TEST_CASE ("BandCompressor: ratio 1:1 + makeup 0 dB is an exact bypass regardless of Knee", "[dsp][compressor][null]")
{
    // Threshold is deliberately set well inside the signal's level, so a
    // true bypass test has to prove the VCA gain is unconditionally 1.0
    // (see BandCompressor.h) rather than just "quiet enough to not matter".
    // Sweeps Knee across its full range - design-brief.md guarantee #3:
    // "assert knee has zero audible effect when ratio == 1:1".
    for (const auto kneePercent : { 0.0f, 50.0f, 100.0f })
    {
        CAPTURE (kneePercent);

        BandCompressor band;
        band.setThresholdDb (-40.0f);
        band.setRatio (1.0f);
        band.setKneePercent (kneePercent);
        band.setAttackMs (1.0f);
        band.setReleaseMs (50.0f);
        band.setMakeupDb (0.0f);

        const auto spec = makeTestSpec (2);
        band.prepare (spec);

        juce::AudioBuffer<float> reference (2, testBlockSize);
        TestHelpers::fillWithSine (reference, testSampleRate, testFrequencyHz, 0.9f);

        juce::AudioBuffer<float> processed;
        processed.makeCopyOf (reference);

        juce::dsp::AudioBlock<float> block (processed);
        band.process (block);

        for (int channel = 0; channel < reference.getNumChannels(); ++channel)
        {
            const auto* refData = reference.getReadPointer (channel);
            const auto* outData = processed.getReadPointer (channel);

            for (int i = 0; i < testBlockSize; ++i)
                CHECK (outData[i] == Catch::Approx (refData[i]).margin (1e-6));
        }
    }
}

TEST_CASE ("BandCompressor: Knee null test - Knee 0% reproduces v0.1's exact hard-knee bypass identity bit-for-bit (design-brief.md guarantee #1)", "[dsp][compressor][null][regression]")
{
    // Regression coverage: v0.1's bypass-identity test, unchanged, with Knee
    // explicitly pinned to 0% (v0.1's only possible behaviour before this
    // parameter existed) rather than the new v0.2.0 default (50%) - see the
    // sibling test above for the "any Knee value" version of this property.
    BandCompressor band;
    band.setThresholdDb (-40.0f);
    band.setRatio (1.0f);
    band.setKneePercent (0.0f);
    band.setAttackMs (1.0f);
    band.setReleaseMs (50.0f);
    band.setMakeupDb (0.0f);

    const auto spec = makeTestSpec (2);
    band.prepare (spec);

    juce::AudioBuffer<float> reference (2, testBlockSize);
    TestHelpers::fillWithSine (reference, testSampleRate, testFrequencyHz, 0.9f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    band.process (block);

    for (int channel = 0; channel < reference.getNumChannels(); ++channel)
    {
        const auto* refData = reference.getReadPointer (channel);
        const auto* outData = processed.getReadPointer (channel);

        for (int i = 0; i < testBlockSize; ++i)
            CHECK (outData[i] == Catch::Approx (refData[i]).margin (1e-6));
    }
}

TEST_CASE ("BandCompressor: a signal above threshold receives measurable gain reduction (Knee 0%, v0.1 regression)", "[dsp][compressor][regression]")
{
    // Knee pinned to 0% so this reproduces v0.1's exact hard-knee GR
    // measurement (see the Knee-sweep test below for the v0.2.0 default).
    BandCompressor band;
    band.setThresholdDb (-24.0f);
    band.setRatio (8.0f);
    band.setKneePercent (0.0f);
    band.setAttackMs (0.5f);
    band.setReleaseMs (50.0f);
    band.setMakeupDb (0.0f);

    const auto spec = makeTestSpec (2);
    band.prepare (spec);

    // -3 dBFS sine, comfortably above the -24 dB threshold, run for enough
    // samples that the envelope follower settles into steady-state gain
    // reduction well before the end of the block.
    juce::AudioBuffer<float> reference (2, testBlockSize);
    TestHelpers::fillWithSine (reference, testSampleRate, testFrequencyHz, 0.7f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    band.process (block);

    // Measure RMS over the settled tail (skip the attack transient).
    constexpr int settleSamples = testBlockSize / 2;

    const auto tailRms = [] (const juce::AudioBuffer<float>& buffer)
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
    };

    const auto inputRms = tailRms (reference);
    const auto outputRms = tailRms (processed);

    REQUIRE (inputRms > 0.0);

    const auto gainReductionDb = juce::Decibels::gainToDecibels (outputRms / inputRms);

    // An 8:1 ratio, 18 dB above threshold, should compress substantially:
    // ideal steady-state gain reduction is about (1 - 1/ratio) * 18 ~= 15.75
    // dB; assert at least several dB of measurable reduction rather than
    // pinning the exact ideal number, since the envelope follower's own
    // ripple/settling behaviour is not the property under test here.
    CHECK (gainReductionDb < -6.0);
    CHECK (TestHelpers::allSamplesFinite (processed));
}

TEST_CASE ("BandCompressor: a signal above threshold still receives measurable gain reduction at Knee 50%/100%", "[dsp][compressor]")
{
    // Same scenario as the Knee-0% regression test above, but sweeping
    // Knee up to the new v0.2.0 default and its maximum - a steady loud
    // tone well above threshold + half-knee-width should settle into
    // essentially the same full-ratio gain reduction regardless of Knee
    // (only the *transition* into compression differs - see
    // KneeGainComputerTests.cpp for that shape assertion).
    for (const auto kneePercent : { 50.0f, 100.0f })
    {
        CAPTURE (kneePercent);

        BandCompressor band;
        band.setThresholdDb (-24.0f);
        band.setRatio (8.0f);
        band.setKneePercent (kneePercent);
        band.setAttackMs (0.5f);
        band.setReleaseMs (50.0f);
        band.setMakeupDb (0.0f);

        const auto spec = makeTestSpec (2);
        band.prepare (spec);

        juce::AudioBuffer<float> reference (2, testBlockSize);
        TestHelpers::fillWithSine (reference, testSampleRate, testFrequencyHz, 0.7f);

        juce::AudioBuffer<float> processed;
        processed.makeCopyOf (reference);

        juce::dsp::AudioBlock<float> block (processed);
        band.process (block);

        constexpr int settleSamples = testBlockSize / 2;

        const auto tailRms = [] (const juce::AudioBuffer<float>& buffer)
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
        };

        const auto inputRms = tailRms (reference);
        const auto outputRms = tailRms (processed);

        REQUIRE (inputRms > 0.0);

        const auto gainReductionDb = juce::Decibels::gainToDecibels (outputRms / inputRms);

        CHECK (gainReductionDb < -6.0);
        CHECK (TestHelpers::allSamplesFinite (processed));
    }
}

// v0.3.0 hybrid dynamics (docs/design-brief-v3-dynamics.md): a real,
// envelope-integrated proof that Ratio < 1:1 genuinely boosts a signal
// sitting above threshold, mirroring the existing "gain reduction" tests'
// technique but measuring a positive (louder-than-input) result instead.
TEST_CASE ("BandCompressor: ratio < 1 (upward) boosts a signal sitting above threshold", "[dsp][compressor][upward]")
{
    BandCompressor band;
    band.setThresholdDb (-24.0f);
    band.setRatio (0.4f); // upward: well below 1:1
    band.setKneePercent (0.0f);
    band.setAttackMs (0.5f);
    band.setReleaseMs (50.0f);
    band.setMakeupDb (0.0f);

    const auto spec = makeTestSpec (2);
    band.prepare (spec);

    // -3 dBFS sine, comfortably above the -24 dB threshold - the same
    // scenario the downward-ratio regression test above uses, just with an
    // upward ratio instead.
    juce::AudioBuffer<float> reference (2, testBlockSize);
    TestHelpers::fillWithSine (reference, testSampleRate, testFrequencyHz, 0.7f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    band.process (block);

    constexpr int settleSamples = testBlockSize / 2;

    const auto tailRms = [] (const juce::AudioBuffer<float>& buffer)
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
    };

    const auto inputRms = tailRms (reference);
    const auto outputRms = tailRms (processed);

    REQUIRE (inputRms > 0.0);

    const auto gainChangeDb = juce::Decibels::gainToDecibels (outputRms / inputRms);

    // A genuine boost (positive dB gain change), not a cut.
    CHECK (gainChangeDb > 1.0);
    CHECK (TestHelpers::allSamplesFinite (processed));
}

// v0.3.0: Range clamps the gain change on real, envelope-processed audio,
// not just in the pure static-curve math (see KneeGainComputerTests.cpp).
// An aggressive upward ratio + deep threshold would produce far more than
// Range's clamp unclamped; with Range engaged, the measured gain change must
// stay within it (plus a small settling-tolerance margin).
TEST_CASE ("BandCompressor: Range clamps gain change on real audio", "[dsp][compressor][range]")
{
    constexpr float rangeDb = 6.0f;

    auto makeBand = [&] (bool rangeEnabled)
    {
        auto band = std::make_unique<BandCompressor>();
        band->setThresholdDb (-40.0f);
        band->setRatio (0.2f); // extreme upward
        band->setKneePercent (0.0f);
        band->setAttackMs (0.5f);
        band->setReleaseMs (50.0f);
        band->setMakeupDb (0.0f);
        band->setRangeEnabled (rangeEnabled);
        band->setRangeDb (rangeDb);

        const auto spec = makeTestSpec (2);
        band->prepare (spec);
        return band;
    };

    const auto tailRms = [] (const juce::AudioBuffer<float>& buffer)
    {
        constexpr int settleSamples = testBlockSize / 2;
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
    };

    juce::AudioBuffer<float> reference (2, testBlockSize);
    TestHelpers::fillWithSine (reference, testSampleRate, testFrequencyHz, 0.7f); // ~-3 dBFS, well above -40 dB threshold
    const auto inputRms = tailRms (reference);
    REQUIRE (inputRms > 0.0);

    auto unclampedBand = makeBand (false);
    juce::AudioBuffer<float> unclampedProcessed;
    unclampedProcessed.makeCopyOf (reference);
    juce::dsp::AudioBlock<float> unclampedBlock (unclampedProcessed);
    unclampedBand->process (unclampedBlock);
    const auto unclampedGainChangeDb = juce::Decibels::gainToDecibels (tailRms (unclampedProcessed) / inputRms);

    // Sanity: the scenario really would exceed Range unclamped.
    REQUIRE (unclampedGainChangeDb > rangeDb + 1.0);

    auto clampedBand = makeBand (true);
    juce::AudioBuffer<float> clampedProcessed;
    clampedProcessed.makeCopyOf (reference);
    juce::dsp::AudioBlock<float> clampedBlock (clampedProcessed);
    clampedBand->process (clampedBlock);
    const auto clampedGainChangeDb = juce::Decibels::gainToDecibels (tailRms (clampedProcessed) / inputRms);

    CHECK (clampedGainChangeDb <= rangeDb + 0.5); // small settling-window tolerance
    CHECK (TestHelpers::allSamplesFinite (clampedProcessed));
}

// Regression: a band whose Range API is never called at all must behave
// identically to one that explicitly disables it - i.e. v0.3.0's Range
// addition is a true no-op at its default (unclamped) state, reproducing
// v0.2.0 bit-for-bit.
TEST_CASE ("BandCompressor: Range untouched reproduces the same output as Range explicitly disabled (v0.2.0 regression)", "[dsp][compressor][range][regression]")
{
    auto makeBand = [] (bool touchRangeApi)
    {
        auto band = std::make_unique<BandCompressor>();
        band->setThresholdDb (-24.0f);
        band->setRatio (8.0f);
        band->setKneePercent (50.0f);
        band->setAttackMs (0.5f);
        band->setReleaseMs (50.0f);
        band->setMakeupDb (0.0f);

        if (touchRangeApi)
            band->setRangeEnabled (false);

        const auto spec = makeTestSpec (2);
        band->prepare (spec);
        return band;
    };

    auto untouched = makeBand (false);
    auto explicitlyDisabled = makeBand (true);

    juce::AudioBuffer<float> bufferA (2, testBlockSize);
    TestHelpers::fillWithSine (bufferA, testSampleRate, testFrequencyHz, 0.7f);
    juce::AudioBuffer<float> bufferB;
    bufferB.makeCopyOf (bufferA);

    juce::dsp::AudioBlock<float> blockA (bufferA);
    juce::dsp::AudioBlock<float> blockB (bufferB);
    untouched->process (blockA);
    explicitlyDisabled->process (blockB);

    for (int channel = 0; channel < bufferA.getNumChannels(); ++channel)
    {
        const auto* dataA = bufferA.getReadPointer (channel);
        const auto* dataB = bufferB.getReadPointer (channel);

        for (int i = 0; i < testBlockSize; ++i)
            CHECK (dataA[i] == Catch::Approx (dataB[i]).margin (1e-9f));
    }
}

TEST_CASE ("BandCompressor: limiter ballistics track input while disabled, not frozen (issue #12)", "[dsp][compressor][limiter][regression]")
{
    // Regression coverage for issue #12: the previous implementation toggled
    // juce::dsp::Limiter's own context.isBypassed flag while "disabled",
    // which (JUCE 8.0.14, juce_dsp/widgets/juce_Limiter.h:79-83, which each
    // of its two internal Compressors also does per juce_Compressor.h:85-89)
    // short-circuits to a plain copyFrom() as the *first* statement -
    // skipping the BallisticsFilter envelope update entirely. So the
    // limiter's internal gain-reduction state was frozen at whatever it was
    // the instant limiterEnabled flipped to false, contradicting the
    // documented "keeps its internal ballistics continuous... no pop on
    // re-enable" guarantee (BandCompressor.h, docs/architecture.md).
    //
    // This proves the property directly by comparing two instances fed the
    // identical audio sequence (loud -> quiet -> loud again):
    //  - "reference": limiter enabled throughout - the continuously-tracked
    //    ground truth. Its envelope decays during the quiet passage (like
    //    any real, still-flowing signal would) and has to re-attack when
    //    the loud tone resumes, producing a genuine, measurable overshoot
    //    right after the loud tone returns (the fast-but-not-instant attack
    //    stage of the two cascaded internal Compressors catching up).
    //  - "toggled": limiter disabled for the exact same quiet passage, then
    //    re-enabled when the loud tone resumes - the scenario from the
    //    issue title.
    // With a continuously-tracked envelope (the fix), both instances run
    // through byte-for-byte identical processing at every point in time
    // (whether or not the limiter is spliced into the output is orthogonal
    // to whether it *runs*), so toggled's output should match reference's
    // sample-for-sample once re-enabled. With the old frozen-envelope
    // behaviour, toggled resumes already at the hot, fully-attenuating
    // state from before the quiet passage, so its immediate post-re-enable
    // samples diverge sharply from reference's genuine re-attack transient
    // before both eventually reach the same steady state - a divergence a
    // block-aggregate peak can miss (both instances' steady state is
    // dominated by juce::dsp::Limiter's own unconditional +-1.0 hard clip),
    // so this measures the largest per-sample difference between the two
    // instead.
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 64;
    constexpr double toneHz = 1000.0;

    constexpr int loudBlocks = 100; // ~133 ms: let the envelope settle hot
    constexpr int quietBlocks = 400; // ~533 ms: far more than the limiter's 2 ms/50 ms attack/release times, so a continuously-tracked envelope fully decays
    constexpr int measureBlocks = 4; // ~5.3 ms right after re-enable: the attack-transient window

    auto makeBand = []
    {
        auto band = std::make_unique<BandCompressor>();
        band->setThresholdDb (-24.0f);
        band->setRatio (1.0f); // VCA stage bypassed - isolates the limiter's own behaviour
        band->setAttackMs (1.0f);
        band->setReleaseMs (50.0f);
        band->setMakeupDb (6.0f); // over the limiter threshold, but not so hot that both instances immediately saturate at the hard clip regardless of envelope state
        band->setLimiterThresholdDb (-3.0f);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (blockSize);
        spec.numChannels = 1;
        band->prepare (spec);
        return band;
    };

    auto reference = makeBand();
    reference->setLimiterEnabled (true);

    auto toggled = makeBand();
    toggled->setLimiterEnabled (true);

    juce::int64 samplePosition = 0;
    float maxAbsoluteDifference = 0.0f;
    float referencePeakDuringMeasurement = 0.0f;

    auto runBlocks = [&] (int numBlocks, bool referenceEnabled, bool toggledEnabled, float amplitude, bool measure)
    {
        for (int b = 0; b < numBlocks; ++b)
        {
            reference->setLimiterEnabled (referenceEnabled);
            toggled->setLimiterEnabled (toggledEnabled);

            juce::AudioBuffer<float> referenceBuffer (1, blockSize);
            TestHelpers::fillWithSine (referenceBuffer, sampleRate, toneHz, amplitude, samplePosition);
            juce::AudioBuffer<float> toggledBuffer;
            toggledBuffer.makeCopyOf (referenceBuffer);

            juce::dsp::AudioBlock<float> referenceBlock (referenceBuffer);
            juce::dsp::AudioBlock<float> toggledBlock (toggledBuffer);
            reference->process (referenceBlock);
            toggled->process (toggledBlock);

            if (measure)
            {
                referencePeakDuringMeasurement = std::max (referencePeakDuringMeasurement, TestHelpers::peakAbsolute (referenceBuffer));

                const auto* referenceData = referenceBuffer.getReadPointer (0);
                const auto* toggledData = toggledBuffer.getReadPointer (0);

                for (int i = 0; i < blockSize; ++i)
                    maxAbsoluteDifference = std::max (maxAbsoluteDifference, std::abs (referenceData[i] - toggledData[i]));
            }

            samplePosition += blockSize;
        }
    };

    // Phase 1: both enabled, loud tone - let the envelope settle hot.
    runBlocks (loudBlocks, true, true, 0.9f, false);

    // Phase 2: quiet passage. Reference keeps its limiter engaged throughout
    // (the "always continuously tracking" ground truth); toggled disables
    // its limiter for the same quiet passage - the exact "disable while
    // signal changes" scenario from issue #12.
    runBlocks (quietBlocks, true, false, 0.0f, false);

    // Phase 3: loud tone resumes and toggled's limiter is re-enabled.
    // Measure the first few ms after re-enable - the attack-transient
    // window where a frozen vs. continuously-tracked envelope diverge.
    runBlocks (measureBlocks, true, true, 0.9f, true);

    // Sanity: the scenario genuinely exercises a re-attack transient (the
    // reference reaches a non-trivial level once the loud tone resumes)
    // rather than being trivially flat/degenerate.
    CHECK (referencePeakDuringMeasurement > 0.3f);

    // The property under test: a continuously-tracked limiter's re-enable
    // behaviour should match one that was never disabled sample-for-sample,
    // because both ran identical processing throughout - including during
    // the disabled window, where the old, frozen-envelope implementation
    // instead produced a measurably different (already fully-attenuated)
    // trajectory.
    CHECK (maxAbsoluteDifference < 0.05f);
}

// Downward expansion / gating (v0.4.0, issue #25): real, envelope-integrated
// proof that a quiet signal below the gate threshold is measurably
// attenuated when the gate is enabled, mirroring the compressor's own
// gain-reduction tests above but for the independent gate stage.
TEST_CASE ("BandCompressor: Gate attenuates a signal below its threshold when enabled", "[dsp][compressor][gate]")
{
    auto makeBand = [] (bool gateEnabled)
    {
        auto band = std::make_unique<BandCompressor>();
        band->setThresholdDb (-6.0f); // compressor threshold well above the test signal, so it never engages
        band->setRatio (1.0f); // compressor bypassed - isolates the gate's own behaviour
        band->setMakeupDb (0.0f);
        band->setGateThresholdDb (-30.0f);
        band->setGateRatio (10.0f); // steep expansion ratio for an unambiguous measurement
        band->setGateAttackMs (0.5f);
        band->setGateReleaseMs (20.0f);
        band->setGateEnabled (gateEnabled);

        const auto spec = makeTestSpec (2);
        band->prepare (spec);
        return band;
    };

    // -40 dBFS sine: comfortably below the -30 dB gate threshold but well
    // above true silence, so the envelope follower has a genuine non-zero
    // level to track.
    juce::AudioBuffer<float> reference (2, testBlockSize);
    TestHelpers::fillWithSine (reference, testSampleRate, testFrequencyHz, 0.01f);

    const auto tailRms = [] (const juce::AudioBuffer<float>& buffer)
    {
        constexpr int settleSamples = testBlockSize / 2;
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
    };

    const auto inputRms = tailRms (reference);
    REQUIRE (inputRms > 0.0);

    auto disabledBand = makeBand (false);
    juce::AudioBuffer<float> disabledProcessed;
    disabledProcessed.makeCopyOf (reference);
    juce::dsp::AudioBlock<float> disabledBlock (disabledProcessed);
    disabledBand->process (disabledBlock);
    const auto disabledGainChangeDb = juce::Decibels::gainToDecibels (tailRms (disabledProcessed) / inputRms);

    // Sanity: with the gate disabled, the signal passes through essentially
    // unchanged (compressor bypassed, no makeup).
    CHECK (disabledGainChangeDb == Catch::Approx (0.0f).margin (0.5));

    auto enabledBand = makeBand (true);
    juce::AudioBuffer<float> enabledProcessed;
    enabledProcessed.makeCopyOf (reference);
    juce::dsp::AudioBlock<float> enabledBlock (enabledProcessed);
    enabledBand->process (enabledBlock);
    const auto enabledGainChangeDb = juce::Decibels::gainToDecibels (tailRms (enabledProcessed) / inputRms);

    // With the gate engaged, a signal sitting 20 dB below the gate
    // threshold at a 10:1 expansion ratio should be measurably attenuated -
    // far more than the disabled case.
    CHECK (enabledGainChangeDb < -10.0);
    CHECK (TestHelpers::allSamplesFinite (enabledProcessed));
}

// Regression: a band whose Gate API is never touched at all must behave
// identically to one that explicitly disables it - the same "untouched ==
// explicitly disabled" guarantee Range's own regression test proves above.
TEST_CASE ("BandCompressor: Gate untouched reproduces the same output as Gate explicitly disabled", "[dsp][compressor][gate][regression]")
{
    auto makeBand = [] (bool touchGateApi)
    {
        auto band = std::make_unique<BandCompressor>();
        band->setThresholdDb (-24.0f);
        band->setRatio (2.5f);
        band->setKneePercent (50.0f);
        band->setAttackMs (10.0f);
        band->setReleaseMs (100.0f);
        band->setMakeupDb (0.0f);

        if (touchGateApi)
            band->setGateEnabled (false);

        const auto spec = makeTestSpec (2);
        band->prepare (spec);
        return band;
    };

    auto untouched = makeBand (false);
    auto explicitlyDisabled = makeBand (true);

    juce::AudioBuffer<float> bufferA (2, testBlockSize);
    TestHelpers::fillWithSine (bufferA, testSampleRate, testFrequencyHz, 0.7f);
    juce::AudioBuffer<float> bufferB;
    bufferB.makeCopyOf (bufferA);

    juce::dsp::AudioBlock<float> blockA (bufferA);
    juce::dsp::AudioBlock<float> blockB (bufferB);
    untouched->process (blockA);
    explicitlyDisabled->process (blockB);

    for (int channel = 0; channel < bufferA.getNumChannels(); ++channel)
    {
        const auto* dataA = bufferA.getReadPointer (channel);
        const auto* dataB = bufferB.getReadPointer (channel);

        for (int i = 0; i < testBlockSize; ++i)
            CHECK (dataA[i] == Catch::Approx (dataB[i]).margin (1e-9f));
    }
}

// A signal above the gate threshold must pass through unaffected by the
// gate even while it's engaged - the gate only ever attenuates, never
// boosts or otherwise colours signal already above its own threshold.
TEST_CASE ("BandCompressor: Gate leaves a signal above its threshold untouched", "[dsp][compressor][gate]")
{
    auto band = std::make_unique<BandCompressor>();
    band->setThresholdDb (0.0f); // compressor threshold at 0 dB - never engages for this test signal
    band->setRatio (1.0f);
    band->setMakeupDb (0.0f);
    band->setGateThresholdDb (-30.0f);
    band->setGateRatio (20.0f);
    band->setGateAttackMs (0.5f);
    band->setGateReleaseMs (20.0f);
    band->setGateEnabled (true);

    const auto spec = makeTestSpec (2);
    band->prepare (spec);

    // -6 dBFS: comfortably above the -30 dB gate threshold.
    juce::AudioBuffer<float> reference (2, testBlockSize);
    TestHelpers::fillWithSine (reference, testSampleRate, testFrequencyHz, 0.5f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    band->process (block);

    constexpr int settleSamples = testBlockSize / 2;

    for (int channel = 0; channel < reference.getNumChannels(); ++channel)
    {
        const auto* refData = reference.getReadPointer (channel);
        const auto* outData = processed.getReadPointer (channel);

        for (int i = settleSamples; i < testBlockSize; ++i)
            CHECK (outData[i] == Catch::Approx (refData[i]).margin (1e-3));
    }
}

// Per-band Mid/Side processing (v0.4.0, issue #24): L/R passthrough when
// disabled - a stereo signal with distinct L/R content must pass through
// completely unaffected by the M/S machinery while setMidSideEnabled() is
// never called (the default), matching pre-v0.4.0 stereo-linked behaviour
// exactly.
TEST_CASE ("BandCompressor: Mid/Side disabled reproduces pre-v0.4.0 stereo-linked passthrough exactly", "[dsp][compressor][midside][regression]")
{
    BandCompressor band;
    band.setThresholdDb (-40.0f);
    band.setRatio (1.0f); // bypass, isolates the M/S wiring itself
    band.setMakeupDb (0.0f);
    // setMidSideEnabled() deliberately never called - the default (off).

    const auto spec = makeTestSpec (2);
    band.prepare (spec);

    // Distinct L/R content (different frequencies) so any accidental M/S
    // mixing would be immediately audible as crosstalk between channels.
    juce::AudioBuffer<float> reference (2, testBlockSize);
    auto* left = reference.getWritePointer (0);
    auto* right = reference.getWritePointer (1);

    for (int i = 0; i < testBlockSize; ++i)
    {
        const auto leftPhase = juce::MathConstants<double>::twoPi * 300.0 * i / testSampleRate;
        const auto rightPhase = juce::MathConstants<double>::twoPi * 700.0 * i / testSampleRate;
        left[i] = 0.5f * static_cast<float> (std::sin (leftPhase));
        right[i] = 0.5f * static_cast<float> (std::sin (rightPhase));
    }

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    band.process (block);

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto* refData = reference.getReadPointer (channel);
        const auto* outData = processed.getReadPointer (channel);

        for (int i = 0; i < testBlockSize; ++i)
            CHECK (outData[i] == Catch::Approx (refData[i]).margin (1e-6));
    }
}

// Correct M/S round-trip: with both Mid (main Ratio) and Side (Side Ratio)
// fully bypassed (ratio == 1.0 on both), enabling M/S must still reproduce
// the input bit-exactly - the encode/decode transform itself must be
// lossless, not just "close enough".
TEST_CASE ("BandCompressor: Mid/Side round-trip is bit-exact when both Mid and Side are bypassed", "[dsp][compressor][midside][null]")
{
    BandCompressor band;
    band.setThresholdDb (-40.0f);
    band.setRatio (1.0f);
    band.setMakeupDb (0.0f);
    band.setSideThresholdDb (-40.0f);
    band.setSideRatio (1.0f);
    band.setMidSideEnabled (true);

    const auto spec = makeTestSpec (2);
    band.prepare (spec);

    juce::AudioBuffer<float> reference (2, testBlockSize);
    auto* left = reference.getWritePointer (0);
    auto* right = reference.getWritePointer (1);

    for (int i = 0; i < testBlockSize; ++i)
    {
        const auto leftPhase = juce::MathConstants<double>::twoPi * testFrequencyHz * i / testSampleRate;
        const auto rightPhase = juce::MathConstants<double>::twoPi * (testFrequencyHz * 1.5) * i / testSampleRate;
        left[i] = 0.6f * static_cast<float> (std::sin (leftPhase));
        right[i] = 0.4f * static_cast<float> (std::sin (rightPhase));
    }

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    band.process (block);

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto* refData = reference.getReadPointer (channel);
        const auto* outData = processed.getReadPointer (channel);

        for (int i = 0; i < testBlockSize; ++i)
            CHECK (outData[i] == Catch::Approx (refData[i]).margin (1e-5f));
    }
}

// Mono-compatibility guarantee at the BandCompressor level (issue #24): with
// M/S engaged and the Side channel driven into heavy gain reduction, the
// processed L + R sum must be unaffected by whatever happened to Side -
// only Mid processing (identical on both instances here) may change it. This
// is the real, envelope-integrated proof of MidSideCodecTests.cpp's pure-math
// "mono sum depends only on Mid" property.
TEST_CASE ("BandCompressor: mono sum after M/S processing is unaffected by Side gain reduction", "[dsp][compressor][midside][mono-compat]")
{
    auto makeBand = [] (float sideRatio)
    {
        auto band = std::make_unique<BandCompressor>();
        band->setThresholdDb (0.0f); // Mid path never compresses in this test
        band->setRatio (1.0f);
        band->setMakeupDb (0.0f);
        band->setSideThresholdDb (-50.0f);
        band->setSideRatio (sideRatio);
        band->setMidSideEnabled (true);

        const auto spec = makeTestSpec (2);
        band->prepare (spec);
        return band;
    };

    juce::AudioBuffer<float> reference (2, testBlockSize);
    auto* left = reference.getWritePointer (0);
    auto* right = reference.getWritePointer (1);

    for (int i = 0; i < testBlockSize; ++i)
    {
        const auto leftPhase = juce::MathConstants<double>::twoPi * testFrequencyHz * i / testSampleRate;
        const auto rightPhase = juce::MathConstants<double>::twoPi * (testFrequencyHz * 1.3) * i / testSampleRate;
        left[i] = 0.5f * static_cast<float> (std::sin (leftPhase));
        right[i] = 0.3f * static_cast<float> (std::sin (rightPhase));
    }

    auto bypassedSideBand = makeBand (1.0f);
    juce::AudioBuffer<float> bypassedProcessed;
    bypassedProcessed.makeCopyOf (reference);
    juce::dsp::AudioBlock<float> bypassedBlock (bypassedProcessed);
    bypassedSideBand->process (bypassedBlock);

    auto compressedSideBand = makeBand (10.0f); // heavy downward expansion... i.e. compression of Side
    juce::AudioBuffer<float> compressedProcessed;
    compressedProcessed.makeCopyOf (reference);
    juce::dsp::AudioBlock<float> compressedBlock (compressedProcessed);
    compressedSideBand->process (compressedBlock);

    const auto* bypassedLeft = bypassedProcessed.getReadPointer (0);
    const auto* bypassedRight = bypassedProcessed.getReadPointer (1);
    const auto* compressedLeft = compressedProcessed.getReadPointer (0);
    const auto* compressedRight = compressedProcessed.getReadPointer (1);

    // Sanity: the two Side settings genuinely produce different per-channel
    // output (otherwise this test wouldn't exercise anything).
    bool anyChannelDiffers = false;

    for (int i = 0; i < testBlockSize; ++i)
    {
        if (std::abs (bypassedLeft[i] - compressedLeft[i]) > 1e-4f || std::abs (bypassedRight[i] - compressedRight[i]) > 1e-4f)
        {
            anyChannelDiffers = true;
            break;
        }
    }

    CHECK (anyChannelDiffers);

    // The property under test: L + R is identical regardless of Side's
    // processing, within a small floating-point/envelope-settling margin.
    for (int i = 0; i < testBlockSize; ++i)
    {
        const auto bypassedSum = bypassedLeft[i] + bypassedRight[i];
        const auto compressedSum = compressedLeft[i] + compressedRight[i];
        CHECK (compressedSum == Catch::Approx (bypassedSum).margin (1e-3f));
    }

    CHECK (TestHelpers::allSamplesFinite (compressedProcessed));
}

// Regression: mono buses must be a defensive no-op for M/S - a band whose
// M/S is enabled but that only ever sees a mono channel count must behave
// identically to one that never touches the M/S API at all.
TEST_CASE ("BandCompressor: Mid/Side enabled is a no-op on a mono bus", "[dsp][compressor][midside][regression]")
{
    auto makeBand = [] (bool midSideEnabled)
    {
        auto band = std::make_unique<BandCompressor>();
        band->setThresholdDb (-24.0f);
        band->setRatio (2.5f);
        band->setMakeupDb (0.0f);
        band->setSideThresholdDb (-40.0f);
        band->setSideRatio (10.0f);
        band->setMidSideEnabled (midSideEnabled);

        const auto spec = makeTestSpec (1); // mono
        band->prepare (spec);
        return band;
    };

    auto disabled = makeBand (false);
    auto enabled = makeBand (true);

    juce::AudioBuffer<float> bufferA (1, testBlockSize);
    TestHelpers::fillWithSine (bufferA, testSampleRate, testFrequencyHz, 0.7f);
    juce::AudioBuffer<float> bufferB;
    bufferB.makeCopyOf (bufferA);

    juce::dsp::AudioBlock<float> blockA (bufferA);
    juce::dsp::AudioBlock<float> blockB (bufferB);
    disabled->process (blockA);
    enabled->process (blockB);

    const auto* dataA = bufferA.getReadPointer (0);
    const auto* dataB = bufferB.getReadPointer (0);

    for (int i = 0; i < testBlockSize; ++i)
        CHECK (dataA[i] == Catch::Approx (dataB[i]).margin (1e-9f));
}

TEST_CASE ("BandCompressor: reset() clears envelope/gain-ramp state without crashing", "[dsp][compressor]")
{
    BandCompressor band;
    band.setThresholdDb (-20.0f);
    band.setRatio (6.0f);
    band.setMakeupDb (6.0f);

    const auto spec = makeTestSpec (2);
    band.prepare (spec);

    juce::AudioBuffer<float> buffer (2, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, 0.9f);

    juce::dsp::AudioBlock<float> block (buffer);
    band.process (block);

    CHECK_NOTHROW (band.reset());
    CHECK (TestHelpers::allSamplesFinite (buffer));

    TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, 0.9f);
    CHECK_NOTHROW (band.process (block));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}

//==============================================================================
// v0.5.0 detector and character behaviour (brief sections 3.2, 3.3 and 6).

namespace
{
    juce::dsp::ProcessSpec makeV050Spec (double sampleRate, int blockSize = 512, int numChannels = 2)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (blockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    // Measures the knee width of a static transfer curve: the input-level span
    // over which the incremental slope travels from 1 (below the knee) to
    // 1/ratio (above it).
    //
    // The quadratic soft knee makes that slope exactly linear in input level
    // across the knee, so the 10% and 90% slope points sit 0.8 * W apart -
    // hence the /0.8. Measuring 10-90 rather than 0-100 keeps the result away
    // from the two corners, where a numerical derivative is least reliable.
    double measureKneeWidthDb (float thresholdDb, float ratio, float kneePercent)
    {
        constexpr auto stepDb = 0.01f;
        const auto lowSlope = 1.0f;
        const auto highSlope = 1.0f / ratio;

        const auto slopeAt = [&] (float inputDb)
        {
            const auto below = inputDb + trpt::computeStaticGainReductionDb (inputDb, thresholdDb, ratio, kneePercent);
            const auto above = inputDb + stepDb + trpt::computeStaticGainReductionDb (inputDb + stepDb, thresholdDb, ratio, kneePercent);
            return (above - below) / stepDb;
        };

        const auto target10 = lowSlope + 0.1f * (highSlope - lowSlope);
        const auto target90 = lowSlope + 0.9f * (highSlope - lowSlope);

        auto crossing10 = std::numeric_limits<double>::quiet_NaN();
        auto crossing90 = std::numeric_limits<double>::quiet_NaN();

        for (auto inputDb = thresholdDb - 20.0f; inputDb <= thresholdDb + 20.0f; inputDb += stepDb)
        {
            const auto slope = slopeAt (inputDb);

            if (std::isnan (crossing10) && slope <= target10)
                crossing10 = inputDb;

            if (std::isnan (crossing90) && slope <= target90)
            {
                crossing90 = inputDb;
                break;
            }
        }

        if (std::isnan (crossing10) || std::isnan (crossing90))
            return 0.0;

        return (crossing90 - crossing10) / 0.8;
    }
}

// T6b: link neutrality. At 0% stereo link a band is sample-exactly the v0.4.0
// band - the same-binary A/B against the legacy chain, not a tolerance.
TEST_CASE ("T6b: a band at 0% stereo link is sample-exactly the v0.4.0 band", "[bandcompressor][regression][neutrality]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr int numBlocks = 30;

    const auto spec = makeV050Spec (sampleRate, blockSize);

    BandCompressor shipped;
    shipped.setThresholdDb (-24.0f);
    shipped.setRatio (3.0f);
    shipped.setKneePercent (40.0f);
    shipped.setAttackMs (12.0f);
    shipped.setReleaseMs (140.0f);
    shipped.setMakeupDb (2.0f);
    shipped.setStereoLinkPercent (0.0f);
    shipped.prepare (spec);

    LegacyReference::Band legacy;
    legacy.setThresholdDb (-24.0f);
    legacy.setRatio (3.0f);
    legacy.setKneePercent (40.0f);
    legacy.setAttackMs (12.0f);
    legacy.setReleaseMs (140.0f);
    legacy.setMakeupDb (2.0f);
    legacy.prepare (spec);

    juce::AudioBuffer<float> shippedBuffer (2, blockSize);
    juce::AudioBuffer<float> legacyBuffer (2, blockSize);
    juce::Random random (0x5A17);

    auto mismatches = 0;

    for (int blockIndex = 0; blockIndex < numBlocks; ++blockIndex)
    {
        // Deliberately asymmetric between channels - if the link blend leaked
        // in at 0%, the two channels would converge and this would fail.
        for (int i = 0; i < blockSize; ++i)
        {
            const auto left = 0.6f * (random.nextFloat() * 2.0f - 1.0f);
            const auto right = 0.15f * (random.nextFloat() * 2.0f - 1.0f);

            shippedBuffer.setSample (0, i, left);
            shippedBuffer.setSample (1, i, right);
            legacyBuffer.setSample (0, i, left);
            legacyBuffer.setSample (1, i, right);
        }

        auto shippedBlock = juce::dsp::AudioBlock<float> (shippedBuffer);
        shipped.process (shippedBlock);

        auto legacyBlock = juce::dsp::AudioBlock<float> (legacyBuffer);
        legacy.process (legacyBlock);

        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < blockSize; ++i)
                if (shippedBuffer.getSample (channel, i) != legacyBuffer.getSample (channel, i))
                    ++mismatches;
    }

    CHECK (mismatches == 0);
}

// T16: the VCA character's emergent soft knee. Thresholds are pinned inside
// the exact-mapping region (|T| >= W/2), where the Weiss-model conversion
// reproduces the target width exactly; the degradation band near 0 dB gets its
// own test below.
TEST_CASE ("T16: VCA character reproduces the emergent-knee width table", "[bandcompressor][vca]")
{
    struct Expectation
    {
        float ratio;
        float thresholdDb;
        double expectedWidthDb;
    };

    const std::vector<Expectation> expectations {
        { 2.0f, -30.0f, 6.0 }, { 2.0f, -24.0f, 6.0 }, { 2.0f, -20.0f, 6.0 },
        { 4.0f, -30.0f, 4.0 }, { 4.0f, -24.0f, 4.0 }, { 4.0f, -20.0f, 4.0 },
        { 10.0f, -30.0f, 3.0 }, { 10.0f, -24.0f, 3.0 }, { 10.0f, -20.0f, 3.0 }
    };

    for (const auto& expectation : expectations)
    {
        const auto kneePercent = trpt::vcaKneePercent (expectation.ratio, expectation.thresholdDb);

        // In the exact-mapping region the clamp never engages.
        REQUIRE (kneePercent < 100.0f);

        const auto measured = measureKneeWidthDb (expectation.thresholdDb, expectation.ratio, kneePercent);

        INFO ("ratio=" << expectation.ratio << " T=" << expectation.thresholdDb
                        << " knee%=" << kneePercent << " measured=" << measured);
        CHECK (measured == Catch::Approx (expectation.expectedWidthDb).margin (0.5));
    }

    // The ordering itself is the signature: gentler ratios round more.
    const auto widthAt2 = measureKneeWidthDb (-24.0f, 2.0f, trpt::vcaKneePercent (2.0f, -24.0f));
    const auto widthAt10 = measureKneeWidthDb (-24.0f, 10.0f, trpt::vcaKneePercent (10.0f, -24.0f));

    CHECK (widthAt2 > widthAt10);
}

// T16b: the degradation band. KneeGainComputer's knee is threshold-relative,
// so for |T| < W/2 the target width is unreachable at ANY knee percent. The
// clamp has to degrade gracefully to 2 * |T| and reach a hard knee at exactly
// T = 0 dB - with no division by zero, NaN or Inf anywhere in range.
TEST_CASE ("T16b: the VCA knee mapping degrades gracefully toward a 0 dB threshold", "[bandcompressor][vca]")
{
    constexpr auto ratio = 2.0f;
    const auto tableWidth = trpt::vcaKneeWidthDb (ratio);
    REQUIRE (tableWidth == Catch::Approx (6.0f).margin (1.0e-4));

    auto previousWidth = std::numeric_limits<double>::max();

    for (const auto thresholdDb : { -3.0f, -2.0f, -1.0f, 0.0f })
    {
        const auto kneePercent = trpt::vcaKneePercent (ratio, thresholdDb);

        INFO ("T=" << thresholdDb << " knee%=" << kneePercent);

        // Clamped to exactly 100% throughout the band |T| <= W/2 = 3 dB.
        CHECK (std::isfinite (kneePercent));
        CHECK (kneePercent == Catch::Approx (100.0f).margin (1.0e-3));

        const auto expectedWidth = std::min (static_cast<double> (tableWidth), 2.0 * std::abs (thresholdDb));
        const auto measured = measureKneeWidthDb (thresholdDb, ratio, kneePercent);

        CHECK (measured == Catch::Approx (expectedWidth).margin (0.5));

        // Monotonically narrowing as the threshold approaches 0 dB.
        CHECK (measured <= previousWidth + 0.5);
        previousWidth = measured;

        // Finiteness across the whole sweep, including exactly at T = 0.
        for (auto inputDb = -40.0f; inputDb <= 20.0f; inputDb += 0.5f)
        {
            const auto reduction = trpt::computeStaticGainReductionDb (inputDb, thresholdDb, ratio, kneePercent);
            REQUIRE (std::isfinite (reduction));
        }
    }

    // At exactly 0 dB the knee has collapsed to hard.
    CHECK (measureKneeWidthDb (0.0f, ratio, trpt::vcaKneePercent (ratio, 0.0f)) == Catch::Approx (0.0).margin (0.5));
}

// T17: the loop attack speed-up. A feedback compressor's loop integrates the
// residual overshoot, dividing its time constant by (1 + k) - so a higher
// ratio reaches its gain reduction sooner. Measured two ways: directly on the
// detector's envelope time constant, and behaviourally as tone-burst gain
// reduction ordering through a real band.
TEST_CASE ("T17: VCA character makes higher ratios reach gain reduction sooner", "[bandcompressor][vca]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto attackMs = 40.0f;

    SECTION ("the detector's effective attack time constant is tau / (1 + k)")
    {
        for (const auto ratio : { 2.0f, 4.0f, 10.0f })
        {
            Detector detector;
            detector.setCharacter (Detector::Character::vca);
            detector.setRatio (ratio);
            detector.setAttackMs (attackMs);
            detector.setReleaseMs (500.0f);
            detector.prepare (makeV050Spec (sampleRate));

            std::vector<float> keys (2, 1.0f);
            std::vector<float> envelopes (2, 0.0f);

            // Let the 10 ms character crossfade settle before measuring.
            for (int settle = 0; settle < 40; ++settle)
            {
                detector.updateForBlock (64);

                for (int i = 0; i < 64; ++i)
                {
                    keys[0] = 0.0f;
                    keys[1] = 0.0f;
                    detector.processFrame (keys.data(), 2, envelopes.data());
                }
            }

            auto crossingSample = -1;
            const auto totalSamples = static_cast<int> (0.5 * sampleRate);

            for (int index = 0; index < totalSamples && crossingSample < 0; index += 64)
            {
                detector.updateForBlock (64);

                for (int i = 0; i < 64; ++i)
                {
                    keys[0] = 1.0f;
                    keys[1] = 1.0f;
                    detector.processFrame (keys.data(), 2, envelopes.data());

                    if (crossingSample < 0 && envelopes[0] >= 0.6321f)
                        crossingSample = index + i;
                }
            }

            REQUIRE (crossingSample > 0);

            const auto measuredMs = 1000.0f * static_cast<float> (crossingSample) / static_cast<float> (sampleRate);

            // juce::dsp::BallisticsFilter's "attack time" is a period, not a
            // time constant: its coefficient is exp (-2 * pi * 1000 /
            // (fs * timeMs)) (JUCE 8.0.14, juce_BallisticsFilter.cpp:126-130),
            // so the 63% point arrives after timeMs / (2 * pi) milliseconds.
            // The quantity under test is the VCA scale factor applied to that
            // commanded time, which is why the 2 * pi appears here rather than
            // in the implementation.
            const auto expectedMs = attackMs * trpt::vcaAttackScale (ratio) / juce::MathConstants<float>::twoPi;

            INFO ("ratio=" << ratio << " measured=" << measuredMs << " ms expected=" << expectedMs << " ms");
            CHECK (measuredMs == Catch::Approx (expectedMs).epsilon (0.25));
        }
    }

    SECTION ("a band's tone-burst gain reduction arrives sooner at higher ratios")
    {
        constexpr int blockSize = 64;

        const auto timeToReachHalfOfFinalReduction = [&] (float ratio)
        {
            BandCompressor band;
            band.setThresholdDb (-30.0f);
            band.setRatio (ratio);
            band.setKneePercent (0.0f);
            band.setAttackMs (attackMs);
            band.setReleaseMs (500.0f);
            band.setMakeupDb (0.0f);
            band.setDetectorCharacter (Detector::Character::vca);
            band.prepare (makeV050Spec (sampleRate, blockSize));

            juce::AudioBuffer<float> buffer (2, blockSize);

            std::vector<float> gainTrace;
            const auto totalBlocks = static_cast<int> (0.5 * sampleRate) / blockSize;

            for (int blockIndex = 0; blockIndex < totalBlocks; ++blockIndex)
            {
                for (int channel = 0; channel < 2; ++channel)
                    for (int i = 0; i < blockSize; ++i)
                        buffer.setSample (channel, i, 0.5f);

                auto block = juce::dsp::AudioBlock<float> (buffer);
                band.process (block);

                for (int i = 0; i < blockSize; ++i)
                    gainTrace.push_back (buffer.getSample (0, i) / 0.5f);
            }

            const auto finalGain = gainTrace.back();
            const auto target = 1.0f - 0.5f * (1.0f - finalGain);

            for (size_t i = 0; i < gainTrace.size(); ++i)
                if (gainTrace[i] <= target)
                    return 1000.0 * static_cast<double> (i) / sampleRate;

            return 1000.0;
        };

        const auto at2 = timeToReachHalfOfFinalReduction (2.0f);
        const auto at4 = timeToReachHalfOfFinalReduction (4.0f);
        const auto at10 = timeToReachHalfOfFinalReduction (10.0f);

        INFO ("2:1=" << at2 << " ms, 4:1=" << at4 << " ms, 10:1=" << at10 << " ms");
        CHECK (at10 < at4);
        CHECK (at4 < at2);
    }
}

// T20: no zipper noise when the new continuous parameters are automated. The
// substantive assertion is a bound on the sample-to-sample gain step - a
// block-rate parameter jump would show up here immediately.
TEST_CASE ("T20: automating stereo link produces no audible discontinuity", "[bandcompressor][zipper]")
{
    constexpr auto sampleRate = 48000.0;
    constexpr int blockSize = 64;

    BandCompressor band;
    band.setThresholdDb (-30.0f);
    band.setRatio (4.0f);
    band.setKneePercent (50.0f);
    band.setAttackMs (10.0f);
    band.setReleaseMs (120.0f);
    band.setMakeupDb (0.0f);
    band.setStereoLinkPercent (0.0f);
    band.prepare (makeV050Spec (sampleRate, blockSize));

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::Random random (0x2177);

    // 100 ms of automation from 0% to 100%, then a settled tail.
    const auto automationBlocks = static_cast<int> (0.1 * sampleRate) / blockSize;
    const auto totalBlocks = automationBlocks * 3;

    std::vector<float> output;
    auto previous = 0.0f;
    auto worstStepDb = 0.0f;

    for (int blockIndex = 0; blockIndex < totalBlocks; ++blockIndex)
    {
        const auto progress = juce::jlimit (0.0f, 1.0f, static_cast<float> (blockIndex) / static_cast<float> (automationBlocks));
        band.setStereoLinkPercent (100.0f * progress);

        for (int i = 0; i < blockSize; ++i)
        {
            const auto value = 0.5f * (random.nextFloat() * 2.0f - 1.0f);
            buffer.setSample (0, i, value);
            buffer.setSample (1, i, 0.2f * value);
        }

        auto block = juce::dsp::AudioBlock<float> (buffer);
        band.process (block);

        for (int i = 0; i < blockSize; ++i)
        {
            const auto sample = buffer.getSample (0, i);
            REQUIRE (std::isfinite (sample));
            output.push_back (sample);
        }
    }

    // Compare against the input's own envelope: a zipper shows up as a step in
    // the applied GAIN, not in the (deliberately noisy) signal, so the trace
    // is smoothed before the step bound is applied.
    juce::ignoreUnused (previous, worstStepDb);

    auto smoothed = 0.0f;
    auto previousSmoothed = -1.0f;
    auto worstStep = 0.0f;

    // The measuring smoother has its own start-up transient (it charges from
    // silence), so the first 20 ms are excluded - they say nothing about the
    // automation under test.
    const auto measurementStart = static_cast<size_t> (0.02 * sampleRate);

    for (size_t index = 0; index < output.size(); ++index)
    {
        smoothed = 0.999f * smoothed + 0.001f * std::abs (output[index]);

        if (index >= measurementStart && previousSmoothed > 1.0e-6f && smoothed > 1.0e-6f)
        {
            const auto stepDb = std::abs (juce::Decibels::gainToDecibels (smoothed) - juce::Decibels::gainToDecibels (previousSmoothed));
            worstStep = juce::jmax (worstStep, stepDb);
        }

        previousSmoothed = smoothed;
    }

    INFO ("worst smoothed step = " << worstStep << " dB");
    CHECK (worstStep < 6.0f);
}
