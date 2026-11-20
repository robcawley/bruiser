#pragma once
// Real-time side of BRUISER: follows the host transport, builds each bar as the playhead
// reaches it, and turns the hits into MIDI with sample-accurate timing.
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include "Groove.h"

namespace bz
{
struct HostState
{
    bool playing = false;
    double ppq = 0.0;            // at the first sample of the block
    double bpm = 120.0;
    int num = 4, den = 4;
    bool hasBar = false;         // host told us where the bar starts
    double barStartPpq = 0.0;
    int64_t barCount = -1;       // host bar index if known
};

// What the editor shows. Written by the audio thread, read with a sequence lock.
struct Snapshot
{
    Bar bar;
    int64_t barIndex = -1;
    int section = Verse, barInSection = 0;
    bool fill = false;
};

class Player
{
public:
    static constexpr int kOutChannel = 10;

    void prepare (double sampleRate);
    void process (const Params& p, bool fillSwitch, bool active, const HostState& host,
                  juce::MidiBuffer& midi, int numSamples);

    // Editor: request a fill at the next opportunity (lock-free).
    void requestFill() { fillRequest.store (true); }

    // Editor-side reads
    bool readSnapshot (Snapshot& out) const;
    std::atomic<double> posInBarQ { -1.0 };     // -1 when stopped
    std::atomic<uint32_t> laneHits[NumLanes] {};
    std::atomic<bool> fillPending { false };

    // Test hooks
    int sectionStartBarForTest() const { return (int) sectionStartBar; }

private:
    struct Slot
    {
        bool used = false;
        int64_t index = 0;
        double startPpq = 0.0;
        Params params;
        BarContext ctx;
        Bar bar;
        int next = 0;
    };

    void flush (juce::MidiBuffer& out, int sample);
    void noteOn (juce::MidiBuffer& out, int note, int vel, int sample);
    void tickOffs (juce::MidiBuffer& out, int numSamples);
    Slot* findSlot (int64_t index);
    Slot& freeSlot (double beforePpq);
    void regen (Slot& s, double fromPpq);
    void build (Slot& s, const Params& p, bool force, int64_t index, double startPpq, const HostState& h, double fromPpq);
    void publish (const Slot& s);

    double sr = 48000.0;
    int noteLengthSamples = 2880;
    bool wasPlaying = false;
    double expectedPpq = 0.0;
    Slot slots[3];

    int64_t sectionStartBar = 0, lastFilledBar = INT64_MIN / 2;
    int trackedSection = -1;
    bool reanchor = true;
    int64_t publishedIndex = -1;
    juce::MidiBuffer scratch;
    std::atomic<bool> fillRequest { false };

    struct Off { int note = 0; int samplesLeft = 0; };
    Off offs[128];
    int numOffs = 0;

    mutable std::atomic<uint32_t> seq { 0 };
    Snapshot snap;
};
} // namespace bz
