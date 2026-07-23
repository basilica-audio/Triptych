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

    // Per-band Mid/Side processing (v0.4.0, issue #24): encodes the band's
    // stereo signal to Mid/Side (src/dsp/MidSideCodec.h's equal-power,
    // exactly-invertible transform) immediately before the per-sample
    // gain-computation loop and decodes back immediately after it, so
    // makeup gain/the optional limiter always run on genuine L/R. The
    // band's main Threshold/Ratio/Knee/Attack/Release/Range above continue
    // to drive the Mid component (matching pre-v0.4.0 stereo-linked
    // behaviour exactly when M/S is disabled); Side gets its own
    // independent Threshold/Ratio (issue #24's documented "at minimum"
    // scope), sharing the band's Knee/Attack/Release/Range - a deliberate
    // scope decision to keep the per-band parameter surface bounded rather
    // than doubling every control. Off by default and a defensive no-op on
    // any bus that isn't exactly 2 channels (see process()'s definition),
    // so existing sessions/presets and mono buses are entirely unaffected.
    //
    // Note: unlike Range/the limiter above, toggling M/S itself is a
    // structural change of basis (L/R vs Mid/Side), not a smoothly
    // rampable scalar gain - unless both Mid and Side happen to be
    // completely bypassed (ratio == 1.0 on both) at the instant of the
    // toggle, switching mid-playback can produce an audible discontinuity,
    // the same caveat every M/S-capable console/plugin carries. Threshold/
    // Ratio changes *within* a fixed M/S mode remain fully smoothed, as
    // usual.
    void setMidSideEnabled (bool shouldBeEnabled) noexcept;
    void setSideThresholdDb (float newThresholdDb);
    void setSideRatio (float newRatio);

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

    // Per-band Mid/Side (v0.4.0): Side's own smoothed threshold/ratio - see
    // setMidSideEnabled()'s doc comment above for why Knee/Attack/Release/
    // Range stay shared with the main (Mid) path instead of doubling here
    // too. No separate envelope follower is needed: envelopeFilter above
    // already tracks independent per-channel state (it is called with a
    // channel index - see process()), so the Side slot (channel index 1
    // after encode) gets its own genuinely independent envelope trajectory
    // "for free" from the same BallisticsFilter instance the Mid slot uses.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> sideThresholdSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> sideRatioSmoothed;
    bool midSideEnabled = false;
    float lastSideThresholdDb = -18.0f;
    float lastSideRatio = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandCompressor)
};
