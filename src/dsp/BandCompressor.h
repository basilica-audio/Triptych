#pragma once

#include <juce_dsp/juce_dsp.h>

// One compression band: threshold/ratio/attack/release feed-forward VCA
// compression (juce::dsp::Compressor) followed by a makeup gain trim
// (juce::dsp::Gain). TriptychEngine owns three of these (Low/Mid/High); kept
// as its own class so each band's state (envelope filter + makeup gain
// ramp) is independent and so it is unit-testable in isolation (see
// tests/BandCompressorTests.cpp) without needing the full 3-band engine and
// its crossovers.
//
// juce::dsp::Compressor (JUCE 8.0.14, juce_dsp/widgets/juce_Compressor.h) is
// a causal ballistics-filter envelope follower with no lookahead, so it adds
// zero latency - this band, and therefore the whole Triptych engine, is
// minimum-phase.
//
// Bypass identity: with ratio == 1.0, juce::dsp::Compressor::processSample
// computes gain = pow(env * thresholdInverse, ratioInverse - 1.0) and
// ratioInverse - 1.0 == 0 for ratio == 1.0, so gain == 1.0 unconditionally
// (independent of threshold/envelope) - i.e. ratio 1:1 is an exact,
// bit-identical bypass of the VCA stage. Combined with makeup == 0 dB (unity
// Gain), setRatio(1.0f) + setMakeupDb(0.0f) makes a band a true identity
// pass-through. This is what TriptychEngine's flat-sum null test relies on.
class BandCompressor
{
public:
    BandCompressor() = default;

    // Allocates all DSP state. Must be called before the first process()
    // call, and again whenever sample rate/block size/channel count change.
    void prepare (const juce::dsp::ProcessSpec& spec);

    // Clears envelope-follower and makeup-gain ramp state without
    // deallocating. Safe to call from the audio thread.
    void reset();

    // Processes `block` in place. Real-time safe: no allocation once
    // prepare() has completed. A zero-sample block is a safe no-op.
    void process (juce::dsp::AudioBlock<float>& block) noexcept;

    void setThresholdDb (float newThresholdDb);
    void setRatio (float newRatio);
    void setAttackMs (float newAttackMs);
    void setReleaseMs (float newReleaseMs);
    void setMakeupDb (float newMakeupDb);

    // Optional post-stage brickwall limiter (M1's "high-band limiter
    // option"; the type is generic so any band could opt in, though only
    // the High band currently exposes it via APVTS - see TriptychEngine and
    // ParameterIds.h). Runs after the compressor + makeup gain, so it
    // catches whatever peaks reach the band's output including any makeup
    // boost.
    //
    // Real-time-safe by construction, but NOT via juce::dsp::Limiter's own
    // context.isBypassed: that flag (JUCE 8.0.14, juce_dsp/widgets/
    // juce_Limiter.h:79-83, which forwards to juce_Compressor.h:85-89 in
    // both of its internal Compressor stages) short-circuits to a plain
    // copyFrom() as the *first* statement, skipping the BallisticsFilter
    // envelope update entirely - so driving it with isBypassed while
    // "disabled" freezes the limiter's ballistics rather than keeping them
    // continuous (issue #12). Instead, process() always runs the limiter at
    // full strength (isBypassed == false, always) against a preallocated
    // scratch copy of the post-compressor/makeup signal, so its envelope
    // honestly tracks whatever is actually flowing through the band at all
    // times; the limited result is only spliced back into the real output
    // when the limiter is enabled. That is what makes re-enabling resume
    // gain reduction from a state consistent with the current input rather
    // than a stale pre-disable snapshot.
    void setLimiterEnabled (bool shouldBeEnabled) noexcept { limiterEnabled = shouldBeEnabled; }
    void setLimiterThresholdDb (float newThresholdDb);

private:
    static constexpr double smoothingTimeSeconds = 0.05;

    // Fixed release for the optional limiter stage - a fast-ish safety
    // ceiling rather than a musical dynamics control, matching the "quick
    // brickwall catch" role the High-band limiter option is meant to play.
    static constexpr float limiterReleaseMs = 50.0f;

    juce::dsp::Compressor<float> compressor;
    juce::dsp::Gain<float> makeupGain;
    juce::dsp::Limiter<float> limiter;
    bool limiterEnabled = false;
    float lastLimiterThresholdDb = -3.0f;

    // Scratch copy of the post-compressor/makeup signal that the limiter
    // always runs against (see setLimiterEnabled's doc comment above), sized
    // to the prepared capacity in prepare() and never reallocated on the
    // audio thread.
    juce::AudioBuffer<float> limiterScratchBuffer;

    // Threshold and ratio are smoothed and re-applied once per block - the
    // same block-rate-recompute compromise TriptychEngine/OvertureEngine use
    // for filter cutoffs: juce::dsp::Compressor has no ramp of its own for
    // these, so an unsmoothed jump (e.g. a fast GUI drag) would otherwise
    // produce an audible, instantaneous step in the VCA gain curve. Attack
    // and Release are the envelope follower's own time constants rather than
    // audio-rate gain values, so applying them unsmoothed (as
    // juce::dsp::Compressor's own setAttack/setRelease do synchronously) is
    // standard practice and matches JUCE's own design.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> thresholdSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> ratioSmoothed;

    // Last commanded values (ParameterLayout defaults until a setter is
    // called), re-applied to the smoothers on every prepare() so re-prepare
    // (sample-rate change, etc.) never resets a live parameter back to a
    // default or lets ratio ramp from an invalid < 1.0 starting point.
    float lastThresholdDb = -18.0f;
    float lastRatio = 4.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandCompressor)
};
