#include "Player.h"
#include <cmath>
#include <cstring>

namespace bz
{
void Player::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    wasPlaying = false;
    for (auto& s : slots) s.used = false;
    numOffs = 0;
    reanchor = true;
    scratch.ensureSize (8192);
}

void Player::flush (juce::MidiBuffer& out, int sample)
{
    for (int i = 0; i < numOffs; ++i) out.addEvent (juce::MidiMessage::noteOff (kOutChannel, offs[i].note), sample);
    numOffs = 0;
}

void Player::noteOn (juce::MidiBuffer& out, int note, int vel, int sample)
{
    for (int i = 0; i < numOffs; ++i)
        if (offs[i].note == note)
        {
            out.addEvent (juce::MidiMessage::noteOff (kOutChannel, note), sample);
            offs[i] = offs[--numOffs];
            break;
        }
    out.addEvent (juce::MidiMessage::noteOn (kOutChannel, note, (juce::uint8) juce::jlimit (1, 127, vel)), sample);
    const int len = noteLengthSamples;
    if (numOffs < (int) std::size (offs)) offs[numOffs++] = { note, sample + len };
    else out.addEvent (juce::MidiMessage::noteOff (kOutChannel, note), sample);
    laneHits[laneFor (note)].fetch_add (1, std::memory_order_relaxed);
}

void Player::tickOffs (juce::MidiBuffer& out, int numSamples)
{
    for (int i = 0; i < numOffs;)
    {
        if (offs[i].samplesLeft < numSamples)
        {
            out.addEvent (juce::MidiMessage::noteOff (kOutChannel, offs[i].note), juce::jmax (0, offs[i].samplesLeft));
            offs[i] = offs[--numOffs];
        }
        else
        {
            offs[i].samplesLeft -= numSamples;
            ++i;
        }
    }
}

Player::Slot* Player::findSlot (int64_t index)
{
    for (auto& s : slots)
        if (s.used && s.index == index) return &s;
    return nullptr;
}

Player::Slot& Player::freeSlot (double beforePpq)
{
    juce::ignoreUnused (beforePpq);
    for (auto& s : slots)
        if (! s.used) return s;
    Slot* oldest = &slots[0];
    for (auto& s : slots)
        if (s.startPpq < oldest->startPpq) oldest = &s;
    return *oldest;
}

void Player::regen (Slot& s, double fromPpq)
{
    generate (s.params, s.ctx, s.bar);
    if (s.bar.fillStartQ >= 0.0) lastFilledBar = s.index;
    else if (lastFilledBar == s.index) lastFilledBar = INT64_MIN / 2;
    s.next = 0;
    while (s.next < s.bar.count && s.startPpq + s.bar.hits[s.next].q < fromPpq) ++s.next;
}

void Player::build (Slot& s, const Params& p, bool force, int64_t index, double startPpq, const HostState& h, double fromPpq)
{
    if ((int) p.section != trackedSection)
    {
        trackedSection = (int) p.section;
        sectionStartBar = index;
    }
    if (index < sectionStartBar) sectionStartBar = index;

    s.used = true;
    s.index = index;
    s.startPpq = startPpq;
    s.params = p;
    s.ctx.bar = index;
    s.ctx.barInSection = (int) juce::jmin<int64_t> (index - sectionStartBar, 1 << 20);
    s.ctx.forceFill = force;
    s.ctx.prevFilled = (lastFilledBar == index - 1);
    s.ctx.num = h.num; s.ctx.den = h.den; s.ctx.bpm = h.bpm;
    regen (s, fromPpq);
    if (force) fillRequest.store (false);
}

void Player::publish (const Slot& s)
{
    seq.fetch_add (1, std::memory_order_acq_rel);
    std::memcpy (&snap.bar, &s.bar, sizeof (Bar));
    snap.barIndex = s.index;
    snap.section = (int) s.params.section;
    snap.barInSection = s.ctx.barInSection;
    snap.fill = s.bar.fillStartQ >= 0.0;
    seq.fetch_add (1, std::memory_order_release);
}

bool Player::readSnapshot (Snapshot& out) const
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const uint32_t a = seq.load (std::memory_order_acquire);
        if (a & 1u) continue;
        std::memcpy (&out, &snap, sizeof (Snapshot));
        std::atomic_thread_fence (std::memory_order_acquire);
        if (seq.load (std::memory_order_relaxed) == a) return out.barIndex >= 0;
    }
    return false;
}

void Player::process (const Params& p, bool fillSwitch, bool active, const HostState& host,
                      juce::MidiBuffer& midi, int numSamples)
{
    noteLengthSamples = juce::roundToInt (sr * noteLengthSeconds (host.bpm));
    // Incoming MIDI: pass it through so hand-played hits still reach the kit, or with
    // KICK FOLLOW on, turn every incoming note into a kick (copy the bass part onto this track).
    scratch.clear();
    scratch.swapWith (midi);
    for (const auto m : scratch)
    {
        const auto msg = m.getMessage();
        if (active && p.kickFollow && (msg.isNoteOn() || msg.isNoteOff()))
        {
            if (msg.isNoteOn()) noteOn (midi, nt::Kick, msg.getVelocity(), m.samplePosition);
            continue;
        }
        midi.addEvent (msg, m.samplePosition);
    }
    fillPending.store (fillRequest.load (std::memory_order_relaxed), std::memory_order_relaxed);

    if (! active || ! host.playing || host.bpm <= 0.0 || numSamples <= 0)
    {
        if (wasPlaying) flush (midi, 0);
        for (auto& s : slots) s.used = false;
        wasPlaying = false;
        publishedIndex = -1;
        posInBarQ.store (-1.0);
        tickOffs (midi, numSamples);
        return;
    }

    const double qPerSample = host.bpm / 60.0 / sr;
    const double ppqStart = host.ppq, ppqEnd = ppqStart + numSamples * qPerSample;
    const double tol = juce::jmax (1.0e-3, 4.0 * qPerSample);

    if (! wasPlaying || std::abs (ppqStart - expectedPpq) > tol)
    {
        flush (midi, 0);                                  // started, looped or jumped
        for (auto& s : slots) s.used = false;
        reanchor = true;
    }
    wasPlaying = true;
    expectedPpq = ppqEnd;

    const double barQ = barLengthQ (host.num, host.den);
    double curStart;
    int64_t curIndex;
    if (host.hasBar)
    {
        curStart = host.barStartPpq;
        int64_t adv = 0;
        while (ppqStart >= curStart + barQ - 1.0e-9) { curStart += barQ; ++adv; }
        while (ppqStart < curStart - 1.0e-9)         { curStart -= barQ; --adv; }
        curIndex = host.barCount >= 0 ? host.barCount + adv : (int64_t) std::llround (curStart / barQ);
    }
    else
    {
        curIndex = (int64_t) std::floor ((ppqStart + 1.0e-9) / barQ);
        curStart = (double) curIndex * barQ;
    }

    if (reanchor)
    {
        // We don't know the history after a jump: assume phrases line up with 8-bar blocks.
        trackedSection = (int) p.section;
        sectionStartBar = curIndex - (((curIndex % 8) + 8) % 8);
        reanchor = false;
    }

    const double lookback = 0.035 * host.bpm / 60.0;     // pushed hits can land this far before their bar
    const bool request = fillRequest.load();
    const bool force = fillSwitch || request;

    if (curIndex >= 0)
    {
        Slot* cur = findSlot (curIndex);
        if (cur == nullptr)
        {
            cur = &freeSlot (ppqStart);
            build (*cur, p, force, curIndex, curStart, host, ppqStart - lookback);
        }
        else if (force && ! cur->ctx.forceFill)
        {
            // A fill asked for mid-bar: rebuild if its start is still ahead of us. The bar
            // before the fill is identical, so nothing already played changes.
            BarContext c2 = cur->ctx; c2.forceFill = true;
            const double fs = cur->startPpq + cur->bar.lengthQ - fillLengthQ (cur->params, c2);
            if (ppqStart < fs - 1.0e-6)
            {
                cur->ctx.forceFill = true;
                regen (*cur, ppqStart);
                fillRequest.store (false);
            }
        }
        if (publishedIndex != curIndex)
        {
            // The bar was built a moment early for any pushed pickups. If the settings moved
            // at the bar line (section automation, say), rebuild it with what's set now.
            if (! (cur->params == p))
                build (*cur, p, cur->ctx.forceFill || force, curIndex, curStart, host, ppqStart);
            publish (*cur);
            publishedIndex = curIndex;
        }
    }

    const double nextStart = curStart + barQ;
    if (nextStart - lookback < ppqEnd && curIndex + 1 >= 0 && findSlot (curIndex + 1) == nullptr)
        build (freeSlot (ppqStart), p, fillSwitch || fillRequest.load(), curIndex + 1, nextStart, host, nextStart - lookback);

    for (auto& s : slots)
    {
        if (! s.used) continue;
        while (s.next < s.bar.count)
        {
            const auto& h = s.bar.hits[s.next];
            const double at = s.startPpq + h.q;
            if (at >= ppqEnd) break;
            const int sample = juce::jlimit (0, numSamples - 1, (int) std::floor ((at - ppqStart) / qPerSample));
            noteOn (midi, h.note, h.vel, sample);
            ++s.next;
        }
        if (s.next >= s.bar.count && s.startPpq + s.bar.lengthQ + 0.5 < ppqStart) s.used = false;
    }

    posInBarQ.store (ppqStart - curStart);
    tickOffs (midi, numSamples);
}
} // namespace bz
