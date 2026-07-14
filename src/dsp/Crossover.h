#pragma once

#include <juce_dsp/juce_dsp.h>

// 4th-order Linkwitz-Riley crossover: splits a signal into a low band and a
// high band whose magnitude-flat sum reconstructs the original signal
// (within floating-point precision). That flat-sum property is the reason
// LR4 - rather than a plain pair of same-order Butterworth filters - is the
// standard choice ahead of independent per-band processing: summing two
// plain Butterworth lowpass/highpass outputs leaves either a notch or a
// bump right at the crossover point.
//
// Triptych cascades two instances of this class (see TriptychEngine) to
// split into three bands: the first splits Low from (Mid+High) at
// LowMidSplit, the second splits that remainder into Mid and High at
// MidHighSplit. Because each stage's low+high sum is flat, the cascade's
// Low+Mid+High sum is flat too.
//
// Wraps juce::dsp::LinkwitzRileyFilter<float> (JUCE 8.0.14,
// juce_dsp/processors/juce_LinkwitzRileyFilter.h) using its dual-output
// processSample(channel, input, outputLow, outputHigh) overload. That
// overload runs a single cascaded TPT (topology-preserving transform) state
// per channel and emits matched low/high outputs from that shared state -
// this is what guarantees the flat-magnitude sum. Using two independently
// configured LinkwitzRileyFilter instances (one set to lowpass, one to
// highpass) would also flat-sum in theory, but doubles the per-channel state
// and risks the two cutoffs drifting apart under future automation/preset
// changes; the single-instance dual-output form makes that impossible by
// construction.
//
// Both the LR4 topology itself and juce::dsp::Compressor's envelope
// follower are minimum-phase/causal with no lookahead, so this crossover
// (and therefore the whole Triptych engine) reports zero added latency.
class Crossover
{
public:
    Crossover() = default;

    // Allocates per-channel filter state. Must be called before process()
    // and whenever the channel count or sample rate changes.
    void prepare (const juce::dsp::ProcessSpec& spec);

    // Clears filter state (e.g. on transport stop) without deallocating.
    void reset();

    // Real-time safe: just recomputes a handful of filter coefficients
    // from the new cutoff, no allocation.
    void setCutoffFrequency (float newCutoffHz);

    float getCutoffFrequency() const noexcept { return filter.getCutoffFrequency(); }

    // Splits `input` sample-for-sample into `lowOutput` and `highOutput`.
    // All three blocks must share the same channel/sample counts (as
    // established by the most recent prepare() call); lowOutput and
    // highOutput must not alias input or each other. Real-time safe: no
    // allocation, just per-sample filter math.
    void process (const juce::dsp::AudioBlock<const float>& input,
                  juce::dsp::AudioBlock<float>& lowOutput,
                  juce::dsp::AudioBlock<float>& highOutput) noexcept;

private:
    juce::dsp::LinkwitzRileyFilter<float> filter;
};
