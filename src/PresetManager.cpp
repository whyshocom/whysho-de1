#include "PresetManager.h"

namespace wsho
{
using V = std::vector<std::pair<juce::String, float>>;

namespace
{
// One complete de-esser setting. Every preset sets all core parameters, so
// switching presets never inherits a stray value from the previous one.
struct P
{
    Mode mode; Detection det; ChannelMode chan;
    float freq, q, thresh, range, attack, release, lookahead;
};

V values (const P& p)
{
    return {
        { pid::mode,      (float) p.mode },
        { pid::detect,    (float) p.det },
        { pid::chan,      (float) p.chan },
        { pid::freq,      p.freq },
        { pid::q,         p.q },
        { pid::thresh,    p.thresh },
        { pid::range,     p.range },
        { pid::attack,    p.attack },
        { pid::release,   p.release },
        { pid::lookahead, p.lookahead },
    };
}

constexpr auto Wide = Mode::Wide, Split = Mode::Split, Bell = Mode::Bell;
constexpr auto Rel = Detection::Relative, Abs = Detection::Absolute;
constexpr auto St = ChannelMode::Stereo, Mid = ChannelMode::Mid, Side = ChannelMode::Side;
} // namespace

const std::vector<PresetManager::Preset>& PresetManager::factory()
{
    //                                   mode   det  chan  freq    Q    thr     rng   att   rel   look
    static const std::vector<Preset> list {
        { "",       "Init",                    values ({ Split, Rel, St,  6000, 1.5f, -18.0f,  8.0f, 1.0f,  60, 2.0f }) },

        { "Vocal",  "Vocal - Gentle",          values ({ Split, Rel, St,  6500, 1.5f, -14.0f,  4.0f, 1.0f,  70, 2.0f }) },
        { "Vocal",  "Vocal - Standard",        values ({ Split, Rel, St,  6000, 1.5f, -18.0f,  8.0f, 1.0f,  60, 2.0f }) },
        { "Vocal",  "Vocal - Female / High",   values ({ Split, Rel, St,  7500, 1.5f, -18.0f,  8.0f, 0.8f,  55, 2.0f }) },
        { "Vocal",  "Vocal - Male / Low",      values ({ Split, Rel, St,  5000, 1.5f, -18.0f,  8.0f, 1.2f,  65, 2.0f }) },
        { "Vocal",  "Vocal - Rap Upfront",     values ({ Bell,  Rel, St,  6200, 1.2f, -20.0f, 10.0f, 0.5f,  40, 3.0f }) },
        { "Vocal",  "Vocal - Aggressive",      values ({ Split, Rel, St,  5500, 1.5f, -24.0f, 14.0f, 0.4f,  50, 4.0f }) },
        { "Vocal",  "Vocal - Breathy Pop",     values ({ Split, Rel, St,  8000, 1.5f, -16.0f,  6.0f, 1.5f,  80, 2.0f }) },
        { "Vocal",  "Vocal - Bright & Airy",   values ({ Bell,  Rel, St,  9500, 2.0f, -16.0f,  6.0f, 1.0f,  60, 2.0f }) },
        { "Vocal",  "Vocal - Narrow S Fix",    values ({ Bell,  Rel, St,  4500, 3.0f, -18.0f, 10.0f, 0.6f,  45, 3.0f }) },
        { "Vocal",  "Vocal - Autotuned / Trap",values ({ Split, Rel, St,  7000, 1.5f, -20.0f, 10.0f, 0.3f,  35, 3.0f }) },
        { "Vocal",  "Backing Vocal Stack",     values ({ Split, Rel, St,  6500, 1.5f, -22.0f, 10.0f, 1.0f,  70, 2.0f }) },
        { "Vocal",  "Podcast / Voice-Over",    values ({ Wide,  Rel, St,  5500, 1.5f, -16.0f,  6.0f, 1.5f,  80, 2.0f }) },
        { "Vocal",  "Whisper / ASMR",          values ({ Split, Abs, St,  7000, 1.5f, -38.0f, 10.0f, 1.0f,  60, 3.0f }) },

        { "Instr",  "Hi-Hat Tamer",            values ({ Bell,  Abs, St,  9000, 1.5f, -28.0f,  8.0f, 0.5f,  50, 2.0f }) },
        { "Instr",  "Cymbals / Overheads",     values ({ Split, Abs, St,  7000, 1.5f, -30.0f,  6.0f, 2.0f, 120, 2.0f }) },
        { "Instr",  "Acoustic Guitar Pick",    values ({ Bell,  Abs, St,  4000, 2.0f, -30.0f,  5.0f, 0.5f,  40, 2.0f }) },
        { "Instr",  "Strings / Brass Edge",    values ({ Bell,  Abs, St,  3500, 1.0f, -26.0f,  5.0f, 3.0f, 100, 2.0f }) },

        { "Bus",    "Mix Bus De-Harsh",        values ({ Split, Abs, Mid, 5000, 1.5f, -24.0f,  3.0f, 3.0f, 100, 2.0f }) },
        { "Bus",    "Master - Gentle",         values ({ Split, Abs, St,  6000, 1.5f, -18.0f,  2.5f, 5.0f, 150, 3.0f }) },
        { "Bus",    "Master - Side Sibilance", values ({ Split, Rel, Side,7000, 1.5f, -20.0f,  4.0f, 2.0f, 100, 3.0f }) },
    };
    return list;
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s) {}

juce::File PresetManager::userDir() const
{
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("Whysho").getChildFile ("DE1").getChildFile ("Presets");
    d.createDirectory();
    return d;
}

juce::StringArray PresetManager::factoryNames() const
{
    juce::StringArray n;
    for (auto& p : factory()) n.add (p.name);
    return n;
}

juce::StringArray PresetManager::userNames() const
{
    juce::StringArray n;
    for (auto& f : userDir().findChildFiles (juce::File::findFiles, false, juce::String ("*") + kUserExt))
        n.add (f.getFileNameWithoutExtension());
    n.sort (true);
    return n;
}

void PresetManager::setParam (const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void PresetManager::applyValues (const V& v) { for (auto& kv : v) setParam (kv.first, kv.second); }

void PresetManager::initToDefault()
{
    for (auto* p : apvts.processor.getParameters())
        p->setValueNotifyingHost (p->getDefaultValue());
}

void PresetManager::applyFactory (const juce::String& name)
{
    for (auto& p : factory())
        if (p.name == name)
        {
            // keep the user's in/out/mix/bypass — presets only shape the de-esser
            applyValues (p.values);
            setParam (pid::listen, 0.0f);
            currentName = name;
            return;
        }
}

void PresetManager::applyUser (const juce::String& name)
{
    auto f = userDir().getChildFile (name + kUserExt);
    if (! f.existsAsFile()) return;
    if (auto xml = juce::XmlDocument::parse (f))
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto theme = apvts.state.getProperty ("uiTheme");
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            if (! theme.isVoid()) apvts.state.setProperty ("uiTheme", theme, nullptr);
            currentName = name;
        }
}

bool PresetManager::saveUser (const juce::String& name)
{
    const auto safe = juce::File::createLegalFileName (name);
    if (safe.isEmpty()) return false;
    if (auto xml = apvts.copyState().createXml())
    {
        currentName = safe;
        return xml->writeTo (userDir().getChildFile (safe + kUserExt));
    }
    return false;
}

bool PresetManager::deleteUser (const juce::String& name)
{
    return userDir().getChildFile (name + kUserExt).deleteFile();
}

void PresetManager::captureTo (bool slotB) { (slotB ? slotBState : slotAState) = apvts.copyState(); }

void PresetManager::recallFrom (bool slotB)
{
    auto& s = slotB ? slotBState : slotAState;
    if (s.isValid()) apvts.replaceState (s.createCopy());
}

} // namespace wsho
