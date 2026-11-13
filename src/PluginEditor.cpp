#include "PluginEditor.h"

using namespace wsho;
using namespace wsho::gui;

namespace { enum Knob { kIn, kOut, kMix, kThresh, kRange, kFreq, kQ, kAttack, kRelease, kLook }; }

WhyshoDEAudioProcessorEditor::WhyshoDEAudioProcessorEditor (WhyshoDEAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), history (p), spectrum (p)
{
    setLookAndFeel (&lnf);

    brand.setText ("whysho DE1", juce::dontSendNotification);
    addAndMakeVisible (brand);
    tagline.setText ("DE-ESSER", juce::dontSendNotification);
    addAndMakeVisible (tagline);

    addAndMakeVisible (history);
    addAndMakeVisible (spectrum);

    // top-bar knobs
    addKnob ("In",  pid::inGain,  false, "Input gain (also drives the detector)");
    addKnob ("Out", pid::outGain, false, "Output gain");
    addKnob ("Mix", pid::mix,     false, "Dry / wet mix");

    // main controls
    addKnob ("Threshold", pid::thresh,    true, "Level above which sibilance is reduced. Drag the line in the display too.");
    addKnob ("Range",     pid::range,     true, "Maximum amount of reduction");
    addKnob ("Frequency", pid::freq,      true, "Where sibilance is detected and reduced. Drag the handle in the spectrum too.");
    addKnob ("Q",         pid::q,         true, "Bandwidth of the Bell mode (mouse-wheel over the spectrum)");
    addKnob ("Attack",    pid::attack,    true, "How fast the reduction grabs");
    addKnob ("Release",   pid::release,   true, "How fast the reduction lets go");
    addKnob ("Lookahead", pid::lookahead, true, "Catches the start of each S (adds this much latency)");

    addCombo ("Mode",      kModeNames,        pid::mode,   "Wide: whole signal  |  Split: highs above Frequency  |  Bell: band around Frequency");
    addCombo ("Detection", kDetectionNames,   pid::detect, "Relative follows the vocal level  |  Absolute uses dBFS");
    addCombo ("Channel",   kChannelModeNames, pid::chan,   "Process linked stereo, or only Mid / Side");
    addCombo ("Audition",  kListenNames,      pid::listen, "Trigger: hear the detector  |  Delta: hear what is removed");

    addToggle ("Bypass", pid::bypass, "Bypass the whole plugin (latency is kept)");
    addToggle ("Ext SC", pid::extSC,  "Key the detector from the sidechain input");

    addAndMakeVisible (licenseBtn);
    licenseBtn.setComponentID ("license");
    licenseBtn.onClick = [this] { openLicense(); };

    // Theme selector (UI preference, persisted in plugin state).
    themeBox.addItemList (themeNames(), 1);
    const int savedTheme = (int) proc.state().state.getProperty ("uiTheme", 0);
    setThemeIndex (savedTheme);
    themeBox.setSelectedId (currentThemeIndex() + 1, juce::dontSendNotification);
    themeBox.onChange = [this] { applyThemeChange (themeBox.getSelectedId() - 1); };
    addAndMakeVisible (themeBox);

    // Preset bar
    for (auto* b : { &prevBtn, &nextBtn, &saveBtn, &abBtn, &copyBtn }) addAndMakeVisible (b);
    abBtn.setTooltip ("A/B compare");
    copyBtn.setTooltip ("Copy the current slot to the other one");
    addAndMakeVisible (presetBox);
    rebuildPresetMenu();
    presetBox.onChange = [this] { applyPresetSelection(); };
    prevBtn.onClick = [this] { selectPresetDelta (-1); };
    nextBtn.onClick = [this] { selectPresetDelta (+1); };
    saveBtn.onClick = [this] { doSavePreset(); };
    abBtn.onClick   = [this] { toggleAB(); };
    copyBtn.onClick = [this] { proc.presetManager().captureTo (! slotB); };
    proc.presetManager().captureTo (false); // seed slot A with current state

    applyThemeChange (currentThemeIndex());

    setResizable (true, true);
    setResizeLimits (900, 600, 1800, 1200);
    setSize (1020, 680);
    startTimerHz (8);
}

WhyshoDEAudioProcessorEditor::~WhyshoDEAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

juce::Slider& WhyshoDEAudioProcessorEditor::addKnob (const juce::String& label, const char* paramId, bool big, const juce::String& tip)
{
    auto* s = new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, big ? 72 : 52, big ? 15 : 13);
    s->setTooltip (tip);
    addAndMakeVisible (s); knobs.add (s);
    sa.add (new SA (proc.state(), paramId, *s));

    auto* l = new juce::Label ({}, big ? caps (label) : label);
    l->setJustificationType (juce::Justification::centred);
    addAndMakeVisible (l); knobLabels.add (l);
    return *s;
}

juce::ComboBox& WhyshoDEAudioProcessorEditor::addCombo (const juce::String& caption, const juce::StringArray& items,
                                                        const char* paramId, const juce::String& tip)
{
    auto* c = new juce::ComboBox(); c->addItemList (items, 1); c->setTooltip (tip);
    addAndMakeVisible (c); combos.add (c);
    ca.add (new CA (proc.state(), paramId, *c));
    auto* l = new juce::Label ({}, caps (caption));
    addAndMakeVisible (l); comboLabels.add (l);
    return *c;
}

juce::ToggleButton& WhyshoDEAudioProcessorEditor::addToggle (const juce::String& t, const char* paramId, const juce::String& tip)
{
    auto* b = new juce::ToggleButton (t); b->setTooltip (tip);
    addAndMakeVisible (b); toggles.add (b);
    ba.add (new BA (proc.state(), paramId, *b));
    return *b;
}

void WhyshoDEAudioProcessorEditor::applyThemeChange (int index)
{
    setThemeIndex (index);
    proc.state().state.setProperty ("uiTheme", index, nullptr);
    lnf.applyTheme();
    sendLookAndFeelChange();   // propagate to children (refreshes label colours)
    repaint();
}

void WhyshoDEAudioProcessorEditor::lookAndFeelChanged()
{
    brand.setColour (juce::Label::textColourId, theme().accent);
    brand.setFont (themeFont (19.0f, true));
    tagline.setColour (juce::Label::textColourId, theme().textDim);
    tagline.setFont (themeFont (10.0f));
    for (int i = 0; i < knobLabels.size(); ++i)
    {
        knobLabels[i]->setColour (juce::Label::textColourId, theme().textDim);
        knobLabels[i]->setFont (themeFont (i >= kThresh ? 10.5f : 10.0f, i >= kThresh));
    }
    for (auto* l : comboLabels)
    {
        l->setColour (juce::Label::textColourId, theme().textDim);
        l->setFont (themeFont (9.5f));
    }
}

// ---- presets ---------------------------------------------------------------
void WhyshoDEAudioProcessorEditor::rebuildPresetMenu()
{
    auto& pm = proc.presetManager();
    presetBox.clear (juce::dontSendNotification);
    const auto& fac = PresetManager::factory();
    auto usr = pm.userNames();

    static const std::pair<const char*, const char*> sections[] {
        { "", "Factory" }, { "Vocal", "Vocals" }, { "Instr", "Instruments" }, { "Bus", "Bus / Master" } };
    for (auto& [cat, heading] : sections)
    {
        presetBox.addSectionHeading (heading);
        for (int i = 0; i < (int) fac.size(); ++i)
            if (fac[(size_t) i].category == cat) presetBox.addItem (fac[(size_t) i].name, i + 1);  // ids 1..F
    }
    if (! usr.isEmpty())
    {
        presetBox.addSectionHeading ("User");
        for (int i = 0; i < usr.size(); ++i) presetBox.addItem (usr[i], 1000 + i);              // ids 1000+
    }
    const auto cur = pm.current();
    for (int i = 0; i < (int) fac.size(); ++i) if (fac[(size_t) i].name == cur) presetBox.setSelectedId (i + 1, juce::dontSendNotification);
    for (int i = 0; i < usr.size(); ++i) if (usr[i] == cur) presetBox.setSelectedId (1000 + i, juce::dontSendNotification);
}

void WhyshoDEAudioProcessorEditor::applyPresetSelection()
{
    auto& pm = proc.presetManager();
    const int id = presetBox.getSelectedId();
    const auto& fac = PresetManager::factory();
    if (id >= 1 && id <= (int) fac.size()) pm.applyFactory (fac[(size_t) (id - 1)].name);
    else if (id >= 1000) { auto u = pm.userNames(); if (id - 1000 < u.size()) pm.applyUser (u[id - 1000]); }
}

void WhyshoDEAudioProcessorEditor::selectPresetDelta (int d)
{
    juce::Array<int> ids;
    for (int i = 0; i < presetBox.getNumItems(); ++i)
        if (auto id = presetBox.getItemId (i)) ids.add (id);
    if (ids.isEmpty()) return;
    int idx = ids.indexOf (presetBox.getSelectedId());
    idx = juce::jlimit (0, ids.size() - 1, (idx < 0 ? 0 : idx) + d);
    presetBox.setSelectedId (ids[idx]); // triggers onChange
}

void WhyshoDEAudioProcessorEditor::doSavePreset()
{
    saveDialog = std::make_unique<juce::AlertWindow> ("Save Preset", "Name:", juce::MessageBoxIconType::NoIcon);
    saveDialog->addTextEditor ("name", "My Preset");
    saveDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int r)
    {
        if (r == 1)
        {
            auto name = saveDialog->getTextEditorContents ("name").trim();
            if (name.isNotEmpty() && proc.presetManager().saveUser (name)) rebuildPresetMenu();
        }
        saveDialog->exitModalState (r);
        saveDialog.reset();
    }), false);
}

void WhyshoDEAudioProcessorEditor::toggleAB()
{
    auto& pm = proc.presetManager();
    pm.captureTo (slotB);                 // store current into the active slot
    slotB = ! slotB;
    if (! pm.hasSlot (slotB)) pm.captureTo (slotB); // seed empty slot
    pm.recallFrom (slotB);
    abBtn.setButtonText (slotB ? "B" : "A");
}

// ---- license ---------------------------------------------------------------
void WhyshoDEAudioProcessorEditor::openLicense()
{
    if (licenseDialog) return;
    licenseDialog = std::make_unique<LicenseDialog> (proc);
    addAndMakeVisible (*licenseDialog);
    licenseDialog->setBounds (getLocalBounds());
    licenseDialog->onClose = [this] { licenseDialog.reset(); repaint(); };
}

void WhyshoDEAudioProcessorEditor::timerCallback()
{
    const auto& i = proc.licenseInfo;
    licenseBtn.setColour (juce::TextButton::textColourOffId,
                          i.state == lic::LicenseState::Licensed ? theme().good
                        : i.state == lic::LicenseState::Trial    ? theme().warn : theme().bad);
    licenseBtn.setButtonText (i.state == lic::LicenseState::Licensed ? "Licensed"
                            : i.state == lic::LicenseState::Trial    ? "Trial" : "Activate");

    // Q only matters in Bell mode
    const bool bell = (int) proc.state().getRawParameterValue (pid::mode)->load() == (int) Mode::Bell;
    knobs[kQ]->setEnabled (bell);
    knobLabels[kQ]->setAlpha (bell ? 1.0f : 0.45f);

    // Ext SC hint when no sidechain is routed
    const bool wantsSC = proc.state().getRawParameterValue (pid::extSC)->load() > 0.5f;
    toggles[1]->setColour (juce::ToggleButton::textColourId,
                           wantsSC && ! proc.sidechainActive() ? theme().warn : theme().text);
}

// ---- layout ----------------------------------------------------------------
void WhyshoDEAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme().bg);
    g.setColour (theme().panel);
    g.fillRect (0, 0, getWidth(), 54);                    // top bar
    g.fillRoundedRectangle (controlPanel.toFloat(), 8.0f); // control panel
}

void WhyshoDEAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (54).reduced (8, 6);

    auto brandArea = top.removeFromLeft (150);
    brand.setBounds (brandArea.removeFromTop (26));
    tagline.setBounds (brandArea);
    licenseBtn.setBounds (top.removeFromRight (90).reduced (2, 4));
    themeBox.setBounds (top.removeFromRight (104).reduced (3, 4));

    auto placeKnob = [&] (juce::Rectangle<int> cell, int idx, int labelH)
    {
        knobLabels[idx]->setBounds (cell.removeFromBottom (labelH));
        knobs[idx]->setBounds (cell);
    };
    for (int i = kIn; i <= kMix; ++i) placeKnob (top.removeFromLeft (52).reduced (2, 0), i, 12);
    top.removeFromLeft (12);
    for (int i = 0; i < 2; ++i) toggles[i]->setBounds (top.removeFromLeft (84).reduced (2, 6));

    // --- preset bar (second row) ---
    auto pr = r.removeFromTop (34).reduced (8, 4);
    prevBtn.setBounds   (pr.removeFromLeft (28).reduced (1));
    presetBox.setBounds (pr.removeFromLeft (260).reduced (2, 1));
    nextBtn.setBounds   (pr.removeFromLeft (28).reduced (1));
    pr.removeFromLeft (10);
    saveBtn.setBounds   (pr.removeFromLeft (64).reduced (2, 1));
    pr.removeFromLeft (10);
    abBtn.setBounds     (pr.removeFromLeft (44).reduced (2, 1));
    copyBtn.setBounds   (pr.removeFromLeft (60).reduced (2, 1));

    // --- control panel (bottom) ---
    controlPanel = r.removeFromBottom (132).reduced (8, 6);
    auto cp = controlPanel.reduced (10, 8);

    auto comboArea = cp.removeFromRight (juce::jmin (300, cp.getWidth() / 3));
    const int colW = comboArea.getWidth() / 2, rowH = comboArea.getHeight() / 2;
    for (int i = 0; i < 4; ++i)
    {
        auto cell = juce::Rectangle<int> (comboArea.getX() + (i % 2) * colW, comboArea.getY() + (i / 2) * rowH, colW, rowH).reduced (5, 4);
        comboLabels[i]->setBounds (cell.removeFromTop (14));
        combos[i]->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), juce::jmin (28, cell.getHeight())));
    }
    cp.removeFromRight (10);
    const int nBig = kLook - kThresh + 1;
    const int kw = cp.getWidth() / nBig;
    for (int i = kThresh; i <= kLook; ++i)
        placeKnob (cp.removeFromLeft (kw).reduced (4, 0), i, 14);

    // --- displays ---
    auto main = r.reduced (8, 6);
    spectrum.setBounds (main.removeFromBottom (juce::jmax (110, main.getHeight() / 3)));
    main.removeFromBottom (8);
    history.setBounds (main);

    if (licenseDialog) licenseDialog->setBounds (getLocalBounds());
}
