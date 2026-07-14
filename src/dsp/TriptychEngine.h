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
// needed anywhere in this engine.
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
    // larger than what prepare() was sized for is defensively trimmed to
    // the prepared capacity rather than causing an out-of-bounds write.
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
    void setLowAttackMs (float newAttackMs) { lowBand.setAttackMs (newAttackMs); }
    void setLowReleaseMs (float newReleaseMs) { lowBand.setReleaseMs (newReleaseMs); }
    void setLowMakeupDb (float newMakeupDb) { lowBand.setMakeupDb (newMakeupDb); }

    void setMidThresholdDb (float newThresholdDb) { midBand.setThresholdDb (newThresholdDb); }
    void setMidRatio (float newRatio) { midBand.setRatio (newRatio); }
    void setMidAttackMs (float newAttackMs) { midBand.setAttackMs (newAttackMs); }
    void setMidReleaseMs (float newReleaseMs) { midBand.setReleaseMs (newReleaseMs); }
    void setMidMakeupDb (float newMakeupDb) { midBand.setMakeupDb (newMakeupDb); }

    void setHighThresholdDb (float newThresholdDb) { highBand.setThresholdDb (newThresholdDb); }
    void setHighRatio (float newRatio) { highBand.setRatio (newRatio); }
    void setHighAttackMs (float newAttackMs) { highBand.setAttackMs (newAttackMs); }
    void setHighReleaseMs (float newReleaseMs) { highBand.setReleaseMs (newReleaseMs); }
    void setHighMakeupDb (float newMakeupDb) { highBand.setMakeupDb (newMakeupDb); }

    // Master output trim, applied after the three bands are summed.
    void setOutputDb (float newOutputDb);

    // Always 0: the LR4 crossovers and juce::dsp::Compressor are both
    // minimum-phase/causal with no lookahead (see class comment above).
    static constexpr int getLatencySamples() noexcept { return 0; }

private:
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

    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriptychEngine)
};
