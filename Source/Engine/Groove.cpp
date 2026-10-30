#include "Groove.h"
#include <algorithm>
#include <cmath>

namespace bz
{
namespace
{
uint64_t mix (uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// Deterministic random in [0, 1) from a key. Same song position + settings = same take.
float rnd (uint64_t a, uint64_t b = 0, uint64_t c = 0, uint64_t d = 0)
{
    const uint64_t h = mix (a ^ mix (b ^ mix (c ^ mix (d + 0x51ull))));
    return (float) (h >> 40) / 16777216.0f;
}

float bell (uint64_t a, uint64_t b, uint64_t c)   // soft bell curve in -1..1
{
    return (rnd (a, b, c, 1) + rnd (a, b, c, 2) + rnd (a, b, c, 3) - 1.5f) / 1.5f;
}

float clamp01 (float v) { return std::clamp (v, 0.0f, 1.0f); }
bool near (double a, double b) { return std::abs (a - b) < 1.0e-6; }

// Strong: kick on the beat. Back: snare on the beat. Weak: neither.
// Trip: a three-eighth group that closes an odd /8 bar: kick on it, snare on its last eighth (2+2+3 = K.S.K.S).
enum Role { Strong, Back, Weak, Trip };
struct Beat { double start = 0.0, len = 1.0; Role role = Strong; };

int makeBeats (int num, int den, TimeMode time, uint64_t take, Beat* out, int maxBeats)
{
    int nb = 0;
    if (den <= 4)
    {
        const int units = std::max (1, (int) std::lround (num * 4.0 / den));
        for (int i = 0; i < units && nb < maxBeats; ++i) out[nb++] = { (double) i, 1.0, Strong };
    }
    else
    {
        const int per = den / 4;                 // units per quarter: 2 for /8, 4 for /16
        const double unit = 4.0 / den;
        if (den == 8 && num % 3 == 0)            // compound: dotted-quarter beats
        {
            for (int i = 0; i < num / 3 && nb < maxBeats; ++i) out[nb++] = { i * 1.5, 1.5, Strong };
        }
        else
        {
            int left = num; double pos = 0.0;
            while (left > 0 && nb < maxBeats)
            {
                int groupUnits = per;
                if (left == per + per / 2 || left < per) groupUnits = left;   // 7/8 = 2+2+3
                out[nb++] = { pos, groupUnits * unit, Strong };
                pos += groupUnits * unit; left -= groupUnits;
            }
        }
    }

    const bool compound = den == 8 && num % 3 == 0;
    const bool oddEighths = den >= 8 && ! compound;
    for (int i = 0; i < nb; ++i)
    {
        if (time == Half && nb >= 2)
            out[i].role = i == 0 ? Strong : (i == nb / 2 ? Back : Weak);
        else if (nb == 3 && compound)                       // 9/8: one ... two ... SNARE
            out[i].role = i == 0 ? Strong : (i == 1 ? Weak : Back);
        else if (nb == 3 && ! oddEighths)                   // 3/4: waltz (oom-pah-pah) or heavy (kick 1, snare 3), by take
            out[i].role = i == 0 ? Strong : ((rnd (take, 977) < 0.5f) ? Back : (i == 1 ? Weak : Back));
        else if (! oddEighths && nb >= 5 && nb % 2 == 1)    // 5/4, 7/4: pairs then a closing three
            out[i].role = i < nb - 3 ? ((i % 2 == 0) ? Strong : Back) : (i == nb - 3 ? Strong : (i == nb - 2 ? Weak : Back));
        else
            out[i].role = (i % 2 == 0) ? Strong : Back;
    }
    // Odd /8 bars end in a group of three: if it falls on a kick beat, it carries its own snare.
    if (oddEighths && nb > 0 && time != Half && out[nb - 1].len > 1.25 && out[nb - 1].role == Strong)
        out[nb - 1].role = Trip;
    return nb;
}

struct Shape { float dL, dC; };
Shape shapeFor (Section s)
{
    switch (s)
    {
        case Intro:  return { -0.08f, -0.20f };
        case Verse:  return { -0.05f,  0.00f };
        case Pre:    return {  0.05f,  0.05f };
        case Chorus: return {  0.18f,  0.08f };
        case Bridge: return { -0.05f,  0.12f };
        case Break:  return {  0.10f,  0.00f };
        case Outro:  return {  0.22f,  0.10f };
        case NumSections: return { 0.0f, 0.0f };
        default:     return { 0.0f, 0.0f };
    }
}

Hand autoHand (Section s, float g, uint64_t take)
{
    switch (s)
    {
        case Intro:  return rnd (take, 901) < 1.0f - g ? HandToms : HandHats;
        case Verse:  return HandHats;
        case Pre:    return g < 0.5f ? HandOpen : HandRide;
        case Chorus:
        case Outro:  return g < 0.35f ? HandCrash : (g < 0.65f ? HandOpen : HandRide);
        case Bridge: return g < 0.4f ? HandToms : HandRide;
        case Break:
        case NumSections: return HandHats;
        default:     return HandHats;
    }
}

float effectiveC (const Params& p) { return clamp01 (p.complexity + shapeFor (p.section).dC); }
} // namespace

double barLengthQ (int num, int den) { return std::max (0.25, num * 4.0 / std::max (1, den)); }

int beatIndexAt (int num, int den, double q)
{
    Beat beats[32];
    const int count = makeBeats (num, den, Normal, 1, beats, 32);
    for (int i = count - 1; i >= 0; --i)
        if (q >= beats[i].start) return i;
    return 0;
}

double noteLengthSeconds (double bpm)
{
    return std::clamp (7.5 / std::max (1.0, bpm), 0.020, 0.120);
}

const char* sectionName (int s)
{
    static const char* names[] = { "INTRO", "VERSE", "PRE", "CHORUS", "BRIDGE", "BREAK", "OUTRO" };
    return (s >= 0 && s < NumSections) ? names[s] : "";
}

int laneFor (int note)
{
    switch (note)
    {
        case nt::Kick: case 35:                                        return LaneKick;
        case nt::Snare: case nt::Rim: case nt::Stick: case 34: case 32: return LaneSnare;
        case nt::Hat: case nt::Pedal: case nt::Open: case 33: case 31:  return LaneHats;
        case nt::TomHi: case 48: case nt::TomMid: case 45: case nt::TomFloor: case 41: return LaneToms;
        default:                                                        return LaneCymbal;
    }
}

double fillLengthQ (const Params& p, const BarContext& c)
{
    const double barQ = barLengthQ (c.num, c.den);
    const float C = effectiveC (p);
    const double paramLen = p.fillLen == FillBeat ? 1.0 : p.fillLen == FillTwo ? 2.0 : p.fillLen == FillBar ? barQ : 0.0;

    double len = 0.0;
    if (c.forceFill)
    {
        len = paramLen > 0.0 ? paramLen : (C < 0.5f ? 2.0 : barQ);
    }
    else if (p.section != Break && p.fills > 0.03f)
    {
        const int n = c.barInSection + 1;
        const uint64_t take = (uint64_t) p.take;
        if (n % 8 == 0)
            len = (p.fills > 0.75f && C > 0.55f) ? barQ : 2.0;
        else if (n % 4 == 0 && rnd (take, (uint64_t) c.bar, 71) < p.fills * 1.1f - 0.15f)
            len = 1.0;
        else if (n % 2 == 0 && rnd (take, (uint64_t) c.bar, 73) < (p.fills - 0.55f) * 1.5f)
            len = 0.5;

        if (len >= 1.0 && paramLen > 0.0) len = paramLen;
    }
    return std::min (len, barQ);
}

void generate (const Params& p, const BarContext& c, Bar& out)
{
    out.count = 0;
    const double barQ = barLengthQ (c.num, c.den);
    out.lengthQ = barQ;
    out.fillStartQ = -1.0;

    Beat beats[32];
    const int nb = makeBeats (c.num, c.den, p.time, (uint64_t) std::max (1, p.take), beats, 32);

    const Section sec = p.section;
    const float g = clamp01 (p.style);
    const auto shape = shapeFor (sec);
    const float L = clamp01 (p.loud + shape.dL);
    const float C = clamp01 (p.complexity + shape.dC);
    const uint64_t take = (uint64_t) std::max (1, p.take);
    const uint64_t sk = (uint64_t) sec * 7919u;
    const float base = 38.0f + 89.0f * L;
    const double qPerSec = c.bpm / 60.0;

    // Drummers repeat themselves: bars 1-3 of every four play the same groove, bar 4 varies.
    // Busier settings vary every other bar.
    int variant = (c.barInSection % 4 == 3) ? 1 : 0;
    if (C > 0.7f && c.barInSection % 2 == 1) variant += 2;

    auto add = [&] (double q, int note, float a, bool fill = false)
    {
        if (out.count >= kMaxHits || q >= barQ - 1.0e-9) return;
        auto& h = out.hits[out.count++];
        h.q = q; h.note = (uint8_t) note;
        h.vel = (uint8_t) std::clamp ((int) std::lround (base * a), 1, 127);
        h.lane = (uint8_t) laneFor (note); h.fill = fill ? 1 : 0;
    };
    auto has = [&] (double q, int note)
    {
        for (int i = 0; i < out.count; ++i)
            if (out.hits[i].note == note && near (out.hits[i].q, q)) return true;
        return false;
    };
    auto kick = [&] (double q, float a)
    {
        if (! p.kickFollow && ! has (q, nt::Kick)) add (q, nt::Kick, a);
    };
    const int snareNote = ((L > 0.8f && g < 0.5f) || L > 0.92f) ? nt::Rim : nt::Snare;
    const double flamQ = 0.022 * qPerSec;

    Hand hand = p.hand == HandAuto ? autoHand (sec, g, take) : p.hand;
    int lvl = C < 0.3f ? 0 : (C < 0.72f ? 1 : 2);
    if (p.time == Double || hand == HandCrash || hand == HandToms || hand == HandRide) lvl = std::min (lvl, 1);

    if (sec == Break)
    {
        // Stops: unison hits with space around them. GALLOP chokes the crashes.
        const int fig = (int) (rnd (take, sk, (uint64_t) (c.barInSection % 2), 61) * 4.0f);
        double pts[4]; int np = 0;
        auto at = [&] (int b, double frac) { b = std::clamp (b, 0, nb - 1); return beats[b].start + frac * beats[b].len; };
        pts[np++] = 0.0;
        if (fig == 1 && nb > 1) pts[np++] = at (1, 0.5);
        if (fig == 2 && nb > 2) { pts[np++] = at (nb - 2, 0.5); pts[np++] = at (nb - 1, 0.5); }
        if (fig == 3 && nb > 2) { pts[np++] = at (1, 0.5); pts[np++] = at (nb - 1, 0.0); }

        for (int i = 0; i < np; ++i)
        {
            const bool left = (i % 2 == 0);
            add (pts[i], left ? nt::CrashL : nt::CrashR, 1.0f);
            kick (pts[i], 1.0f);
            if (L > 0.5f) add (pts[i], snareNote, 0.95f);
            if (rnd (take, sk, (uint64_t) i, 62) < g * g)
                add (pts[i] + 0.5, left ? nt::StopL : nt::StopR, 0.7f);
        }
        if (C > 0.3f)
            for (int b = 0; b < nb; ++b)
            {
                bool hit = false;
                for (int i = 0; i < np; ++i) hit |= near (pts[i], beats[b].start);
                if (! hit) add (beats[b].start, C > 0.65f ? nt::Hat : nt::Pedal, 0.5f);
            }
    }
    else
    {
        const float gp = g * g * (3.0f - 2.0f * g);   // smoothstep: middle settings mix both drummers

        for (int b = 0; b < nb; ++b)
        {
            const auto& bt = beats[b];
            const double bs = bt.start, Lb = bt.len;
            const double last8 = Lb - 0.5, last16 = Lb - 0.25;
            const bool gallop = rnd (take, sk, (uint64_t) b, 11) < gp;

            // ---- kick
            if (p.time == Double)
            {
                kick (bs, 0.95f);
                if (C > 0.6f && rnd (take, sk, (uint64_t) b, (uint64_t) variant + 13) < C - 0.4f) kick (bs + last16, 0.75f);
            }
            else if (gallop)
            {
                const bool full = bt.role == Strong || bt.role == Trip || (bt.role == Back && C > 0.45f) || (bt.role == Weak && C > 0.6f);
                const double ts = near (Lb, 1.0) ? 1.0 / 6.0 : 0.25;
                if (C > 0.7f && b == nb - 1 && rnd (take, sk, (uint64_t) b, (uint64_t) variant + 23) < (C - 0.6f) * 1.6f)
                {
                    kick (bs, 0.95f); kick (bs + ts, 0.78f); kick (bs + 2 * ts, 0.82f); kick (bs + last8, 0.85f);
                }
                else
                {
                    if (full) kick (bs, 0.95f);
                    kick (bs + last8, 0.8f);
                    kick (bs + last16, 0.76f);
                }
            }
            else
            {
                double opts[4][3]; int nOpt[4]; float thr[4];
                auto set = [&] (int i, std::initializer_list<double> v, float t)
                {
                    int k = 0; for (double x : v) opts[i][k++] = x;
                    nOpt[i] = k; thr[i] = t;
                };
                if (bt.role == Strong)
                {
                    set (0, { 0.0 }, 0.0f); set (1, { 0.0, last8 }, 0.35f);
                    set (2, { 0.0, last16 }, 0.55f); set (3, { 0.0, last8, last16 }, 0.78f);
                }
                else if (bt.role == Back)
                {
                    set (0, {}, 0.0f); set (1, { last8 }, 0.25f);
                    set (2, { last16 }, 0.5f); set (3, { last8, last16 }, 0.8f);
                }
                else if (bt.role == Trip)
                {
                    set (0, { 0.0 }, 0.0f); set (1, { 0.0, 0.5 }, 0.4f);
                    set (2, { 0.0, 0.5, last16 }, 0.7f); set (3, { 0.0, 0.25, 0.5 }, 0.85f);
                }
                else
                {
                    set (0, {}, 0.0f); set (1, { last8 }, 0.3f);
                    set (2, { 0.0, last8 }, 0.5f); set (3, { last16 }, 0.7f);
                }
                int allowed = 0;
                for (int i = 0; i < 4; ++i) if (C >= thr[i]) allowed = i + 1;
                const float r = rnd (take, sk, (uint64_t) b, (uint64_t) variant + 5);
                const int idx = std::clamp (allowed - 1 - (int) (r * r * (float) allowed), 0, allowed - 1);
                for (int k = 0; k < nOpt[idx]; ++k) kick (bs + opts[idx][k], near (opts[idx][k], 0.0) ? 0.95f : 0.82f);
                if (sec == Pre) kick (bs, 0.9f);     // four on the floor into the chorus
            }

            // ---- snare
            if (p.time == Double)
            {
                add (bs + last8, snareNote, 0.95f);
            }
            else if (bt.role == Trip)
            {
                add (bs + last8, snareNote, 1.0f);
            }
            else if (bt.role == Back)
            {
                if (g < 0.5f && C > 0.5f && b == nb - 1 && rnd (take, sk, (uint64_t) variant, 17) < 0.5f)
                    add (bs - flamQ, nt::Snare, 0.45f);                      // flam on the last backbeat
                add (bs, snareNote, 1.0f);
            }

            // ---- ghost notes
            if (p.time != Double && (hand == HandHats || hand == HandRide || hand == HandOpen) && near (Lb, 1.0))
            {
                const float prob = std::max (0.0f, C - 0.45f) * 0.7f * (1.0f - 0.5f * g);
                const double pos[2] = { 0.25, 0.75 };
                for (int k = 0; k < 2; ++k)
                    if (rnd (take, sk, (uint64_t) (b * 8 + k), (uint64_t) variant + 31) < prob
                        && ! (bt.role == Back && k == 0))
                        add (bs + pos[k], nt::Snare, 0.24f);
            }

            // ---- the hand
            double hp[8]; int nh = 0;
            if (lvl == 0) hp[nh++] = 0.0;
            else
            {
                const double step = lvl == 1 ? 0.5 : 0.25;
                if (lvl == 2 && gallop && hand == HandHats) { hp[nh++] = 0.0; hp[nh++] = last8; hp[nh++] = last16; }
                else for (double x = 0.0; x < Lb - 1.0e-9 && nh < 8; x += step) hp[nh++] = x;
            }
            for (int k = 0; k < nh; ++k)
            {
                const double x = hp[k];
                const bool on = near (x, 0.0);
                const bool eighth = near (std::fmod (x, 0.5), 0.0);
                float a = on ? 1.0f : (eighth ? (g < 0.5f ? 0.62f : 0.78f) : 0.5f);
                int n = nt::Hat;
                switch (hand)
                {
                    case HandHats:
                        if (g < 0.6f && C > 0.45f && b == nb - 1 && near (x, last8) && x > 0.0
                            && rnd (take, sk, (uint64_t) variant, 41) < 0.6f) { n = nt::Open; a = 0.85f; }
                        break;
                    case HandOpen:  n = nt::Open; break;
                    case HandRide:  n = (on && (g > 0.55f || C > 0.7f)) ? nt::Bell : nt::Ride; break;
                    case HandCrash: n = (b == 0 && on && (c.bar % 2 == 1)) ? nt::CrashR : nt::CrashL; a *= 0.92f; break;
                    case HandToms:
                        if (bt.role == Back && on) continue;
                        n = nt::TomFloor; a *= 0.9f; break;
                    case HandAuto: break;
                    default: break;
                }
                add (bs + x, n, a);
            }
            if (hand == HandRide || hand == HandCrash || hand == HandToms)
                if (p.time != Double && (bt.role == Back || C > 0.6f)) add (bs, nt::Pedal, 0.55f);
        }

        // ---- punctuation
        const bool land = c.prevFilled || (c.barInSection == 0 && sec != Intro);
        if (land)
        {
            if (! has (0.0, nt::CrashL)) add (0.0, nt::CrashL, 1.0f);
            if ((L > 0.85f && g < 0.6f) || hand == HandCrash) add (0.0, nt::CrashR, 0.95f);
            kick (0.0, 1.0f);
        }
        else if ((sec == Chorus || sec == Outro || sec == Pre) && c.barInSection % 4 == 0 && hand != HandCrash)
        {
            add (0.0, (c.barInSection / 4) % 2 ? nt::CrashL : nt::CrashR, 0.95f);
            kick (0.0, 1.0f);
        }
        if (g > 0.5f && C > 0.5f && c.barInSection % 4 == 3 && nb > 0
            && rnd (take, sk, (uint64_t) (c.barInSection / 4), 51) < 0.6f)
        {
            const double q = beats[nb - 1].start + beats[nb - 1].len - 0.5;
            add (q, nt::Splash, 0.9f);
            kick (q, 0.85f);
        }
    }

    // ---- fill
    const double fl = fillLengthQ (p, c);
    if (fl > 0.0)
    {
        const double fs = barQ - fl;
        out.fillStartQ = fs;
        int w = 0;
        for (int i = 0; i < out.count; ++i)
            if (out.hits[i].q < fs - 1.0e-6) out.hits[w++] = out.hits[i];
        out.count = w;

        const bool gal = rnd (take, (uint64_t) c.bar, 81) < g;
        bool compound = false;
        for (int b = 0; b < nb; ++b) compound |= ! near (beats[b].len, 1.0);
        auto ff = [&] (double q, int n, float a) { add (q, n, a, true); };
        auto fk = [&] (double q, float a) { add (q, nt::Kick, a, true); };

        if (fl <= 0.5 + 1.0e-9)
        {
            if (gal) { ff (fs, nt::TomHi, 0.8f); ff (fs + fl / 3, nt::TomMid, 0.85f); ff (fs + 2 * fl / 3, nt::TomFloor, 0.92f); }
            else     { ff (fs - flamQ, nt::Snare, 0.45f); ff (fs, nt::Snare, 0.9f); ff (fs + fl / 2, nt::Snare, 0.97f); }
        }
        else if (! gal)
        {
            const double step = C > 0.4f ? 0.25 : 0.5;
            const int n = std::max (2, (int) std::lround (fl / step));
            const float r = rnd (take, (uint64_t) c.bar, 83);
            if (sec == Pre && r < 0.6f)
            {
                for (int i = 0; i < n; ++i)                                     // snare build
                    ff (fs + i * step, i == n - 1 ? nt::Rim : nt::Snare, 0.45f + 0.55f * (float) i / (float) (n - 1));
                fk (fs + (n - 1) * step, 1.0f);
            }
            else if (r < 0.5f)
            {
                // Down the kit: snare, then toms high to floor, floor and kick together at the end.
                const int drums[4] = { nt::Snare, nt::TomHi, nt::TomMid, nt::TomFloor };
                for (int i = 0; i < n; ++i)
                {
                    const int d = std::min (3, i * 4 / n);
                    const float a = 0.65f + 0.35f * (float) i / (float) (n - 1);
                    ff (fs + i * step, drums[d], a);
                    if (i >= n - 2) fk (fs + i * step, a);
                }
            }
            else
            {
                // Unison eighths: flammed snare against floor tom and kick.
                const int m = std::max (2, (int) std::lround (fl / 0.5));
                for (int i = 0; i < m; ++i)
                {
                    const double q = fs + i * 0.5;
                    const float a = 0.72f + 0.28f * (float) i / (float) (m - 1);
                    if (i % 2 == 0) { ff (q - flamQ, nt::Snare, a * 0.5f); ff (q, nt::Snare, a); }
                    else            { ff (q, nt::TomFloor, a); fk (q, a); }
                }
            }
        }
        else
        {
            // Around the toms in triplets with the kick in the gaps.
            const bool sext = C > 0.55f && ! compound;
            const double step = sext ? 1.0 / 6.0 : 0.25;
            const int n = std::max (3, (int) std::lround (fl / step));
            const int drums[4] = { nt::Snare, nt::TomHi, nt::TomMid, nt::TomFloor };
            const bool kickTrip = rnd (take, (uint64_t) c.bar, 85) < 0.55f;
            for (int i = 0; i < n; ++i)
            {
                const double q = fs + i * step;
                const float a = 0.72f + 0.28f * (float) i / (float) (n - 1);
                const int d = std::min (3, i * 4 / n);
                if (sext && kickTrip && i % 3 == 2) fk (q, a);
                else if (! sext && i % 4 == 0)      { ff (q, drums[d], a); fk (q, a * 0.9f); }
                else                                ff (q, drums[d], a);
            }
            fk (fs + (n - 1) * step, 1.0f);
        }
    }

    // ---- feel: swing, pocket, style lean, human timing and dynamics
    const float human = clamp01 (p.human);
    for (int i = 0; i < out.count; ++i)
    {
        auto& h = out.hits[i];
        double q = h.q;
        for (int b = nb - 1; b >= 0; --b)
            if (q >= beats[b].start - 1.0e-9)
            {
                if (near (beats[b].len, 1.0))
                {
                    const double f = q - beats[b].start;
                    if (lvl < 2 && near (f, 0.5))                     q += p.swing / 6.0;
                    else if (near (f, 0.25) || near (f, 0.75))        q += p.swing / 12.0;
                }
                if (p.swing > 0.0f && c.den == 8 && c.num % 3 == 0
                    && near (q - beats[b].start, 0.5))
                    q += p.swing / 24.0;
                break;
            }

        float ms = -p.pocket * 12.0f;
        float sd = 3.5f;
        switch (h.lane)
        {
            case LaneSnare: ms += 6.0f * (1.0f - g); sd = 2.5f; break;   // fat backbeat sits back
            case LaneHats:  ms -= 3.0f * g;          sd = 4.0f; break;   // busy hands push
            case LaneKick:  ms -= 2.0f * g;          sd = 3.0f; break;
            case LaneToms:  ms -= h.fill ? 2.0f * g : 0.0f; break;
            case LaneCymbal:
            case NumLanes: break;
            default: break;
        }
        ms += bell (take, (uint64_t) c.bar, (uint64_t) (i * 131 + h.note)) * human * sd;
        q += ms * 0.001 * qPerSec;
        h.q = std::max (q, -0.25);

        float v = (float) h.vel * (1.0f + bell (take, (uint64_t) c.bar, (uint64_t) (i * 977 + h.note + 7)) * human * 0.09f);
        if (h.lane == LaneHats)
            v *= 1.0f + 0.05f * human * std::sin (6.2831853f * (float) (h.q / barQ) + (float) (c.bar % 64) * 0.7f);
        h.vel = (uint8_t) std::clamp ((int) std::lround (v), 1, 127);
    }

    std::sort (out.hits, out.hits + out.count, [] (const Hit& a, const Hit& b) { return a.q < b.q; });
}
} // namespace bz
