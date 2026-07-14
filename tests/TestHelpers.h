#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

// Small shared helpers used across the Tests target. Kept dependency-free
// (just juce_audio_basics) so it can be included from any test file.
namespace TestHelpers
{
    // Fills every channel of the buffer with a sine wave of the given
    // frequency. `startSampleIndex` offsets the phase calculation, so
    // calling this for consecutive blocks with startSampleIndex incremented
    // by each block's length produces a phase-continuous sine across block
    // boundaries (needed whenever a test processes multiple blocks through a
    // stateful IIR/envelope-follower processor - a phase discontinuity at
    // block boundaries would inject spurious broadband energy and pollute
    // level measurements).
    inline void fillWithSine (juce::AudioBuffer<float>& buffer,
                              double sampleRate,
                              double frequencyHz,
                              float amplitude = 0.5f,
                              juce::int64 startSampleIndex = 0)
    {
        const auto numChannels = buffer.getNumChannels();
        const auto numSamples = buffer.getNumSamples();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* data = buffer.getWritePointer (channel);

            for (int sample = 0; sample < numSamples; ++sample)
            {
                const auto phase = juce::MathConstants<double>::twoPi * frequencyHz
                                    * static_cast<double> (startSampleIndex + sample) / sampleRate;
                data[sample] = amplitude * static_cast<float> (std::sin (phase));
            }
        }
    }

    // Root-mean-square level across all channels/samples in the buffer.
    inline double rms (const juce::AudioBuffer<float>& buffer)
    {
        double sumOfSquares = 0.0;
        juce::int64 numValues = 0;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto value = static_cast<double> (data[sample]);
                sumOfSquares += value * value;
                ++numValues;
            }
        }

        return numValues > 0 ? std::sqrt (sumOfSquares / static_cast<double> (numValues)) : 0.0;
    }

    // Largest absolute sample value across all channels/samples.
    inline float peakAbsolute (const juce::AudioBuffer<float>& buffer)
    {
        float peak = 0.0f;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                peak = std::max (peak, std::abs (data[sample]));
        }

        return peak;
    }

    // Returns true if every sample in the buffer is finite (no NaN/Inf).
    inline bool allSamplesFinite (const juce::AudioBuffer<float>& buffer)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                if (! std::isfinite (data[sample]))
                    return false;
        }

        return true;
    }

    // Pearson correlation coefficient between two equally-sized single-
    // channel signals - a shape-similarity measure that is insensitive to
    // constant gain and DC offset. 1.0 means the two signals are perfectly
    // linearly related.
    inline double correlation (const float* a, const float* b, int numSamples)
    {
        if (numSamples <= 0)
            return 0.0;

        double meanA = 0.0;
        double meanB = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            meanA += a[i];
            meanB += b[i];
        }

        meanA /= numSamples;
        meanB /= numSamples;

        double covariance = 0.0;
        double varianceA = 0.0;
        double varianceB = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto da = static_cast<double> (a[i]) - meanA;
            const auto db = static_cast<double> (b[i]) - meanB;

            covariance += da * db;
            varianceA += da * da;
            varianceB += db * db;
        }

        const auto denominator = std::sqrt (varianceA * varianceB);
        return denominator > 0.0 ? covariance / denominator : 0.0;
    }

    // Best Pearson correlation across a small range of sample-alignment
    // offsets. An LR4 crossover's low+high sum is magnitude-flat but is an
    // *all-pass* (not an identity/pure delay): a real, frequency-dependent
    // phase shift survives the reconstruction even though the reported
    // engine latency is 0. For a single steady-state tone that phase shift
    // is equivalent to a small time shift, so searching a modest alignment
    // window isolates genuine shape distortion from that legitimate,
    // unreported group delay.
    inline double bestCorrelationOverShift (const float* a, const float* b, int numSamples, int maxShiftSamples)
    {
        double best = -1.0;

        for (int shift = -maxShiftSamples; shift <= maxShiftSamples; ++shift)
        {
            const auto length = numSamples - std::abs (shift);
            if (length <= 0)
                continue;

            const auto* aStart = a + std::max (0, shift);
            const auto* bStart = b + std::max (0, -shift);

            best = std::max (best, correlation (aStart, bStart, length));
        }

        return best;
    }
}
