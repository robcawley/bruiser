#include "Export.h"

namespace bz
{
std::vector<TimedHit> renderSong (const Params& base, const std::vector<SongPart>& parts, double bpm, int num, int den)
{
    std::vector<TimedHit> out;
    const double barQ = barLengthQ (num, den);
    auto bar = std::make_unique<Bar>();
    int64_t index = 0;
    bool prevFilled = false;
    for (const auto& part : parts)
    {
        Params p = base;
        p.section = part.section;
        for (int i = 0; i < part.bars; ++i, ++index)
        {
            BarContext c;
            c.bar = index; c.barInSection = i; c.num = num; c.den = den; c.bpm = bpm;
            c.prevFilled = prevFilled;
            c.forceFill = part.fillAtEnd && i == part.bars - 1;
            generate (p, c, *bar);
            prevFilled = bar->fillStartQ >= 0.0;
            for (int k = 0; k < bar->count; ++k)
            {
                const auto& h = bar->hits[k];
                out.push_back ({ juce::jmax (0.0, (double) index * barQ + h.q), h.note, h.vel, h.fill != 0 });
            }
        }
    }
    return out;
}

juce::MidiFile toMidiFile (const std::vector<TimedHit>& hits, double bpm, int num, int den)
{
    constexpr int tpq = 960;
    juce::MidiMessageSequence seq;
    auto tempo = juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / bpm));
    tempo.setTimeStamp (0); seq.addEvent (tempo);
    auto sig = juce::MidiMessage::timeSignatureMetaEvent (num, den);
    sig.setTimeStamp (0); seq.addEvent (sig);
    auto name = juce::MidiMessage::textMetaEvent (3, "BRUISER");
    name.setTimeStamp (0); seq.addEvent (name);

    auto sorted = hits;
    std::stable_sort (sorted.begin(), sorted.end(), [] (const TimedHit& a, const TimedHit& b) { return a.q < b.q; });
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        const auto& h = sorted[i];
        const double t = std::round (h.q * tpq);
        double len = std::round (noteLengthSeconds (bpm) * bpm / 60.0 * tpq);
        for (size_t j = i + 1; j < sorted.size() && sorted[j].q * tpq < t + len + 1; ++j)
            if (sorted[j].note == h.note) { len = juce::jmax (1.0, std::round (sorted[j].q * tpq) - t - 1); break; }
        auto on = juce::MidiMessage::noteOn (10, h.note, (juce::uint8) h.vel);
        on.setTimeStamp (t);
        auto off = juce::MidiMessage::noteOff (10, h.note);
        off.setTimeStamp (t + len);
        seq.addEvent (on);
        seq.addEvent (off);
    }
    seq.sort();
    seq.updateMatchedPairs();

    juce::MidiFile f;
    f.setTicksPerQuarterNote (tpq);
    f.addTrack (seq);
    return f;
}

bool writeMidiFile (const juce::File& file, const juce::MidiFile& m)
{
    file.deleteFile();
    juce::FileOutputStream os (file);
    if (! os.openedOk()) return false;
    return m.writeTo (os, 1);
}
} // namespace bz
