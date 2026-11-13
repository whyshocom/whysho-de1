#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// ===========================================================================
//  Whysho DE1 — parameter model
//  A single source of truth for every automatable parameter in the plugin.
// ===========================================================================
namespace wsho
{
// --- Enumerations (indices MUST stay stable: they are serialized) ----------

// How the gain reduction is applied to the audio.
//  Wide  — the whole signal is turned down while sibilance is detected.
//  Split — only the highs above the frequency are reduced (dynamic shelf).
//  Bell  — only a band around the frequency is reduced (dynamic bell, uses Q).
enum class Mode { Wide = 0, Split, Bell };
static const juce::StringArray kModeNames { "Wide", "Split", "Bell" };

// How the detector level is compared against the threshold.
//  Relative — sibilant band level relative to the full-band level, so the
//             de-esser keeps working when the vocal gets louder / quieter.
//  Absolute — sibilant band level in dBFS (classic de-esser behaviour).
enum class Detection { Relative = 0, Absolute };
static const juce::StringArray kDetectionNames { "Relative", "Absolute" };

enum class ChannelMode { Stereo = 0, Mid, Side };
static const juce::StringArray kChannelModeNames { "Stereo", "Mid", "Side" };

// Audition modes.
//  Trigger — hear what the detector hears (the sibilance band).
//  Delta   — hear only what is being removed.
enum class Listen { Off = 0, Trigger, Delta };
static const juce::StringArray kListenNames { "Listen: Off", "Listen: Trigger", "Listen: Delta" };

// Fixed gain-computer character: steep ratio with a soft knee, the depth is
// capped by the Range parameter. Keeps the UI to the controls that matter.
static constexpr float kRatio  = 6.0f;
static constexpr float kKneeDb = 6.0f;
static constexpr float kMaxLookaheadMs = 10.0f;

// --- Parameter IDs ---------------------------------------------------------
namespace pid
{
    constexpr const char* thresh    = "thresh";     // dB
    constexpr const char* range     = "range";      // dB (max reduction)
    constexpr const char* freq      = "freq";       // Hz
    constexpr const char* q         = "q";
    constexpr const char* mode      = "mode";
    constexpr const char* detect    = "detect";
    constexpr const char* chan      = "chan";
    constexpr const char* listen    = "listen";
    constexpr const char* attack    = "attack";     // ms
    constexpr const char* release   = "release";    // ms
    constexpr const char* lookahead = "lookahead";  // ms
    constexpr const char* extSC     = "ext_sc";     // key from external sidechain

    constexpr const char* inGain    = "in_gain";
    constexpr const char* outGain   = "out_gain";
    constexpr const char* mix       = "mix";
    constexpr const char* bypass    = "bypass";
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace wsho
