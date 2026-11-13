#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"

namespace wsho::gui
{
// Spectrum strip: input / output spectrum, the detection band (shaded) and
// the live reduction curve. Drag the handle horizontally to set the
// frequency, mouse-wheel to set Q (Bell mode), double-click to reset.
class SpectrumBand : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumBand (WhyshoDEAudioProcessor& p);
    ~SpectrumBand() override;

    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    float freqToX (float hz) const;
    float xToFreq (float x) const;
    float dbToY (float db) const;
    float param (const char* id) const;
    void  setParam (const char* id, float value);
    void  drawSpectrum (juce::Graphics&, dsp::SpectrumAnalyzer&, juce::Colour, bool fill);

    WhyshoDEAudioProcessor& proc;
    float shownGr = 0.0f;
    bool dragging = false;

    static constexpr float minHz = 20.0f, maxHz = 22000.0f;
    static constexpr float minDb = -36.0f, maxDb = 18.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumBand)
};
} // namespace wsho::gui
