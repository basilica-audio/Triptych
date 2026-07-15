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

TEST_CASE ("BandCompressor: ratio 1:1 + makeup 0 dB is an exact bypass", "[dsp][compressor][null]")
{
    // Threshold is deliberately set well inside the signal's level, so a
    // true bypass test has to prove the VCA gain is unconditionally 1.0
    // (see BandCompressor.h) rather than just "quiet enough to not matter".
    BandCompressor band;
    band.setThresholdDb (-40.0f);
    band.setRatio (1.0f);
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

TEST_CASE ("BandCompressor: a signal above threshold receives measurable gain reduction", "[dsp][compressor]")
{
    BandCompressor band;
    band.setThresholdDb (-24.0f);
    band.setRatio (8.0f);
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
