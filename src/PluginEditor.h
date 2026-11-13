#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "gui/WhyshoLookAndFeel.h"
#include "gui/HistoryDisplay.h"
#include "gui/SpectrumBand.h"
#include "gui/LicenseDialog.h"

namespace wsho
{
class WhyshoDEAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit WhyshoDEAudioProcessorEditor (WhyshoDEAudioProcessor&);
    ~WhyshoDEAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    void timerCallback() override;
    void openLicense();

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    juce::Slider&   addKnob (const juce::String& label, const char* paramId, bool big, const juce::String& tip);
    juce::ComboBox& addCombo (const juce::String& caption, const juce::StringArray& items, const char* paramId, const juce::String& tip);
    juce::ToggleButton& addToggle (const juce::String& t, const char* paramId, const juce::String& tip);

    WhyshoDEAudioProcessor& proc;
    gui::WhyshoLookAndFeel lnf;

    juce::TooltipWindow    tooltipWindow { this, 600 };
    gui::HistoryDisplay    history;
    gui::SpectrumBand      spectrum;
    std::unique_ptr<gui::LicenseDialog> licenseDialog;

    // knobs: 0 in, 1 out, 2 mix | 3 thresh, 4 range, 5 freq, 6 q, 7 attack, 8 release, 9 lookahead
    juce::OwnedArray<juce::Slider> knobs;
    juce::OwnedArray<juce::Label>  knobLabels;
    // combos: 0 mode, 1 detection, 2 channel, 3 listen
    juce::OwnedArray<juce::ComboBox> combos;
    juce::OwnedArray<juce::Label>    comboLabels;
    // toggles: 0 bypass, 1 ext sc
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::TextButton licenseBtn { "License" };
    juce::ComboBox themeBox;
    juce::Label brand, tagline;

    // preset bar
    juce::ComboBox presetBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "Save" },
                     abBtn { "A" }, copyBtn { "Copy" };
    bool slotB = false;
    std::unique_ptr<juce::AlertWindow> saveDialog;

    juce::Rectangle<int> controlPanel;

    void applyThemeChange (int index);
    void rebuildPresetMenu();
    void selectPresetDelta (int d);
    void applyPresetSelection();
    void doSavePreset();
    void toggleAB();

    juce::OwnedArray<SA> sa; juce::OwnedArray<CA> ca; juce::OwnedArray<BA> ba;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WhyshoDEAudioProcessorEditor)
};
} // namespace wsho
