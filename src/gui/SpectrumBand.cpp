#include "SpectrumBand.h"
#include "Theme.h"

using namespace wsho;
using namespace wsho::gui;

SpectrumBand::SpectrumBand (WhyshoDEAudioProcessor& p) : proc (p) { startTimerHz (30); }
SpectrumBand::~SpectrumBand() { stopTimer(); }

void SpectrumBand::timerCallback()
{
    proc.preAnalyzer().render();
    proc.postAnalyzer().render();
    const float gr = proc.engine().currentGrDb();
    shownGr = gr > shownGr ? gr : shownGr + (gr - shownGr) * 0.25f;
    repaint();
}

float SpectrumBand::param (const char* id) const
{
    if (auto* v = proc.state().getRawParameterValue (id)) return v->load();
    return 0.0f;
}
void SpectrumBand::setParam (const char* id, float value)
{
    if (auto* p = proc.state().getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

float SpectrumBand::freqToX (float hz) const
{
    const float t = std::log (hz / minHz) / std::log (maxHz / minHz);
    return t * (float) getWidth();
}
float SpectrumBand::xToFreq (float x) const
{
    return minHz * std::pow (maxHz / minHz, juce::jlimit (0.0f, 1.0f, x / (float) juce::jmax (1, getWidth())));
}
float SpectrumBand::dbToY (float db) const
{
    return juce::jmap (juce::jlimit (minDb, maxDb, db), maxDb, minDb, 4.0f, (float) getHeight() - 4.0f);
}

void SpectrumBand::drawSpectrum (juce::Graphics& g, dsp::SpectrumAnalyzer& an, juce::Colour c, bool fill)
{
    const auto& mags = an.magnitudes();
    juce::Path p; bool started = false;
    for (int bin = 1; bin < dsp::SpectrumAnalyzer::numBins; ++bin)
    {
        const float f = an.binToFreq (bin);
        if (f < minHz || f > maxHz) continue;
        const float tilt = 4.5f * std::log2 (f / 1000.0f);         // musical tilt
        const float y = dbToY (mags[(size_t) bin] + 24.0f + tilt);
        const float x = freqToX (f);
        if (! started) { p.startNewSubPath (x, y); started = true; } else p.lineTo (x, y);
    }
    if (! started) return;
    if (fill)
    {
        juce::Path f2 = p;
        f2.lineTo ((float) getWidth(), (float) getHeight());
        f2.lineTo (0.0f, (float) getHeight());
        f2.closeSubPath();
        g.setColour (c.withMultipliedAlpha (0.6f));
        g.fillPath (f2);
    }
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.2f));
}

void SpectrumBand::paint (juce::Graphics& g)
{
    const auto& t = theme();
    const auto b = getLocalBounds().toFloat();
    g.setColour (t.light ? t.panel2 : t.panel);
    g.fillRoundedRectangle (b, 6.0f);

    juce::Graphics::ScopedSaveState clip (g);
    juce::Path rounded; rounded.addRoundedRectangle (b, 6.0f);
    g.reduceClipRegion (rounded);

    // ---- grid ----
    g.setFont (themeFont (9.5f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        const float x = freqToX (f);
        g.setColour (t.grid.withAlpha (f == 1000.0f || f == 10000.0f ? 0.8f : 0.35f));
        g.drawVerticalLine ((int) x, 0.0f, b.getBottom());
        g.setColour (t.textDim);
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                    (int) x + 3, getHeight() - 14, 32, 12, juce::Justification::left);
    }
    const float zeroY = dbToY (0.0f);
    g.setColour (t.grid.withAlpha (0.6f));
    g.drawHorizontalLine ((int) zeroY, 0.0f, b.getRight());

    // ---- detection band ----
    const auto mode = (Mode) juce::jlimit (0, 2, (int) param (pid::mode));
    const float freq = param (pid::freq), q = param (pid::q);
    const float fx = freqToX (freq);
    juce::Rectangle<float> band;
    if (mode == Mode::Bell)
    {
        const float bwOct = 2.0f / std::log (2.0f) * std::asinh (1.0f / (2.0f * q));
        const float lo = freq / std::pow (2.0f, bwOct * 0.5f), hi = freq * std::pow (2.0f, bwOct * 0.5f);
        band = { freqToX (lo), 0.0f, freqToX (hi) - freqToX (lo), b.getHeight() };
    }
    else
        band = { fx, 0.0f, b.getRight() - fx, b.getHeight() };

    g.setColour (t.accent.withAlpha (t.light ? 0.10f : 0.12f));
    g.fillRect (band);

    // ---- spectra ----
    drawSpectrum (g, proc.preAnalyzer(), t.specPre, true);
    drawSpectrum (g, proc.postAnalyzer(), t.specPost.withAlpha (1.0f), false);

    // ---- live reduction curve ----
    juce::Path curve;
    const int steps = juce::jmax (64, getWidth() / 2);
    for (int i = 0; i <= steps; ++i)
    {
        const float x = (float) i / (float) steps * b.getWidth();
        const float db = dsp::DeEsserEngine::reductionResponseDb (mode, xToFreq (x), freq, q, shownGr);
        const float y = dbToY (db);
        if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
    if (t.curveGlow) { g.setColour (t.curve.withAlpha (0.18f)); g.strokePath (curve, juce::PathStrokeType (6.0f)); }
    g.setColour (t.curve);
    g.strokePath (curve, juce::PathStrokeType (t.light ? 2.2f : 2.0f));

    // ---- frequency handle ----
    g.setColour (t.accent);
    g.drawVerticalLine ((int) fx, 0.0f, b.getBottom());
    const float hy = 22.0f, r = dragging ? 9.0f : 8.0f;
    g.fillEllipse (fx - r, hy - r, r * 2, r * 2);
    g.setColour (t.light ? t.text.withAlpha (0.85f) : t.bg);
    g.drawEllipse (fx - r, hy - r, r * 2, r * 2, 1.6f);

    juce::String label = freq >= 1000.0f ? juce::String (freq / 1000.0f, 2) + " kHz" : juce::String ((int) freq) + " Hz";
    if (mode == Mode::Bell) label << "  Q " << juce::String (q, 2);
    g.setFont (themeFont (11.0f, true));
    const int lw = mode == Mode::Bell ? 130 : 76;
    const bool flip = fx + 14 + lw > b.getRight();
    g.setColour (t.text);
    g.drawText (label, flip ? (int) fx - 14 - lw : (int) fx + 14, (int) hy - 8, lw, 16,
                flip ? juce::Justification::right : juce::Justification::left);
}

// ---- interaction ----------------------------------------------------------
void SpectrumBand::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    if (auto* p = proc.state().getParameter (pid::freq)) p->beginChangeGesture();
    setParam (pid::freq, juce::jlimit (1000.0f, 16000.0f, xToFreq (e.position.x)));
}
void SpectrumBand::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging) setParam (pid::freq, juce::jlimit (1000.0f, 16000.0f, xToFreq (e.position.x)));
}
void SpectrumBand::mouseUp (const juce::MouseEvent&)
{
    if (dragging) if (auto* p = proc.state().getParameter (pid::freq)) p->endChangeGesture();
    dragging = false;
}
void SpectrumBand::mouseDoubleClick (const juce::MouseEvent&)
{
    for (auto* id : { pid::freq, pid::q })
        if (auto* p = proc.state().getParameter (id)) p->setValueNotifyingHost (p->getDefaultValue());
}
void SpectrumBand::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (auto* p = proc.state().getParameter (pid::q))
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + w.deltaY * 0.25f));
}
