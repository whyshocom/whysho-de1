#include "DeEsserEngine.h"
#include <complex>

namespace wsho::dsp
{
static inline float toDb (float g) noexcept   { return g > 1.0e-6f ? 20.0f * std::log10 (g) : -120.0f; }
static inline float fromDb (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

void DeEsserEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (maxBlockSize);
    fs = sampleRate > 0.0 ? sampleRate : 44100.0;
    numCh = juce::jlimit (1, 2, numChannels);

    const int maxDelay = (int) std::ceil (kMaxLookaheadMs * 0.001 * fs) + 2;
    int size = 1; while (size < maxDelay) size <<= 1;
    for (auto& d : delayBuf) d.assign ((size_t) size, 0.0f);
    delayMask = size - 1;

    inGain.reset (fs, 0.02);  outGain.reset (fs, 0.02);  mixSm.reset (fs, 0.02);
    inGain.setCurrentAndTargetValue (fromDb (params.inGainDb));
    outGain.setCurrentAndTargetValue (fromDb (params.outGainDb));
    mixSm.setCurrentAndTargetValue (params.mix);

    chunkLen = juce::jmax (1, (int) (fs * 0.010));
    filtersDirty = true;
    reset();
    updateFilters();
}

void DeEsserEngine::reset()
{
    for (auto& ch : detS) for (auto& s : ch) s.reset();
    for (auto& s : procS) s.reset();
    for (auto& d : delayBuf) std::fill (d.begin(), d.end(), 0.0f);
    writePos = 0;
    envSib = envFull = 0.0f;
    shelfGr = 0.0f; shelfCountdown = 0;
    chunk = {}; chunkPos = 0;
    meterGr = 0.0f; meterDetect = -120.0f;
}

void DeEsserEngine::setParameters (const EngineParams& p)
{
    params = p;

    if (p.freq != lastFreq || p.q != lastQ || p.mode != lastMode || fs != lastFs)
        filtersDirty = true;

    // envelope coefficients (time to ~63 %)
    attCoef = std::exp (-1.0f / (float) (juce::jmax (0.01f, p.attackMs)  * 0.001 * fs));
    relCoef = std::exp (-1.0f / (float) (juce::jmax (1.0f,  p.releaseMs) * 0.001 * fs));

    latency = juce::jlimit (0, delayMask - 1, (int) std::round (p.lookaheadMs * 0.001 * fs));

    inGain.setTargetValue (fromDb (p.inGainDb));
    outGain.setTargetValue (fromDb (p.outGainDb));
    mixSm.setTargetValue (p.mix);

    if (filtersDirty) updateFilters();
}

void DeEsserEngine::updateFilters()
{
    const double f = juce::jlimit (200.0, fs * 0.45, (double) params.freq);
    if (params.mode != lastMode)   // HP and BP don't share a meaningful state
    {
        for (auto& s : procS) s.reset();
        for (auto& c : detS) for (auto& s : c) s.reset();
    }
    if (params.mode == Mode::Bell)
    {
        detC[0] = makeBandPass (fs, f, params.q);
        detSections = 1;
    }
    else
    {
        // 4th-order Butterworth high pass: two sections, Q = 0.5412 / 1.3066
        detC[0] = makePass (fs, f, 0.54119610, true);
        detC[1] = makePass (fs, f, 1.30656296, true);
        detSections = 2;
    }
    procBP = makeBandPass (fs, f, params.q);
    procShelf = makeShelf (fs, f, -shelfGr, 1.0, true);
    shelfCountdown = 0;

    lastFreq = params.freq; lastQ = params.q; lastMode = params.mode; lastFs = fs;
    filtersDirty = false;
}

float DeEsserEngine::gainComputer (float levelDb) const noexcept
{
    const float over  = levelDb - params.threshDb;
    const float slope = 1.0f - 1.0f / kRatio;
    float gr;
    if (over <= -kKneeDb * 0.5f)      gr = 0.0f;
    else if (over < kKneeDb * 0.5f)   { const float t = over + kKneeDb * 0.5f; gr = slope * t * t / (2.0f * kKneeDb); }
    else                              gr = slope * over;
    return juce::jlimit (0.0f, juce::jmax (0.0f, params.rangeDb), gr);
}

void DeEsserEngine::pushHistory (const HistoryPoint& p) noexcept
{
    if (historyFifo.getFreeSpace() < 1) return; // GUI not draining (closed) — drop
    const auto scope = historyFifo.write (1);
    if (scope.blockSize1 > 0) history[(size_t) scope.startIndex1] = p;
    else if (scope.blockSize2 > 0) history[(size_t) scope.startIndex2] = p;
}

void DeEsserEngine::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>& sidechain)
{
    const int n  = buffer.getNumSamples();
    const int ch = juce::jmin (2, buffer.getNumChannels());
    if (n == 0 || ch == 0) return;

    float* io[2] = { buffer.getWritePointer (0), buffer.getWritePointer (ch > 1 ? 1 : 0) };
    const bool stereo = ch > 1;
    const bool useSC  = params.extSC && sidechain.getNumChannels() > 0 && sidechain.getNumSamples() >= n;
    const bool ms     = stereo && params.chan != ChannelMode::Stereo;
    const bool side   = params.chan == ChannelMode::Side;
    const int  secs   = detSections;

    float blockMaxGr = 0.0f, blockMaxDet = -120.0f;

    auto detFilter = [&] (int s, float x) noexcept
    {
        x = detS[s][0].process (x, detC[0]);
        if (secs > 1) x = detS[s][1].process (x, detC[1]);
        return x;
    };

    for (int i = 0; i < n; ++i)
    {
        const float gIn = inGain.getNextValue();
        const float gOut = outGain.getNextValue();
        const float mix = mixSm.getNextValue();

        const float inL = io[0][i];
        const float inR = stereo ? io[1][i] : inL;

        // ---- write raw input into the lookahead delay ----
        delayBuf[0][(size_t) writePos] = inL;
        delayBuf[1][(size_t) writePos] = inR;
        const int readPos = (writePos - latency) & delayMask;
        writePos = (writePos + 1) & delayMask;
        const float dL = delayBuf[0][(size_t) readPos];
        const float dR = delayBuf[1][(size_t) readPos];

        // ---- detector (runs on the undelayed signal = lookahead) ----
        const float xL = inL * gIn, xR = inR * gIn;
        float keyHi = 0.0f, keyFull = 0.0f, trigL = 0.0f, trigR = 0.0f;

        if (useSC)
        {
            float k = sidechain.getSample (0, i);
            if (sidechain.getNumChannels() > 1) k = 0.5f * (k + sidechain.getSample (1, i));
            const float h = detFilter (2, k);
            keyHi = std::abs (h); keyFull = std::abs (k);
            trigL = trigR = h;
        }
        else if (ms)
        {
            const float k = side ? 0.5f * (xL - xR) : 0.5f * (xL + xR);
            const float h = detFilter (2, k);
            keyHi = std::abs (h); keyFull = std::abs (k);
            trigL = trigR = h;
        }
        else
        {
            trigL = detFilter (0, xL);
            trigR = stereo ? detFilter (1, xR) : trigL;
            keyHi   = juce::jmax (std::abs (trigL), std::abs (trigR));   // linked stereo
            keyFull = juce::jmax (std::abs (xL), std::abs (xR));
        }

        envSib  = keyHi   > envSib  ? attCoef * envSib  + (1.0f - attCoef) * keyHi   : relCoef * envSib  + (1.0f - relCoef) * keyHi;
        envFull = keyFull > envFull ? attCoef * envFull + (1.0f - attCoef) * keyFull : relCoef * envFull + (1.0f - relCoef) * keyFull;

        const float sibDb  = toDb (envSib);
        const float fullDb = toDb (envFull);
        float detDb;
        if (params.detect == Detection::Relative)
            detDb = fullDb < -70.0f ? -120.0f : sibDb - fullDb;   // gate silence / noise floor
        else
            detDb = sibDb;

        const float gr = gainComputer (detDb);
        const float g  = fromDb (-gr);
        const float k1 = 1.0f - g;

        // ---- audio path (delayed) ----
        const float aL = dL * gIn, aR = dR * gIn;
        float wL, wR;

        // Split: 2nd-order dynamic high shelf, coefficients follow the gain
        // reduction at control rate (every 8 samples or on a >0.05 dB move).
        if (params.mode == Mode::Split && (--shelfCountdown <= 0 || std::abs (gr - shelfGr) > 0.05f))
        {
            shelfGr = gr; shelfCountdown = 8;
            procShelf = makeShelf (fs, juce::jlimit (200.0, fs * 0.45, (double) params.freq), -gr, 1.0 /* shelf slope S */, true);
        }

        auto apply = [&] (int s, float y) noexcept
        {
            switch (params.mode)
            {
                case Mode::Wide:  return y * g;
                case Mode::Split: return procS[s].process (y, procShelf);
                case Mode::Bell:  default: return y - k1 * procS[s].process (y, procBP);
            }
        };

        if (ms)
        {
            float m = 0.5f * (aL + aR), sd = 0.5f * (aL - aR);
            if (side) sd = apply (0, sd); else m = apply (0, m);
            wL = m + sd; wR = m - sd;
        }
        else
        {
            wL = apply (0, aL);
            wR = stereo ? apply (1, aR) : wL;
        }

        float oL, oR;
        if (params.bypass)                         { oL = dL; oR = dR; }
        else if (params.listen == Listen::Trigger) { oL = trigL * gOut; oR = trigR * gOut; }
        else if (params.listen == Listen::Delta)   { oL = (aL - wL) * gOut; oR = (aR - wR) * gOut; }
        else
        {
            oL = (aL + mix * (wL - aL)) * gOut;
            oR = (aR + mix * (wR - aR)) * gOut;
        }

        io[0][i] = oL;
        if (stereo) io[1][i] = oR;

        // ---- metering ----
        blockMaxGr  = juce::jmax (blockMaxGr, gr);
        blockMaxDet = juce::jmax (blockMaxDet, detDb);
        chunk.grDb     = juce::jmax (chunk.grDb, gr);
        chunk.detectDb = juce::jmax (chunk.detectDb, detDb);
        chunk.inputDb  = juce::jmax (chunk.inputDb, toDb (juce::jmax (std::abs (xL), std::abs (xR))));
        if (++chunkPos >= chunkLen) { pushHistory (chunk); chunk = {}; chunkPos = 0; }
    }

    meterGr.store (params.bypass ? 0.0f : blockMaxGr, std::memory_order_relaxed);
    meterDetect.store (blockMaxDet, std::memory_order_relaxed);
}

float DeEsserEngine::reductionResponseDb (Mode mode, float hz, float freq, float q, float grDb)
{
    const float g = fromDb (-juce::jmax (0.0f, grDb));
    if (mode == Mode::Wide) return -grDb;

    using C = std::complex<double>;
    const double w = (double) hz / (double) juce::jmax (1.0f, freq);
    const C s (0.0, w);
    C h;
    if (mode == Mode::Split)
    {
        // RBJ analog high-shelf prototype, S = 1 (Q = 0.7071)
        const double A = std::pow (10.0, -grDb / 40.0), sa = std::sqrt (A) / 0.70710678;
        h = A * (A * s * s + sa * s + 1.0) / (s * s + sa * s + A);
    }
    else
    {
        const double qq = juce::jmax (0.1, (double) q);
        const C bp = (s / qq) / (s * s + s / qq + 1.0);
        h = 1.0 - (1.0 - g) * bp;
    }
    return (float) (20.0 * std::log10 (juce::jmax (1.0e-6, std::abs (h))));
}

} // namespace wsho::dsp
