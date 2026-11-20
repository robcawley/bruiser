#pragma once
// Offline rendering of BRUISER's playing to MIDI: the drag-to-track tile and the tests.
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include "Groove.h"

namespace bz
{
struct SongPart { Section section; int bars; bool fillAtEnd = false; };

struct TimedHit { double q; int note; int vel; bool fill; };

// Plays the parts back to back from bar 1, exactly as the live player would.
std::vector<TimedHit> renderSong (const Params& base, const std::vector<SongPart>& parts, double bpm, int num, int den);

juce::MidiFile toMidiFile (const std::vector<TimedHit>& hits, double bpm, int num, int den);
bool writeMidiFile (const juce::File& f, const juce::MidiFile& m);
} // namespace bz
