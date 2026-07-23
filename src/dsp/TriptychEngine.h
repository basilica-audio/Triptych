#pragma once

#include <juce_dsp/juce_dsp.h>

#include "BandCompressor.h"
#include "Crossover.h"

// The complete Triptych signal path, independent of juce::AudioProcessor so
// it can be exercised directly by unit tests without instantiating a full
// plugin (see tests/EngineTests.cpp). Owns all DSP state; every buffer/
// filter is allocated in prepare() and never reallocated on the audio
// thread.
//
// Signal flow (see docs/architecture.md for the full diagram):
//
//   input --[LR4 @ LowMidSplit]--> low ------------------------------> BandCompressor (Low)  --\
//                               \-> midHigh --[LR4 @ MidHighSplit]--> mid  -> BandCompressor (Mid)  ---+--> sum --> Output --> output
//                                                                  \-> high -> BandCompressor (High) --/
//
// Both crossovers are 4th-order Linkwitz-Riley (see Crossover.h): each
// stage's low+high sum reconstructs its input flat (within floating-point
// precision), so the cascade's Low+Mid+High sum reconstructs the original
// input flat too. With every band's compressor bypassed (ratio 1:1, makeup
// 0 dB - see BandCompressor.h for why that is an exact identity), the whole
// engine becomes a bit-exact identity pass-through; this is exactly what
// tests/EngineTests.cpp's flat-sum null test verifies.
//
// Neither the LR4 crossovers (minimum-phase IIR) nor juce::dsp::Compressor
// (causal envelope follower, no lookahead) add latency, so
// getLatencySamples() is always 0 and no dry-path delay compensation is
// needed anywhere in this engine. The M1 additions below - per-band
// Mute/Solo (resolved at the summing stage) and the High band's optional
// juce::dsp::Limiter stage (inside BandCompressor) - are both zero-latency
// too, so this remains true after them.
class TriptychEngine
{
public:
    TriptychEngine();

    // Allocates all DSP state. Must be called (and completed) before the
    // first process() call, and again whenever sample rate/block size/
    // channel count change.
    void prepare (const juce::dsp::ProcessSpec& spec);

    // Clears all filter/envelope/gain-ramp state without deallocating.
    // Safe to call from the audio thread (e.g. on playback stop/loop).
    void reset();

    // Processes `block` in place. Real-time safe: no allocation once
    // prepare() has completed. A zero-sample block is a safe no-op; a block
    // larger than what prepare() was sized for is chunked internally into
    // <= prepared-capacity pieces (each run through the full signal chain in
    // turn) rather than leaving the excess samples unprocessed.
    void process (juce::dsp::AudioBlock<float>& block);

    // Crossover split frequencies, in Hz. Real-time safe - smoothed and
    // re-applied once per block. The Mid/High split is always kept at least
    // minimumSplitSeparationHz above the (possibly still-ramping) Low/Mid
    // split, so automation can never invert band order.
    void setLowMidSplitHz (float newFrequencyHz);
    void setMidHighSplitHz (float newFrequencyHz);

    // Per-band parameter setters, in real units (dB, ratio as a plain
    // number >= 1, ms). Safe to call every block from the audio thread.
    void setLowThresholdDb (float newThresholdDb) { lowBand.setThresholdDb (newThresholdDb); }
    void setLowRatio (float newRatio) { lowBand.setRatio (newRatio); }
    void setLowKneePercent (float newKneePercent) { lowBand.setKneePercent (newKneePercent); }
    void setLowAttackMs (float newAttackMs) { lowBand.setAttackMs (newAttackMs); }
    void setLowReleaseMs (float newReleaseMs) { lowBand.setReleaseMs (newReleaseMs); }
    void setLowMakeupDb (float newMakeupDb) { lowBand.setMakeupDb (newMakeupDb); }
    void setLowRangeEnabled (bool shouldBeEnabled) noexcept { lowBand.setRangeEnabled (shouldBeEnabled); }
    void setLowRangeDb (float newRangeDb) { lowBand.setRangeDb (newRangeDb); }

    // Downward expansion / gating (v0.4.0, issue #25). See BandCompressor.h.
    void setLowGateEnabled (bool shouldBeEnabled) noexcept { lowBand.setGateEnabled (shouldBeEnabled); }
    void setLowGateThresholdDb (float newThresholdDb) { lowBand.setGateThresholdDb (newThresholdDb); }
    void setLowGateRatio (float newRatio) { lowBand.setGateRatio (newRatio); }
    void setLowGateAttackMs (float newAttackMs) { lowBand.setGateAttackMs (newAttackMs); }
    void setLowGateReleaseMs (float newReleaseMs) { lowBand.setGateReleaseMs (newReleaseMs); }

    void setMidThresholdDb (float newThresholdDb) { midBand.setThresholdDb (newThresholdDb); }
    void setMidRatio (float newRatio) { midBand.setRatio (newRatio); }
    void setMidKneePercent (float newKneePercent) { midBand.setKneePercent (newKneePercent); }
    void setMidAttackMs (float newAttackMs) { midBand.setAttackMs (newAttackMs); }
    void setMidReleaseMs (float newReleaseMs) { midBand.setReleaseMs (newReleaseMs); }
    void setMidMakeupDb (float newMakeupDb) { midBand.setMakeupDb (newMakeupDb); }
    void setMidRangeEnabled (bool shouldBeEnabled) noexcept { midBand.setRangeEnabled (shouldBeEnabled); }
    void setMidRangeDb (float newRangeDb) { midBand.setRangeDb (newRangeDb); }

    void setMidGateEnabled (bool shouldBeEnabled) noexcept { midBand.setGateEnabled (shouldBeEnabled); }
    void setMidGateThresholdDb (float newThresholdDb) { midBand.setGateThresholdDb (newThresholdDb); }
    void setMidGateRatio (float newRatio) { midBand.setGateRatio (newRatio); }
    void setMidGateAttackMs (float newAttackMs) { midBand.setGateAttackMs (newAttackMs); }
    void setMidGateReleaseMs (float newReleaseMs) { midBand.setGateReleaseMs (newReleaseMs); }

    void setHighThresholdDb (float newThresholdDb) { highBand.setThresholdDb (newThresholdDb); }
    void setHighRatio (float newRatio) { highBand.setRatio (newRatio); }
    void setHighKneePercent (float newKneePercent) { highBand.setKneePercent (newKneePercent); }
    void setHighAttackMs (float newAttackMs) { highBand.setAttackMs (newAttackMs); }
    void setHighReleaseMs (float newReleaseMs) { highBand.setReleaseMs (newReleaseMs); }
    void setHighMakeupDb (float newMakeupDb) { highBand.setMakeupDb (newMakeupDb); }
    void setHighRangeEnabled (bool shouldBeEnabled) noexcept { highBand.setRangeEnabled (shouldBeEnabled); }
    void setHighRangeDb (float newRangeDb) { highBand.setRangeDb (newRangeDb); }

    void setHighGateEnabled (bool shouldBeEnabled) noexcept { highBand.setGateEnabled (shouldBeEnabled); }
    void setHighGateThresholdDb (float newThresholdDb) { highBand.setGateThresholdDb (newThresholdDb); }
    void setHighGateRatio (float newRatio) { highBand.setGateRatio (newRatio); }
    void setHighGateAttackMs (float newAttackMs) { highBand.setGateAttackMs (newAttackMs); }
    void setHighGateReleaseMs (float newReleaseMs) { highBand.setGateReleaseMs (newReleaseMs); }

    // High-band limiter option (M1). See BandCompressor::setLimiterEnabled
    // for why toggling this is real-time safe with no added latency.
    void setHighLimiterEnabled (bool shouldBeEnabled) noexcept { highBand.setLimiterEnabled (shouldBeEnabled); }
    void setHighLimiterThresholdDb (float newThresholdDb) { highBand.setLimiterThresholdDb (newThresholdDb); }

    // Per-band Mute/Solo (M1), applied at the summing stage below - see
    // ParameterIds.h for the console-style semantics. The booleans
    // themselves are set directly (a Mute/Solo toggle is a discrete mix
    // decision, not an audio-rate value), but the *gain* they resolve to is
    // smoothed (see lowGainSmoothed et al.) so the toggle itself never
    // produces an audible click (issue #13).
    void setLowMute (bool shouldBeMuted) noexcept { lowMuted = shouldBeMuted; }
    void setLowSolo (bool shouldBeSoloed) noexcept { lowSoloed = shouldBeSoloed; }
    void setMidMute (bool shouldBeMuted) noexcept { midMuted = shouldBeMuted; }
    void setMidSolo (bool shouldBeSoloed) noexcept { midSoloed = shouldBeSoloed; }
    void setHighMute (bool shouldBeMuted) noexcept { highMuted = shouldBeMuted; }
    void setHighSolo (bool shouldBeSoloed) noexcept { highSoloed = shouldBeSoloed; }

    // Master output trim, applied after the three bands are summed.
    void setOutputDb (float newOutputDb);

    // Always 0: the LR4 crossovers and juce::dsp::Compressor are both
    // minimum-phase/causal with no lookahead (see class comment above).
    static constexpr int getLatencySamples() noexcept { return 0; }

private:
    // Processes a single chunk of at most the prepared per-band buffer
    // capacity - the full signal chain (crossovers, band compressors,
    // Mute/Solo gate, output trim) for one call. process() above splits any
    // larger host-supplied block into a sequence of these.
    void processChunk (juce::dsp::AudioBlock<float> workingBlock);

    static constexpr double smoothingTimeSeconds = 0.05;

    // Minimum separation enforced between the two split frequencies so the
    // second (Mid/High) crossover's cutoff can never end up at or below the
    // first (Low/Mid) one, which would invert band order and momentarily
    // break the flat-sum property while the two ranges cross over each
    // other during automation.
    static constexpr float minimumSplitSeparationHz = 20.0f;

    Crossover lowMidCrossover;
    Crossover midHighCrossover;

    BandCompressor lowBand;
    BandCompressor midBand;
    BandCompressor highBand;

    juce::dsp::Gain<float> outputGain;

    // Intermediate per-band buffers, sized to the maximum block/channel
    // count in prepare() and never reallocated on the audio thread.
    juce::AudioBuffer<float> lowBuffer;
    juce::AudioBuffer<float> midHighBuffer;
    juce::AudioBuffer<float> midBuffer;
    juce::AudioBuffer<float> highBuffer;

    // Frequencies are perceived logarithmically, so both splits use
    // multiplicative smoothing (matching OvertureEngine's Tight/Tone
    // frequency smoothers).
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowMidSplitSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> midHighSplitSmoothed;

    // Last commanded split frequencies, re-applied to the smoothers on every
    // prepare() so re-prepare never resets a live split back to a default or
    // lets a smoother ramp from an invalid 0 Hz starting point.
    float lastLowMidSplitHz = 200.0f;
    float lastMidHighSplitHz = 3000.0f;

    // Per-band Mute/Solo state (M1), all off by default so their addition
    // never changes existing default behaviour.
    bool lowMuted = false;
    bool lowSoloed = false;
    bool midMuted = false;
    bool midSoloed = false;
    bool highMuted = false;
    bool highSoloed = false;

    // Per-band Mute/Solo gain, smoothed rather than a bare per-block 0/1
    // constant (issue #13): resolving Mute/Solo to a plain 0.0f/1.0f
    // multiplier applied uniformly across a whole block introduces a hard
    // step discontinuity at whatever sample happens to fall on the block
    // boundary when the state actually changes mid-playback - audible as a
    // click, independent of (and in addition to) each band's own
    // compressor/limiter envelopes staying continuous underneath. Ramped
    // the same way every other real-time-varying scalar in this engine is
    // (see thresholdSmoothed/ratioSmoothed in BandCompressor, and the split
    // frequency smoothers below).
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lowGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> midGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> highGainSmoothed;

    // Per-sample Mute/Solo gain ramps, filled once per processChunk() call
    // from the smoothers above and consumed by the summing loop - 3 mono
    // channels (Low/Mid/High), sized to the maximum chunk capacity in
    // prepare() and never reallocated on the audio thread.
    juce::AudioBuffer<float> muteSoloGainBuffer;

    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriptychEngine)
};
