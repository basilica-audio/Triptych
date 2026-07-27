#include "dsp/Detector.h"

#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

// Detector v2 (v0.5.0, src/dsp/Detector.h): the selectable Peak/RMS detection
// law, the dual-time-constant program-dependent auto release, and the
// variable stereo link. Every assertion here is a measurement on the
// envelope the detector actually produces, not a code-shape check.
namespace
{
    juce::dsp::ProcessSpec makeSpec (double sampleRate, int numChannels = 2, int blockSize = 512)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (blockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    // Runs `numSamples` of a sine through the detector one frame at a time and
    // returns channel 0's envelope trace.
    std::vector<float> renderSineEnvelope (Detector& detector,
                                            double sampleRate,
                                            double frequencyHz,
                                            float amplitude,
                                            int numSamples,
                                            int blockSize = 512)
    {
        std::vector<float> trace (static_cast<size_t> (numSamples), 0.0f);
        std::vector<float> keys (2, 0.0f);
        std::vector<float> envelopes (2, 0.0f);

        auto sampleIndex = 0;

        while (sampleIndex < numSamples)
        {
            const auto thisBlock = std::min (blockSize, numSamples - sampleIndex);
            detector.updateForBlock (thisBlock);

            for (int i = 0; i < thisBlock; ++i)
            {
                const auto phase = juce::MathConstants<double>::twoPi * frequencyHz
                                    * static_cast<double> (sampleIndex + i) / sampleRate;
                const auto value = amplitude * static_cast<float> (std::sin (phase));

                keys[0] = value;
                keys[1] = value;

                detector.processFrame (keys.data(), 2, envelopes.data());
                trace[static_cast<size_t> (sampleIndex + i)] = envelopes[0];
            }

            sampleIndex += thisBlock;
        }

        return trace;
    }

    float settledValue (const std::vector<float>& trace)
    {
        // Average the last 10 ms-ish worth of samples so the detector's own
        // residual ripple does not decide the assertion.
        const auto window = std::min<size_t> (trace.size(), 512);
        auto sum = 0.0;

        for (auto i = trace.size() - window; i < trace.size(); ++i)
            sum += trace[i];

        return static_cast<float> (sum / static_cast<double> (window));
    }
}

// T3: the two detection laws differ by exactly the sine's crest factor. For a
// sine of amplitude A, peak detection settles at A and RMS detection at
// A / sqrt(2) - 3.01 dB below it, the textbook difference and the whole point
// of offering the choice.
TEST_CASE ("T3: RMS detection reads 3.01 dB below peak detection on a steady sine", "[detector][rms]")
{
    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto spec = makeSpec (sampleRate);
        const auto numSamples = static_cast<int> (2.0 * sampleRate);

        // A fast attack with a slow release makes the peak path settle at the
        // waveform's actual peak rather than somewhere between peak and mean.
        Detector peakDetector;
        peakDetector.prepare (spec);
        peakDetector.setAttackMs (0.1f);
        peakDetector.setReleaseMs (1000.0f);
        peakDetector.setLaw (Detector::Law::peak);

        Detector rmsDetector;
        rmsDetector.prepare (spec);
        rmsDetector.setAttackMs (0.1f);
        rmsDetector.setReleaseMs (1000.0f);
        rmsDetector.setLaw (Detector::Law::rms);

        const auto peakTrace = renderSineEnvelope (peakDetector, sampleRate, 1000.0, 0.5f, numSamples);
        const auto rmsTrace = renderSineEnvelope (rmsDetector, sampleRate, 1000.0, 0.5f, numSamples);

        const auto peakDb = juce::Decibels::gainToDecibels (settledValue (peakTrace));
        const auto rmsDb = juce::Decibels::gainToDecibels (settledValue (rmsTrace));

        INFO ("sampleRate=" << sampleRate << " peakDb=" << peakDb << " rmsDb=" << rmsDb);
        CHECK ((peakDb - rmsDb) == Catch::Approx (3.01f).margin (0.5f));
    }
}

// T4: the RMS law's mean-square one-pole runs at tau_rms = max (attack, 5 ms).
// Stepping the input from -20 dBFS to 0 dBFS, the mean-square envelope should
// cover 63.2% of its total excursion in exactly one tau_rms.
TEST_CASE ("T4: RMS step response reaches 63% of its final mean square in tau_rms", "[detector][rms]")
{
    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto spec = makeSpec (sampleRate);

        Detector detector;
        detector.prepare (spec);
        // Attack below the 5 ms floor, so tau_rms is the floor itself and the
        // ballistics stage tracks the mean-square rise essentially instantly.
        detector.setAttackMs (0.1f);
        detector.setReleaseMs (1000.0f);
        detector.setLaw (Detector::Law::rms);

        constexpr auto quietAmplitude = 0.1f;  // -20 dBFS
        constexpr auto loudAmplitude = 1.0f;   //   0 dBFS
        constexpr auto expectedTauSeconds = 0.005f;

        const auto settleSamples = static_cast<int> (0.5 * sampleRate);
        const auto stepSamples = static_cast<int> (0.05 * sampleRate);

        std::vector<float> keys (2, 0.0f);
        std::vector<float> envelopes (2, 0.0f);

        const auto pump = [&] (float amplitude, int numSamples, std::vector<float>* trace)
        {
            auto index = 0;

            while (index < numSamples)
            {
                const auto thisBlock = std::min (512, numSamples - index);
                detector.updateForBlock (thisBlock);

                for (int i = 0; i < thisBlock; ++i)
                {
                    const auto phase = juce::MathConstants<double>::twoPi * 1000.0
                                        * static_cast<double> (index + i) / sampleRate;
                    const auto value = amplitude * static_cast<float> (std::sin (phase));

                    keys[0] = value;
                    keys[1] = value;
                    detector.processFrame (keys.data(), 2, envelopes.data());

                    if (trace != nullptr)
                        trace->push_back (envelopes[0]);
                }

                index += thisBlock;
            }
        };

        pump (quietAmplitude, settleSamples, nullptr);

        const auto startMeanSquare = envelopes[0] * envelopes[0];

        std::vector<float> trace;
        trace.reserve (static_cast<size_t> (stepSamples));
        pump (loudAmplitude, stepSamples, &trace);

        const auto finalMeanSquare = 0.5f * loudAmplitude * loudAmplitude;
        const auto target = startMeanSquare + 0.6321f * (finalMeanSquare - startMeanSquare);

        auto crossingSample = -1;

        for (size_t i = 0; i < trace.size(); ++i)
        {
            if (trace[i] * trace[i] >= target)
            {
                crossingSample = static_cast<int> (i);
                break;
            }
        }

        REQUIRE (crossingSample > 0);

        const auto measuredTau = static_cast<float> (crossingSample) / static_cast<float> (sampleRate);

        INFO ("sampleRate=" << sampleRate << " measuredTau=" << measuredTau);
        CHECK (measuredTau == Catch::Approx (expectedTauSeconds).epsilon (0.2));
    }
}

// T5: program-dependent auto release. A brief peak has to recover on the fast
// branch alone; sustained gain reduction has to charge the slow reservoir and
// grow a materially longer tail; and the tail must be monotonic - the failure
// mode a naive parallel-envelope implementation produces is a "bounce" where
// the slow branch overtakes the fast one and the envelope rises again.
TEST_CASE ("T5: auto release recovers fast after a brief peak and slowly after sustained reduction", "[detector][autorelease]")
{
    constexpr auto sampleRate = 48000.0;
    const auto spec = makeSpec (sampleRate);

    // Measures how long the envelope takes to fall to 10% of the level it had
    // when the input stopped (i.e. 90% recovery), and whether it ever rose on
    // the way down.
    struct Recovery
    {
        double seconds = 0.0;
        bool monotonic = true;
    };

    const auto measure = [&spec] (double burstSeconds, double tailSeconds)
    {
        Detector detector;
        detector.prepare (spec);
        detector.setAttackMs (1.0f);
        // 300 ms is the reference point at which the branch constants are
        // reproduced exactly (scale factor 1.0).
        detector.setReleaseMs (300.0f);
        detector.setAutoReleaseEnabled (true);

        std::vector<float> keys (2, 0.0f);
        std::vector<float> envelopes (2, 0.0f);

        const auto burstSamples = static_cast<int> (burstSeconds * sampleRate);
        const auto tailSamples = static_cast<int> (tailSeconds * sampleRate);

        const auto pump = [&] (float amplitude, int numSamples, std::vector<float>* trace)
        {
            auto index = 0;

            while (index < numSamples)
            {
                const auto thisBlock = std::min (512, numSamples - index);
                detector.updateForBlock (thisBlock);

                for (int i = 0; i < thisBlock; ++i)
                {
                    keys[0] = amplitude;
                    keys[1] = amplitude;
                    detector.processFrame (keys.data(), 2, envelopes.data());

                    if (trace != nullptr)
                        trace->push_back (envelopes[0]);
                }

                index += thisBlock;
            }
        };

        pump (1.0f, burstSamples, nullptr);

        const auto peak = envelopes[0];
        REQUIRE (peak > 0.5f);

        std::vector<float> tail;
        tail.reserve (static_cast<size_t> (tailSamples));
        pump (0.0f, tailSamples, &tail);

        Recovery recovery;
        recovery.seconds = tailSeconds;

        for (size_t i = 0; i < tail.size(); ++i)
        {
            if (tail[i] <= 0.1f * peak)
            {
                recovery.seconds = static_cast<double> (i) / sampleRate;
                break;
            }
        }

        // Monotonicity on a 5 ms-smoothed trace, so the assertion is about
        // the envelope's shape rather than single-sample numerical noise.
        const auto window = static_cast<size_t> (0.005 * sampleRate);
        auto previous = std::numeric_limits<double>::max();

        for (size_t start = 0; start + window <= tail.size(); start += window)
        {
            auto sum = 0.0;

            for (size_t i = start; i < start + window; ++i)
                sum += tail[i];

            const auto average = sum / static_cast<double> (window);

            if (average > previous * 1.0001 + 1.0e-9)
                recovery.monotonic = false;

            previous = average;
        }

        return recovery;
    };

    const auto brief = measure (0.05, 2.0);
    const auto sustained = measure (10.0, 20.0);

    INFO ("brief=" << brief.seconds << "s sustained=" << sustained.seconds << "s");

    // (a) a 50 ms burst recovers on the fast branch.
    CHECK (brief.seconds < 0.8);

    // (b) sustained reduction charges the slow reservoir, at least doubling
    //     the recovery time.
    CHECK (sustained.seconds >= 2.0 * brief.seconds);

    // The tail never bounces in either case.
    CHECK (brief.monotonic);
    CHECK (sustained.monotonic);
}

// T6: the shared-gain invariant. At 100% stereo link the blend is applied to
// the detector *inputs*, so both channels integrate literally the same value
// sequence and their envelopes must agree bit for bit - not approximately.
// That is what guarantees a hard-panned transient can never shift the image.
TEST_CASE ("T6: 100% stereo link makes both channels' envelopes bit-identical", "[detector][stereolink]")
{
    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto spec = makeSpec (sampleRate);

        Detector detector;
        detector.prepare (spec);
        detector.setAttackMs (5.0f);
        detector.setReleaseMs (120.0f);
        detector.setStereoLinkPercent (100.0f);
        // Prime the link smoother at 100% (prepare re-applies the last
        // commanded value), so the very first sample is already fully linked.
        detector.prepare (spec);

        std::vector<float> keys (2, 0.0f);
        std::vector<float> envelopes (2, 0.0f);

        const auto numSamples = static_cast<int> (0.5 * sampleRate);
        auto index = 0;
        auto mismatches = 0;

        while (index < numSamples)
        {
            const auto thisBlock = std::min (512, numSamples - index);
            detector.updateForBlock (thisBlock);

            for (int i = 0; i < thisBlock; ++i)
            {
                const auto phase = juce::MathConstants<double>::twoPi * 220.0
                                    * static_cast<double> (index + i) / sampleRate;

                // Signal in the LEFT channel only - the classic image-wander
                // scenario.
                keys[0] = 0.8f * static_cast<float> (std::sin (phase));
                keys[1] = 0.0f;

                detector.processFrame (keys.data(), 2, envelopes.data());

                if (envelopes[0] != envelopes[1])
                    ++mismatches;
            }

            index += thisBlock;
        }

        INFO ("sampleRate=" << sampleRate);
        CHECK (mismatches == 0);
    }
}

// The complement of T6: at 0% link the two channels stay genuinely
// independent, which is the v0.4.0 behaviour every existing session has.
TEST_CASE ("Stereo link at 0% leaves the two channels independent", "[detector][stereolink]")
{
    constexpr auto sampleRate = 48000.0;
    const auto spec = makeSpec (sampleRate);

    Detector detector;
    detector.setStereoLinkPercent (0.0f);
    detector.prepare (spec);
    detector.setAttackMs (5.0f);
    detector.setReleaseMs (120.0f);

    std::vector<float> keys (2, 0.0f);
    std::vector<float> envelopes (2, 0.0f);

    detector.updateForBlock (512);

    // At 0% link the detector must also resolve to the structural neutral
    // path - the literal v0.4.0 BallisticsFilter call.
    CHECK (detector.isNeutral());

    auto separated = 0;

    for (int i = 0; i < 512; ++i)
    {
        keys[0] = 0.8f;
        keys[1] = 0.0f;

        detector.processFrame (keys.data(), 2, envelopes.data());

        if (envelopes[0] > envelopes[1])
            ++separated;
    }

    CHECK (separated > 400);
}

// The VCA-character helper tables, asserted directly against the published
// feedback-loop relation RATIO_fb = 1 + k that generated them.
TEST_CASE ("VCA character helpers reproduce the emergent-knee table and the loop attack speed-up", "[detector][vca]")
{
    SECTION ("emergent knee width, log-interpolated between the anchors")
    {
        CHECK (trpt::vcaKneeWidthDb (2.0f) == Catch::Approx (6.0f).margin (1.0e-4));
        CHECK (trpt::vcaKneeWidthDb (4.0f) == Catch::Approx (4.0f).margin (1.0e-4));
        CHECK (trpt::vcaKneeWidthDb (10.0f) == Catch::Approx (3.0f).margin (1.0e-4));

        // Clamped outside the anchor range, monotonically narrowing inside it.
        CHECK (trpt::vcaKneeWidthDb (1.2f) == Catch::Approx (6.0f).margin (1.0e-4));
        CHECK (trpt::vcaKneeWidthDb (20.0f) == Catch::Approx (3.0f).margin (1.0e-4));
        CHECK (trpt::vcaKneeWidthDb (3.0f) < trpt::vcaKneeWidthDb (2.0f));
        CHECK (trpt::vcaKneeWidthDb (3.0f) > trpt::vcaKneeWidthDb (4.0f));

        // Upward ratios mirror the table through 1 / ratio.
        CHECK (trpt::vcaKneeWidthDb (0.25f) == Catch::Approx (trpt::vcaKneeWidthDb (4.0f)).margin (1.0e-4));
    }

    SECTION ("effective attack scale 1 / (1 + k), k = ratio - 1")
    {
        CHECK (trpt::vcaAttackScale (2.0f) == Catch::Approx (1.0f / 2.0f).margin (1.0e-5));
        CHECK (trpt::vcaAttackScale (4.0f) == Catch::Approx (1.0f / 4.0f).margin (1.0e-5));
        CHECK (trpt::vcaAttackScale (10.0f) == Catch::Approx (1.0f / 10.0f).margin (1.0e-5));

        // Higher ratio => faster effective attack is the loop signature, so
        // the time-constant scale must SHRINK monotonically as ratio rises.
        CHECK (trpt::vcaAttackScale (2.0f) > trpt::vcaAttackScale (4.0f));
        CHECK (trpt::vcaAttackScale (4.0f) > trpt::vcaAttackScale (10.0f));

        // At 1:1 the loop is doing nothing, so the time constant must be left
        // essentially untouched - and k is floored at 0.01 so nothing divides
        // by zero there.
        CHECK (std::isfinite (trpt::vcaAttackScale (1.0f)));
        CHECK (trpt::vcaAttackScale (1.0f) == Catch::Approx (1.0f).margin (0.02f));
    }
}
