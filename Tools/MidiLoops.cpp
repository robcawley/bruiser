// RobKitMidiLoops: renders arbitrary MIDI drum loops through the ROB KIT engine (local tool).
// Usage: RobKitMidiLoops <jobs.json>
// jobs.json: { "samples": "<ROB KIT Samples>", "jobs": [ { "midi": "a.mid", "out": "a.wav", "bpm": 90, "beats": 16,
//   "tune": [kick, snare, hats, toms, cymbals, perc], "decay": [...], "sub": 0.5, "room": 0.2, "punch": 0.45, "smash": 0.3 } ] }
// Each loop is the middle of three passes (tails wrap); written as 32-bit float, not normalised.
#include <juce_audio_formats/juce_audio_formats.h>
#include "DSP/Kit.h"

namespace
{
constexpr double SR = 48000.0;

juce::AudioBuffer<float> render (const juce::var& job, rk::Kit& kit)
{
    const double bpm = job["bpm"];
    const int beats = (int) job["beats"];
    const double spq = SR * 60.0 / bpm;
    const int loopSamples = (int) std::llround (spq * beats);

    juce::MidiFile mf;
    {
        juce::FileInputStream in (juce::File (job["midi"].toString()));
        if (! in.openedOk() || ! mf.readFrom (in)) { std::printf ("  cannot read %s\n", job["midi"].toString().toRawUTF8()); return {}; }
    }
    const double tpq = mf.getTimeFormat() > 0 ? mf.getTimeFormat() : 480;
    struct H { int at; int note; float vel; };
    std::vector<H> ev;
    for (int t = 0; t < mf.getNumTracks(); ++t)
        for (auto* e : *mf.getTrack (t))
            if (e->message.isNoteOn())
                for (int pass = 0; pass < 3; ++pass)
                    ev.push_back ({ (int) std::llround ((e->message.getTimeStamp() / tpq + (double) beats * pass) * spq) + 4800,
                                    e->message.getNoteNumber(), e->message.getFloatVelocity() });
    std::sort (ev.begin(), ev.end(), [] (auto& a, auto& b) { return a.at < b.at; });

    rk::Settings s;
    for (int g = 0; g < NumGroups; ++g) { s.levelDb[(size_t) g] = 0.0f; s.decay[(size_t) g] = 1.0f; }
    if (auto* a = job["tune"].getArray())  for (int g = 0; g < juce::jmin (6, a->size()); ++g) s.tune[(size_t) g]  = (float) (double) (*a)[g];
    if (auto* a = job["decay"].getArray()) for (int g = 0; g < juce::jmin (6, a->size()); ++g) s.decay[(size_t) g] = (float) (double) (*a)[g];
    if (auto* a = job["level"].getArray()) for (int g = 0; g < juce::jmin (6, a->size()); ++g) s.levelDb[(size_t) g] = (float) (double) (*a)[g];
    auto num = [&] (const char* k, float& dst) { if (job.hasProperty (k)) dst = (float) (double) job[k]; };
    num ("sub", s.sub); num ("room", s.room); num ("roomSize", s.roomSize); num ("punch", s.punch); num ("smash", s.smash);
    num ("glue", s.glue); num ("drive", s.drive); num ("human", s.human); num ("hatCloseMs", s.hatCloseMs); num ("wires", s.wires);

    kit.prepare (SR, 256);
    kit.setSettings (s);
    kit.allSoundOff();
    const int total = 4800 + loopSamples * 3 + (int) SR * 2;
    juce::AudioBuffer<float> all (2, total), blk (2, 256);
    size_t next = 0;
    for (int pos = 0; pos < total; pos += 256)
    {
        const int n = juce::jmin (256, total - pos);
        blk.clear();
        int cursor = 0;
        while (next < ev.size() && ev[next].at < pos + n)
        {
            const int at = juce::jlimit (0, n, ev[next].at - pos);
            if (at > cursor) { kit.render (blk, cursor, at - cursor); cursor = at; }
            kit.noteOn (ev[next].note, ev[next].vel, 0);
            ++next;
        }
        if (cursor < n) kit.render (blk, cursor, n - cursor);
        kit.finish (blk, n);
        for (int c = 0; c < 2; ++c) all.copyFrom (c, pos, blk, c, 0, n);
    }
    const int start = 4800 + loopSamples + kit.latencySamples();
    juce::AudioBuffer<float> loop (2, loopSamples);
    for (int c = 0; c < 2; ++c) loop.copyFrom (c, 0, all, c, start, loopSamples);
    return loop;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::printf ("usage: RobKitMidiLoops <jobs.json>\n"); return 1; }
    const auto root = juce::JSON::parse (juce::File (juce::String (argv[1])));
    rk::Kit kit;
    juce::String err;
    if (! kit.load (juce::File (root["samples"].toString()), err)) { std::printf ("kit: %s\n", err.toRawUTF8()); return 1; }
    int fails = 0;
    for (auto& job : *root["jobs"].getArray())
    {
        auto b = render (job, kit);
        if (b.getNumSamples() == 0) { ++fails; continue; }
        juce::File f (job["out"].toString());
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), SR, 2, 32, {}, 0));
        if (w == nullptr || ! w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples())) { ++fails; continue; }
        std::printf ("%-60s peak %6.2f dB\n", f.getFileName().toRawUTF8(), juce::Decibels::gainToDecibels (b.getMagnitude (0, b.getNumSamples())));
    }
    std::printf ("%s\n", fails == 0 ? "DONE" : "FAILURES");
    return fails == 0 ? 0 : 1;
}
