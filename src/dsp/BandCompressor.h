#pragma once

#include <juce_dsp/juce_dsp.h>

// One compression band: threshold/ratio/knee feed-forward VCA compression
// (a from-scratch gain computer, see src/dsp/KneeGainComputer.h, driven by
// juce::dsp::BallisticsFilter's peak envelope follower) followed by a
// makeup gain trim (juce::dsp::Gain). TriptychEngine owns three of these
// (Low/Mid/High); kept as its own class so each band's state (envelope
// filter + makeup gain ramp) is independent and so it is unit-testable in
// isolation (see tests/BandCompressorTests.cpp) without needing the full
// 3-band engine and its crossovers.
//
// juce::dsp::BallisticsFilter (JUCE 8.0.14, juce_dsp/processors/
// juce_BallisticsFilter.h) is a causal envelope follower with no lookahead,
// so this band - and therefore the whole Triptych engine - stays
// minimum-phase, exactly as it was when BandCompressor wrapped
// juce::dsp::Compressor directly in v0.1.
//
// v0.2.0 (docs/design-brief.md): v0.1 wrapped juce::dsp::Compressor, whose
// own gain formula (`gain = pow(env * thresholdInverse, ratioInverse - 1.0)`
// for `env >= threshold`, else `1.0`) is a hard knee with zero transition
// width - no knee parameter existed anywhere in v0.1. This class now owns
// its own knee-aware gain computer (src/dsp/KneeGainComputer.h) driven
// directly off a juce::dsp::BallisticsFilter (the same envelope-follower
// class juce::dsp::Compressor used internally), so a soft, threshold-relative
// knee (the reference-class-standard behaviour documented in
// docs/research-notes.md) can be layered in without changing the envelope
// follower's own behaviour at all.
//
// Bypass identity: with ratio == 1.0, KneeGainComputer::computeGainLinear()
// returns 1.0 unconditionally (independent of threshold/envelope/knee/range)
// - see KneeGainComputer.h/.cpp. Combined with makeup == 0 dB (unity Gain),
// setRatio(1.0f) + setMakeupDb(0.0f) makes a band a true identity
// pass-through regardless of Knee/Range, which is what TriptychEngine's
// flat-sum null test relies on (tests/EngineTests.cpp).
//
// Knee null test (docs/design-brief.md guarantee #1): at Knee == 0% and
// Range disabled, KneeGainComputer::computeGainLinear() takes a dedicated
// linear-domain fast path that reproduces juce::dsp::Compressor::
// processSample()'s exact hard-knee formula bit-for-bit (see
// KneeGainComputer.h/.cpp) - the v0.1 bypass-identity and gain-reduction
// tests are preserved unchanged at that extreme (tests/BandCompressorTests.cpp).
//
// v0.3.0 hybrid dynamics (docs/design-brief-v3-dynamics.md): Ratio now
// spans 0.2:1-20:1 (previously 1:1-20:1) - values below 1:1 are upward
// compression/expansion (signal above threshold is boosted, not cut),
// continuously tapering through ratio == 1.0's exact null point (still a
// bit-exact bypass, unaffected by this range extension). An optional
// per-band Range clamp (`setRangeEnabled`/`setRangeDb`) limits the maximum
// gain change in either direction; disabled by default so a band with
// Range never touched behaves identically to v0.2.0.
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
    void setKneePercent (float newKneePercent);
    void setAttackMs (float newAttackMs);
    void setReleaseMs (float newReleaseMs);
    void setMakeupDb (float newMakeupDb);

    // Range (v0.3.0): an optional maximum gain-change clamp in dB, applied
    // to both downward (cut) and upward (boost) processing - see
    // KneeGainComputer.h. Off by default (the smoothed target starts at
    // KneeGainComputer's unlimitedRangeDb sentinel), so a band whose Range
    // API is never called at all reproduces v0.2.0's unclamped behaviour
    // exactly.
    void setRangeEnabled (bool shouldBeEnabled) noexcept;
    void setRangeDb (float newRangeDb);

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

    // Downward expansion / gating (v0.4.0, issue #25): an independent,
    // per-band noise-gate/expander stage with its own threshold/ratio/
    // attack/release - see src/dsp/GateGainComputer.h for the pure transfer-
    // curve math. Reuses the exact same detector topology as the compressor
    // above rather than a second, structurally different detection method:
    // a second juce::dsp::BallisticsFilter instance (gateEnvelopeFilter),
    // with its own independently configurable attack/release (issue #25
    // explicitly asks that gate ballistics not just reuse the compressor's
    // own Attack/Release, since gates typically want a much faster attack
    // and a slower, chatter-avoiding release than the compressor sitting in
    // the same band). Applied as an additional multiplicative gain alongside
    // the compressor's own VCA gain, keyed off the *same pre-compression
    // input sample* the compressor's own envelope follower sees - so gating
    // a band is never masked by (or interacting with) that band's own
    // compression curve.
    //
    // Off by default: setGateEnabled(false) (the constructed default)
    // smooths gateRatioSmoothed's target to 1.0 - GateGainComputer's own
    // bit-exact bypass value, mirroring Range's "smooth to the bypass
    // sentinel while disabled" idiom (see setRangeEnabled() above) rather
    // than gating a separate boolean multiplier - so a band whose Gate API
    // is never touched at all reproduces pre-v0.4.0 behaviour exactly.
    void setGateEnabled (bool shouldBeEnabled) noexcept;
    void setGateThresholdDb (float newThresholdDb);
    void setGateRatio (float newRatio);
    void setGateAttackMs (float newAttackMs);
    void setGateReleaseMs (float newReleaseMs);

private:
    static constexpr double smoothingTimeSeconds = 0.05;

    // Fixed release for the optional limiter stage - a fast-ish safety
    // ceiling rather than a musical dynamics control, matching the "quick
    // brickwall catch" role the High-band limiter option is meant to play.
    static constexpr float limiterReleaseMs = 50.0f;

    // v0.2.0's own knee-aware VCA gain computer, replacing v0.1's
    // juce::dsp::Compressor - see the class-level doc comment above and
    // src/dsp/KneeGainComputer.h. Uses the exact same peak-detection
    // envelope-follower class juce::dsp::Compressor used internally, so
    // attack/release behaviour is unchanged from v0.1.
    juce::dsp::BallisticsFilter<float> envelopeFilter;
    juce::dsp::Gain<float> makeupGain;
    juce::dsp::Limiter<float> limiter;
    bool limiterEnabled = false;
    float lastLimiterThresholdDb = -3.0f;

    // Scratch copy of the post-compressor/makeup signal that the limiter
    // always runs against (see setLimiterEnabled's doc comment above), sized
    // to the prepared capacity in prepare() and never reallocated on the
    // audio thread.
    juce::AudioBuffer<float> limiterScratchBuffer;

    // Threshold, ratio, and knee are smoothed and re-applied once per block -
    // the same block-rate-recompute compromise TriptychEngine/OvertureEngine
    // use for filter cutoffs: the knee-aware gain computer has no ramp of
    // its own for these, so an unsmoothed jump (e.g. a fast GUI drag) would
    // otherwise produce an audible, instantaneous step in the VCA gain
    // curve. Attack and Release are the envelope follower's own time
    // constants rather than audio-rate gain values, so applying them
    // unsmoothed (as juce::dsp::BallisticsFilter's own setAttackTime/
    // setReleaseTime do synchronously) is standard practice and matches
    // juce::dsp::Compressor's own v0.1 design.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> thresholdSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> ratioSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> kneeSmoothed;

    // Range (v0.3.0): the smoothed value is the *effective* clamp bound fed
    // straight to KneeGainComputer - when Range is disabled, its target is
    // KneeGainComputer::unlimitedRangeDb (not a separate "disabled" flag),
    // so toggling Range on/off ramps the clamp boundary smoothly over
    // smoothingTimeSeconds instead of producing a hard discontinuity, the
    // same real-time-safe approach used for the High-band limiter's
    // continuously-tracked ballistics (see setLimiterEnabled's doc comment
    // below).
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> rangeSmoothed;

    // Last commanded values (ParameterLayout defaults until a setter is
    // called), re-applied to the smoothers on every prepare() so re-prepare
    // (sample-rate change, etc.) never resets a live parameter back to a
    // default or lets ratio ramp from an invalid < 1.0 starting point. The
    // per-band defaults below match this class's own hard-coded fallback
    // only until BandCompressor's owner (TriptychEngine/PluginProcessor)
    // calls the real setters with the actual per-band ParameterLayout
    // defaults (docs/design-brief.md) - see ParameterLayout.cpp for the
    // authoritative values.
    float lastThresholdDb = -18.0f;
    float lastRatio = 4.0f;
    float lastKneePercent = 50.0f;

    // Range (v0.3.0): tracked independently of the smoothed *effective*
    // bound above, since the effective target depends on both of these
    // (see setRangeEnabled()/setRangeDb()'s definitions).
    bool rangeEnabled = false;
    float lastRangeDb = 12.0f;

    // Downward expansion / gating (v0.4.0): a second, independent envelope
    // follower (see setGateEnabled()'s doc comment above) plus its own
    // smoothed threshold/ratio, re-applied once per block for the same
    // real-time-safety reason as thresholdSmoothed/ratioSmoothed above.
    juce::dsp::BallisticsFilter<float> gateEnvelopeFilter;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gateThresholdSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gateRatioSmoothed;
    bool gateEnabled = false;
    float lastGateThresholdDb = -50.0f;
    float lastGateRatio = 2.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandCompressor)
};
