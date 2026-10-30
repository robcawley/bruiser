#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
const char* kMidiControlIds[3] = { pid::section, pid::fill, pid::time };
constexpr int kMidiControlRaw[3] = { 1, 7, 12 };
thread_local const BruiserProcessor* midiMirrorOwner = nullptr;

bool isMidiSwitch (const juce::MidiMessageMetadata& event)
{
    if (event.numBytes != 3) return false;
    const auto status = event.data[0];
    return (status & 0x0f) == BruiserProcessor::kSwitchChannel - 1
        && ((status & 0xf0) == 0x90 || (status & 0xf0) == 0x80)
        && event.data[1] >= BruiserProcessor::kFirstSwitchNote && event.data[1] <= BruiserProcessor::kLastSwitchNote;
}

const char* kOrder[16] = { pid::active, pid::section, pid::loud, pid::complexity, pid::style, pid::fills,
                           pid::fillLen, pid::fill, pid::pocket, pid::swing, pid::human, pid::take,
                           pid::time, pid::hand, pid::kickFollow, pid::meter };

juce::String pct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
}

juce::StringArray BruiserProcessor::meterNames()
{
    juce::StringArray n { "Follow Logic" };
    for (int i = 1; i < kNumMeters; ++i) n.add (juce::String (kMeters[i][0]) + "/" + juce::String (kMeters[i][1]));
    return n;
}

void BruiserProcessor::effectiveMeter (int& num, int& den) const
{
    const int m = juce::jlimit (0, kNumMeters - 1, juce::roundToInt (raw.v[15]->load()));
    if (m == 0) { num = hostNum.load(); den = hostDen.load(); }
    else        { num = kMeters[m][0];   den = kMeters[m][1]; }
}

APVTS::ParameterLayout BruiserProcessor::layout()
{
    using namespace juce;
    APVTS::ParameterLayout l;
    auto pctAttr = AudioParameterFloatAttributes().withStringFromValueFunction (pct)
                       .withValueFromStringFunction ([] (const String& s) { return s.retainCharacters ("-.0123456789").getFloatValue() / 100.0f; });

    l.add (std::make_unique<AudioParameterBool> (ParameterID { pid::active, 1 }, "Play", true));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::section, 1 }, "Section",
               StringArray { "Intro", "Verse", "Pre-Chorus", "Chorus", "Bridge", "Break", "Outro" }, (int) bz::Verse));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::loud, 1 }, "Loudness", NormalisableRange<float> (0, 1), 0.6f, pctAttr));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::complexity, 1 }, "Complexity", NormalisableRange<float> (0, 1), 0.45f, pctAttr));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::style, 1 }, "Slam / Gallop", NormalisableRange<float> (0, 1), 0.5f,
               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
               {
                   const int g = roundToInt (v * 100.0f);
                   return g <= 2 ? String ("ALL SLAM") : g >= 98 ? String ("ALL GALLOP") : String (100 - g) + " / " + String (g);
               })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::fills, 1 }, "Fills", NormalisableRange<float> (0, 1), 0.5f, pctAttr));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::fillLen, 1 }, "Fill Length",
               StringArray { "Auto", "1 Beat", "2 Beats", "Whole Bar" }, 0));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { pid::fill, 1 }, "Fill", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::pocket, 1 }, "Pocket", NormalisableRange<float> (-1, 1), 0.0f,
               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
               {
                   const int ms = roundToInt (std::abs (v) * 12.0f);
                   return ms == 0 ? String ("ON TOP") : (v > 0 ? "PUSH " : "DRAG ") + String (ms) + " ms";
               })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::swing, 1 }, "Swing", NormalisableRange<float> (0, 1), 0.0f,
               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
               { return v < 0.005f ? String ("STRAIGHT") : String (roundToInt (50.0f + v * 16.7f)) + "%"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::human, 1 }, "Human", NormalisableRange<float> (0, 1), 0.5f, pctAttr));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { pid::take, 1 }, "Take", 1, 99, 1));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::time, 1 }, "Time Feel", StringArray { "Half", "Normal", "Double" }, 1));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::hand, 1 }, "Hand",
               StringArray { "Auto", "Hats", "Open Hats", "Ride", "Crash", "Toms" }, 0));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { pid::kickFollow, 1 }, "Kick Follow", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::meter, 1 }, "Meter", meterNames(), 0));
    return l;
}

BruiserProcessor::BruiserProcessor()
    : AudioProcessor (BusesProperties()),
      apvts (*this, &undoManager, "BRUISER", layout())
{
    for (int i = 0; i < 16; ++i) raw.v[i] = apvts.getRawParameterValue (kOrder[i]);
    for (const auto* id : kMidiControlIds) apvts.addParameterListener (id, this);
    startTimerHz (60);
}

BruiserProcessor::~BruiserProcessor()
{
    stopTimer();
    for (const auto* id : kMidiControlIds) apvts.removeParameterListener (id, this);
}

void BruiserProcessor::parameterChanged (const juce::String& id, float)
{
    if (midiMirrorOwner == this) return;
    for (int i = 0; i < 3; ++i)
        if (id == kMidiControlIds[i]) midiControls[i].revision.fetch_add (1, std::memory_order_release);
}

float BruiserProcessor::midiControlValue (int control, float base) const
{
    const auto& state = midiControls[control];
    const auto request = state.request.load (std::memory_order_acquire);
    if (request != 0 && (uint32_t) (request >> 32) == state.revision.load (std::memory_order_acquire))
        return (float) ((int) (request & 0xff) - 1);
    return base;
}

void BruiserProcessor::receiveMidiSwitch (int note)
{
    const int key = note - kFirstSwitchNote;
    const int control = key < 7 ? 0 : key < 9 ? 1 : 2;
    const int value = key < 7 ? key : key < 9 ? (key == 7 ? 1 : 0) : key - 9;
    auto& state = midiControls[control];
    state.serial = (state.serial + 1) & 0x00ffffffu;
    if (state.serial == 0) ++state.serial;
    const auto request = ((uint64_t) state.revision.load (std::memory_order_acquire) << 32)
                       | ((uint64_t) state.serial << 8) | (uint64_t) (value + 1);
    state.request.store (request, std::memory_order_release);
}

void BruiserProcessor::synchroniseMidiControls()
{
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm == nullptr || ! mm->isThisTheMessageThread()) return;
    for (int i = 0; i < 3; ++i)
    {
        auto& state = midiControls[i];
        const auto request = state.request.load (std::memory_order_acquire);
        if (request == 0 || request == state.mirrored
            || (uint32_t) (request >> 32) != state.revision.load (std::memory_order_acquire)) continue;
        auto* parameter = apvts.getParameter (kMidiControlIds[i]);
        const float value = parameter->convertTo0to1 ((float) ((int) (request & 0xff) - 1));
        // Notify the host/UI off the audio thread. Thread-local tagging lets real
        // host automation on another thread supersede the MIDI state normally.
        juce::ScopedValueSetter<const BruiserProcessor*> owner (midiMirrorOwner, this);
        if (std::abs (parameter->getValue() - value) > 1.0e-6f)
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (value);
            parameter->endChangeGesture();
        }
        state.mirrored = request;
    }
}

void BruiserProcessor::prepareToPlay (double sampleRate, int)
{
    player.prepare (sampleRate);
    switchSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    switchInput.ensureSize (16384);
    switchSlice.ensureSize (16384);
}

bz::Params BruiserProcessor::currentParams() const
{
    auto v = [&] (int i)
    {
        const float base = raw.v[i]->load (std::memory_order_relaxed);
        return i == 1 ? midiControlValue (0, base) : i == 12 ? midiControlValue (2, base) : base;
    };
    bz::Params p;
    p.section = (bz::Section) juce::jlimit (0, (int) bz::NumSections - 1, juce::roundToInt (v (1)));
    p.loud = v (2); p.complexity = v (3); p.style = v (4); p.fills = v (5);
    p.fillLen = (bz::FillLen) juce::jlimit (0, 3, juce::roundToInt (v (6)));
    p.pocket = v (8); p.swing = v (9); p.human = v (10);
    p.take = juce::jlimit (1, 99, juce::roundToInt (v (11)));
    p.time = (bz::TimeMode) juce::jlimit (0, 2, juce::roundToInt (v (12)));
    p.hand = (bz::Hand) juce::jlimit (0, 5, juce::roundToInt (v (13)));
    p.kickFollow = v (14) > 0.5f;
    return p;
}

void BruiserProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    bz::HostState h;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            h.playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) h.ppq = *q; else h.playing = false;
            if (auto b = pos->getBpm()) h.bpm = juce::jlimit (20.0, 400.0, *b);
            if (auto ts = pos->getTimeSignature())
            {
                h.num = juce::jlimit (1, 32, ts->numerator);
                h.den = ts->denominator;
                if (h.den != 1 && h.den != 2 && h.den != 4 && h.den != 8 && h.den != 16) h.den = 4;
            }
            if (auto bs = pos->getPpqPositionOfLastBarStart()) { h.hasBar = true; h.barStartPpq = *bs; }
            if (auto bc = pos->getBarCount()) h.barCount = *bc;
        }
    hostBpm.store (h.bpm); hostNum.store (h.num); hostDen.store (h.den); hostPlaying.store (h.playing);

    // A fixed METER plays its own bars, counted from the start of the song.
    const int m = juce::jlimit (0, kNumMeters - 1, juce::roundToInt (raw.v[15]->load()));
    if (m > 0 && (h.num != kMeters[m][0] || h.den != kMeters[m][1]))
    {
        h.num = kMeters[m][0]; h.den = kMeters[m][1];
        h.hasBar = false; h.barCount = -1;
    }
    playNum.store (h.num); playDen.store (h.den);

    const auto process = [&] (bz::HostState host, juce::MidiBuffer& events, int count)
    {
        player.process (currentParams(), midiControlValue (1, raw.v[7]->load()) > 0.5f,
                        raw.v[0]->load() > 0.5f, host, events, count);
    };
    bool switches = false;
    for (const auto event : midi) if (isMidiSwitch (event)) { switches = true; break; }
    if (! switches) { process (h, midi, buffer.getNumSamples()); return; }

    // Split at key-switch timestamps: a command near a bar line must not be
    // applied early simply because both sides of the line share an audio block.
    switchInput.clear(); switchInput.swapWith (midi); midi.clear();
    auto read = switchInput.cbegin();
    int start = 0;
    const auto renderTo = [&] (int end)
    {
        if (end <= start) return;
        switchSlice.clear();
        while (read != switchInput.cend() && (*read).samplePosition < end)
        {
            const auto event = *read++;
            if (! isMidiSwitch (event))
                switchSlice.addEvent (event.data, event.numBytes, event.samplePosition - start);
        }
        auto segment = h;
        segment.ppq += (double) start * h.bpm / 60.0 / switchSampleRate;
        process (segment, switchSlice, end - start);
        midi.addEvents (switchSlice, 0, end - start, start);
        start = end;
    };
    for (const auto event : switchInput)
        if (isMidiSwitch (event) && (event.data[0] & 0xf0) == 0x90 && event.data[2] > 0)
        {
            renderTo (juce::jlimit (0, buffer.getNumSamples(), event.samplePosition));
            receiveMidiSwitch (event.data[1]);
        }
    renderTo (buffer.getNumSamples());
}

int BruiserProcessor::getNumPrograms() { return bz::kNumPresets; }

const juce::String BruiserProcessor::getProgramName (int index)
{
    if (! juce::isPositiveAndBelow (index, bz::kNumPresets)) return {};
    return juce::String (bz::kPresets[index].category) + " / " + presetDisplayName (index);
}

juce::String BruiserProcessor::presetDisplayName (int index) const
{
    if (! juce::isPositiveAndBelow (index, bz::kNumPresets)) return {};
    const auto& preset = bz::kPresets[index];
    auto name = juce::String (preset.name);
    if (preset.meter > 0) name += "  " + meterNames()[preset.meter];
    return name;
}

void BruiserProcessor::surprisePreset()
{
    const int choice = juce::Random::getSystemRandom().nextInt (bz::kNumPresets - 1);
    applyPreset (choice >= currentPreset ? choice + 1 : choice);
}

namespace
{
// Restore the selected program alongside its parameter changes in one transaction.
class PresetIndexAction final : public juce::UndoableAction
{
public:
    PresetIndexAction (BruiserProcessor& p, int target) : proc (p), before (p.currentPreset), after (target) {}
    bool perform() override { return set (after); }
    bool undo() override { return set (before); }
private:
    bool set (int index)
    {
        proc.currentPreset = index;
        proc.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withProgramChanged (true));
        return true;
    }
    BruiserProcessor& proc;
    int before, after;
};
struct PresetValue { const char* id; float v; };
void presetValues (const bz::Preset& p, PresetValue* out)
{
    out[0] = { pid::loud, p.loud };       out[1] = { pid::complexity, p.complexity };
    out[2] = { pid::style, p.style };     out[3] = { pid::fills, p.fills };
    out[4] = { pid::fillLen, (float) p.fillLen };
    out[5] = { pid::pocket, p.pocket };   out[6] = { pid::swing, p.swing };
    out[7] = { pid::human, p.human };     out[8] = { pid::time, (float) p.time };
    out[9] = { pid::hand, (float) p.hand }; out[10] = { pid::take, (float) p.take };
    out[11] = { pid::meter, (float) p.meter };
}
constexpr int kPresetValues = 12;
}

void BruiserProcessor::applyPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, bz::kNumPresets)) return;
    synchroniseMidiControls();
    // Flush pending APVTS values before and after the transaction so rapid edits
    // cannot drift into the next preset's undo step.
    apvts.copyState();
    undoManager.beginNewTransaction ("Load preset");
    undoManager.perform (new PresetIndexAction (*this, index));
    PresetValue vals[kPresetValues];
    presetValues (bz::kPresets[index], vals);
    for (auto& pv : vals)
        if (auto* prm = apvts.getParameter (pv.id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (pv.v));
            prm->endChangeGesture();
        }
    apvts.copyState();
    undoManager.beginNewTransaction();
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

bool BruiserProcessor::presetModified() const
{
    if (! juce::isPositiveAndBelow (currentPreset, bz::kNumPresets)) return true;
    PresetValue vals[kPresetValues];
    presetValues (bz::kPresets[currentPreset], vals);
    for (auto& pv : vals)
    {
        auto* prm = apvts.getParameter (pv.id);
        if (std::abs (prm->getValue() - prm->convertTo0to1 (pv.v)) > 0.004f) return true;
    }
    return false;
}

void BruiserProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    // Saving does not depend on the UI timer having mirrored the latest note yet.
    for (auto child : state)
        for (int i = 0; i < 3; ++i)
            if (child.getProperty ("id").toString() == kMidiControlIds[i])
                child.setProperty ("value", midiControlValue (i, raw.v[kMidiControlRaw[i]]->load()), nullptr);
    state.setProperty ("uiScale", uiScale, nullptr);
    state.setProperty ("preset", currentPreset, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void BruiserProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto t = juce::ValueTree::fromXml (*xml);
            uiScale = juce::jlimit (0.75f, 2.0f, (float) t.getProperty ("uiScale", 1.0f));
            currentPreset = juce::jlimit (0, bz::kNumPresets - 1, (int) t.getProperty ("preset", 0));
            for (auto& control : midiControls) control.request.store (0, std::memory_order_release);
            apvts.replaceState (t);
        }
}

juce::AudioProcessorEditor* BruiserProcessor::createEditor() { return new BruiserEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BruiserProcessor(); }
