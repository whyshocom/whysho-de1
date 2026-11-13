// Headless DSP checks for the de-esser engine (dev only, never shipped).
// Build with -DWHYSHO_BUILD_TESTS=ON and run: returns non-zero on failure.
#include "dsp/DeEsserEngine.h"
#include <iostream>

using namespace wsho;
static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << "\n";
    if (! ok) ++failures;
}

// Steady sine through the engine; returns output RMS gain (dB) after settling.
static float sineGainDb (dsp::EngineParams p, float hz, float amp, double fs = 48000.0)
{
    dsp::DeEsserEngine e; e.prepare (fs, 512, 2); e.setParameters (p);
    juce::AudioBuffer<float> buf (2, 512), none;
    double ph = 0.0, inSq = 0.0, outSq = 0.0;
    for (int b = 0; b < 200; ++b)
    {
        for (int i = 0; i < 512; ++i)
        {
            const float x = amp * (float) std::sin (ph); ph += 2.0 * juce::MathConstants<double>::pi * hz / fs;
            buf.setSample (0, i, x); buf.setSample (1, i, x);
            if (b >= 100) inSq += (double) x * x;
        }
        e.process (buf, none);
        if (b >= 100) for (int i = 0; i < 512; ++i) { const float y = buf.getSample (0, i); outSq += (double) y * y; if (! std::isfinite (y)) return 999.0f; }
    }
    return (float) (10.0 * std::log10 (outSq / inSq));
}

int main()
{
    dsp::EngineParams p;              // defaults: Split, Relative, -18 dB, range 8
    p.lookaheadMs = 0.0f;

    std::cout << "Relative detection\n";
    check (std::abs (sineGainDb (p, 300.0f, 0.5f)) < 0.2f,  "300 Hz tone passes untouched");
    const float hiSplit = sineGainDb (p, 9000.0f, 0.5f);
    check (hiSplit < -6.0f && hiSplit > -8.5f,               "9 kHz tone reduced ~Range in Split (" + juce::String (hiSplit, 2) + " dB)");

    p.mode = Mode::Wide;
    const float hiWide = sineGainDb (p, 9000.0f, 0.5f);
    check (hiWide < -7.5f && hiWide > -8.5f,                 "Wide reduces by Range (" + juce::String (hiWide, 2) + " dB)");

    p.mode = Mode::Bell; p.freq = 7000.0f; p.q = 2.0f;
    const float bellCenter = sineGainDb (p, 7000.0f, 0.5f);
    check (bellCenter < -7.0f && bellCenter > -8.5f,         "Bell reduces at its centre (" + juce::String (bellCenter, 2) + " dB)");

    std::cout << "Absolute detection\n";
    p = {}; p.lookaheadMs = 0.0f; p.detect = Detection::Absolute; p.threshDb = -20.0f;
    check (std::abs (sineGainDb (p, 9000.0f, 0.02f)) < 0.2f, "quiet highs below threshold untouched");
    check (sineGainDb (p, 9000.0f, 0.8f) < -6.0f,            "loud highs reduced");

    std::cout << "Range / mix / bypass\n";
    p = {}; p.lookaheadMs = 0.0f; p.mode = Mode::Wide; p.rangeDb = 0.0f;
    check (std::abs (sineGainDb (p, 9000.0f, 0.5f)) < 0.05f, "Range 0 = no reduction");
    p.rangeDb = 8.0f; p.mix = 0.0f;
    check (std::abs (sineGainDb (p, 9000.0f, 0.5f)) < 0.05f, "Mix 0 = dry");
    p.mix = 1.0f; p.bypass = true; p.lookaheadMs = 5.0f;
    check (std::abs (sineGainDb (p, 9000.0f, 0.5f)) < 0.05f, "Bypass is transparent");

    std::cout << "Lookahead latency\n";
    {
        dsp::DeEsserEngine e; e.prepare (48000.0, 512, 2);
        dsp::EngineParams q; q.lookaheadMs = 5.0f; q.bypass = true; e.setParameters (q);
        check (e.getLatencySamples() == 240, "5 ms @ 48 kHz = 240 samples (" + juce::String (e.getLatencySamples()) + ")");
        juce::AudioBuffer<float> buf (2, 512), none; buf.clear(); buf.setSample (0, 0, 1.0f); buf.setSample (1, 0, 1.0f);
        e.process (buf, none);
        check (buf.getSample (0, 240) == 1.0f && buf.getSample (0, 0) == 0.0f, "impulse delayed by exactly the reported latency");
    }

    std::cout << "Display curve\n";
    check (std::abs (dsp::DeEsserEngine::reductionResponseDb (Mode::Split, 20000.0f, 6000.0f, 1.5f, 8.0f) + 8.0f) < 0.6f, "Split curve reaches -GR in the highs");
    check (std::abs (dsp::DeEsserEngine::reductionResponseDb (Mode::Split, 500.0f, 6000.0f, 1.5f, 8.0f)) < 0.3f,         "Split curve flat in the lows");

    std::cout << (failures == 0 ? "ALL PASSED\n" : juce::String (failures) + " FAILED\n");
    return failures == 0 ? 0 : 1;
}
