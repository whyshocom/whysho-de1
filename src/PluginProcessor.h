#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/DeEsserEngine.h"
#include "dsp/SpectrumAnalyzer.h"
#include "licensing/License.h"
#include "licensing/Activation.h"
#include "licensing/AntiTamper.h"
#include "PresetManager.h"

namespace wsho
{
class WhyshoDEAudioProcessor : public juce::AudioProcessor
{
public:
    WhyshoDEAudioProcessor();
    ~WhyshoDEAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "whysho DE1"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int sizeInBytes) override;

    // --- accessors for the editor ---
    juce::AudioProcessorValueTreeState& state() { return apvts; }
    dsp::DeEsserEngine& engine() { return deEsser; }
    dsp::SpectrumAnalyzer& preAnalyzer()  { return analyzerPre; }
    dsp::SpectrumAnalyzer& postAnalyzer() { return analyzerPost; }

    lic::LicenseManager& licenseManager() { return licMgr; }
    lic::ActivationClient& activationClient() { return activation; }
    PresetManager& presetManager() { return presets; }
    void refreshLicense();
    lic::LicenseInfo licenseInfo;

    bool sidechainActive() const { return scActive.load(); }

private:
    dsp::EngineParams buildSnapshot();
    void cacheParamPointers();

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets { apvts };
    dsp::DeEsserEngine deEsser;
    dsp::SpectrumAnalyzer analyzerPre, analyzerPost;

    lic::LicenseManager   licMgr;
    lic::ActivationClient activation;
    lic::Guard            guard;

    std::atomic<float> *pThresh{}, *pRange{}, *pFreq{}, *pQ{}, *pMode{}, *pDetect{}, *pChan{},
                       *pListen{}, *pAttack{}, *pRelease{}, *pLookahead{}, *pExtSC{},
                       *pInGain{}, *pOutGain{}, *pMix{}, *pBypass{};

    juce::AudioBuffer<float> sidechainCopy;
    std::atomic<bool> scActive { false };
    int  blockCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WhyshoDEAudioProcessor)
};
} // namespace wsho
