// Offscreen GUI renderer: instantiates the real plugin editor and writes PNGs.
// Used to preview the UI on headless machines (run under xvfb-run). Not shipped.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>

// Feed a synthetic "vocal": a low vowel tone with periodic bursts of
// high-passed noise (the S sounds) so the displays show real activity.
static void feedAudio (wsho::WhyshoDEAudioProcessor& proc, double seconds)
{
    const double fs = 48000.0; const int block = 512;
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    static juce::Random rng (7);
    static double phase = 0.0; static float hp = 0.0f, prev = 0.0f; static long n = 0;
    for (int b = 0; b < (int) (seconds * fs / block); ++b)
    {
        for (int i = 0; i < block; ++i, ++n)
        {
            const double t = (double) n / fs;
            const bool ess = std::fmod (t, 0.6) > 0.42;
            phase += 2.0 * juce::MathConstants<double>::pi * 180.0 / fs;
            const float vowel = 0.25f * (float) (std::sin (phase) + 0.4 * std::sin (2.0 * phase) + 0.2 * std::sin (3.0 * phase));
            const float wn = rng.nextFloat() * 2.0f - 1.0f;
            hp = 0.5f * (wn - prev); prev = wn;
            const float s = (ess ? 0.9f : 0.01f) * hp;
            buf.setSample (0, i, vowel + s);
            buf.setSample (1, i, vowel + s);
        }
        proc.processBlock (buf, midi);
    }
}

static void renderShot (int w, int h, const juce::File& out, bool openLicense, int themeIndex = 0)
{
    wsho::WhyshoDEAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    proc.state().state.setProperty ("uiTheme", themeIndex, nullptr);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setBounds (0, 0, w, h);
    ed->setVisible (true);

    // stream audio while the editor's timers drain history / spectra
    for (int k = 0; k < 45; ++k)
    {
        feedAudio (proc, 0.1);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }

    if (openLicense)
        if (auto* lb = dynamic_cast<juce::Button*> (ed->findChildWithID ("license")))
            lb->triggerClick();

    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 2.0f);
    out.deleteFile();
    juce::FileOutputStream os (out);
    if (os.openedOk()) { juce::PNGImageFormat png; png.writeImageToStream (img, os); }
    std::cout << "wrote " << out.getFullPathName() << " (" << img.getWidth() << "x" << img.getHeight() << ")\n";
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    dir.createDirectory();
    renderShot (1100, 720, dir.getChildFile ("whysho-de1-main.png"),     false, 0); // Paper (default)
    renderShot (1100, 720, dir.getChildFile ("whysho-de1-midnight.png"), false, 1); // Midnight
    renderShot (1100, 720, dir.getChildFile ("whysho-de1-license.png"),  true,  0);
    return 0;
}
