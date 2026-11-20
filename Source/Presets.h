#pragma once
// BRUISER factory presets. A preset sets how the drummer plays; it never touches SECTION,
// KICK FOLLOW or PLAY, so it's safe to change presets over an arranged song.
// fillLen: 0 auto 1 beat 2 two beats 3 bar | time: 0 half 1 normal 2 double
// hand: 0 auto 1 hats 2 open 3 ride 4 crash 5 toms
// meter: 0 follow Logic 1 3/4 2 5/4 3 6/8 4 7/8 5 7/4 6 9/8 7 11/8 8 12/8 9 5/8 10 13/8 11 6/4
// Presets with their own meter play it whatever Logic is set to; set Logic to match so the grid lines up.

namespace bz
{
struct Preset
{
    const char* category;
    const char* name;
    float loud, complexity, style, fills;
    int fillLen;
    float pocket, swing, human;
    int time, hand, take;
    int meter = 0;     // METER choice: 0 follows Logic (see BruiserProcessor::kMeters)
};

inline constexpr Preset kPresets[] = {
    //  category      name                    loud  cplx  style fills fl  pocket swing human  t  hd take
    { "START",      "Bruiser",               0.60f, 0.45f, 0.50f, 0.50f, 0,  0.00f, 0.00f, 0.50f, 1, 0, 1, 0 },

    { "ARENA",      "Stadium Slam",          0.85f, 0.45f, 0.05f, 0.55f, 0, -0.35f, 0.00f, 0.45f, 1, 0, 1, 0 },
    { "ARENA",      "Wall of Crash",         0.95f, 0.40f, 0.00f, 0.50f, 0, -0.30f, 0.00f, 0.40f, 1, 4, 3, 0 },
    { "ARENA",      "Big Backbeat",          0.80f, 0.30f, 0.10f, 0.40f, 0, -0.50f, 0.00f, 0.40f, 1, 1, 2, 0 },
    { "ARENA",      "Behind the Beat",       0.75f, 0.40f, 0.15f, 0.45f, 0, -0.80f, 0.00f, 0.55f, 1, 0, 5, 0 },
    { "ARENA",      "Open Hat Roar",         0.88f, 0.50f, 0.10f, 0.60f, 0, -0.30f, 0.00f, 0.50f, 1, 2, 4, 0 },
    { "ARENA",      "Floor Tom Stomp",       0.80f, 0.35f, 0.05f, 0.40f, 0, -0.40f, 0.00f, 0.45f, 1, 5, 7, 0 },
    { "ARENA",      "Chorus Detonator",      1.00f, 0.55f, 0.05f, 0.75f, 2, -0.30f, 0.00f, 0.45f, 1, 4, 9, 0 },
    { "ARENA",      "Stripped Thump",        0.60f, 0.15f, 0.10f, 0.20f, 1, -0.40f, 0.00f, 0.40f, 1, 1, 11, 0 },

    { "GALLOP",     "Full Gallop",           0.85f, 0.55f, 1.00f, 0.60f, 0,  0.20f, 0.00f, 0.40f, 1, 0, 1, 0 },
    { "GALLOP",     "Cavalry Charge",        0.95f, 0.70f, 0.95f, 0.70f, 0,  0.35f, 0.00f, 0.35f, 1, 4, 2, 0 },
    { "GALLOP",     "Ride Bell Run",         0.80f, 0.60f, 0.90f, 0.55f, 0,  0.25f, 0.00f, 0.40f, 1, 3, 3, 0 },
    { "GALLOP",     "Night Rider",           0.70f, 0.50f, 0.85f, 0.50f, 0,  0.15f, 0.00f, 0.45f, 1, 1, 4, 0 },
    { "GALLOP",     "Triplet Thunder",       0.90f, 0.85f, 1.00f, 0.80f, 3,  0.30f, 0.00f, 0.35f, 1, 0, 5, 0 },
    { "GALLOP",     "Hoofbeats",             0.65f, 0.40f, 0.80f, 0.35f, 0,  0.10f, 0.00f, 0.50f, 1, 1, 6, 0 },
    { "GALLOP",     "Banner Raised",         0.92f, 0.60f, 0.90f, 0.65f, 2,  0.30f, 0.00f, 0.40f, 1, 3, 7, 0 },
    { "GALLOP",     "Over the Ridge",        0.85f, 0.75f, 0.95f, 0.70f, 0,  0.40f, 0.00f, 0.30f, 1, 2, 8, 0 },

    { "CROSSFIRE",  "Even Split",            0.80f, 0.50f, 0.50f, 0.55f, 0,  0.00f, 0.00f, 0.45f, 1, 0, 1, 0 },
    { "CROSSFIRE",  "Slam Leaning",          0.80f, 0.50f, 0.35f, 0.50f, 0, -0.20f, 0.00f, 0.45f, 1, 0, 2, 0 },
    { "CROSSFIRE",  "Gallop Leaning",        0.80f, 0.50f, 0.65f, 0.50f, 0,  0.15f, 0.00f, 0.45f, 1, 0, 3, 0 },
    { "CROSSFIRE",  "Heavy Engine",          0.90f, 0.60f, 0.55f, 0.60f, 0,  0.00f, 0.00f, 0.40f, 1, 2, 4, 0 },
    { "CROSSFIRE",  "Two Drummers",          0.85f, 0.70f, 0.50f, 0.70f, 0,  0.00f, 0.00f, 0.50f, 1, 0, 5, 0 },
    { "CROSSFIRE",  "Restless",              0.75f, 0.80f, 0.60f, 0.80f, 0,  0.10f, 0.00f, 0.55f, 1, 0, 6, 0 },
    { "CROSSFIRE",  "Grit and Gallop",       0.88f, 0.55f, 0.60f, 0.60f, 0,  0.05f, 0.00f, 0.45f, 1, 4, 7, 0 },
    { "CROSSFIRE",  "Midnight Engine",       0.70f, 0.60f, 0.45f, 0.45f, 0, -0.10f, 0.00f, 0.50f, 1, 3, 8, 0 },

    { "FAST",       "Double Time Riot",      0.95f, 0.40f, 0.20f, 0.50f, 0,  0.20f, 0.00f, 0.45f, 2, 4, 1, 0 },
    { "FAST",       "Basement Show",         0.85f, 0.35f, 0.15f, 0.45f, 0,  0.25f, 0.00f, 0.60f, 2, 1, 2, 0 },
    { "FAST",       "Two-Step",              0.80f, 0.30f, 0.20f, 0.40f, 0,  0.15f, 0.00f, 0.50f, 2, 2, 3, 0 },
    { "FAST",       "Skate Park",            0.88f, 0.50f, 0.30f, 0.55f, 0,  0.30f, 0.00f, 0.50f, 2, 3, 4, 0 },
    { "FAST",       "Gallop Sprint",         0.90f, 0.60f, 0.85f, 0.60f, 0,  0.35f, 0.00f, 0.40f, 2, 3, 5, 0 },
    { "FAST",       "Encore",                1.00f, 0.45f, 0.20f, 0.70f, 0,  0.25f, 0.00f, 0.50f, 2, 4, 6, 0 },
    { "FAST",       "Pogo",                  0.85f, 0.25f, 0.10f, 0.35f, 0,  0.20f, 0.00f, 0.55f, 2, 1, 7, 0 },
    { "FAST",       "Overdrive",             0.95f, 0.70f, 0.60f, 0.75f, 0,  0.30f, 0.00f, 0.45f, 2, 2, 8, 0 },

    { "HEAVY",      "Half-Time Crush",       0.90f, 0.40f, 0.10f, 0.40f, 0, -0.40f, 0.00f, 0.40f, 0, 4, 1, 0 },
    { "HEAVY",      "Sludge",                0.85f, 0.25f, 0.05f, 0.30f, 0, -0.90f, 0.00f, 0.50f, 0, 3, 2, 0 },
    { "HEAVY",      "Heavy Sway",            0.80f, 0.45f, 0.30f, 0.45f, 0, -0.30f, 0.00f, 0.50f, 0, 2, 3, 0 },
    { "HEAVY",      "Doom Bell",             0.78f, 0.40f, 0.70f, 0.35f, 0, -0.20f, 0.00f, 0.45f, 0, 3, 4, 0 },
    { "HEAVY",      "Breakdown",             1.00f, 0.60f, 0.50f, 0.50f, 1, -0.20f, 0.00f, 0.40f, 0, 4, 5, 0 },
    { "HEAVY",      "Slow Burn",             0.65f, 0.35f, 0.20f, 0.30f, 0, -0.40f, 0.00f, 0.50f, 0, 1, 6, 0 },
    { "HEAVY",      "Thunderhead",           0.95f, 0.70f, 0.80f, 0.60f, 0,  0.10f, 0.00f, 0.40f, 0, 5, 7, 0 },
    { "HEAVY",      "Dead Weight",           0.90f, 0.30f, 0.10f, 0.40f, 0, -0.70f, 0.00f, 0.45f, 0, 2, 8, 0 },

    { "SHUFFLE",    "Bar Room Shuffle",      0.70f, 0.50f, 0.30f, 0.45f, 0, -0.20f, 0.80f, 0.50f, 1, 1, 1, 0 },
    { "SHUFFLE",    "Swagger",               0.75f, 0.45f, 0.20f, 0.40f, 0, -0.50f, 0.50f, 0.50f, 1, 0, 2, 0 },
    { "SHUFFLE",    "Dirty Shuffle",         0.80f, 0.60f, 0.40f, 0.50f, 0, -0.30f, 0.90f, 0.55f, 1, 2, 3, 0 },
    { "SHUFFLE",    "Gallop Swing",          0.75f, 0.55f, 0.80f, 0.45f, 0,  0.10f, 0.60f, 0.45f, 1, 3, 4, 0 },
    { "SHUFFLE",    "Lazy Sunday",           0.50f, 0.35f, 0.20f, 0.30f, 0, -0.60f, 0.60f, 0.60f, 1, 3, 5, 0 },
    { "SHUFFLE",    "Boogie",                0.80f, 0.55f, 0.30f, 0.55f, 0,  0.00f, 1.00f, 0.50f, 1, 1, 6, 0 },
    { "SHUFFLE",    "Strut",                 0.70f, 0.40f, 0.15f, 0.35f, 0, -0.40f, 0.35f, 0.45f, 1, 1, 7, 0 },
    { "SHUFFLE",    "Rolling Ride",          0.70f, 0.60f, 0.60f, 0.50f, 0,  0.00f, 0.70f, 0.50f, 1, 3, 8, 0 },

    { "INDIE",      "Bedroom Kit",           0.45f, 0.35f, 0.30f, 0.30f, 0, -0.20f, 0.00f, 0.60f, 1, 1, 1, 0 },
    { "INDIE",      "Tight and Dry",         0.55f, 0.30f, 0.20f, 0.25f, 1,  0.00f, 0.00f, 0.25f, 1, 0, 2, 0 },
    { "INDIE",      "Indie Drive",           0.70f, 0.45f, 0.35f, 0.40f, 0,  0.10f, 0.00f, 0.45f, 1, 2, 3, 0 },
    { "INDIE",      "Straight Line",         0.60f, 0.25f, 0.10f, 0.10f, 1,  0.00f, 0.00f, 0.20f, 1, 1, 4, 0 },
    { "INDIE",      "Quiet Verse",           0.40f, 0.30f, 0.20f, 0.25f, 0, -0.30f, 0.00f, 0.55f, 1, 1, 5, 0 },
    { "INDIE",      "Late Night Ride",       0.50f, 0.40f, 0.50f, 0.30f, 0, -0.20f, 0.00f, 0.55f, 1, 3, 6, 0 },
    { "INDIE",      "Ghost Notes",           0.55f, 0.70f, 0.10f, 0.30f, 0, -0.30f, 0.00f, 0.50f, 1, 1, 7, 0 },
    { "INDIE",      "Small Room Push",       0.60f, 0.45f, 0.40f, 0.35f, 0,  0.40f, 0.00f, 0.45f, 1, 0, 8, 0 },

    { "CINEMATIC",  "Power Ballad",          0.65f, 0.30f, 0.10f, 0.50f, 2, -0.40f, 0.00f, 0.45f, 1, 0, 1, 0 },
    { "CINEMATIC",  "Big Ballad Chorus",     0.85f, 0.35f, 0.10f, 0.60f, 2, -0.40f, 0.00f, 0.45f, 1, 4, 2, 0 },
    { "CINEMATIC",  "Tom Procession",        0.70f, 0.30f, 0.20f, 0.35f, 0, -0.30f, 0.00f, 0.40f, 0, 5, 3, 0 },
    { "CINEMATIC",  "War Drums",             0.90f, 0.55f, 0.60f, 0.55f, 0,  0.00f, 0.00f, 0.35f, 1, 5, 4, 0 },
    { "CINEMATIC",  "Slow Build",            0.55f, 0.25f, 0.20f, 0.30f, 0, -0.30f, 0.00f, 0.45f, 0, 3, 5, 0 },
    { "CINEMATIC",  "Epic Outro",            1.00f, 0.60f, 0.40f, 0.80f, 3, -0.20f, 0.00f, 0.45f, 1, 4, 6, 0 },
    { "CINEMATIC",  "Procession Bell",       0.60f, 0.20f, 0.30f, 0.20f, 0, -0.40f, 0.00f, 0.40f, 0, 5, 7, 0 },
    { "CINEMATIC",  "Horizon",               0.75f, 0.45f, 0.65f, 0.50f, 0,  0.00f, 0.00f, 0.45f, 0, 3, 8, 0 },

    { "SHOWOFF",    "Fill Every Bar",        0.85f, 0.70f, 0.50f, 1.00f, 1,  0.10f, 0.00f, 0.45f, 1, 0, 1, 0 },
    { "SHOWOFF",    "Kit Tour",              0.95f, 1.00f, 0.60f, 1.00f, 3,  0.20f, 0.00f, 0.45f, 1, 0, 2, 0 },
    { "SHOWOFF",    "Around the Kit",        0.85f, 0.80f, 0.30f, 0.90f, 2,  0.00f, 0.00f, 0.45f, 1, 0, 3, 0 },
    { "SHOWOFF",    "Triplet Madness",       0.90f, 0.90f, 0.95f, 0.95f, 2,  0.30f, 0.00f, 0.40f, 1, 0, 4, 0 },
    { "SHOWOFF",    "Flams and Fury",        0.90f, 0.70f, 0.05f, 0.85f, 1, -0.20f, 0.00f, 0.45f, 1, 0, 5, 0 },
    { "SHOWOFF",    "Splash Happy",          0.85f, 0.80f, 0.90f, 0.70f, 0,  0.20f, 0.00f, 0.45f, 1, 0, 6, 0 },
    { "SHOWOFF",    "Busy Hands",            0.80f, 0.95f, 0.50f, 0.60f, 0,  0.10f, 0.00f, 0.50f, 1, 1, 7, 0 },
    { "SHOWOFF",    "Showoff",               1.00f, 0.90f, 0.70f, 0.90f, 0,  0.20f, 0.00f, 0.45f, 1, 0, 8, 0 },

    //  category      name                    loud  cplx  style fills fl  pocket swing human  t  hd take meter
    { "THREES",     "Big Waltz",             0.80f, 0.35f, 0.10f, 0.50f, 0, -0.30f, 0.00f, 0.45f, 1, 4, 1, 1 },
    { "THREES",     "Parlour Waltz",         0.50f, 0.30f, 0.30f, 0.30f, 0, -0.20f, 0.00f, 0.50f, 1, 3, 2, 1 },
    { "THREES",     "Three-Four Stomp",      0.90f, 0.45f, 0.15f, 0.50f, 0, -0.40f, 0.00f, 0.45f, 1, 2, 3, 1 },
    { "THREES",     "Rolling Sixes",         0.70f, 0.45f, 0.30f, 0.45f, 0, -0.20f, 0.00f, 0.50f, 1, 1, 1, 3 },
    { "THREES",     "Rough Sea",             0.85f, 0.50f, 0.60f, 0.55f, 0,  0.00f, 0.00f, 0.45f, 1, 4, 2, 3 },
    { "THREES",     "Slow Twelve",           0.60f, 0.35f, 0.15f, 0.45f, 2, -0.40f, 0.00f, 0.50f, 1, 1, 1, 8 },
    { "THREES",     "Twelve Gallop",         0.85f, 0.60f, 0.90f, 0.60f, 0,  0.20f, 0.00f, 0.40f, 1, 3, 2, 8 },
    { "THREES",     "Six-Four Anthem",       0.90f, 0.40f, 0.10f, 0.55f, 2, -0.30f, 0.00f, 0.45f, 1, 4, 3, 11 },

    { "ODD TIME",   "Fives",                 0.75f, 0.45f, 0.30f, 0.45f, 0, -0.10f, 0.00f, 0.45f, 1, 1, 1, 2 },
    { "ODD TIME",   "Five Gallop",           0.85f, 0.55f, 0.90f, 0.50f, 0,  0.20f, 0.00f, 0.40f, 1, 3, 2, 2 },
    { "ODD TIME",   "Limp Seven",            0.85f, 0.45f, 0.10f, 0.50f, 0, -0.30f, 0.00f, 0.45f, 1, 2, 1, 4 },
    { "ODD TIME",   "Seven Gallop",          0.90f, 0.60f, 0.90f, 0.55f, 0,  0.25f, 0.00f, 0.40f, 1, 3, 2, 4 },
    { "ODD TIME",   "Long Seven",            0.80f, 0.45f, 0.40f, 0.50f, 0, -0.10f, 0.00f, 0.45f, 1, 1, 3, 5 },
    { "ODD TIME",   "Nine Steps",            0.75f, 0.50f, 0.50f, 0.45f, 0,  0.00f, 0.00f, 0.45f, 1, 3, 1, 6 },
    { "ODD TIME",   "Eleven",                0.80f, 0.55f, 0.50f, 0.50f, 0,  0.10f, 0.00f, 0.45f, 1, 1, 2, 7 },
    { "ODD TIME",   "Five-Eight Punch",      0.90f, 0.40f, 0.20f, 0.45f, 0,  0.20f, 0.00f, 0.45f, 1, 4, 3, 9 },
    { "ODD TIME",   "Thirteen",              0.80f, 0.60f, 0.60f, 0.55f, 0,  0.10f, 0.00f, 0.45f, 1, 2, 4, 10 },
};

inline constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (kPresets[0]));
} // namespace bz
