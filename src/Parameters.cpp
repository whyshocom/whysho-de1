#include "Parameters.h"

namespace wsho
{
using APVTS = juce::AudioProcessorValueTreeState;
using Range = juce::NormalisableRange<float>;

static Range freqRange()
{
    Range r (1000.0f, 16000.0f, 1.0f);
    r.setSkewForCentre (5500.0f);
    return r;
}
static Range qRange()
{
    Range r (0.3f, 8.0f, 0.01f);
    r.setSkewForCentre (1.5f);
    return r;
}

APVTS::ParameterLayout createParameterLayout()
{
    APVTS::ParameterLayout layout;
    using FP = juce::AudioParameterFloat;
    using BP = juce::AudioParameterBool;
    using CP = juce::AudioParameterChoice;

    auto dbAttr = juce::AudioParameterFloatAttributes().withLabel ("dB");
    auto msAttr = juce::AudioParameterFloatAttributes().withLabel ("ms");
    auto hzAttr = juce::AudioParameterFloatAttributes().withLabel ("Hz")
        .withStringFromValueFunction ([] (float v, int)
        { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " k" : juce::String ((int) v); });
    auto pctAttr = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });

    // ---- De-esser core ----
    layout.add (std::make_unique<FP> (juce::ParameterID { pid::thresh, 1 },    "Threshold", Range (-60.0f, 0.0f, 0.1f), -18.0f, dbAttr));
    layout.add (std::make_unique<FP> (juce::ParameterID { pid::range, 1 },     "Range",     Range (0.0f, 24.0f, 0.1f), 8.0f, dbAttr));
    layout.add (std::make_unique<FP> (juce::ParameterID { pid::freq, 1 },      "Frequency", freqRange(), 6000.0f, hzAttr));
    layout.add (std::make_unique<FP> (juce::ParameterID { pid::q, 1 },         "Q",         qRange(), 1.5f));
    layout.add (std::make_unique<CP> (juce::ParameterID { pid::mode, 1 },      "Mode",      kModeNames, (int) Mode::Split));
    layout.add (std::make_unique<CP> (juce::ParameterID { pid::detect, 1 },    "Detection", kDetectionNames, (int) Detection::Relative));
    layout.add (std::make_unique<CP> (juce::ParameterID { pid::chan, 1 },      "Channel",   kChannelModeNames, (int) ChannelMode::Stereo));
    layout.add (std::make_unique<CP> (juce::ParameterID { pid::listen, 1 },    "Listen",    kListenNames, (int) Listen::Off));
    layout.add (std::make_unique<FP> (juce::ParameterID { pid::attack, 1 },    "Attack",    Range (0.1f, 20.0f, 0.01f, 0.4f), 1.0f, msAttr));
    layout.add (std::make_unique<FP> (juce::ParameterID { pid::release, 1 },   "Release",   Range (5.0f, 300.0f, 0.1f, 0.5f), 60.0f, msAttr));
    layout.add (std::make_unique<FP> (juce::ParameterID { pid::lookahead, 1 }, "Lookahead", Range (0.0f, kMaxLookaheadMs, 0.01f), 2.0f, msAttr));
    layout.add (std::make_unique<BP> (juce::ParameterID { pid::extSC, 1 },     "External Sidechain", false));

    // ---- Global ----
    layout.add (std::make_unique<FP> (juce::ParameterID { pid::inGain, 1 },  "Input Gain",  Range (-24.0f, 24.0f, 0.01f), 0.0f, dbAttr));
    layout.add (std::make_unique<FP> (juce::ParameterID { pid::outGain, 1 }, "Output Gain", Range (-24.0f, 24.0f, 0.01f), 0.0f, dbAttr));
    layout.add (std::make_unique<FP> (juce::ParameterID { pid::mix, 1 },     "Mix", Range (0.0f, 1.0f, 0.001f), 1.0f, pctAttr));
    layout.add (std::make_unique<BP> (juce::ParameterID { pid::bypass, 1 },  "Bypass", false));

    return layout;
}

} // namespace wsho
