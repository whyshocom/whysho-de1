#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>
#include <vector>
#include "Biquad.h"
#include "../Parameters.h"

namespace wsho::dsp
{
// Snapshot of every parameter the engine needs, built once per block.
struct EngineParams
{
    float threshDb = -18.0f, rangeDb = 8.0f, freq = 6000.0f, q = 1.5f;
    Mode mode = Mode::Split;
    Detection detect = Detection::Relative;
    ChannelMode chan = ChannelMode::Stereo;
    Listen listen = Listen::Off;
    float attackMs = 1.0f, releaseMs = 60.0f, lookaheadMs = 2.0f;
    bool  extSC = false;
    float inGainDb = 0.0f, outGainDb = 0.0f, mix = 1.0f;
    bool  bypass = false;
};

// One point of the scrolling history display (~10 ms of audio).
struct HistoryPoint
{
    float inputDb  = -120.0f;  // full-band input peak (dBFS)
    float detectDb = -120.0f;  // detector level, i.e. the value compared to threshold
    float grDb     = 0.0f;     // gain reduction (positive dB)
};

// ===========================================================================
//  The de-esser.
//
//  Detector:  key signal -> detection filter (4th-order HP at Freq, or a band
//             pass for Bell) -> peak envelope (attack / release) -> level in dB,
//             absolute or relative to the full-band envelope -> soft-knee gain
//             computer, capped at Range.
//  Audio:     the main signal is delayed by the lookahead time, then reduced:
//               Wide  : y * g
//               Split : 2nd-order high shelf at Freq, gain = -GR (coefficients
//                       follow the reduction at control rate, 8 samples)
//               Bell  : y - (1-g) * BP(y)    (exactly a notch at g = 0; the
//                       complementary form needs no coefficient updates)
// ===========================================================================
class DeEsserEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    void setParameters (const EngineParams& p);

    // `sidechain` may be empty (0 channels) when no external key is connected.
    void process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>& sidechain);

    int    getLatencySamples() const noexcept { return latency; }
    double getSampleRate() const noexcept { return fs; }

    // --- metering (written on the audio thread, read by the GUI) ---
    float currentGrDb() const noexcept     { return meterGr.load (std::memory_order_relaxed); }
    float currentDetectDb() const noexcept { return meterDetect.load (std::memory_order_relaxed); }

    // GUI thread: pops every pending history point (oldest first).
    template <typename Fn> void drainHistory (Fn&& fn)
    {
        const auto scope = historyFifo.read (historyFifo.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i) fn (history[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i) fn (history[(size_t) (scope.startIndex2 + i)]);
    }

    // Static helper so the GUI can draw the current reduction curve.
    static float reductionResponseDb (Mode mode, float hz, float freq, float q, float grDb);

private:
    void updateFilters();
    float gainComputer (float levelDb) const noexcept;
    void pushHistory (const HistoryPoint& p) noexcept;

    double fs = 44100.0;
    int    numCh = 2;
    EngineParams params;
    bool   filtersDirty = true;
    float  lastFreq = -1.0f, lastQ = -1.0f; Mode lastMode = Mode::Wide; double lastFs = 0.0;

    // detection filters: [channel 0, channel 1, mono key] x 2 sections
    BiquadCoeffs detC[2];
    BiquadState  detS[3][2];
    int          detSections = 2;

    // processing filters (per channel)
    BiquadCoeffs procShelf, procBP;
    BiquadState  procS[2];
    float shelfGr = 0.0f; int shelfCountdown = 0;

    // envelopes (linear)
    float envSib = 0.0f, envFull = 0.0f;
    float attCoef = 0.0f, relCoef = 0.0f;

    // lookahead delay
    std::vector<float> delayBuf[2];
    int delayMask = 0, writePos = 0, latency = 0;

    juce::SmoothedValue<float> inGain, outGain, mixSm;

    // metering / history
    std::atomic<float> meterGr { 0.0f }, meterDetect { -120.0f };
    static constexpr int kHistorySize = 2048;
    std::array<HistoryPoint, kHistorySize> history {};
    juce::AbstractFifo historyFifo { kHistorySize };
    HistoryPoint chunk;
    int chunkLen = 441, chunkPos = 0;
};

} // namespace wsho::dsp
