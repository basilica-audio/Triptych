#include "ParameterLayout.h"
#include "ParameterIds.h"

namespace
{
    // True logarithmic (base-10) mapping, so slider/knob travel spends equal
    // space per octave/decade rather than per linear unit. Used for both
    // frequency (Hz) and time-constant (ms) parameters, which are both
    // perceived logarithmically. Uses juce::mapToLog10/mapFromLog10 rather
    // than NormalisableRange's built-in power-law skew, which only
    // approximates a log curve.
    juce::NormalisableRange<float> makeLogRange (float rangeMin, float rangeMax)
    {
        return juce::NormalisableRange<float> (
            rangeMin,
            rangeMax,
            [] (float start, float end, float normalised)
            { return juce::mapToLog10 (normalised, start, end); },
            [] (float start, float end, float value)
            { return juce::mapFromLog10 (value, start, end); });
    }

    // Ratio's lower bound (v0.3.0 / docs/design-brief-v3-dynamics.md): 0.2
    // (i.e. "1:5") is the Weiss DS1-MK3 manual's own documented lower
    // endpoint ("adjustable from 1000:1 to 1:5") for what it calls "upward
    // expansion (for over-compressed signals)" - see docs/research-notes.md.
    // Values below 1:1 boost signal above threshold instead of cutting it,
    // continuously tapering through the exact null at 1:1 (see
    // KneeGainComputer.h). The upper bound (20:1) is unchanged from v0.2.0.
    constexpr float ratioMin = 0.2f;
    constexpr float ratioMax = 20.0f;

    // Adds the six per-band parameters (Threshold, Ratio, Knee, Attack,
    // Release, Makeup) shared *structurally* by the Low/Mid/High bands, so
    // the ranges/units live in exactly one place - but per docs/design-brief.md
    // (v0.2.0's research-derived recalibration), Threshold/Ratio/Attack/
    // Release *defaults* now differ per band, passed in explicitly rather
    // than hard-coded, replacing v0.1's single uniform default copy-pasted
    // three times. Knee and Makeup keep one shared default across all three
    // bands - no sourced reason found for a per-band Knee/Makeup default (see
    // docs/research-notes.md).
    void addBandParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                             const char* thresholdId,
                             const char* ratioId,
                             const char* kneeId,
                             const char* attackId,
                             const char* releaseId,
                             const char* makeupId,
                             const juce::String& labelPrefix,
                             float thresholdDefaultDb,
                             float ratioDefault,
                             float attackDefaultMs,
                             float releaseDefaultMs)
    {
        // Threshold: -60 to 0 dB, per-band default (see call sites below).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { thresholdId, 1 },
            labelPrefix + " Threshold",
            juce::NormalisableRange<float> (-60.0f, 0.0f, 0.01f),
            thresholdDefaultDb,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        // Ratio: 0.2:1 (upward, v0.3.0) through 1:1 (no processing) to 20:1
        // (downward), per-band default. Skewed so 1:1 - the exact
        // boundary between upward and downward processing - sits at the
        // knob's centre travel position rather than being crowded toward
        // one end. See ratioMin's own doc comment above for the sourced
        // lower bound.
        auto ratioRange = juce::NormalisableRange<float> (ratioMin, ratioMax, 0.01f);
        ratioRange.setSkewForCentre (1.0f);

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ratioId, 1 },
            labelPrefix + " Ratio",
            ratioRange,
            ratioDefault,
            juce::AudioParameterFloatAttributes().withLabel (":1")));

        // Knee (new in v0.2.0): 0-100%, default 50% on every band. 0% is an
        // exact hard knee (v0.1's prior behaviour, preserved bit-for-bit at
        // this extreme - see src/dsp/KneeGainComputer.h); 100% is the widest
        // soft-knee transition, extent scaled to twice the threshold-to-0dB
        // span (Weiss DS1-MK3 "0 to twice the threshold value" convention,
        // see docs/research-notes.md).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { kneeId, 1 },
            labelPrefix + " Knee",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f),
            50.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        // Attack: 0.1-100 ms, per-band default.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { attackId, 1 },
            labelPrefix + " Attack",
            makeLogRange (0.1f, 100.0f),
            attackDefaultMs,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        // Release: 10-1000 ms, per-band default.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { releaseId, 1 },
            labelPrefix + " Release",
            makeLogRange (10.0f, 1000.0f),
            releaseDefaultMs,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        // Makeup: -12 to +24 dB, default 0 dB on every band - a calibration
        // trim, not a voicing parameter (see docs/design-brief.md).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { makeupId, 1 },
            labelPrefix + " Makeup",
            juce::NormalisableRange<float> (-12.0f, 24.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));
    }

    // Adds a band's Mute/Solo pair, both defaulting to off so adding these
    // parameters never changes existing default behaviour.
    void addMuteSoloParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                                 const char* muteId,
                                 const char* soloId,
                                 const juce::String& labelPrefix)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { muteId, 1 }, labelPrefix + " Mute", false));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { soloId, 1 }, labelPrefix + " Solo", false));
    }

    // Range (v0.3.0 / docs/design-brief-v3-dynamics.md): a band's maximum
    // gain-change clamp, off by default so adding these parameters never
    // changes existing (v0.2.0) default behaviour - see
    // src/dsp/KneeGainComputer.h/BandCompressor.h. The 0-30 dB range and its
    // 12 dB "if you turn it on" default are a reasoned engineering choice
    // (the reference class - FabFilter Pro-MB's Range knob, Waves C6's Range
    // paradigm - documents the *concept* of a maximum-gain-change clamp but
    // not a specific numeric range; see docs/research-notes.md's v0.3.0
    // addendum), not sourced to a specific manual number.
    void addRangeParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                              const char* rangeEnabledId,
                              const char* rangeId,
                              const juce::String& labelPrefix)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { rangeEnabledId, 1 }, labelPrefix + " Range Enabled", false));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { rangeId, 1 },
            labelPrefix + " Range",
            juce::NormalisableRange<float> (0.0f, 30.0f, 0.01f),
            12.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));
    }

    // Per-band Mid/Side processing (v0.4.0 / GitHub issue #24): off by
    // default so adding these parameters never changes existing default
    // behaviour. Side Threshold/Ratio share the same ranges as the band's
    // own main Threshold/Ratio (see addBandParameters() above) for
    // consistency, with Side Ratio defaulting to 1:1 (bit-exact bypass) -
    // so simply enabling M/S with no further tweaking compresses only the
    // Mid (centre) component using the band's existing Threshold/Ratio,
    // leaving Side untouched, a musically sensible starting point (tighten
    // the centre without touching stereo width) rather than an arbitrary
    // doubled default. Side Threshold defaults to the same value as the
    // band's own main Threshold default, so a user who *does* raise Side
    // Ratio above 1:1 starts from a coherent operating point.
    void addMidSideParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                                const char* midSideEnabledId,
                                const char* sideThresholdId,
                                const char* sideRatioId,
                                const juce::String& labelPrefix,
                                float sideThresholdDefaultDb)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { midSideEnabledId, 1 }, labelPrefix + " M/S Enabled", false));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { sideThresholdId, 1 },
            labelPrefix + " Side Threshold",
            juce::NormalisableRange<float> (-60.0f, 0.0f, 0.01f),
            sideThresholdDefaultDb,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        auto sideRatioRange = juce::NormalisableRange<float> (ratioMin, ratioMax, 0.01f);
        sideRatioRange.setSkewForCentre (1.0f);

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { sideRatioId, 1 },
            labelPrefix + " Side Ratio",
            sideRatioRange,
            1.0f,
            juce::AudioParameterFloatAttributes().withLabel (":1")));
    }
}

namespace trpt
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        //======================================================================
        // Low/Mid split: 40-1000 Hz, default 200 Hz. Unchanged in v0.2.0 -
        // research confirms this default already sits inside the reference
        // class's converged starting-point range (see docs/research-notes.md).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::lowMidSplit, 1 },
            "Low/Mid Split",
            makeLogRange (40.0f, 1000.0f),
            200.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        //======================================================================
        // Mid/High split: 400-12000 Hz, default 3000 Hz. Unchanged in
        // v0.2.0 (see above). Deliberately overlaps the Low/Mid range at the
        // edges - TriptychEngine enforces a minimum runtime separation
        // between the two rather than the ranges themselves being disjoint,
        // so a user can still, e.g., set a fairly high Low/Mid split and a
        // fairly low Mid/High split for a narrow Mid band.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::midHighSplit, 1 },
            "Mid/High Split",
            makeLogRange (400.0f, 12000.0f),
            3000.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        //======================================================================
        // v0.2.0 per-band defaults (docs/design-brief.md, sourced in
        // docs/research-notes.md): Low leans moderate/peak-control, Mid
        // leans density/knit-together, High leans peak-control with faster
        // ballistics - replacing v0.1's single uniform default (threshold
        // -18 dB, ratio 4:1, attack 10 ms, release 100 ms) copy-pasted three
        // times. Attack/Release also now diverge per band
        // (lowAttack > midAttack > highAttack; lowRelease > midRelease >
        // highRelease - see tests/VoicingGuaranteesTests.cpp's standing
        // invariant test).
        addBandParameters (layout,
                            ParamIDs::lowThreshold, ParamIDs::lowRatio, ParamIDs::lowKnee, ParamIDs::lowAttack, ParamIDs::lowRelease, ParamIDs::lowMakeup,
                            "Low",
                            -24.0f, 2.5f, 25.0f, 180.0f);

        addBandParameters (layout,
                            ParamIDs::midThreshold, ParamIDs::midRatio, ParamIDs::midKnee, ParamIDs::midAttack, ParamIDs::midRelease, ParamIDs::midMakeup,
                            "Mid",
                            -30.0f, 1.8f, 10.0f, 100.0f);

        addBandParameters (layout,
                            ParamIDs::highThreshold, ParamIDs::highRatio, ParamIDs::highKnee, ParamIDs::highAttack, ParamIDs::highRelease, ParamIDs::highMakeup,
                            "High",
                            -20.0f, 2.0f, 5.0f, 55.0f);

        //======================================================================
        // Range (v0.3.0). See addRangeParameters()'s doc comment above.
        addRangeParameters (layout, ParamIDs::lowRangeEnabled, ParamIDs::lowRange, "Low");
        addRangeParameters (layout, ParamIDs::midRangeEnabled, ParamIDs::midRange, "Mid");
        addRangeParameters (layout, ParamIDs::highRangeEnabled, ParamIDs::highRange, "High");

        //======================================================================
        // Per-band Mid/Side processing (v0.4.0 / issue #24). See
        // addMidSideParameters()'s doc comment above.
        addMidSideParameters (layout, ParamIDs::lowMidSideEnabled, ParamIDs::lowSideThreshold, ParamIDs::lowSideRatio, "Low", -24.0f);
        addMidSideParameters (layout, ParamIDs::midMidSideEnabled, ParamIDs::midSideThreshold, ParamIDs::midSideRatio, "Mid", -30.0f);
        addMidSideParameters (layout, ParamIDs::highMidSideEnabled, ParamIDs::highSideThreshold, ParamIDs::highSideRatio, "High", -20.0f);

        //======================================================================
        // Per-band Mute/Solo (M1). See ParameterIds.h for the console-style
        // semantics (Mute always wins; Solo isolates unmuted soloed bands).
        addMuteSoloParameters (layout, ParamIDs::lowMute, ParamIDs::lowSolo, "Low");
        addMuteSoloParameters (layout, ParamIDs::midMute, ParamIDs::midSolo, "Mid");
        addMuteSoloParameters (layout, ParamIDs::highMute, ParamIDs::highSolo, "High");

        //======================================================================
        // High-band limiter option (M1): off by default so adding it never
        // changes existing default behaviour. Threshold -24 to 0 dB, default
        // -3 dB (a typical safety-ceiling setting for cymbal/harmonic peaks).
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::highLimiterEnabled, 1 }, "High Limiter Enabled", false));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::highLimiterThreshold, 1 },
            "High Limiter Threshold",
            juce::NormalisableRange<float> (-24.0f, 0.0f, 0.01f),
            -3.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        //======================================================================
        // Output: master trim after the three bands are summed, -24 to +24 dB.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::output, 1 },
            "Output",
            juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        return layout;
    }
}
