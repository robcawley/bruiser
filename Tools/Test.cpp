// BRUISER test bench. Run: BruiserTest <outdir> [robkit samples dir]
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Engine/Export.h"
#include "Presets.h"
#include "DSP/Kit.h"
#include <cstdlib>
#include <map>
#include <new>
#include <set>
#include <cstring>

thread_local bool probe = false;
thread_local uint64_t allocs = 0;
void* operator new (std::size_t n) { if (probe) ++allocs; if (void* p = std::malloc (std::max (n, std::size_t (1)))) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n) { return ::operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
constexpr double SR = 48000.0;
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

void set (BruiserProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

struct Head final : juce::AudioPlayHead
{
    PositionInfo info;
    juce::Optional<PositionInfo> getPosition() const override { return info; }
};

struct Ev { double ppq; int note; int vel; bool on; };

// Drive the processor like a host: random block sizes, transport from 'from' to 'to' (ppq).
struct Host
{
    BruiserProcessor& p;
    Head head;
    double bpm = 120.0; int num = 4, den = 4;
    juce::Random rng { 11 };
    std::vector<Ev> events;
    std::function<void (double ppq, juce::MidiBuffer&, int n)> before;
    bool probeAllocs = false;
    uint64_t allocCount = 0;
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> audio { 0, 2048 };

    explicit Host (BruiserProcessor& pr) : p (pr) { p.setPlayHead (&head); p.prepareToPlay (SR, 1024); midi.ensureSize (16384); }

    void block (double ppq, int n, bool playing)
    {
        const double barQ = bz::barLengthQ (num, den);
        head.info.setIsPlaying (playing);
        head.info.setPpqPosition (ppq);
        head.info.setBpm (bpm);
        head.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { num, den });
        const double bar = std::floor (ppq / barQ + 1.0e-9);
        head.info.setPpqPositionOfLastBarStart (bar * barQ);
        head.info.setBarCount ((int64_t) bar);
        midi.clear();
        if (before) before (ppq, midi, n);
        audio.setSize (0, n, false, false, true);
        probe = probeAllocs; allocs = 0;
        p.processBlock (audio, midi);
        probe = false; allocCount += allocs;
        const double qps = bpm / 60.0 / SR;
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            if (msg.isNoteOnOrOff())
                events.push_back ({ ppq + m.samplePosition * qps, msg.getNoteNumber(), msg.getVelocity(), msg.isNoteOn() });
        }
    }

    void play (double from, double to)
    {
        const double qps = bpm / 60.0 / SR;
        double ppq = from;
        while (ppq < to - 1.0e-9)
        {
            int n = 32 + rng.nextInt (1000);
            n = juce::jmin (n, (int) std::ceil ((to - ppq) / qps));
            block (ppq, n, true);
            ppq += n * qps;
        }
    }
    void stop() { block (0.0, 512, false); }

    std::vector<Ev> ons() const
    {
        std::vector<Ev> o;
        for (auto& e : events) if (e.on) o.push_back (e);
        return o;
    }
};

bool balanced (const std::vector<Ev>& ev)
{
    std::map<int, int> open;
    for (auto& e : ev) open[e.note] += e.on ? 1 : -1;
    for (auto& [n, c] : open) if (c != 0) return false;
    return true;
}

int duplicates (const std::vector<Ev>& ons, double bpm)
{
    const double win = 0.002 * bpm / 60.0;   // 2 ms
    int d = 0;
    for (size_t i = 0; i < ons.size(); ++i)
        for (size_t j = i + 1; j < ons.size() && ons[j].ppq - ons[i].ppq < win; ++j)
            if (ons[j].note == ons[i].note) ++d;
    return d;
}

// Every hit the export makes is played live at the same time and velocity, and nothing extra.
bool sameAs (std::vector<Ev> live, std::vector<bz::TimedHit> off, double bpm, double from, double to, juce::String& why)
{
    const double tol = 2.0 * bpm / 60.0 / SR;
    std::vector<Ev> o;
    for (auto& h : off) if (h.q >= from - 1.0e-9 && h.q < to) o.push_back ({ h.q, h.note, h.vel, true });
    std::vector<Ev> l;
    for (auto& e : live) if (e.ppq >= from - 1.0e-9 && e.ppq < to) l.push_back (e);
    auto key = [] (const Ev& a, const Ev& b) { return a.ppq < b.ppq - 1.0e-7 || (std::abs (a.ppq - b.ppq) <= 1.0e-7 && a.note < b.note); };
    std::sort (o.begin(), o.end(), key);
    std::sort (l.begin(), l.end(), key);
    if (o.size() != l.size()) { why = "count " + juce::String ((int) l.size()) + " live vs " + juce::String ((int) o.size()) + " export"; return false; }
    std::multiset<std::pair<int, int>> unmatched;
    std::vector<bool> used (l.size(), false);
    for (auto& e : o)
    {
        bool found = false;
        for (size_t k = 0; k < l.size(); ++k)
            if (! used[k] && l[k].note == e.note && l[k].vel == e.vel && std::abs (l[k].ppq - e.ppq) <= tol) { used[k] = true; found = true; break; }
        if (! found) { why = "no live match for note " + juce::String (e.note) + " at " + juce::String (e.ppq, 4); return false; }
    }
    return true;
}

std::vector<bz::SongPart> song()
{
    return { { bz::Intro, 4 }, { bz::Verse, 8 }, { bz::Pre, 4 }, { bz::Chorus, 8 }, { bz::Bridge, 4 },
             { bz::Break, 2 }, { bz::Chorus, 8 }, { bz::Outro, 4, true } };
}

struct SecHit { double sec; int note; int vel; };

std::vector<SecHit> toSeconds (const std::vector<bz::TimedHit>& hits, double bpm, double offsetSec = 0.0)
{
    std::vector<SecHit> v;
    for (auto& h : hits) v.push_back ({ offsetSec + h.q * 60.0 / bpm, h.note, h.vel });
    return v;
}

void renderAudio (std::vector<SecHit> sorted, const juce::File& out, rk::Kit& kit)
{
    double end = 0.0;
    for (auto& h : sorted) end = juce::jmax (end, h.sec);
    const int total = (int) ((end + 3.0) * SR);
    juce::AudioBuffer<float> all (2, total), blk (2, 256);
    kit.prepare (SR, 256);
    rk::Settings s;
    for (int g = 0; g < NumGroups; ++g) { s.levelDb[(size_t) g] = g == Perc ? -2.0f : 0.0f; s.decay[(size_t) g] = 1.0f; }
    kit.setSettings (s);
    kit.allSoundOff();
    size_t next = 0;
    std::sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.sec < b.sec; });
    for (int pos = 0; pos < total; pos += 256)
    {
        const int n = juce::jmin (256, total - pos);
        blk.clear();
        int cursor = 0;
        while (next < sorted.size() && (int) (sorted[next].sec * SR) < pos + n)
        {
            const int at = juce::jlimit (0, n, (int) (sorted[next].sec * SR) - pos);
            if (at > cursor) { kit.render (blk, cursor, at - cursor); cursor = at; }
            kit.noteOn (sorted[next].note, (float) sorted[next].vel / 127.0f, 0);
            ++next;
        }
        if (cursor < n) kit.render (blk, cursor, n - cursor);
        kit.finish (blk, n);
        for (int c = 0; c < 2; ++c) all.copyFrom (c, pos, blk, c, 0, n);
    }
    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (out), SR, 2, 24, {}, 0));
    if (w) w->writeFromAudioSampleBuffer (all, 0, total);
}

double tourBpm (const juce::String& cat)
{
    if (cat == "GALLOP") return 160; if (cat == "FAST") return 150; if (cat == "HEAVY") return 96;
    if (cat == "SHUFFLE") return 112; if (cat == "INDIE") return 118; if (cat == "CINEMATIC") return 84;
    if (cat == "ARENA") return 128; if (cat == "SHOWOFF") return 132; return 124;
}

void meterOf (const bz::Preset& pr, int& num, int& den)
{
    num = pr.meter > 0 ? BruiserProcessor::kMeters[pr.meter][0] : 4;
    den = pr.meter > 0 ? BruiserProcessor::kMeters[pr.meter][1] : 4;
}

bz::Params paramsFor (const bz::Preset& pr)
{
    bz::Params p;
    p.loud = pr.loud; p.complexity = pr.complexity; p.style = pr.style; p.fills = pr.fills;
    p.fillLen = (bz::FillLen) pr.fillLen; p.pocket = pr.pocket; p.swing = pr.swing; p.human = pr.human;
    p.time = (bz::TimeMode) pr.time; p.hand = (bz::Hand) pr.hand; p.take = pr.take;
    return p;
}

juce::MouseEvent mouse (juce::Component& c, juce::Point<float> p, juce::ModifierKeys mods = {})
{
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, mods, 1.0f, 0, 0, 0, 0,
                             &c, &c, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, false);
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    out.createDirectory();

    // ---- 1.4 MIDI performance switches
    {
        BruiserProcessor p; p.prepareToPlay (SR, 1024);
        juce::AudioBuffer<float> audio (0, 512); juce::MidiBuffer midi; midi.ensureSize (16384);
        uint64_t allocations = 0; bool mapped = true, swallowed = true;
        const char* ids[] = { pid::section, pid::fill, pid::time };
        const auto send = [&] (int note, int channel = BruiserProcessor::kSwitchChannel, int velocity = 100)
        {
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (channel, note, (juce::uint8) velocity), 20);
            midi.addEvent (juce::MidiMessage::noteOff (channel, note), 100);
            probe = true; allocs = 0; p.processBlock (audio, midi); probe = false; allocations += allocs;
        };
        for (int key = 0; key < 12; ++key)
        {
            send (12 + key);
            swallowed &= midi.isEmpty();
            const int control = key < 7 ? 0 : key < 9 ? 1 : 2;
            const int value = key < 7 ? key : key < 9 ? (key == 7 ? 1 : 0) : key - 9;
            if (control == 0) mapped &= (int) p.currentParams().section == value;
            if (control == 2) mapped &= (int) p.currentParams().time == value;
            p.synchroniseMidiControls();
            mapped &= juce::roundToInt (p.apvts.getRawParameterValue (ids[control])->load()) == value;
        }
        check (mapped, "channel 16 notes 12-23 select all sections, Fill On/Off and Half/Normal/Double");
        check (swallowed, "MIDI switch note-ons and note-offs never reach ROB KIT");
        send (19); send (19); p.synchroniseMidiControls();
        const bool staysOn = p.apvts.getRawParameterValue (pid::fill)->load() > 0.5f;
        send (20); send (20); p.synchroniseMidiControls();
        check (staysOn && p.apvts.getRawParameterValue (pid::fill)->load() < 0.5f,
               "explicit Fill On/Off notes are repeatable when regions loop");
        const auto previous = p.currentParams();
        send (12, 1);
        check (p.currentParams() == previous && midi.getNumEvents() == 2,
               "switch pitches on other MIDI channels pass through without changing controls");
        send (36);
        check (midi.getNumEvents() == 2, "ordinary drum notes on channel 16 still pass through");
        send (12, 16, 0);
        check (p.currentParams() == previous && midi.isEmpty(), "velocity-zero switch notes are releases, not commands");
        set (p, pid::kickFollow, 1);
        send (15);
        check (p.currentParams().section == bz::Chorus && midi.isEmpty(), "switch notes are filtered before Kick Follow");
        set (p, pid::kickFollow, 0);
        p.synchroniseMidiControls();
        set (p, pid::section, (float) bz::Verse);
        p.synchroniseMidiControls();
        check (p.currentParams().section == bz::Verse
            && juce::roundToInt (p.apvts.getRawParameterValue (pid::section)->load()) == bz::Verse,
               "subsequent UI or host parameter changes supersede a MIDI selection");
        juce::MemoryBlock verseState; p.getStateInformation (verseState);
        send (12); // Save immediately, before a message-thread mirror.
        juce::MemoryBlock midiState; p.getStateInformation (midiState);
        BruiserProcessor recalled; recalled.setStateInformation (midiState.getData(), (int) midiState.getSize());
        check (recalled.currentParams().section == bz::Intro, "saving immediately after a switch note recalls its selected section");
        p.setStateInformation (verseState.getData(), (int) verseState.getSize()); p.synchroniseMidiControls();
        check (p.currentParams().section == bz::Verse, "restoring a song cancels stale pending MIDI selections");
        check (allocations == 0, "MIDI switches allocate no memory on the audio thread (" + juce::String ((int) allocations) + ")");
    }
    {
        BruiserProcessor keyed, automated;
        Host live (keyed), reference (automated);
        const double positions[] = { 0.0, 4.25, 8.25, 9.0, 11.0, 12.25, 16.25, 20.25 };
        const int notes[] = { 12, 13, 15, 19, 20, 21, 23, 22 };
        int keyStep = 0, parameterStep = 0;
        live.probeAllocs = true;
        live.before = [&] (double q, juce::MidiBuffer& midi, int)
        {
            if (keyStep < (int) std::size (positions) && q >= positions[keyStep])
            {
                const int note = notes[keyStep++];
                midi.addEvent (juce::MidiMessage::noteOn (16, note, (juce::uint8) 100), 0);
                midi.addEvent (juce::MidiMessage::noteOff (16, note), 1);
            }
        };
        reference.before = [&] (double q, juce::MidiBuffer&, int)
        {
            if (parameterStep < (int) std::size (positions) && q >= positions[parameterStep])
            {
                const int key = notes[parameterStep++] - 12;
                if (key < 7) set (automated, pid::section, (float) key);
                else if (key < 9) set (automated, pid::fill, key == 7 ? 1.0f : 0.0f);
                else set (automated, pid::time, (float) (key - 9));
            }
        };
        live.play (0, 28); live.stop(); reference.play (0, 28); reference.stop();
        bool same = live.events.size() == reference.events.size();
        for (size_t i = 0; same && i < live.events.size(); ++i)
        {
            const auto& a = live.events[i]; const auto& b = reference.events[i];
            same = a.note == b.note && a.vel == b.vel && a.on == b.on && std::abs (a.ppq - b.ppq) < 1e-9;
        }
        check (same, "recorded MIDI switches reproduce equivalent section, Fill and time parameter automation");
        check (live.allocCount == 0, "MIDI switching during transport remains allocation-free");
    }
    {
        // A switch after a bar line inside one block must not change its prefix.
        BruiserProcessor keyed, reference;
        Host live (keyed), split (reference);
        live.before = [] (double, juce::MidiBuffer& midi, int)
        {
            midi.addEvent (juce::MidiMessage::noteOn (16, 15, (juce::uint8) 100), 700);
        };
        live.block (3.98, 1024, true);
        split.block (3.98, 700, true);
        set (reference, pid::section, (float) bz::Chorus);
        split.block (3.98 + 700.0 * 120.0 / 60.0 / SR, 324, true);
        bool same = live.events.size() == split.events.size();
        for (size_t i = 0; same && i < live.events.size(); ++i)
        {
            const auto& a = live.events[i]; const auto& b = split.events[i];
            same = a.note == b.note && a.vel == b.vel && a.on == b.on && std::abs (a.ppq - b.ppq) < 1e-9;
        }
        check (same, "switch timestamps inside a block match processing split at the exact MIDI sample");
    }

    {
        // A ready-to-drag control region: five four-bar sections in 4/4.
        constexpr int tpq = 960;
        juce::MidiMessageSequence sequence;
        const auto key = [&] (double q, int note)
        {
            auto on = juce::MidiMessage::noteOn (16, note, (juce::uint8) 100);
            auto off = juce::MidiMessage::noteOff (16, note);
            on.setTimeStamp (q * tpq); off.setTimeStamp ((q + 0.125) * tpq);
            sequence.addEvent (on); sequence.addEvent (off);
        };
        const char* names[] = { "Intro", "Verse", "Chorus", "Bridge", "Outro" };
        const int sections[] = { 12, 13, 15, 16, 18 };
        key (0, 22); // Normal time, explicit so restarting is repeatable.
        for (int i = 0; i < 5; ++i)
        {
            const double q = i * 16.0;
            auto label = juce::MidiMessage::textMetaEvent (6, names[i]); label.setTimeStamp (q * tpq); sequence.addEvent (label);
            key (q, sections[i]); key (q, 20); // Reset Fill Off at every section start.
            if (i < 4) key (q + 12, 19);      // Fill in the last bar before the change.
        }
        auto end = juce::MidiMessage::endOfTrack(); end.setTimeStamp (80 * tpq); sequence.addEvent (end);
        sequence.sort(); sequence.updateMatchedPairs();
        juce::MidiFile file; file.setTicksPerQuarterNote (tpq); file.addTrack (sequence);
        check (bz::writeMidiFile (out.getChildFile ("BRUISER MIDI arrangement - channel 16.mid"), file),
               "wrote a 20-bar Intro/Verse/Chorus/Bridge/Outro MIDI switch example");
    }

    // ---- Fill automation: the visible button must write what the host can replay.
    {
        struct Capture final : juce::AudioProcessorParameter::Listener
        {
            juce::StringArray events;
            void parameterValueChanged (int, float v) override { events.add (v > 0.5f ? "on" : "off"); }
            void parameterGestureChanged (int, bool starting) override { events.add (starting ? "begin" : "end"); }
        } capture;
        BruiserProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto& panel = dynamic_cast<BruiserEditor*> (ed.get())->getPanel();
        auto* button = panel.findControl (pid::fill);
        auto* fill = p.apvts.getParameter (pid::fill);
        fill->addListener (&capture);
        button->mouseDown (mouse (*button, { 30.0f, 15.0f }));
        const bool pressed = fill->getValue() > 0.5f && capture.events.joinIntoString (",") == "begin,on";
        button->mouseUp (mouse (*button, { 30.0f, 15.0f }));
        const bool held = fill->getValue() > 0.5f && capture.events.joinIntoString (",") == "begin,on,end";
        button->mouseDown (mouse (*button, { 30.0f, 15.0f }));
        button->mouseUp (mouse (*button, { 30.0f, 15.0f }));
        check (fill->isAutomatable() && pressed && held && fill->getValue() < 0.5f
            && capture.events.joinIntoString (",") == "begin,on,end,begin,off,end",
               "FILL normal clicks notify the host with balanced gestures and persistent On/Off values");
        button->mouseDown (mouse (*button, { 30.0f, 15.0f }));
        button->mouseUp (mouse (*button, { 30.0f, 15.0f }));
        juce::MemoryBlock state; p.getStateInformation (state);
        BruiserProcessor restored; restored.setStateInformation (state.getData(), (int) state.getSize());
        const bool saved = restored.apvts.getParameter (pid::fill)->getValue() > 0.5f;
        const bool undone = p.undoManager.undo() && fill->getValue() < 0.5f;
        const bool redone = p.undoManager.redo() && fill->getValue() > 0.5f;
        check (saved && undone && redone, "Fill On survives saved state and supports undo/redo");
        fill->removeListener (&capture);

        BruiserProcessor written, replayed;
        set (written, pid::fills, 0); set (replayed, pid::fills, 0);
        std::unique_ptr<juce::AudioProcessorEditor> liveEditor (written.createEditor());
        auto* liveButton = dynamic_cast<BruiserEditor*> (liveEditor.get())->getPanel().findControl (pid::fill);
        std::vector<std::pair<double, float>> recorded;
        Host live (written); int step = 0;
        live.before = [&] (double q, juce::MidiBuffer&, int)
        {
            if ((step == 0 && q >= 4.25) || (step == 1 && q >= 8.25))
            {
                liveButton->mouseDown (mouse (*liveButton, { 30.0f, 15.0f }));
                liveButton->mouseUp (mouse (*liveButton, { 30.0f, 15.0f }));
                recorded.emplace_back (q, written.apvts.getParameter (pid::fill)->getValue());
                ++step;
            }
        };
        live.play (0, 16); live.stop();
        Host playback (replayed); size_t next = 0;
        playback.before = [&] (double q, juce::MidiBuffer&, int)
        {
            while (next < recorded.size() && q >= recorded[next].first)
                replayed.apvts.getParameter (pid::fill)->setValueNotifyingHost (recorded[next++].second);
        };
        playback.play (0, 16); playback.stop();
        bool same = recorded.size() == 2 && live.events.size() == playback.events.size();
        for (size_t i = 0; same && i < live.events.size(); ++i)
        {
            const auto& a = live.events[i]; const auto& b = playback.events[i];
            same = a.note == b.note && a.vel == b.vel && a.on == b.on && std::abs (a.ppq - b.ppq) < 1e-9;
        }
        check (same, "replaying recorded Fill parameter changes reproduces button-driven MIDI exactly");
    }

    // Every automatable parameter: record UI gestures, replay values like a host,
    // and round-trip the resulting song state. Meter uses the same attachment path
    // as its asynchronous menu callback, without opening a popup in the test.
    {
        struct Change { double q; int index; float value; };
        struct Capture final : juce::AudioProcessorParameter::Listener
        {
            Capture (double& at, std::vector<Change>& out) : q (at), changes (out) {}
            void parameterValueChanged (int index, float value) override { changes.push_back ({ q, index, value }); }
            void parameterGestureChanged (int, bool starting) override { if (starting) ++begins; else ++ends; }
            double& q; std::vector<Change>& changes; int begins = 0, ends = 0;
        };
        BruiserProcessor written, replayed;
        double currentQ = 0;
        std::vector<Change> changes;
        std::vector<std::unique_ptr<Capture>> listeners;
        for (auto* param : written.getParameters())
        {
            listeners.push_back (std::make_unique<Capture> (currentQ, changes));
            param->addListener (listeners.back().get());
        }
        std::unique_ptr<juce::AudioProcessorEditor> ed (written.createEditor());
        auto& panel = dynamic_cast<BruiserEditor*> (ed.get())->getPanel();
        auto click = [&] (const char* id, float across)
        {
            auto* c = panel.findControl (id);
            const juce::Point<float> point { c->getWidth() * across, c->getHeight() * 0.2f };
            c->mouseDown (mouse (*c, point)); c->mouseUp (mouse (*c, point));
        };
        Host live (written); int step = 0;
        const double positions[] = { 0.25, 2.25, 4.25, 6.25, 8.25, 9.25, 12.25, 14.25, 15.25 };
        live.before = [&] (double q, juce::MidiBuffer&, int)
        {
            currentQ = q;
            if (step >= (int) std::size (positions) || q < positions[step]) return;
            switch (step++)
            {
                case 0: click (pid::section, 0.5f / 7.0f); break; // Intro
                case 1:
                {
                    click (pid::style, 0.9f);
                    for (const auto* id : { pid::fills, pid::pocket, pid::swing, pid::human })
                    {
                        auto* c = panel.findControl (id);
                        juce::MouseWheelDetails wheel; wheel.deltaY = 0.5f;
                        c->mouseWheelMove (mouse (*c, { 20.0f, 20.0f }), wheel);
                    }
                    click (pid::take, 0.9f); click (pid::time, 0.5f / 3.0f);
                    click (pid::hand, 3.5f / 6.0f); click (pid::fillLen, 1.5f / 4.0f);
                    break;
                }
                case 2: click (pid::section, 3.5f / 7.0f); break; // Chorus
                case 3:
                {
                    click (pid::loud, 0.8f);
                    juce::ParameterAttachment meter (*written.apvts.getParameter (pid::meter), [] (float) {}, &written.undoManager);
                    written.apvts.copyState(); meter.setValueAsCompleteGesture (4); written.apvts.copyState();
                    break;
                }
                case 4: click (pid::section, 1.5f / 7.0f); break; // Verse
                case 5: click (pid::fill, 0.5f); break;
                case 6: click (pid::fill, 0.5f); click (pid::kickFollow, 0.5f); break;
                case 7: click (pid::active, 0.5f); break;
                case 8: click (pid::active, 0.5f); break;
                default: break;
            }
        };
        live.play (0, 20); live.stop();
        bool gestures = true;
        std::set<int> changed;
        for (const auto& change : changes) changed.insert (change.index);
        for (size_t i = 0; i < listeners.size(); ++i)
        {
            gestures &= listeners[i]->begins > 0 && listeners[i]->begins == listeners[i]->ends;
            written.getParameters()[(int) i]->removeListener (listeners[i].get());
        }
        check (gestures && changed.size() == 16, "all 16 parameters send host-recordable values and balanced gestures");
        std::vector<int> sections;
        const int sectionIndex = written.apvts.getParameter (pid::section)->getParameterIndex();
        for (const auto& change : changes)
            if (change.index == sectionIndex)
                sections.push_back (juce::roundToInt (written.apvts.getParameter (pid::section)->convertFrom0to1 (change.value)));
        check (sections == std::vector<int> { bz::Intro, bz::Chorus, bz::Verse }, "Intro, Chorus and Verse clicks reach host automation in order");
        Host playback (replayed); size_t next = 0;
        playback.before = [&] (double q, juce::MidiBuffer&, int)
        {
            while (next < changes.size() && q >= changes[next].q)
            {
                const auto change = changes[next++];
                replayed.getParameters()[change.index]->setValueNotifyingHost (change.value);
            }
        };
        playback.play (0, 20); playback.stop();
        bool same = next == changes.size() && live.events.size() == playback.events.size();
        for (size_t i = 0; same && i < live.events.size(); ++i)
        {
            const auto& a = live.events[i]; const auto& b = playback.events[i];
            same = a.note == b.note && a.vel == b.vel && a.on == b.on && std::abs (a.ppq - b.ppq) < 1e-9;
        }
        check (same, "replaying all recorded controls and section changes reproduces live MIDI exactly");
        juce::MemoryBlock state; written.getStateInformation (state);
        BruiserProcessor recalled; recalled.setStateInformation (state.getData(), (int) state.getSize());
        bool stored = true;
        for (int i = 0; i < written.getParameters().size(); ++i)
            stored &= std::abs (written.getParameters()[i]->getValue() - recalled.getParameters()[i]->getValue()) < 1e-6f;
        check (stored, "song state recalls all 16 parameters after an automated performance");
    }

    // ---- 1.3 regression checks
    {
        check (bz::beatIndexAt (4, 4, 0.0) == 0 && bz::beatIndexAt (4, 4, 0.999) == 0
            && bz::beatIndexAt (4, 4, 1.0) == 1 && bz::beatIndexAt (4, 4, 3.999) == 3,
               "beatIndexAt counts quarter groups in 4/4");
        check (bz::beatIndexAt (7, 8, 0.999) == 0 && bz::beatIndexAt (7, 8, 1.0) == 1
            && bz::beatIndexAt (7, 8, 2.0) == 2 && bz::beatIndexAt (7, 8, 3.49) == 2,
               "beatIndexAt counts 2+2+3 groups in 7/8");
        check (bz::beatIndexAt (12, 8, 1.499) == 0 && bz::beatIndexAt (12, 8, 1.5) == 1
            && bz::beatIndexAt (12, 8, 3.0) == 2 && bz::beatIndexAt (12, 8, 5.99) == 3,
               "beatIndexAt counts dotted-quarter groups in 12/8");

        // Golden fingerprint captured from the pre-1.3 engine. Exclude only the
        // intentionally new compound swing > 0 behavior; hash fields, not padding.
        uint64_t hash = 1469598103934665603ull;
        auto add = [&] (const auto& v)
        {
            unsigned char bytes[sizeof (v)]; std::memcpy (bytes, &v, sizeof (v));
            for (auto byte : bytes) { hash ^= byte; hash *= 1099511628211ull; }
        };
        for (int den : { 4, 8, 16 }) for (int num = 1; num <= 16; ++num)
            for (int take = 1; take <= 3; ++take) for (int section = 0; section < 7; ++section)
                for (int mode = 0; mode < 3; ++mode) for (float swing : { 0.0f, 0.75f })
                {
                    if (den == 8 && num % 3 == 0 && swing > 0) continue;
                    bz::Params p; p.take = take; p.section = (bz::Section) section;
                    p.time = (bz::TimeMode) mode; p.swing = swing; p.complexity = 0.83f;
                    bz::BarContext c; c.num = num; c.den = den; c.bar = 7; c.barInSection = 7; c.forceFill = take == 3;
                    bz::Bar b; bz::generate (p, c, b); add (b.count); add (b.lengthQ); add (b.fillStartQ);
                    for (int i = 0; i < b.count; ++i)
                    {
                        auto h = b.hits[i]; add (h.q); add (h.note); add (h.vel); add (h.lane); add (h.fill);
                    }
                }
        check (hash == 15367741002564795744ull, "pre-1.3 groove fingerprint unchanged outside compound swing");

        bool swingOK = true; int moved = 0, untouched = 0;
        for (int num : { 6, 9, 12 }) for (int mode = 0; mode < 3; ++mode) for (bool fill : { false, true })
        {
            bz::Params p; p.style = 1; p.human = 0; p.pocket = 0; p.complexity = 0.9f; p.time = (bz::TimeMode) mode;
            bz::BarContext c; c.num = num; c.den = 8; c.bar = 7; c.barInSection = 7; c.forceFill = fill;
            bz::Bar straight, swung; bz::generate (p, c, straight); p.swing = 1; bz::generate (p, c, swung);
            swingOK &= straight.count == swung.count;
            for (int note = 0; note < 128; ++note)
            {
                std::vector<bz::Hit> a, b;
                for (int i = 0; i < straight.count; ++i) if (straight.hits[i].note == note) a.push_back (straight.hits[i]);
                for (int i = 0; i < swung.count; ++i) if (swung.hits[i].note == note) b.push_back (swung.hits[i]);
                if (a.size() != b.size()) { swingOK = false; continue; }
                for (size_t i = 0; i < a.size(); ++i)
                {
                    // Remove the unchanged style lean to recover the nominal subdivision.
                    double ms = a[i].lane == bz::LaneHats ? -3.0 : a[i].lane == bz::LaneKick ? -2.0
                              : a[i].lane == bz::LaneToms && a[i].fill ? -2.0 : 0.0;
                    const double nominal = a[i].q - ms * 0.001 * c.bpm / 60.0;
                    const bool second = std::abs (std::fmod (nominal, 1.5) - 0.5) < 1.0e-6;
                    swingOK &= std::abs (b[i].q - a[i].q - (second ? 1.0 / 24.0 : 0.0)) < 1.0e-9
                            && a[i].vel == b[i].vel && a[i].fill == b[i].fill;
                    if (second) ++moved; else ++untouched;
                }
            }
        }
        check (swingOK && moved > 0 && untouched > 0, "compound swing moves only the second eighth in 6/8, 9/8 and 12/8");

        for (double bpm : { 60.0, 180.0 })
        {
            const double expected = bpm == 60.0 ? 0.120 : 1.0 / 24.0;
            bz::Player player; player.prepare (SR); bz::Params p; p.kickFollow = true;
            bz::HostState host; host.bpm = bpm;
            juce::MidiBuffer midi; midi.ensureSize (16384); int offAt = -1;
            for (int start = 0; start < 12000; start += 256)
            {
                midi.clear();
                if (start == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                player.process (p, false, true, host, midi, 256);
                for (const auto e : midi) if (e.getMessage().isNoteOff()) offAt = start + e.samplePosition;
            }
            const auto mf = bz::toMidiFile ({ { 0.0, bz::nt::Kick, 100, false } }, bpm, 4, 4);
            double offTicks = -1;
            for (auto* e : *mf.getTrack (0)) if (e->message.isNoteOff()) offTicks = e->message.getTimeStamp();
            check (offAt == juce::roundToInt (expected * SR)
                && std::abs (offTicks - std::round (expected * bpm / 60.0 * 960.0)) < 0.1,
                   "live and exported note lengths agree at " + juce::String (bpm) + " BPM");
        }
        check (std::abs (bz::noteLengthSeconds (20) - 0.120) < 1e-9
            && std::abs (bz::noteLengthSeconds (400) - 0.020) < 1e-9, "note length respects 20-120 ms limits");

        BruiserProcessor p; p.applyPreset (2); set (p, pid::loud, 0.123f); p.apvts.copyState();
        const auto before = p.currentParams();
        p.applyPreset (40);
        const bool undone = p.undoManager.undo() && p.currentPreset == 2 && p.currentParams() == before;
        const bool redone = p.undoManager.redo() && p.currentPreset == 40 && ! p.presetModified();
        check (undone && redone, "preset load is one undo step, restoring values and program index; redo works");
        bool different = true;
        for (int i = 0; i < 1000; ++i) { const int old = p.currentPreset; p.surprisePreset(); different &= p.currentPreset != old; }
        check (different, "Surprise me never repeats the current preset (1000 selections)");
        bool metersNamed = true;
        for (int i = 0; i < bz::kNumPresets; ++i)
            if (bz::kPresets[i].meter > 0)
                metersNamed &= p.getProgramName (i).endsWith (BruiserProcessor::meterNames()[bz::kPresets[i].meter]);
        check (metersNamed, "time-signature program names include their meter");

        auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("BRUISER");
        temp.createDirectory();
        auto stale = temp.getChildFile ("BRUISER qa-stale.mid"), recent = temp.getChildFile ("BRUISER qa-recent.mid");
        auto other = temp.getChildFile ("qa-unrelated.mid");
        stale.replaceWithText ("test"); recent.replaceWithText ("test"); other.replaceWithText ("test");
        stale.setLastModificationTime (juce::Time::getCurrentTime() - juce::RelativeTime::days (2));
        other.setLastModificationTime (juce::Time::getCurrentTime() - juce::RelativeTime::days (2));
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* editor = dynamic_cast<BruiserEditor*> (ed.get());
        check (! stale.existsAsFile() && recent.existsAsFile() && other.existsAsFile(), "drag cleanup removes only old BRUISER MIDI files");
        recent.deleteFile(); other.deleteFile();
        p.applyPreset (3);
        editor->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
        const bool forward = p.currentPreset == 4;
        editor->keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
        const bool backward = p.currentPreset == 3;
        editor->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
        const bool keyUndo = p.currentPreset == 4;
        editor->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0));
        check (forward && backward && keyUndo && p.currentPreset == 3, "editor arrows, Cmd-Z and Shift-Cmd-Z work");
        bool tips = true;
        for (const auto& id : editor->getPanel().boundParameterIDs())
        {
            auto* tip = dynamic_cast<juce::TooltipClient*> (editor->getPanel().findControl (id));
            tips &= tip != nullptr && tip->getTooltip().isNotEmpty();
        }
        check (tips, "all parameter controls have producer-facing tooltips");
        const auto beforeStyle = p.currentParams();
        auto* style = editor->getPanel().findControl (pid::style);
        style->mouseDown (mouse (*style, { (float) style->getWidth() - 10.0f, 30.0f }));
        style->mouseUp (mouse (*style, { (float) style->getWidth() - 10.0f, 30.0f }));
        const bool styleChanged = ! (p.currentParams() == beforeStyle);
        editor->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
        check (styleChanged && p.currentParams() == beforeStyle, "parameter attachment gesture undoes independently of preset load");
        const auto beforePad = p.currentParams();
        auto* pad = editor->getPanel().findControl (pid::loud);
        pad->mouseDown (mouse (*pad, { 200.0f, 80.0f }));
        pad->mouseUp (mouse (*pad, { 200.0f, 80.0f }));
        const bool padChanged = ! (p.currentParams() == beforePad);
        editor->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
        check (padChanged && p.currentParams() == beforePad, "pad loudness and complexity undo together in one step");
        set (p, pid::meter, 4);
        auto f = editor->getPanel().renderDragFile (8);
        check (f.getFileName().contains ("8 bars 7-8 take"), "drag MIDI filename includes the meter");
    }

    // ---- 1. engine: determinism, ranges, note map, every meter
    {
        bz::Params p;
        auto a = bz::renderSong (p, song(), 120, 4, 4), b = bz::renderSong (p, song(), 120, 4, 4);
        bool same = a.size() == b.size();
        for (size_t i = 0; same && i < a.size(); ++i) same = a[i].q == b[i].q && a[i].note == b[i].note && a[i].vel == b[i].vel;
        check (same && ! a.empty(), "same settings give the same performance (" + juce::String ((int) a.size()) + " hits, 42 bars)");

        p.take = 2;
        auto c = bz::renderSong (p, song(), 120, 4, 4);
        int diff = (int) std::abs ((long) c.size() - (long) a.size());
        for (size_t i = 0; i < juce::jmin (a.size(), c.size()); ++i) diff += a[i].note != c[i].note || a[i].q != c[i].q;
        check (diff > 20, "TAKE 2 is a different performance (" + juce::String (diff) + " hits differ)");

        const std::set<int> kitNotes { 28, 29, 36, 37, 38, 40, 42, 43, 44, 46, 47, 49, 50, 51, 53, 55, 57 };
        bool inMap = true, velOk = true;
        for (float g : { 0.0f, 0.5f, 1.0f })
            for (float cx : { 0.0f, 0.5f, 1.0f })
                for (float f : { 0.0f, 1.0f })
                {
                    bz::Params q; q.style = g; q.complexity = cx; q.fills = f; q.loud = cx;
                    for (auto& h : bz::renderSong (q, song(), 140, 4, 4))
                    {
                        inMap &= kitNotes.count (h.note) > 0;
                        velOk &= h.vel >= 1 && h.vel <= 127;
                    }
                }
        check (inMap, "every note is a ROB KIT articulation");
        check (velOk, "velocities stay in 1..127");

        bool meters = true;
        juce::String bad;
        for (auto sig : std::vector<std::pair<int, int>> { { 3, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 12, 8 }, { 2, 2 } })
            for (int t = 0; t < 3; ++t)
            {
                bz::Params q; q.time = (bz::TimeMode) t; q.style = 0.5f; q.complexity = 0.8f;
                const double barQ = bz::barLengthQ (sig.first, sig.second);
                auto hits = bz::renderSong (q, song(), 120, sig.first, sig.second);
                std::set<int> barsWithKick;
                for (auto& h : hits)
                {
                    const int bar = (int) std::floor ((h.q + 0.05) / barQ);
                    if (h.note == bz::nt::Kick) barsWithKick.insert (bar);
                    if (h.q > 42 * barQ + 0.3) { meters = false; bad = juce::String (sig.first) + "/" + juce::String (sig.second); }
                }
                if ((int) barsWithKick.size() < 38) { meters = false; bad = juce::String (sig.first) + "/" + juce::String (sig.second) + " kicks"; }
            }
        check (meters, "3/4 5/4 6/8 7/8 12/8 2/2 in half, normal and double time " + bad);

        int fills = 0, crashes = 0;
        bz::Params q;
        auto hits = bz::renderSong (q, song(), 120, 4, 4);
        for (auto& h : hits) { fills += h.fill; crashes += h.note == bz::nt::CrashL || h.note == bz::nt::CrashR; }
        check (fills > 20 && crashes > 8, "fills and crashes land (" + juce::String (fills) + " fill hits, " + juce::String (crashes) + " crashes)");

        for (auto [name, g] : std::vector<std::pair<const char*, float>> { { "slam", 0.1f }, { "blend", 0.5f }, { "gallop", 0.92f } })
        {
            bz::Params s; s.style = g; s.complexity = 0.55f; s.loud = 0.7f; s.fills = 0.6f;
            const double bpm = g > 0.8f ? 160.0 : 128.0;
            bz::writeMidiFile (out.getChildFile (juce::String ("song_") + name + ".mid"), bz::toMidiFile (bz::renderSong (s, song(), bpm, 4, 4), bpm, 4, 4));
        }
    }

    // ---- 2. live player matches the export
    {
        BruiserProcessor p;
        Host h (p);
        h.play (0.0, 32 * 4.0);
        h.stop();
        juce::String why;
        const bool same = sameAs (h.ons(), bz::renderSong (p.currentParams(), { { bz::Verse, 32 } }, 120, 4, 4), 120, 0.0, 31 * 4.0, why);
        check (same, "live playback = dragged MIDI, 32 bars, random block sizes " + why);
        check (balanced (h.events), "every note-on gets a note-off");
        check (duplicates (h.ons(), 120) == 0, "no double hits");
    }

    // ---- 3a. how odd meters are phrased
    {
        auto bar = std::make_unique<bz::Bar>();
        auto positions = [&] (int num, int den, int note, int take = 1)
        {
            bz::Params q; q.complexity = 0.1f; q.style = 0.0f; q.fills = 0.0f; q.human = 0.0f; q.take = take;
            bz::BarContext c; c.bar = 1; c.barInSection = 1; c.num = num; c.den = den;
            bz::generate (q, c, *bar);
            juce::String r;
            for (int i = 0; i < bar->count; ++i)
                if (bar->hits[i].note == note || (note == 38 && bar->hits[i].note == 40))
                    r << juce::String (std::round (bar->hits[i].q * 2.0) / 2.0, 1) << " ";
            return r.trim();
        };
        const auto k78 = positions (7, 8, 36), s78 = positions (7, 8, 38);
        check (k78 == "0.0 2.0" && s78 == "1.0 3.0", "7/8 plays 2+2+3: kick " + k78 + " / snare " + s78);
        const auto k54 = positions (5, 4, 36), s54 = positions (5, 4, 38);
        check (k54 == "0.0 2.0" && s54 == "1.0 4.0", "5/4 plays 2+3: kick " + k54 + " / snare " + s54);
        const auto s68 = positions (6, 8, 38), s98 = positions (9, 8, 38), s128 = positions (12, 8, 38);
        check (s68 == "1.5" && s98 == "3.0" && s128 == "1.5 4.5", "6/8, 9/8, 12/8 backbeats on the dotted beats: " + s68 + " | " + s98 + " | " + s128);
        const auto s118 = positions (11, 8, 38);
        check (s118 == "1.0 3.0 5.0", "11/8 plays 2+2+2+2+3: snare " + s118);
        juce::String waltz, heavy;
        for (int t = 1; t < 30 && (waltz.isEmpty() || heavy.isEmpty()); ++t)
        {
            const auto s34 = positions (3, 4, 38, t);
            if (s34 == "1.0 2.0") waltz = s34;
            if (s34 == "2.0") heavy = s34;
        }
        check (waltz.isNotEmpty() && heavy.isNotEmpty(), "3/4 has waltz and heavy takes");
    }

    // ---- 3b. METER override: BRUISER plays 7/8 while Logic sits in 4/4
    {
        BruiserProcessor p;
        set (p, pid::meter, 4.0f);
        Host h (p);
        h.play (0.0, 12 * 3.5);
        juce::String why;
        check (sameAs (h.ons(), bz::renderSong (p.currentParams(), { { bz::Verse, 12 } }, 120, 7, 8), 120, 0.0, 11 * 3.5, why),
               "METER 7/8 over Logic 4/4 plays 7/8 bars from the song start " + why);
        check (p.playNum.load() == 7 && p.playDen.load() == 8 && p.hostNum.load() == 4, "it knows Logic is still in 4/4 (for the warning)");
    }

    // ---- 3. tempo, meter, loop
    {
        BruiserProcessor p;
        Host h (p);
        h.bpm = 97.0; h.num = 7; h.den = 8;
        h.play (0.0, 16 * 3.5);
        juce::String why;
        check (sameAs (h.ons(), bz::renderSong (p.currentParams(), { { bz::Verse, 16 } }, 97, 7, 8), 97, 0.0, 15 * 3.5, why), "7/8 at 97 BPM matches export " + why);

        BruiserProcessor p2;
        Host l (p2);
        std::vector<std::vector<Ev>> passes;
        for (int pass = 0; pass < 3; ++pass)
        {
            const size_t first = l.events.size();
            l.play (16.0, 32.0);                          // cycle bars 5-8
            std::vector<Ev> o;
            for (size_t k = first; k < l.events.size(); ++k) if (l.events[k].on) o.push_back (l.events[k]);
            for (auto& e : o) e.ppq -= 16.0;
            passes.push_back (o);
        }
        bool same = passes[0].size() == passes[1].size() && passes[1].size() == passes[2].size() && ! passes[0].empty();
        for (size_t i = 0; same && i < passes[0].size(); ++i)
            same = passes[0][i].note == passes[2][i].note && std::abs (passes[0][i].ppq - passes[2][i].ppq) < 1.0e-3;
        bool inside = true;
        for (auto& ps : passes) for (auto& e : ps) inside &= e.ppq >= -1.0e-6 && e.ppq < 16.0;
        check (same && inside, "cycle in Logic plays the same bars each pass (" + juce::String ((int) passes[0].size()) + " hits per pass)");
        l.stop();
        check (balanced (l.events), "loop jumps leave no hanging notes");
    }

    // ---- 4. fill button mid-bar, section change, kick follow
    {
        BruiserProcessor a, b;
        set (a, pid::fills, 0.0f); set (b, pid::fills, 0.0f);
        Host ha (a), hb (b);
        ha.play (0.0, 28.0);
        bool requested = false;
        hb.before = [&] (double ppq, juce::MidiBuffer&, int) { if (! requested && ppq >= 21.2) { b.player.requestFill(); requested = true; } };
        hb.play (0.0, 28.0);
        auto inRange = [] (const std::vector<Ev>& v, double f, double t, bool tomsOnly)
        {
            std::vector<Ev> r;
            for (auto& e : v) if (e.ppq >= f && e.ppq < t && (! tomsOnly || e.note == 50 || e.note == 47 || e.note == 43 || e.note == 38)) r.push_back (e);
            return r;
        };
        const auto preA = inRange (ha.ons(), 20.0, 21.2, false), preB = inRange (hb.ons(), 20.0, 21.2, false);
        const auto fillB = inRange (hb.ons(), 22.0, 24.0, true), fillA = inRange (ha.ons(), 22.0, 24.0, true);
        bool samePrefix = preA.size() == preB.size();
        for (size_t i = 0; samePrefix && i < preA.size(); ++i) samePrefix = preA[i].note == preB[i].note && std::abs (preA[i].ppq - preB[i].ppq) < 1e-6;
        check (samePrefix && fillB.size() > fillA.size() + 3, "FILL pressed mid-bar: bar plays on, fill in the last two beats ("
                                                                   + juce::String ((int) fillB.size()) + " drum hits vs " + juce::String ((int) fillA.size()) + ")");
        bool landed = false;
        for (auto& e : hb.ons()) landed |= (e.note == 49 || e.note == 57) && std::abs (e.ppq - 24.0) < 0.05;
        check (landed, "the fill lands on a crash");
        check (duplicates (hb.ons(), 120) == 0, "no double hits around the fill");

        BruiserProcessor c;
        Host hc (c);
        bool switched = false;
        hc.before = [&] (double ppq, juce::MidiBuffer&, int) { if (! switched && ppq >= 31.9) { set (c, pid::section, (float) bz::Chorus); switched = true; } };
        hc.play (0.0, 40.0);
        bool crash = false;
        for (auto& e : hc.ons()) crash |= (e.note == 49 || e.note == 57) && std::abs (e.ppq - 32.0) < 0.05;
        int crashHand = 0;
        for (auto& e : hc.ons()) crashHand += e.ppq > 32.2 && e.ppq < 36.0 && (e.note == 49 || e.note == 57 || e.note == 46 || e.note == 51 || e.note == 53);
        check (crash && crashHand > 3, "section change at bar 9 crashes in and changes the hand");

        BruiserProcessor d;
        set (d, pid::kickFollow, 1.0f);
        set (d, pid::fills, 0.0f);
        Host hd (d);
        std::vector<double> cues { 1.0, 2.75, 5.5, 9.25 };
        hd.before = [&] (double ppq, juce::MidiBuffer& m, int n)
        {
            const double qps = 120.0 / 60.0 / SR;
            for (double q : cues)
                if (q >= ppq && q < ppq + n * qps) m.addEvent (juce::MidiMessage::noteOn (1, 33, (juce::uint8) 100), (int) ((q - ppq) / qps));
        };
        hd.play (0.0, 12.0);
        std::vector<double> kicks;
        for (auto& e : hd.ons()) if (e.note == 36) kicks.push_back (e.ppq);
        bool follows = kicks.size() == cues.size();
        for (size_t i = 0; follows && i < cues.size(); ++i) follows = std::abs (kicks[i] - cues[i]) < 0.001;
        bool passed = false;
        for (auto& e : hd.ons()) passed |= e.note == 33;
        check (follows && ! passed, "KICK FOLLOW: incoming notes become the kick (" + juce::String ((int) kicks.size()) + " kicks)");

        BruiserProcessor e;
        set (e, pid::active, 0.0f);
        Host he (e);
        he.before = [&] (double ppq, juce::MidiBuffer& m, int) { if (ppq == 0.0) m.addEvent (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 90), 10); };
        he.play (0.0, 8.0);
        check (he.ons().size() == 1 && he.ons()[0].note == 38, "PLAY off: BRUISER is silent and your own notes pass through");
    }

    // ---- 5. real-time safety
    {
        BruiserProcessor p;
        set (p, pid::complexity, 1.0f); set (p, pid::fills, 1.0f); set (p, pid::style, 0.7f);
        Host h (p);
        h.play (0.0, 8.0);
        h.probeAllocs = true;
        h.allocCount = 0;
        h.before = [&] (double ppq, juce::MidiBuffer&, int) { if (std::fmod (ppq, 6.0) < 0.05) p.player.requestFill(); };
        h.play (8.0, 200.0);
        h.play (40.0, 80.0);                               // a jump
        check (h.allocCount == 0, "no memory allocation on the audio thread (" + juce::String ((int) h.allocCount) + ")");
        h.probeAllocs = false;
    }

    // ---- 6. saved state
    {
        BruiserProcessor a;
        set (a, pid::section, 3); set (a, pid::loud, 0.83f); set (a, pid::style, 0.21f); set (a, pid::take, 42);
        set (a, pid::pocket, -0.4f); set (a, pid::hand, 4); set (a, pid::kickFollow, 1); set (a, pid::time, 0);
        a.uiScale = 1.5f;
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        BruiserProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        int differ = 0;
        for (auto* prm : a.getParameters())
        {
            auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm);
            differ += std::abs (r->getValue() - b.apvts.getParameter (r->getParameterID())->getValue()) > 1.0e-6f;
        }
        check (differ == 0 && std::abs (b.uiScale - 1.5f) < 1e-6f, "saved state restores all " + juce::String (a.getParameters().size()) + " parameters and window size");
    }

    // ---- 7. interface
    {
        BruiserProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* be = dynamic_cast<BruiserEditor*> (ed.get());
        auto& panel = be->getPanel();
        auto ids = panel.boundParameterIDs();
        int missing = 0;
        for (auto* prm : p.getParameters())
            missing += ! ids.contains (dynamic_cast<juce::RangedAudioParameter*> (prm)->getParameterID());
        check (missing == 0 && ids.size() == p.getParameters().size(), "all " + juce::String (ids.size()) + " parameters have a control");

        auto* pad = panel.findControl (pid::loud);
        const auto pb = pad->getLocalBounds().toFloat().reduced (10.0f);
        auto at = juce::Point<float> (pb.getX() + pb.getWidth() * 0.8f, pb.getY() + pb.getHeight() * 0.25f);
        pad->mouseDown (mouse (*pad, at)); pad->mouseUp (mouse (*pad, at));
        const auto pr = p.currentParams();
        check (std::abs (pr.complexity - 0.8f) < 0.01f && std::abs (pr.loud - 0.75f) < 0.01f, "pad sets complexity across and loudness up");

        auto* sec = panel.findControl (pid::section);
        sec->mouseDown (mouse (*sec, { sec->getWidth() * (3.5f / 7.0f), 10.0f }));
        check (p.currentParams().section == bz::Chorus, "section buttons select CHORUS");

        auto* hand = panel.findControl (pid::hand);
        hand->mouseDown (mouse (*hand, { hand->getWidth() * (3.5f / 6.0f), 10.0f }));
        check (p.currentParams().hand == bz::HandRide, "hand buttons select RIDE");

        set (p, pid::style, 0.9f);
        be->tickForTest();
        auto* st = dynamic_cast<juce::Component*> (panel.findControl (pid::style));
        check (st != nullptr, "host automation reaches the controls");

        auto f = panel.renderDragFile (8);
        juce::MidiFile mf;
        juce::FileInputStream in (f);
        const bool read = in.openedOk() && mf.readFrom (in);
        int notes = 0;
        if (read) for (int t = 0; t < mf.getNumTracks(); ++t) for (auto* e : *mf.getTrack (t)) notes += e->message.isNoteOn();
        check (read && notes > 60, "DRAG MIDI writes a readable 8-bar file (" + juce::String (notes) + " notes): " + f.getFileName());

        set (p, pid::section, 3); set (p, pid::style, 0.35f); set (p, pid::complexity, 0.62f); set (p, pid::hand, 0);
        be->tickForTest();
        for (float s : { 1.0f, 2.0f })
        {
            auto img = panel.createComponentSnapshot (panel.getLocalBounds(), true, s);
            juce::PNGImageFormat png;
            auto file = out.getChildFile (s > 1.5f ? "bruiser@2x.png" : "bruiser.png");
            file.deleteFile();
            juce::FileOutputStream os (file);
            png.writeImageToStream (img, os);
        }
    }

    // ---- 8. presets
    {
        std::set<juce::String> names;
        for (int i = 0; i < bz::kNumPresets; ++i) names.insert (juce::String (bz::kPresets[i].category) + bz::kPresets[i].name);
        std::set<juce::String> cats;
        for (int i = 0; i < bz::kNumPresets; ++i) cats.insert (bz::kPresets[i].category);
        int meterPresets = 0;
        std::set<int> meters;
        for (int i = 0; i < bz::kNumPresets; ++i) if (bz::kPresets[i].meter > 0) { ++meterPresets; meters.insert (bz::kPresets[i].meter); }
        check (meterPresets >= 16 && (int) meters.size() >= 10, juce::String (meterPresets) + " time-signature presets across " + juce::String ((int) meters.size()) + " meters");
        check (bz::kNumPresets >= 85 && (int) names.size() == bz::kNumPresets,
               juce::String (bz::kNumPresets) + " factory presets in " + juce::String ((int) cats.size()) + " categories, names unique");

        BruiserProcessor p;
        set (p, pid::section, (float) bz::Bridge); set (p, pid::kickFollow, 1.0f);
        bool exact = true, kept = true, clean = true, named = true;
        for (int i = 0; i < p.getNumPrograms(); ++i)
        {
            p.setCurrentProgram (i);
            const auto c = p.currentParams();
            const auto& pr = bz::kPresets[i];
            exact &= std::abs (c.loud - pr.loud) < 0.005f && std::abs (c.complexity - pr.complexity) < 0.005f
                  && std::abs (c.style - pr.style) < 0.005f && std::abs (c.fills - pr.fills) < 0.005f
                  && std::abs (c.pocket - pr.pocket) < 0.005f && std::abs (c.swing - pr.swing) < 0.005f
                  && std::abs (c.human - pr.human) < 0.005f && (int) c.fillLen == pr.fillLen && (int) c.time == pr.time
                  && (int) c.hand == pr.hand && c.take == pr.take
                  && juce::roundToInt (p.apvts.getRawParameterValue (pid::meter)->load()) == pr.meter;
            kept &= c.section == bz::Bridge && c.kickFollow;
            clean &= ! p.presetModified() && p.getCurrentProgram() == i;
            named &= p.getProgramName (i).isNotEmpty();
        }
        check (exact, "every preset loads its exact settings");
        check (kept, "presets leave SECTION and KICK FOLLOW alone");
        check (clean && named, "Logic sees them as named factory presets");
        int odd = 0;
        while (bz::kPresets[odd].meter == 0) ++odd;
        p.setCurrentProgram (odd);
        const bool setMeter = juce::roundToInt (p.apvts.getRawParameterValue (pid::meter)->load()) == bz::kPresets[odd].meter;
        p.setCurrentProgram (1);
        check (setMeter && juce::roundToInt (p.apvts.getRawParameterValue (pid::meter)->load()) == 0,
               "a time-signature preset sets METER; a 4/4 preset puts it back to Follow Logic");
        set (p, pid::loud, 0.11f);
        check (p.presetModified(), "moving a control marks the preset EDITED");

        BruiserProcessor a; a.setCurrentProgram (20);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        BruiserProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (b.getCurrentProgram() == 20 && ! b.presetModified(), "the chosen preset is saved with the song");

        // Each preset plays something different, all of it on the kit.
        const std::set<int> kitNotes { 28, 29, 36, 37, 38, 40, 42, 43, 44, 46, 47, 49, 50, 51, 53, 55, 57 };
        std::vector<std::vector<bz::TimedHit>> perf;
        bool onKit = true;
        for (int i = 0; i < bz::kNumPresets; ++i)
        {
            int mn, md; meterOf (bz::kPresets[i], mn, md);
            perf.push_back (bz::renderSong (paramsFor (bz::kPresets[i]), { { bz::Verse, 4 }, { bz::Chorus, 4 } }, 120, mn, md));
            for (auto& h : perf.back()) onKit &= kitNotes.count (h.note) > 0;
        }
        int clones = 0;
        for (size_t i = 0; i < perf.size(); ++i)
            for (size_t j = i + 1; j < perf.size(); ++j)
            {
                if (perf[i].size() != perf[j].size()) continue;
                bool same = true;
                for (size_t k = 0; same && k < perf[i].size(); ++k)
                    same = perf[i][k].note == perf[j][k].note && std::abs (perf[i][k].q - perf[j][k].q) < 0.01 && std::abs (perf[i][k].vel - perf[j][k].vel) < 4;
                clones += same;
            }
        check (onKit && clones == 0, "no two presets play the same 8 bars (" + juce::String (clones) + " clones)");

        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto& panel = dynamic_cast<BruiserEditor*> (ed.get())->getPanel();
        auto* bar = panel.findControl ("preset");
        p.setCurrentProgram (5);
        bar->mouseDown (mouse (*bar, { (float) bar->getWidth() - 5.0f, 10.0f }));
        const bool fwd = p.getCurrentProgram() == 6;
        bar->mouseDown (mouse (*bar, { 5.0f, 10.0f }));
        bar->mouseDown (mouse (*bar, { 5.0f, 10.0f }));
        check (fwd && p.getCurrentProgram() == 4, "preset arrows step forward and back");
        p.setCurrentProgram (9);
        dynamic_cast<BruiserEditor*> (ed.get())->tickForTest();
        auto img = panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto file = out.getChildFile ("bruiser_presets.png");
        file.deleteFile();
        juce::FileOutputStream os (file);
        png.writeImageToStream (img, os);
    }

    // ---- 9. audio through ROB KIT
    if (argc > 2)
    {
        rk::Kit kit;
        juce::String err;
        if (kit.load (juce::File (juce::String (argv[2])), err))
        {
            for (auto [name, g, bpm] : std::vector<std::tuple<const char*, float, double>> { { "slam", 0.1f, 128.0 }, { "blend", 0.5f, 136.0 }, { "gallop", 0.92f, 160.0 } })
            {
                bz::Params s; s.style = g; s.complexity = 0.55f; s.loud = 0.7f; s.fills = 0.6f;
                renderAudio (toSeconds (bz::renderSong (s, song(), bpm, 4, 4), bpm), out.getChildFile (juce::String ("bruiser_") + name + ".wav"), kit);
            }
            check (true, "rendered three songs through ROB KIT");

            // Preset tour: the first two presets of each category, 2 bars verse + 2 bars chorus ending in a fill.
            std::vector<SecHit> tour;
            juce::String listing;
            double t = 0.5;
            juce::String lastCat;
            int perCat = 0;
            for (int i = 0; i < bz::kNumPresets; ++i)
            {
                const auto& pr = bz::kPresets[i];
                if (lastCat != pr.category) { lastCat = pr.category; perCat = 0; }
                if (perCat++ >= 2) continue;
                const double bpm = tourBpm (pr.category);
                int mn, md; meterOf (pr, mn, md);
                auto hits = bz::renderSong (paramsFor (pr), { { bz::Verse, 2 }, { bz::Chorus, 2, true } }, bpm, mn, md);
                for (auto& h : toSeconds (hits, bpm, t)) tour.push_back (h);
                listing << juce::String (t, 1) << "s  " << pr.category << " / " << pr.name << "  (" << juce::String ((int) bpm) << " bpm, "
                        << mn << "/" << md << ")\n";
                t += 4.0 * bz::barLengthQ (mn, md) * 60.0 / bpm + 1.2;
            }
            renderAudio (tour, out.getChildFile ("bruiser_preset_tour.wav"), kit);
            out.getChildFile ("bruiser_preset_tour.txt").replaceWithText (listing);

            // Every time-signature preset, 4 bars each (2 verse, 2 chorus with a fill).
            std::vector<SecHit> odd;
            juce::String oddList;
            t = 0.5;
            for (int i = 0; i < bz::kNumPresets; ++i)
            {
                const auto& pr = bz::kPresets[i];
                if (pr.meter == 0) continue;
                int mn, md; meterOf (pr, mn, md);
                const double bpm = juce::String (pr.category) == "THREES" ? 108.0 : 124.0;
                auto hits = bz::renderSong (paramsFor (pr), { { bz::Verse, 2 }, { bz::Chorus, 2, true } }, bpm, mn, md);
                for (auto& h : toSeconds (hits, bpm, t)) odd.push_back (h);
                oddList << juce::String (t, 1) << "s  " << pr.name << "  " << mn << "/" << md << "  (" << juce::String ((int) bpm) << " bpm)\n";
                t += 4.0 * bz::barLengthQ (mn, md) * 60.0 / bpm + 1.2;
            }
            renderAudio (odd, out.getChildFile ("bruiser_time_signatures.wav"), kit);
            out.getChildFile ("bruiser_time_signatures.txt").replaceWithText (oddList);
            check (true, "rendered the preset tour");
        }
        else check (false, "ROB KIT samples: " + err);
    }

    std::printf ("\n%s (%d failed)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
