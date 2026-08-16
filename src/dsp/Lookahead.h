#pragma once

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <vector>

// Lookahead primitives (v0.5.0, .scaffold brief section 3.4). Header-only,
// every buffer sized in prepare(), nothing allocating or locking on the audio
// thread.
//
// Latency policy: a single global lookahead length L = round (lookaheadSeconds
// * fs) samples is reported once, via the AudioProcessor's setLatencySamples
// plumbing, and every band delays its audio by exactly that L - so the summed
// output stays sample-aligned across bands and the host's PDC value is exact
// and integral by construction (hosts accept integer sample counts only).
namespace trpt
{
    //==========================================================================
    // A fixed per-channel FIFO delay line. Capacity is allocated once for the
    // worst-case lookahead; the active delay is a plain integer that only ever
    // changes through a prepare-equivalent reconfigure on the message thread,
    // so no fractional/interpolating read is needed anywhere.
    class LookaheadDelay
    {
    public:
        LookaheadDelay() = default;

        void prepare (size_t numChannels, int maximumDelaySamples)
        {
            capacity = juce::jmax (1, maximumDelaySamples + 1);
            buffer.assign (numChannels * static_cast<size_t> (capacity), 0.0f);
            channels = numChannels;
            reset();
        }

        void reset()
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            writeIndex = 0;
        }

        // Real-time safe. Values above the prepared capacity are clamped.
        void setDelaySamples (int newDelaySamples) noexcept
        {
            const auto clamped = juce::jlimit (0, capacity - 1, newDelaySamples);

            if (clamped != delaySamples)
            {
                delaySamples = clamped;
                reset();
            }
        }

        int getDelaySamples() const noexcept { return delaySamples; }

        // Delays `block` in place by the current delay length. A delay of 0 is
        // a structural no-op: the block is not touched at all, so a
        // lookahead-Off instance is bit-identical to having no delay line in
        // the signal path.
        void processInPlace (juce::dsp::AudioBlock<float>& block) noexcept
        {
            if (delaySamples == 0)
                return;

            const auto numChannels = juce::jmin (block.getNumChannels(), channels);
            const auto numSamples = block.getNumSamples();
            const auto localWrite = writeIndex;

            for (size_t channel = 0; channel < numChannels; ++channel)
            {
                auto* data = block.getChannelPointer (channel);
                auto* line = buffer.data() + channel * static_cast<size_t> (capacity);
                auto write = localWrite;

                for (size_t sample = 0; sample < numSamples; ++sample)
                {
                    auto read = write - delaySamples;

                    if (read < 0)
                        read += capacity;

                    const auto delayed = line[read];
                    line[write] = data[sample];
                    data[sample] = delayed;

                    if (++write >= capacity)
                        write = 0;
                }
            }

            writeIndex = static_cast<int> ((static_cast<size_t> (localWrite) + numSamples) % static_cast<size_t> (capacity));
        }

    private:
        std::vector<float> buffer;
        size_t channels = 0;
        int capacity = 1;
        int delaySamples = 0;
        int writeIndex = 0;
    };

    //==========================================================================
    // Sliding minimum over the last `window` inputs, in amortised O(1) per
    // sample, via a monotonic wedge deque held in a fixed ring buffer
    // (implemented from the published description of the technique, no code
    // copied). The deque holds strictly increasing values; anything a new
    // sample undercuts can never be the minimum again and is popped.
    class SlidingMinimum
    {
    public:
        SlidingMinimum() = default;

        void prepare (int maximumWindow)
        {
            // One slot of headroom: a push can momentarily hold `window` old
            // entries plus the newcomer before the front is evicted.
            const auto capacity = static_cast<size_t> (juce::jmax (1, maximumWindow) + 1);
            values.assign (capacity, 0.0f);
            positions.assign (capacity, 0);
            reset (1.0f);
        }

        // Changing the window is a structural reconfigure, not a smoothly
        // rampable value, so the wedge is cleared with it - otherwise entries
        // stored under the old window can outlive their validity.
        void setWindow (int newWindow) noexcept
        {
            const auto clamped = juce::jlimit (1, juce::jmax (1, static_cast<int> (values.size()) - 1), newWindow);

            if (clamped == window)
                return;

            window = clamped;
            reset (primeValue);
        }

        void reset (float fillValue) noexcept
        {
            head = 0;
            count = 0;
            position = 0;
            primeValue = fillValue;
        }

        // Returns min (x[n - window + 1] ... x[n]).
        float process (float input) noexcept
        {
            const auto capacity = static_cast<int> (values.size());

            // Pop every stored value the newcomer undercuts - none of them can
            // ever win a minimum again.
            while (count > 0)
            {
                const auto tail = (head + count - 1) % capacity;

                if (values[static_cast<size_t> (tail)] < input)
                    break;

                --count;
            }

            const auto tail = (head + count) % capacity;
            values[static_cast<size_t> (tail)] = input;
            positions[static_cast<size_t> (tail)] = position;
            ++count;

            // Drop the front once it has aged out of the window.
            while (count > 0 && positions[static_cast<size_t> (head)] <= position - window)
            {
                head = (head + 1) % capacity;
                --count;
            }

            ++position;

            const auto currentMinimum = values[static_cast<size_t> (head)];

            // Before `window` samples have been seen the window is only
            // partially filled; prime the missing history with the reset value
            // (1.0 = no gain reduction) so start-up never invents attenuation.
            return position < window ? juce::jmin (currentMinimum, primeValue) : currentMinimum;
        }

    private:
        std::vector<float> values;
        std::vector<int> positions;
        int window = 1;
        int head = 0;
        int count = 0;
        int position = 0;
        float primeValue = 1.0f;
    };

    //==========================================================================
    // Two cascaded causal box (running-mean) filters of length `length`,
    // giving a triangular impulse response - a C1-smooth gain ramp with total
    // FIR support 2 * length - 1 samples and no extra latency of its own (the
    // means run over already-available history).
    class CascadedBoxSmoother
    {
    public:
        CascadedBoxSmoother() = default;

        void prepare (int maximumLength)
        {
            const auto capacity = static_cast<size_t> (juce::jmax (1, maximumLength));
            first.assign (capacity, 1.0f);
            second.assign (capacity, 1.0f);
            reset (1.0f);
        }

        // Both stages carry a running sum that must stay exactly equal to the
        // sum of their `length` stored samples. Changing the length while the
        // buffers hold history for a different length would break that
        // invariant (and, once the sums drift, produce gains above 1 - i.e.
        // amplification from a limiter), so the state is re-primed here.
        void setLength (int newLength) noexcept
        {
            const auto clamped = juce::jlimit (1, static_cast<int> (first.size()), newLength);

            if (clamped == length)
                return;

            length = clamped;
            reset (fillValue);
        }

        // Total FIR support in samples: the smoothed output at n depends on
        // inputs [n - getSupport(), n].
        int getSupport() const noexcept { return 2 * length - 2; }

        void reset (float newFillValue) noexcept
        {
            fillValue = newFillValue;

            std::fill (first.begin(), first.end(), fillValue);
            std::fill (second.begin(), second.end(), fillValue);
            firstIndex = 0;
            secondIndex = 0;
            firstSum = static_cast<double> (fillValue) * static_cast<double> (length);
            secondSum = static_cast<double> (fillValue) * static_cast<double> (length);
        }

        float process (float input) noexcept
        {
            firstSum += input - static_cast<double> (first[static_cast<size_t> (firstIndex)]);
            first[static_cast<size_t> (firstIndex)] = input;

            if (++firstIndex >= length)
                firstIndex = 0;

            const auto stage = static_cast<float> (firstSum / static_cast<double> (length));

            secondSum += stage - static_cast<double> (second[static_cast<size_t> (secondIndex)]);
            second[static_cast<size_t> (secondIndex)] = stage;

            if (++secondIndex >= length)
                secondIndex = 0;

            return static_cast<float> (secondSum / static_cast<double> (length));
        }

    private:
        std::vector<float> first;
        std::vector<float> second;
        int length = 1;
        int firstIndex = 0;
        int secondIndex = 0;
        double firstSum = 0.0;
        double secondSum = 0.0;
        float fillValue = 1.0f;
    };

    //==========================================================================
    // The overshoot-proof lookahead brickwall used by the High band's
    // "Mastering Safety Ceiling" when lookahead is engaged.
    //
    //   g_req[n] = min (1, threshold / max over channels |x[n]|)
    //   g_min[n] = min (g_req[n - L] ... g_req[n])          (window L + 1)
    //   G[n]     = cascaded-box smoothed g_min, FIR support S <= L
    //   G'[n]    = G[n] clamped by a 50 ms release-only recovery pole
    //   out[n]   = x[n - L] * G'[n]
    //
    // Zero-overshoot proof. Every g_min[m] feeding G[n] has m in [n - S, n]
    // with S <= L, and g_min[m] is a minimum over [m - L, m], an interval that
    // contains n - L for every such m. So every value averaged into G[n] is
    // <= g_req[n - L]; an average of values that are all <= g_req[n - L] is
    // itself <= g_req[n - L], and the release pole only ever lowers it
    // further. Therefore
    //
    //   |out[n]| = |x[n - L]| * G'[n] <= |x[n - L]| * threshold / |x[n - L]|
    //            = threshold
    //
    // for every sample, independent of program material - the property
    // tests/LookaheadLimiterTests.cpp asserts over a randomised signal corpus.
    class LookaheadLimiter
    {
    public:
        LookaheadLimiter() = default;

        void prepare (const juce::dsp::ProcessSpec& spec, int maximumLookaheadSamples)
        {
            sampleRate = spec.sampleRate;

            delay.prepare (spec.numChannels, maximumLookaheadSamples);
            slidingMinimum.prepare (maximumLookaheadSamples + 1);
            smoother.prepare (juce::jmax (1, maximumLookaheadSamples / 2));

            // Force a full reconfigure of the freshly-allocated primitives:
            // setLookaheadSamples() early-returns when the value is unchanged.
            const auto configured = lookaheadSamples;
            lookaheadSamples = -1;
            setLookaheadSamples (configured);

            reset();
        }

        void reset()
        {
            delay.reset();
            slidingMinimum.reset (1.0f);
            smoother.reset (1.0f);
            releaseState = 1.0f;
        }

        void setThresholdDb (float newThresholdDb) noexcept
        {
            thresholdLinear = juce::Decibels::decibelsToGain (newThresholdDb, -100.0f);
        }

        void setLookaheadSamples (int newLookaheadSamples) noexcept
        {
            const auto clamped = juce::jmax (0, newLookaheadSamples);

            if (clamped == lookaheadSamples)
                return;

            lookaheadSamples = clamped;

            delay.setDelaySamples (lookaheadSamples);
            slidingMinimum.setWindow (lookaheadSamples + 1);
            smoother.setLength (juce::jmax (1, lookaheadSamples / 2));
        }

        int getLookaheadSamples() const noexcept { return lookaheadSamples; }

        // Processes `block` in place. `scratch` must hold at least the block's
        // sample count and is used to carry the per-sample gain trajectory
        // across the two passes (no allocation).
        void process (juce::dsp::AudioBlock<float>& block, float* scratch) noexcept
        {
            const auto numChannels = block.getNumChannels();
            const auto numSamples = block.getNumSamples();

            if (numChannels == 0 || numSamples == 0)
                return;

            const auto releaseCoefficient = std::exp (-1.0f / (0.001f * releaseMs * static_cast<float> (sampleRate)));

            // Pass 1: derive the gain trajectory from the UNDELAYED signal, so
            // the sliding minimum genuinely reaches L samples into the future
            // of whatever is about to leave the delay line.
            for (size_t sample = 0; sample < numSamples; ++sample)
            {
                auto peak = 0.0f;

                for (size_t channel = 0; channel < numChannels; ++channel)
                    peak = juce::jmax (peak, std::abs (block.getChannelPointer (channel)[sample]));

                const auto required = peak > thresholdLinear && peak > 0.0f ? thresholdLinear / peak : 1.0f;
                const auto smoothed = smoother.process (slidingMinimum.process (required));

                releaseState = smoothed < releaseState
                                 ? smoothed
                                 : smoothed + releaseCoefficient * (releaseState - smoothed);

                scratch[sample] = releaseState;
            }

            // Pass 2: delay the audio, then apply the trajectory.
            delay.processInPlace (block);

            for (size_t channel = 0; channel < numChannels; ++channel)
            {
                auto* data = block.getChannelPointer (channel);

                for (size_t sample = 0; sample < numSamples; ++sample)
                    data[sample] *= scratch[sample];
            }
        }

    private:
        // Matches BandCompressor's legacy juce::dsp::Limiter release constant,
        // retained as the slow recovery pole after the lookahead window.
        static constexpr float releaseMs = 50.0f;

        LookaheadDelay delay;
        SlidingMinimum slidingMinimum;
        CascadedBoxSmoother smoother;

        double sampleRate = 44100.0;
        int lookaheadSamples = 0;
        float thresholdLinear = 1.0f;
        float releaseState = 1.0f;
    };
}
