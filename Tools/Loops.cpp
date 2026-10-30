// BruiserLoops: renders BRUISER drum loops through the ROB KIT engine for Logic Live Loops.
// Usage: BruiserLoops <outdir> <robkit samples dir> [bpm]
// Each loop is 4 bars, rendered as the middle of three passes so tails wrap and pushed hits
// from the next pass land at the end: drop-in seamless. Also writes the MIDI (pushed hits wrapped).
#include <juce_audio_formats/juce_audio_formats.h>
#include "Engine/Export.h"
#include "Presets.h"
#include "DSP/Kit.h"

namespace
{
constexpr double SR = 48000.0;

struct Job { const char* scene; const char* preset; bz::Section section; int startBis; };

const Job kJobs[] = {
    { "01 Intro",          "Floor Tom Stomp",   bz::Intro,  4 },
    { "02 Verse Slam",     "Stadium Slam",      bz::Verse,  4 },
    { "03 Verse Gallop",   "Full Gallop",       bz::Verse,  4 },
    { "04 Build",          "Slam Leaning",      bz::Pre,    0 },
    { "05 Chorus Slam",    "Wall of Crash",     bz::Chorus, 0 },
    { "06 Chorus Gallop",  "Cavalry Charge",    bz::Chorus, 0 },
    { "07 Bridge",         "Heavy Engine",      bz::Bridge, 4 },
    { "08 Half-Time",      "Half-Time Crush",   bz::Chorus, 0 },
    { "09 Breakdown",      "Breakdown",         bz::Break,  0 },
    { "10 Double Time",    "Double Time Riot",  bz::Verse,  4 },
    { "11 Outro",          "Epic Outro",        bz::Outro,  0 },
};

const bz::Preset* findPreset (const char* name)
{
    for (int i = 0; i < bz::kNumPresets; ++i)
        if (juce::String (bz::kPresets[i].name) == name) return &bz::kPresets[i];
    return nullptr;
}

bz::Params paramsFor (const bz::Preset& pr)
{
    bz::Params p;
    p.loud = pr.loud; p.complexity = pr.complexity; p.style = pr.style; p.fills = pr.fills;
    p.fillLen = (bz::FillLen) pr.fillLen; p.pocket = pr.pocket; p.swing = pr.swing; p.human = pr.human;
    p.time = (bz::TimeMode) pr.time; p.hand = (bz::Hand) pr.hand; p.take = pr.take;
    return p;
}

std::vector<bz::TimedHit> loopHits (const Job& j, bool fill, double bpm)
{
    auto p = paramsFor (*findPreset (j.preset));
    p.section = j.section;
    p.fills = 0.0f;                                    // no auto fills: the B loop carries the fill
    std::vector<bz::TimedHit> out;
    auto bar = std::make_unique<bz::Bar>();
    for (int i = 0; i < 4; ++i)
    {
        bz::BarContext c;
        c.bar = 200 + i; c.barInSection = j.startBis + i; c.num = 4; c.den = 4; c.bpm = bpm;
        c.forceFill = fill && i == 3;
        bz::generate (p, c, *bar);
        for (int k = 0; k < bar->count; ++k)
            out.push_back ({ i * 4.0 + bar->hits[k].q, bar->hits[k].note, bar->hits[k].vel, bar->hits[k].fill != 0 });
    }
    return out;
}

juce::AudioBuffer<float> renderSeamless (const std::vector<bz::TimedHit>& hits, double bpm, rk::Kit& kit, int loopSamples)
{
    struct H { int at; int note; int vel; };
    std::vector<H> ev;
    const double spq = SR * 60.0 / bpm;
    for (int pass = 0; pass < 3; ++pass)
        for (auto& h : hits)
            ev.push_back ({ (int) std::llround ((h.q + 16.0 * pass) * spq) + 4800, h.note, h.vel });   // 100 ms pre-roll
    std::sort (ev.begin(), ev.end(), [] (auto& a, auto& b) { return a.at < b.at; });

    const int total = 4800 + loopSamples * 3 + (int) SR * 2;
    juce::AudioBuffer<float> all (2, total), blk (2, 256);
    kit.prepare (SR, 256);
    rk::Settings s;
    for (int g = 0; g < NumGroups; ++g) { s.levelDb[(size_t) g] = g == Perc ? -2.0f : 0.0f; s.decay[(size_t) g] = 1.0f; }
    kit.setSettings (s);
    kit.allSoundOff();
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
            kit.noteOn (ev[next].note, (float) ev[next].vel / 127.0f, 0);
            ++next;
        }
        if (cursor < n) kit.render (blk, cursor, n - cursor);
        kit.finish (blk, n);
        for (int c = 0; c < 2; ++c) all.copyFrom (c, pos, blk, c, 0, n);
    }
    // the kit has a fixed latency: compensate so beat 1 sits on sample 0
    const int start = 4800 + loopSamples + kit.latencySamples();
    juce::AudioBuffer<float> loop (2, loopSamples);
    for (int c = 0; c < 2; ++c) loop.copyFrom (c, 0, all, c, start, loopSamples);
    return loop;
}

bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double bpm, int beats)
{
    f.deleteFile();
    juce::StringPairArray meta;
    meta.set (juce::WavAudioFormat::acidOneShot, "0");
    meta.set (juce::WavAudioFormat::acidRootSet, "0");
    meta.set (juce::WavAudioFormat::acidStretch, "1");
    meta.set (juce::WavAudioFormat::acidDiskBased, "0");
    meta.set (juce::WavAudioFormat::acidizerFlag, "1");
    meta.set (juce::WavAudioFormat::acidRootNote, "62");
    meta.set (juce::WavAudioFormat::acidBeats, juce::String (beats));
    meta.set (juce::WavAudioFormat::acidDenominator, "4");
    meta.set (juce::WavAudioFormat::acidNumerator, "4");
    meta.set (juce::WavAudioFormat::acidTempo, juce::String (bpm));
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), SR, 2, 24, meta, 0));
    return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3) { std::printf ("usage: BruiserLoops <outdir> <robkit samples> [bpm]\n"); return 1; }
    const juce::File out { juce::String (argv[1]) };
    const double bpm = argc > 3 ? juce::String (argv[3]).getDoubleValue() : 85.0;
    auto audioDir = out.getChildFile ("Drums"), midiDir = out.getChildFile ("MIDI").getChildFile ("Drums");
    audioDir.createDirectory(); midiDir.createDirectory();

    rk::Kit kit;
    juce::String err;
    if (! kit.load (juce::File (juce::String (argv[2])), err)) { std::printf ("kit: %s\n", err.toRawUTF8()); return 1; }

    const int loopSamples = (int) std::llround (SR * 16.0 * 60.0 / bpm);
    struct Out { juce::String name; juce::AudioBuffer<float> buf; };
    std::vector<Out> outs;
    for (auto& j : kJobs)
    {
        if (findPreset (j.preset) == nullptr) { std::printf ("missing preset %s\n", j.preset); return 1; }
        for (int v = 0; v < 2; ++v)
        {
            auto hits = loopHits (j, v == 1, bpm);
            const juce::String name = juce::String (j.scene) + " - Drums" + (v == 1 ? " Fill" : "");
            // MIDI: a hit pushed ahead of beat 1 snaps to beat 1, so the cell starts on its downbeat
            std::vector<bz::TimedHit> wrapped;
            for (auto h : hits) { if (h.q < 0.0) h.q = 0.0; wrapped.push_back (h); }
            bz::writeMidiFile (midiDir.getChildFile (name + ".mid"), bz::toMidiFile (wrapped, bpm, 4, 4));
            outs.push_back ({ name, renderSeamless (hits, bpm, kit, loopSamples) });
        }
    }
    float peak = 0.0f;
    for (auto& o : outs) peak = juce::jmax (peak, o.buf.getMagnitude (0, o.buf.getNumSamples()));
    const float gain = juce::Decibels::decibelsToGain (-1.0f) / juce::jmax (peak, 1.0e-6f);   // one gain for the whole set
    for (auto& o : outs)
    {
        o.buf.applyGain (gain);
        writeWav (audioDir.getChildFile (o.name + ".wav"), o.buf, bpm, 16);
        std::printf ("%-36s peak %6.2f dB\n", o.name.toRawUTF8(), juce::Decibels::gainToDecibels (o.buf.getMagnitude (0, o.buf.getNumSamples())));
    }
    std::printf ("set gain %.2f dB, loop %d samples\n", juce::Decibels::gainToDecibels (gain), loopSamples);
    return 0;
}
