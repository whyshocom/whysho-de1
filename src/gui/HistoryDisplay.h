#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"

namespace wsho::gui
{
// Scrolling view of the last few seconds: input level (fill), detector level
// (line), gain reduction (hanging from the top) and the threshold, which can
// be dragged vertically. A gain-reduction meter sits on the right edge.
class HistoryDisplay : public juce::Component, private juce::Timer
{
public:
    explicit HistoryDisplay (WhyshoDEAudioProcessor& p);
    ~HistoryDisplay() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    float dbToY (float db) const;
    float yToDb (float y) const;
    float param (const char* id) const;
    void  setThreshold (float db);
    bool  nearThreshold (float y) const;

    WhyshoDEAudioProcessor& proc;
    juce::Rectangle<int> plot, meter;

    static constexpr int kPoints = 420;          // ~4.2 s at 10 ms / point
    std::vector<dsp::HistoryPoint> points;
    int head = 0;                                 // next write index

    float meterGr = 0.0f, meterPeak = 0.0f; int peakHoldFrames = 0;
    bool dragging = false, hoverThresh = false;
    juce::Rectangle<int> lastBounds;

    static constexpr float minDb = -60.0f, maxDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HistoryDisplay)
};
} // namespace wsho::gui
