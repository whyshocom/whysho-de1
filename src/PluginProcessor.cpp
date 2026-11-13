#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace wsho
{
WhyshoDEAudioProcessor::WhyshoDEAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "WHYSHO_DE1", createParameterLayout())
{
    cacheParamPointers();
    refreshLicense();
}

void WhyshoDEAudioProcessor::cacheParamPointers()
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pThresh    = get (pid::thresh);
    pRange     = get (pid::range);
    pFreq      = get (pid::freq);
    pQ         = get (pid::q);
    pMode      = get (pid::mode);
    pDetect    = get (pid::detect);
    pChan      = get (pid::chan);
    pListen    = get (pid::listen);
    pAttack    = get (pid::attack);
    pRelease   = get (pid::release);
    pLookahead = get (pid::lookahead);
    pExtSC     = get (pid::extSC);
    pInGain    = get (pid::inGain);
    pOutGain   = get (pid::outGain);
    pMix       = get (pid::mix);
    pBypass    = get (pid::bypass);
}

void WhyshoDEAudioProcessor::refreshLicense()
{
    licenseInfo = licMgr.refresh();
    guard.setLicensed (licenseInfo.isUsable());
}

void WhyshoDEAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    deEsser.prepare (sampleRate, samplesPerBlock, 2);
    deEsser.setParameters (buildSnapshot());
    analyzerPre.prepare (sampleRate);
    analyzerPost.prepare (sampleRate);
    guard.prepare (sampleRate);
    sidechainCopy.setSize (2, samplesPerBlock);
    setLatencySamples (deEsser.getLatencySamples());
}

bool WhyshoDEAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getChannelSet (false, 0);
    const auto& mainIn  = layouts.getChannelSet (true, 0);
    if (mainIn != mainOut) return false;
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto& sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

dsp::EngineParams WhyshoDEAudioProcessor::buildSnapshot()
{
    dsp::EngineParams s;
    s.threshDb    = pThresh->load();
    s.rangeDb     = pRange->load();
    s.freq        = pFreq->load();
    s.q           = pQ->load();
    s.mode        = (Mode)        juce::jlimit (0, 2, (int) pMode->load());
    s.detect      = (Detection)   juce::jlimit (0, 1, (int) pDetect->load());
    s.chan        = (ChannelMode) juce::jlimit (0, 2, (int) pChan->load());
    s.listen      = (Listen)      juce::jlimit (0, 2, (int) pListen->load());
    s.attackMs    = pAttack->load();
    s.releaseMs   = pRelease->load();
    s.lookaheadMs = pLookahead->load();
    s.extSC       = pExtSC->load() > 0.5f;
    s.inGainDb    = pInGain->load();
    s.outGainDb   = pOutGain->load();
    s.mix         = pMix->load();
    s.bypass      = pBypass->load() > 0.5f;
    return s;
}

void WhyshoDEAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto mainBuf = getBusBuffer (buffer, false, 0);
    const int n = mainBuf.getNumSamples();

    // ---- sidechain extraction ----
    bool sc = false;
    if (getBusCount (true) > 1)
    {
        auto* scBus = getBus (true, 1);
        if (scBus != nullptr && scBus->isEnabled())
        {
            auto scBuf = getBusBuffer (buffer, true, 1);
            const int scCh = juce::jmin (2, scBuf.getNumChannels());
            if (scCh > 0 && n > 0)
            {
                if (sidechainCopy.getNumSamples() < n) sidechainCopy.setSize (2, n, false, false, true);
                for (int c = 0; c < 2; ++c)
                    sidechainCopy.copyFrom (c, 0, scBuf, juce::jmin (c, scCh - 1), 0, n);
                sc = true;
            }
        }
    }
    scActive = sc;

    // ---- analyzer: pre tap ----
    const int lastCh = mainBuf.getNumChannels() - 1;
    for (int i = 0; i < n; ++i)
        analyzerPre.push (0.5f * (mainBuf.getSample (0, i) + mainBuf.getSample (lastCh, i)));

    // ---- DSP ----
    deEsser.setParameters (buildSnapshot());
    if (getLatencySamples() != deEsser.getLatencySamples())
        setLatencySamples (deEsser.getLatencySamples());

    juce::AudioBuffer<float> scView;
    if (sc) scView.setDataToReferTo (sidechainCopy.getArrayOfWritePointers(), 2, n);
    deEsser.process (mainBuf, scView);

    // ---- licensing gate (deterrent degradation when unlicensed) ----
    guard.applyDegradation (mainBuf);

    // ---- analyzer: post tap ----
    for (int i = 0; i < n; ++i)
        analyzerPost.push (0.5f * (mainBuf.getSample (0, i) + mainBuf.getSample (lastCh, i)));

    // periodic re-check of license state (cheap, every ~500 blocks)
    if (++blockCounter >= 500) { blockCounter = 0; guard.setLicensed (licenseInfo.isUsable()); }
}

void WhyshoDEAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void WhyshoDEAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* WhyshoDEAudioProcessor::createEditor()
{
    return new WhyshoDEAudioProcessorEditor (*this);
}
} // namespace wsho

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new wsho::WhyshoDEAudioProcessor();
}
