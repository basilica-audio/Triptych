#pragma once

#include <juce_dsp/juce_dsp.h>

#include <vector>

// Detector v2 (v0.5.0, .scaffold brief section 3.1-3.3): the compressor-side
// envelope detector for one band, replacing BandCompressor's bare
// juce::dsp::BallisticsFilter usage. The gate keeps its own, untouched
// BallisticsFilter instance - only the *compressor* envelope goes through
// this class.
//
// Three orthogonal capabilities, every one of them neutral at its default:
//
//   1. Detection law (Peak / RMS). Peak is a thin pass-through wrapper around
//      the exact juce::dsp::BallisticsFilter<float> call BandCompressor made
//      in v0.4.0 - deliberately NOT a re-implementation, so the v0.4.0
//      bit-identity guarantee is structural rather than a numerical
//      coincidence. RMS runs a one-pole on the squared key signal
//      (THAT2252-style mean-square detection):
//
//        ms[n] = a * ms[n-1] + (1 - a) * key[n]^2,  a = exp(-1 / (tau_rms * fs))
//        m[n]  = sqrt (ms[n] + 1e-30)
//
//      with tau_rms = max (attackMs, 5 ms) and the resulting magnitude then
//      run through the same attack/release ballistics as the Peak path, so
//      switching law never changes which smoothing chain is in circuit. The
//      1e-30 floor is the denormal/log guard.
//
//   2. Program-dependent auto release: the validated dual-time-constant
//      approximation of the SSL-lineage dual-RC ladder. A fast branching
//      one-pole (release 0.15 s) runs in parallel with a slow reservoir
//      (charge 0.6 s, release 4 s) and the envelope is their maximum, so a
//      brief peak recovers on the fast constant while sustained gain
//      reduction grows a multi-second tail. When enabled, the band's Release
//      knob scales BOTH branch release constants by releaseMs / 300 ms, so
//      the 300 ms default region reproduces the reference constants exactly.
//
//   3. Variable stereo link (0-100%). The link blend is applied to the
//      *detector inputs*, before the ballistics, so at 100% both channels
//      integrate literally the same sequence of values and therefore end up
//      with bit-identical envelope state (the shared-gain invariant):
//
//        m_link  = max over channels of m_ch
//        m_ch'   = (1 - lambda) * m_ch + lambda * m_link
//
//      In a band running Mid/Side, channels 0/1 are Mid/Side by the time the
//      detector sees them, so the same blend links the M/S pair.
//
// Neutral path (Peak + Clean character + auto release off + link settled at
// exactly 0%): isNeutral() returns true and BandCompressor calls
// processSampleLegacy(), which is *literally* the v0.4.0 statement. No extra
// arithmetic touches the signal at all, which is what makes the v0.4.0
// same-binary A/B identity test (T1/T6b) a structural guarantee.
//
// Real-time safe: every buffer is sized in prepare(), nothing allocates or
// locks in processFrame().
class Detector
{
public:
    // Detection law. Peak is the v0.4.0 behaviour and the neutral default.
    enum class Law
    {
        peak = 0,
        rms = 1
    };

    // Per-band character. Clean is the v0.4.0 behaviour and the neutral
    // default; VCA statically approximates the feedback-compressor loop (see
    // vcaKneeWidthDb()/vcaAttackScale() below). Deliberately no
    // nonlinearity/harmonics stage - the modelled behaviour is envelope
    // behaviour, not distortion, so no oversampling is implied.
    enum class Character
    {
        clean = 0,
        vca = 1
    };

    Detector() = default;

    // Allocates all per-channel state. Must be called before processFrame()
    // and again on any sample-rate/channel-count change.
    void prepare (const juce::dsp::ProcessSpec& spec);

    // Clears envelope state without deallocating. Audio-thread safe.
    void reset();

    void setLaw (Law newLaw) noexcept { law = newLaw; }
    void setCharacter (Character newCharacter) noexcept { character = newCharacter; }
    void setAutoReleaseEnabled (bool shouldBeEnabled) noexcept { autoReleaseEnabled = shouldBeEnabled; }

    // 0-100%. Smoothed over the house 50 ms constant; 0% is the neutral,
    // structurally-bypassed legacy path.
    void setStereoLinkPercent (float newPercent) noexcept;

    void setAttackMs (float newAttackMs) noexcept { attackMs = newAttackMs; }
    void setReleaseMs (float newReleaseMs) noexcept { releaseMs = newReleaseMs; }

    // The band's compression ratio, used only by VCA character (effective
    // attack scaling and the emergent-knee width table).
    void setRatio (float newRatio) noexcept { ratio = newRatio; }

    // Resolves the smoothed link amount and re-derives every coefficient for
    // the coming block. Must be called once per block before processFrame().
    void updateForBlock (int numSamples) noexcept;

    // True when the block resolved to the exact v0.4.0 code path.
    bool isNeutral() const noexcept { return neutralPath; }

    // The literal v0.4.0 statement - see the class comment.
    float processSampleLegacy (int channel, float input) noexcept
    {
        return ballistics.processSample (channel, input);
    }

    // One frame across every channel at once (the stereo link needs both
    // channels' key magnitudes at the same sample index). `keys` and
    // `envelopes` must each hold `numChannels` entries; `numChannels` must
    // not exceed the channel count passed to prepare().
    void processFrame (const float* keys, size_t numChannels, float* envelopes) noexcept;

    // The effective knee percent BandCompressor should hand to
    // trpt::computeGainLinear() for this block: the commanded knee in Clean
    // character, or the VCA emergent-knee mapping in VCA character. Crossfaded
    // over 10 ms on a character change (see updateForBlock()).
    float resolveKneePercent (float commandedKneePercent, float thresholdDb, float channelRatio) const noexcept;

private:
    // Reference dual-time-constant values (seconds) for auto release, and the
    // Release-knob value at which they are reproduced exactly.
    static constexpr float autoFastReleaseSeconds = 0.15f;
    static constexpr float autoSlowChargeSeconds = 0.6f;
    static constexpr float autoSlowReleaseSeconds = 4.0f;
    static constexpr float autoReleaseReferenceMs = 300.0f;

    // RMS detection time constant floor - below this the mean-square one-pole
    // stops smoothing the squared signal's own ripple usefully.
    static constexpr float minimumRmsTimeConstantMs = 5.0f;

    static constexpr double smoothingTimeSeconds = 0.05;
    static constexpr double characterCrossfadeSeconds = 0.01;

    static float onePoleCoefficient (float timeConstantSeconds, double sampleRate) noexcept;

    juce::dsp::BallisticsFilter<float> ballistics;

    Law law = Law::peak;
    Character character = Character::clean;
    bool autoReleaseEnabled = false;
    float attackMs = 10.0f;
    float releaseMs = 100.0f;
    float ratio = 4.0f;
    float lastLinkPercent = 0.0f;

    // Resolved once per block by updateForBlock().
    bool neutralPath = true;
    float blockLink = 0.0f;
    float blockCharacterBlend = 0.0f;
    float blockRmsCoefficient = 0.0f;
    float blockSlowChargeCoefficient = 0.0f;
    float blockSlowReleaseCoefficient = 0.0f;

    // Link is a real-time-varying scalar, so it rides the house 50 ms linear
    // smoother; the character blend is the 10 ms coefficient crossfade that
    // keeps a Clean <-> VCA switch from stepping the knee/attack coefficients.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> linkSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> characterSmoothed;

    // Per-channel mean-square (RMS law) and slow-reservoir (auto release)
    // state, sized in prepare() and never reallocated on the audio thread.
    std::vector<float> meanSquareState;
    std::vector<float> slowEnvelopeState;

    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Detector)
};

namespace trpt
{
    // VCA character's emergent soft-knee width, in dB, as a function of the
    // band's ratio: the feedback loop's static curve rounds more at low
    // ratios than at high ones. Anchors {2:1 -> 6 dB, 4:1 -> 4 dB,
    // 10:1 -> 3 dB}, log-interpolated in ratio between them and clamped to
    // [3, 6] outside. Ratios below 1:1 (upward processing) mirror the table
    // through 1/ratio.
    float vcaKneeWidthDb (float ratio) noexcept;

    // The knee percent that reproduces vcaKneeWidthDb() through
    // KneeGainComputer's threshold-relative (Weiss-model) knee, whose half
    // width is |thresholdDb| * kneePercent / 100:
    //
    //   kneePercent_eff = min (100, 100 * (W / 2) / max (|thresholdDb|, W / 2))
    //
    // The max() in the denominator is load-bearing: the exact conversion
    // 100 * (W/2) / |T| divides by zero at T = 0 dB (which is inside the
    // shipped -60..0 dB Threshold range) and exceeds 100% for |T| < W/2,
    // where the target width is unreachable at ANY knee percent. Clamping
    // there degrades the achieved width gracefully to 2 * |T| (the Weiss
    // model's own maximum), reaching a hard knee at exactly T = 0 dB with no
    // division by zero, NaN or Inf anywhere in range. That degradation band
    // (T above -3 dB at the widest table width) is documented in
    // docs/manual.md.
    float vcaKneePercent (float ratio, float thresholdDb) noexcept;

    // VCA character's effective-attack scale factor:
    //
    //   tau_att_eff = tau_att / (1 + k),   k = ratio - 1  (k = 1/ratio - 1
    //                                                      below 1:1),
    //   k clamped to >= 0.01.
    //
    // Higher ratio means faster effective attack - the loop-speedup signature
    // of a feedback topology, reproduced statically. The source derivation is
    // that a feedback compressor's loop integrates the *residual* overshoot,
    // which divides the loop time constant by (1 + k).
    //
    // DEVIATION FROM THE BRIEF (documented in the PR): brief section 3.2 item
    // 2 writes this factor as `k / (1 + k)`, which contradicts both its own
    // stated rationale ("higher ratio => faster effective attack") and test
    // T17's binding ordering (10:1 faster than 4:1 faster than 2:1) - the
    // `k / (1 + k)` form makes higher ratios *slower*, and degenerates to an
    // instantaneous attack at 1:1, where a feedback loop that is doing nothing
    // must leave the time constant untouched. `1 / (1 + k)` reproduces the
    // documented intent, the research derivation, and T17.
    float vcaAttackScale (float ratio) noexcept;
}
