#pragma once
// BRUISER groove engine.
// Pure, deterministic, allocation-free: one call builds one bar of drum hits from the
// player settings and where we are in the song. The same code drives the live MIDI FX
// and the offline MIDI export, so what you drag out is what you heard.
#include <cstdint>

namespace bz
{
enum Section  { Intro, Verse, Pre, Chorus, Bridge, Break, Outro, NumSections };
enum TimeMode { Half, Normal, Double };
enum FillLen  { FillAuto, FillBeat, FillTwo, FillBar };
enum Hand     { HandAuto, HandHats, HandOpen, HandRide, HandCrash, HandToms };

// ROB KIT note map (General MIDI compatible where GM has the sound).
namespace nt
{
    constexpr int Kick = 36, Snare = 38, Rim = 40, Stick = 37,
                  Hat = 42, Pedal = 44, Open = 46,
                  CrashL = 49, CrashR = 57, Splash = 55, Ride = 51, Bell = 53,
                  StopL = 28, StopR = 29,
                  TomHi = 50, TomMid = 47, TomFloor = 43;
}

struct Params
{
    Section section = Verse;
    float loud = 0.6f, complexity = 0.45f;   // the pad
    float style = 0.5f;                      // 0 = SLAM ... 1 = GALLOP
    float fills = 0.5f;
    FillLen fillLen = FillAuto;
    float pocket = 0.0f;                     // -1 drag ... +1 push
    float swing = 0.0f, human = 0.5f;
    int take = 1;
    TimeMode time = Normal;
    Hand hand = HandAuto;
    bool kickFollow = false;

    // Exact member comparison is intentional: even tiny automation changes must
    // invalidate the cached bar. An epsilon here would change playback decisions.
   #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wfloat-equal"
   #endif
    bool operator== (const Params& o) const
    {
        return section == o.section && loud == o.loud && complexity == o.complexity && style == o.style
            && fills == o.fills && fillLen == o.fillLen && pocket == o.pocket && swing == o.swing && human == o.human
            && take == o.take && time == o.time && hand == o.hand && kickFollow == o.kickFollow;
    }
   #if defined(__clang__)
    #pragma clang diagnostic pop
   #endif
};

struct BarContext
{
    int64_t bar = 0;          // absolute bar index (song position)
    int barInSection = 0;     // 0 = first bar after a section change
    bool forceFill = false;   // FILL switch / button for this bar
    bool prevFilled = false;  // previous bar ended in a fill: land it with a crash
    int num = 4, den = 4;     // time signature
    double bpm = 120.0;
};

struct Hit
{
    double q = 0.0;           // quarter notes from the bar line, feel offsets included (may be slightly < 0)
    uint8_t note = 0, vel = 0;
    uint8_t lane = 0;         // display lane, see Lane
    uint8_t fill = 0;         // part of a fill
};

enum Lane : uint8_t { LaneCymbal, LaneHats, LaneSnare, LaneToms, LaneKick, NumLanes };
int laneFor (int note);

constexpr int kMaxHits = 224;

struct Bar
{
    Hit hits[kMaxHits];
    int count = 0;
    double lengthQ = 4.0;
    double fillStartQ = -1.0;  // < 0: no fill in this bar
};

// Fill length in quarters for this bar (0 = none). Exposed so the player can tell the
// next bar that this one filled.
double fillLengthQ (const Params&, const BarContext&);

// Build one bar. Hits come back sorted by time.
void generate (const Params&, const BarContext&, Bar& out);

double barLengthQ (int num, int den);
// Zero-based group containing q quarters from the bar start; clamped to this bar.
int beatIndexAt (int num, int den, double q);
// A 32nd note, constrained to a useful drum trigger duration.
double noteLengthSeconds (double bpm);
const char* sectionName (int s);
} // namespace bz
