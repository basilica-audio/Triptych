#include "dsp/BandCompressor.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>

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
