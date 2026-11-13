#include "HistoryDisplay.h"
#include "Theme.h"

using namespace wsho;
using namespace wsho::gui;

HistoryDisplay::HistoryDisplay (WhyshoDEAudioProcessor& p) : proc (p)
{
    points.assign ((size_t) kPoints, dsp::HistoryPoint {});
    startTimerHz (60);
}

HistoryDisplay::~HistoryDisplay() { stopTimer(); }

void HistoryDisplay::resized()
{
    auto r = getLocalBounds();
    meter = r.removeFromRight (54);
    r.removeFromRight (6);
    plot = r;
}

float HistoryDisplay::param (const char* id) const
{
    if (auto* v = proc.state().getRawParameterValue (id)) return v->load();
    return 0.0f;
}

float HistoryDisplay::dbToY (float db) const
{
    const auto b = plot.toFloat().reduced (0.0f, 8.0f);
    return juce::jmap (juce::jlimit (minDb, maxDb, db), maxDb, minDb, b.getY(), b.getBottom());
}
float HistoryDisplay::yToDb (float y) const
{
    const auto b = plot.toFloat().reduced (0.0f, 8.0f);
    return juce::jmap (y, b.getY(), b.getBottom(), maxDb, minDb);
}

void HistoryDisplay::timerCallback()
{
    proc.engine().drainHistory ([this] (const dsp::HistoryPoint& pt)
    {
        points[(size_t) head] = pt;
        head = (head + 1) % kPoints;
    });

    // GR meter: fast attack, smooth fall, 1 s peak hold
    const float gr = proc.engine().currentGrDb();
    meterGr = gr > meterGr ? gr : meterGr + (gr - meterGr) * 0.18f;
    if (gr >= meterPeak) { meterPeak = gr; peakHoldFrames = 60; }
    else if (--peakHoldFrames <= 0) meterPeak = juce::jmax (0.0f, meterPeak - 0.25f);

    repaint();
}

void HistoryDisplay::paint (juce::Graphics& g)
{
    const auto& t = theme();
    const auto pf = plot.toFloat();

    // ---- panel ----
    g.setColour (t.light ? t.panel2 : t.panel);
    g.fillRoundedRectangle (pf, 6.0f);

    // ---- grid ----
    g.setFont (themeFont (9.5f));
    for (int db = 0; db >= (int) minDb; db -= 12)
    {
        const float y = dbToY ((float) db);
        g.setColour (t.grid.withAlpha (db == 0 ? 0.8f : 0.4f));
        g.drawHorizontalLine ((int) y, pf.getX(), pf.getRight());
        g.setColour (t.textDim);
        g.drawText (juce::String (db), (int) pf.getX() + 4, (int) y - 12, 30, 11, juce::Justification::left);
    }

    const bool rel = (int) param (pid::detect) == (int) Detection::Relative;
    g.setColour (t.textDim);
    g.drawText (caps (rel ? "detector  dB rel" : "detector  dBFS"),
                plot.withTrimmedRight (10).removeFromBottom (22), juce::Justification::centredRight);

    // ---- series (oldest at left) ----
    const float x0 = pf.getX(), w = pf.getWidth();
    auto xAt = [&] (int k) { return x0 + w * (float) k / (float) (kPoints - 1); };
    auto at  = [&] (int k) -> const dsp::HistoryPoint& { return points[(size_t) ((head + k) % kPoints)]; };

    juce::Path input, detect, gr;
    const float bottom = dbToY (minDb), top = dbToY (maxDb);
    input.startNewSubPath (x0, bottom);
    gr.startNewSubPath (x0, top);
    for (int k = 0; k < kPoints; ++k)
    {
        const auto& p = at (k);
        const float x = xAt (k);
        input.lineTo (x, dbToY (p.inputDb));
        gr.lineTo (x, dbToY (-p.grDb));
        const float dy = dbToY (p.detectDb);
        if (k == 0) detect.startNewSubPath (x, dy); else detect.lineTo (x, dy);
    }
    input.lineTo (pf.getRight(), bottom); input.closeSubPath();
    gr.lineTo (pf.getRight(), top);       gr.closeSubPath();

    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (plot);
        g.setColour (t.specPre);
        g.fillPath (input);

        g.setColour (t.accent2.withAlpha (0.9f));
        g.strokePath (detect, juce::PathStrokeType (1.5f));

        g.setColour (t.accent.withAlpha (t.light ? 0.30f : 0.35f));
        g.fillPath (gr);
        g.setColour (t.accent);
        g.strokePath (gr, juce::PathStrokeType (1.4f));
    }

    // ---- threshold ----
    const float thr = param (pid::thresh);
    const float ty = dbToY (thr);
    const bool hot = dragging || hoverThresh;
    g.setColour (t.text.withAlpha (hot ? 1.0f : 0.75f));
    {
        juce::Path line, dash; const float dashes[] = { 6.0f, 4.0f };
        line.startNewSubPath (pf.getX(), ty); line.lineTo (pf.getRight(), ty);
        juce::PathStrokeType (hot ? 2.0f : 1.4f).createDashedStroke (dash, line, dashes, 2);
        g.fillPath (dash);
    }

    const auto tag = juce::Rectangle<float> (pf.getRight() - 118.0f, ty - 10.0f, 112.0f, 20.0f);
    g.setColour (t.text);
    g.fillRoundedRectangle (tag, 4.0f);
    g.setColour (t.bg);
    g.setFont (themeFont (11.0f, true));
    g.drawText ("THR " + juce::String (thr, 1) + " dB", tag, juce::Justification::centred);

    // ---- legend ----
    auto lg = plot.withTrimmedLeft (40).removeFromBottom (22);
    auto legend = [&] (juce::Colour c, const juce::String& s)
    {
        g.setColour (c); g.fillRoundedRectangle (lg.removeFromLeft (10).withSizeKeepingCentre (10, 4).toFloat(), 2.0f);
        g.setColour (t.textDim); g.setFont (themeFont (9.5f));
        g.drawText (caps (s), lg.removeFromLeft (80).withTrimmedLeft (5), juce::Justification::centredLeft);
    };
    legend (t.specPost.withAlpha (1.0f), "input");
    legend (t.accent2, "sibilance");
    legend (t.accent, "reduction");

    // ---- GR meter ----
    const auto mf = meter.toFloat();
    g.setColour (t.light ? t.panel2 : t.panel);
    g.fillRoundedRectangle (mf, 6.0f);
    auto bar = mf.reduced (14.0f, 0.0f).withTrimmedTop (22.0f).withTrimmedBottom (26.0f);
    g.setColour (t.grid.withAlpha (0.6f));
    g.fillRoundedRectangle (bar, 3.0f);
    const float maxGr = 24.0f;
    const float fillH = bar.getHeight() * juce::jlimit (0.0f, 1.0f, meterGr / maxGr);
    g.setColour (t.accent);
    g.fillRoundedRectangle (bar.withHeight (fillH), 3.0f);
    const float peakY = bar.getY() + bar.getHeight() * juce::jlimit (0.0f, 1.0f, meterPeak / maxGr);
    g.setColour (t.text);
    g.fillRect (bar.getX() - 3.0f, peakY - 1.0f, bar.getWidth() + 6.0f, 2.0f);

    g.setColour (t.textDim);
    g.setFont (themeFont (9.5f));
    g.drawText ("GR", meter.withHeight (20), juce::Justification::centred);
    for (int db : { 6, 12, 18 })
    {
        const float y = bar.getY() + bar.getHeight() * (float) db / maxGr;
        g.drawHorizontalLine ((int) y, bar.getRight() + 1.0f, bar.getRight() + 5.0f);
    }
    g.setColour (meterPeak > 0.05f ? t.text : t.textDim);
    g.setFont (themeFont (11.0f, true));
    g.drawText (meterPeak > 0.05f ? "-" + juce::String (meterPeak, 1) : "0.0",
                meter.withTrimmedTop (meter.getHeight() - 24), juce::Justification::centred);
}

// ---- interaction: drag anywhere on the plot to set the threshold ----------
bool HistoryDisplay::nearThreshold (float y) const { return std::abs (y - dbToY (param (pid::thresh))) < 10.0f; }

void HistoryDisplay::setThreshold (float db)
{
    if (auto* p = proc.state().getParameter (pid::thresh))
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (-60.0f, 0.0f, db)));
}

void HistoryDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (! plot.contains (e.getPosition())) return;
    dragging = true;
    if (auto* p = proc.state().getParameter (pid::thresh)) p->beginChangeGesture();
    setThreshold (yToDb (e.position.y));
}

void HistoryDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging) setThreshold (yToDb (e.position.y));
}

void HistoryDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
        if (auto* p = proc.state().getParameter (pid::thresh)) p->endChangeGesture();
    dragging = false;
}

void HistoryDisplay::mouseMove (const juce::MouseEvent& e)
{
    const bool h = plot.contains (e.getPosition()) && nearThreshold (e.position.y);
    if (h != hoverThresh) { hoverThresh = h; repaint(); }
    setMouseCursor (plot.contains (e.getPosition()) ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void HistoryDisplay::mouseExit (const juce::MouseEvent&) { hoverThresh = false; }

void HistoryDisplay::mouseDoubleClick (const juce::MouseEvent&)
{
    if (auto* p = proc.state().getParameter (pid::thresh))
        p->setValueNotifyingHost (p->getDefaultValue());
}
