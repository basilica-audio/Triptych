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

    // Adds the five per-band parameters (Threshold, Ratio, Attack, Release,
    // Makeup) shared identically by the Low/Mid/High bands, so the actual
    // values/ranges below live in exactly one place rather than being
    // repeated three times with room for the copies to drift apart.
    void addBandParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                             const char* thresholdId,
                             const char* ratioId,
                             const char* attackId,
                             const char* releaseId,
                             const char* makeupId,
                             const juce::String& labelPrefix)
    {
        // Threshold: -60 to 0 dB, default -18 dB.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { thresholdId, 1 },
            labelPrefix + " Threshold",
            juce::NormalisableRange<float> (-60.0f, 0.0f, 0.01f),
            -18.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        // Ratio: 1:1 (no compression) to 20:1, default 4:1.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ratioId, 1 },
            labelPrefix + " Ratio",
            juce::NormalisableRange<float> (1.0f, 20.0f, 0.01f),
            4.0f,
            juce::AudioParameterFloatAttributes().withLabel (":1")));

        // Attack: 0.1-100 ms, default 10 ms.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { attackId, 1 },
            labelPrefix + " Attack",
            makeLogRange (0.1f, 100.0f),
            10.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        // Release: 10-1000 ms, default 100 ms.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { releaseId, 1 },
            labelPrefix + " Release",
            makeLogRange (10.0f, 1000.0f),
            100.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        // Makeup: -12 to +24 dB, default 0 dB.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { makeupId, 1 },
            labelPrefix + " Makeup",
            juce::NormalisableRange<float> (-12.0f, 24.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));
    }
}

namespace trpt
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        //======================================================================
        // Low/Mid split: 40-1000 Hz, default 200 Hz.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::lowMidSplit, 1 },
            "Low/Mid Split",
            makeLogRange (40.0f, 1000.0f),
            200.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        //======================================================================
        // Mid/High split: 400-12000 Hz, default 3000 Hz. Deliberately
        // overlaps the Low/Mid range at the edges - TriptychEngine enforces
        // a minimum runtime separation between the two rather than the
        // ranges themselves being disjoint, so a user can still, e.g., set a
        // fairly high Low/Mid split and a fairly low Mid/High split for a
        // narrow Mid band.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::midHighSplit, 1 },
            "Mid/High Split",
            makeLogRange (400.0f, 12000.0f),
            3000.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        //======================================================================
        addBandParameters (layout,
                            ParamIDs::lowThreshold, ParamIDs::lowRatio, ParamIDs::lowAttack, ParamIDs::lowRelease, ParamIDs::lowMakeup,
                            "Low");

        addBandParameters (layout,
                            ParamIDs::midThreshold, ParamIDs::midRatio, ParamIDs::midAttack, ParamIDs::midRelease, ParamIDs::midMakeup,
                            "Mid");

        addBandParameters (layout,
                            ParamIDs::highThreshold, ParamIDs::highRatio, ParamIDs::highAttack, ParamIDs::highRelease, ParamIDs::highMakeup,
                            "High");

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
