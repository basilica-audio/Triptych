#pragma once

#include <juce_dsp/juce_dsp.h>

#include <vector>

// Linkwitz-Riley crossover: splits a signal into a low band and a high band
// whose magnitude-flat sum reconstructs the original signal (within
// floating-point precision). That flat-sum property is the reason
// Linkwitz-Riley - rather than a plain pair of same-order Butterworth
// filters - is the standard choice ahead of independent per-band processing:
// summing two plain Butterworth lowpass/highpass outputs leaves either a
// notch or a bump right at the crossover point.
//
// Triptych cascades two instances of this class (see TriptychEngine) to
// split into three bands: the first splits Low from (Mid+High) at
// LowMidSplit, the second splits that remainder into Mid and High at
// MidHighSplit. Because each stage's low+high sum is flat, the cascade's
// Low+Mid+High sum is flat too.
//
// v0.5.0 selectable slopes (issue #1, part 2). Three orders are available,
// applying to both split points (per-split slopes were rejected for parameter
// economy):
//
//   - Slope::lr4 (24 dB/oct, the DEFAULT and the v0.1-v0.4 behaviour): wraps
//     juce::dsp::LinkwitzRileyFilter<float> (JUCE 8.0.14, juce_dsp/processors/
//     juce_LinkwitzRileyFilter.h) using its dual-output processSample
//     (channel, input, outputLow, outputHigh) overload. That overload runs a
//     single cascaded TPT (topology-preserving transform) state per channel
//     and emits matched low/high outputs from that shared state - this is what
//     guarantees the flat-magnitude sum. Using two independently configured
//     LinkwitzRileyFilter instances (one lowpass, one highpass) would also
//     flat-sum in theory, but doubles the per-channel state and risks the two
//     cutoffs drifting apart under future automation/preset changes; the
//     single-instance dual-output form makes that impossible by construction.
//     This path is byte-for-byte the v0.4.0 code, which is what makes the
//     default-slope bit-identity test (tests/CrossoverSlopeTests.cpp, T9a) a
//     structural guarantee rather than a numerical coincidence.
//
//   - Slope::lr2 (12 dB/oct): squared 1st-order Butterworth, built from one
//     TPT one-pole pair per channel. LR2's complementary sum requires the
//     highpass output to be polarity-inverted (LP - HP is the flat sum for an
//     even-order-2 Linkwitz-Riley), so the highpass is emitted already
//     inverted and the caller's plain LP + HP addition stays flat exactly as
//     it does for LR4.
//
//   - Slope::lr8 (48 dB/oct): squared 4th-order Butterworth, i.e. two
//     cascaded LR4 sections sharing one dual-output topology per channel, so
//     LP and HP stay complementary by construction the same way the LR4 path
//     does.
//
// Phase honesty (binding): Triptych's 3-band tree applies the second
// crossover only to the midHigh branch, so the summed output carries the
// second split's allpass rotation on Mid+High only. That uncompensated tree
// is the existing, tested compromise (flat-sum within +-0.1 dB for LR4);
// LR2 halves and LR8 doubles that rotation, so the per-slope flat-sum
// tolerances differ and are asserted separately. Adding low-branch allpass
// compensation would change the LR4 default sound, which the v0.5.0
// neutrality contract forbids - it is evaluated together with the
// linear-phase mode in v0.6.0.
//
// Slope is an unsmoothed structural switch (the coefficient topology itself
// changes); switching resets filter state, so it can click, exactly like the
// per-band M/S toggle. Documented in docs/manual.md.
class Crossover
{
public:
    enum class Slope
    {
        lr2 = 0, // 12 dB/oct
        lr4 = 1, // 24 dB/oct - default, the v0.1-v0.4 path
        lr8 = 2  // 48 dB/oct
    };

    Crossover() = default;

    // Allocates per-channel filter state. Must be called before process()
    // and whenever the channel count or sample rate changes.
    void prepare (const juce::dsp::ProcessSpec& spec);

    // Clears filter state (e.g. on transport stop) without deallocating.
    void reset();

    // Real-time safe: just recomputes a handful of filter coefficients
    // from the new cutoff, no allocation.
    void setCutoffFrequency (float newCutoffHz);

    // Structural switch: resets filter state when the slope actually changes
    // (see the class comment). A no-op when the slope is already `newSlope`.
    void setSlope (Slope newSlope);

    Slope getSlope() const noexcept { return slope; }

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
    // Butterworth 4th-order Q pair; LR8 is this section pair applied twice
    // (squared BW4), independently down the lowpass and the highpass chain.
    static constexpr float butterworth4Q1 = 0.54119610f;
    static constexpr float butterworth4Q2 = 1.30656296f;

    static constexpr size_t numLr8Sections = 4; // Q1, Q2, Q1, Q2

    // One channel's LR2 state: a TPT one-pole run twice down the lowpass
    // chain and twice down the highpass chain (squared 1st-order
    // Butterworth). All four integrators share the single `lr2G`, so the
    // LP/HP pair can never drift apart.
    struct Lr2State
    {
        float lowState[2] { 0.0f, 0.0f };
        float highState[2] { 0.0f, 0.0f };
    };

    // One TPT state-variable-filter section's integrator state.
    struct SvfState
    {
        float ic1 = 0.0f;
        float ic2 = 0.0f;
    };

    // One channel's LR8 state: four second-order sections down each chain.
    struct Lr8State
    {
        SvfState low[numLr8Sections];
        SvfState high[numLr8Sections];
    };

    // A TPT SVF section's resolved coefficients (one per distinct Q).
    struct SvfCoefficients
    {
        float k = 1.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
        float a3 = 0.0f;
    };

    // LR4, the default path: juce::dsp::LinkwitzRileyFilter's own state.
    juce::dsp::LinkwitzRileyFilter<float> filter;

    std::vector<Lr2State> lr2States;
    std::vector<Lr8State> lr8States;

    Slope slope = Slope::lr4;
    float lr2G = 0.0f;
    SvfCoefficients lr8Coefficients[2];
    float lastCutoffHz = 1000.0f;
    double sampleRate = 44100.0;

    void updateAlternateCoefficients();
};
