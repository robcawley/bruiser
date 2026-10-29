#include "PluginEditor.h"
#include "Engine/Export.h"
#include "Presets.h"
#include "BinaryData.h"

namespace bzui
{
namespace col
{
    const juce::Colour bg { 0xff090e12 }, plateA { 0xff182328 }, plateB { 0xff10181d }, edge { 0xff26343a },
                       bone { 0xffe7eadf }, dim { 0xff8d9d9e }, label { 0xffbac6c0 },
                       lime { 0xffc4fb58 }, cyan { 0xff68dfdd }, brass { 0xffdfb56c }, red { 0xffee6358 }, pink { 0xffed83bc };
}

namespace
{
struct Fonts
{
    juce::Typeface::Ptr bold, semi, med, mono;
    static Fonts& get()
    {
        static Fonts f;
        return f;
    }
    Fonts()
    {
        bold = juce::Typeface::createSystemTypefaceFor (BinaryData::RajdhaniBold_ttf, BinaryData::RajdhaniBold_ttfSize);
        semi = juce::Typeface::createSystemTypefaceFor (BinaryData::RajdhaniSemiBold_ttf, BinaryData::RajdhaniSemiBold_ttfSize);
        med  = juce::Typeface::createSystemTypefaceFor (BinaryData::RajdhaniMedium_ttf, BinaryData::RajdhaniMedium_ttfSize);
        mono = juce::Typeface::createSystemTypefaceFor (BinaryData::ShareTechMonoRegular_ttf, BinaryData::ShareTechMonoRegular_ttfSize);
    }
};
juce::Font font (const juce::Typeface::Ptr& t, float h) { return juce::Font (juce::FontOptions (t).withHeight (h)); }
juce::Font bold (float h) { return font (Fonts::get().bold, h); }
juce::Font semi (float h) { return font (Fonts::get().semi, h); }
juce::Font mono (float h) { return font (Fonts::get().mono, h); }

void plate (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour accent = {})
{
    juce::ColourGradient grad (col::plateA, r.getX(), r.getY(), col::plateB, r.getX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (col::edge);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    if (! accent.isTransparent())
    {
        g.setColour (accent.withAlpha (0.8f));
        g.fillRect (r.getX() + 12.0f, r.getY(), 26.0f, 2.0f);
    }
}

void heading (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, juce::Colour c = col::label)
{
    g.setColour (c);
    g.setFont (semi (13.0f).withExtraKerningFactor (0.16f));
    g.drawText (t, r, juce::Justification::topLeft, false);
}

juce::Colour sectionColour (int s)
{
    switch (s)
    {
        case bz::Intro:  return col::cyan;
        case bz::Verse:  return col::lime;
        case bz::Pre:    return col::brass;
        case bz::Chorus: return col::red;
        case bz::Bridge: return col::cyan;
        case bz::Break:  return col::pink;
        case bz::Outro:  return col::red;
        default:         return col::lime;
    }
}

} // namespace

//==============================================================================
// A component bound to one parameter through JUCE's ParameterAttachment (gestures,
// host automation and undo all handled there).
class Bound : public juce::Component, public juce::SettableTooltipClient
{
public:
    Bound (juce::RangedAudioParameter& p, BruiserProcessor& owner)
        : param (p), att (p, [this] (float v) { value = v; repaint(); }, &owner.undoManager), proc (owner)
    {
        att.sendInitialUpdate();
        setTitle (p.getName (64));
        static const char* tips[] = {
            "Turn the drummer on or off.", "Choose the section; changes land on the next bar. MIDI channel 16, notes 12-18 also select sections.",
            "Set how hard the drummer plays.", "Set how busy the playing is.",
            "Blend a heavy backbeat with driving kick patterns.", "Set how often automatic fills happen.",
            "Choose how much of the bar a fill uses.", "Click to turn fills on; click again to turn them off. Logic can record both changes. MIDI channel 16: 19 On, 20 Off.",
            "Push the playing ahead or drag it behind the beat.", "Add a lilt to the subdivisions.",
            "Add small timing and loudness variations.", "Choose a repeatable alternative performance.",
            "Choose half, normal or double time. MIDI channel 16: notes 21, 22, 23.", "Choose the main cymbal or tom sound.",
            "Turn incoming track notes into kick hits.", "Follow Logic's meter or choose a fixed time signature."
        };
        const int index = p.getParameterIndex();
        setTooltip (juce::isPositiveAndBelow (index, (int) std::size (tips)) ? tips[index] : p.getName (64));
    }
    float norm() const { return param.convertTo0to1 (value); }
    juce::String paramID() const { return param.getParameterID(); }
    juce::String valueText() const { return param.getText (norm(), 32); }

    void beginEdit() { proc.synchroniseMidiControls(); proc.apvts.copyState(); att.beginGesture(); }
    void endEdit() { att.endGesture(); proc.apvts.copyState(); }
    void setComplete (float v) { proc.synchroniseMidiControls(); proc.apvts.copyState(); att.setValueAsCompleteGesture (v); proc.apvts.copyState(); }

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment att;
    BruiserProcessor& proc;
    float value = 0.0f;
};

class Dial final : public Bound
{
public:
    Dial (juce::RangedAudioParameter& p, juce::String l, juce::Colour c, BruiserProcessor& pr) : Bound (p, pr), text (std::move (l)), accent (c) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::label);
        g.setFont (semi (12.5f).withExtraKerningFactor (0.14f));
        g.drawText (text, r.removeFromTop (16.0f), juce::Justification::centred);
        auto val = r.removeFromBottom (16.0f);
        const float d = juce::jmin (r.getWidth(), r.getHeight()) - 6.0f;
        auto k = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
        const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
        const float n = norm();
        const bool bipolar = param.getNormalisableRange().start < 0.0f;

        juce::Path track; track.addCentredArc (k.getCentreX(), k.getCentreY(), d * 0.5f, d * 0.5f, 0, a0, a1, true);
        g.setColour (col::edge.brighter (0.2f));
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float from = bipolar ? (a0 + a1) * 0.5f : a0, to = a0 + (a1 - a0) * n;
        juce::Path arc; arc.addCentredArc (k.getCentreX(), k.getCentreY(), d * 0.5f, d * 0.5f, 0, juce::jmin (from, to), juce::jmax (from, to), true);
        g.setColour (accent);
        g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        auto body = k.reduced (d * 0.16f);
        juce::ColourGradient grad (juce::Colour (0xff2b383d), body.getX(), body.getY(), juce::Colour (0xff0c1215), body.getRight(), body.getBottom(), false);
        g.setGradientFill (grad);
        g.fillEllipse (body);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (body, 1.0f);
        const float ang = a0 + (a1 - a0) * n;
        const auto c = body.getCentre();
        const float rr = body.getWidth() * 0.5f;
        g.setColour (accent);
        g.drawLine ({ c.getPointOnCircumference (rr * 0.25f, ang), c.getPointOnCircumference (rr * 0.88f, ang) }, 2.5f);

        g.setColour (hover ? col::bone : col::dim);
        g.setFont (mono (12.0f));
        g.drawText (valueText(), val, juce::Justification::centred);
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override { start = norm(); beginEdit(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float sens = e.mods.isShiftDown() ? 900.0f : 180.0f;
        set (start + (float) (-e.getDistanceFromDragStartY() + e.getDistanceFromDragStartX() * 0.3f) / sens);
    }
    void mouseUp (const juce::MouseEvent&) override { endEdit(); }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        setComplete (param.convertFrom0to1 (param.getDefaultValue()));
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        setComplete (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, norm() + w.deltaY * 0.15f)));
    }

private:
    void set (float n) { att.setValueAsPartOfGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n))); }
    juce::String text;
    juce::Colour accent;
    float start = 0.0f;
    bool hover = false;
};

// Loudness (up) by complexity (across), like Session Player's pad.
class Pad final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Pad (juce::RangedAudioParameter& x, juce::RangedAudioParameter& y, BruiserProcessor& owner)
        : px (x), py (y), proc (owner),
          ax (x, [this] (float v) { vx = v; repaint(); }, &owner.undoManager),
          ay (y, [this] (float v) { vy = v; repaint(); }, &owner.undoManager)
    {
        ax.sendInitialUpdate(); ay.sendInitialUpdate();
        setTitle ("Loudness / Complexity");
        setTooltip ("Drag up to play harder and right to play a busier pattern.");
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff070b0e));
        g.fillRoundedRectangle (r, 5.0f);
        auto in = area();
        g.setColour (col::edge);
        for (int i = 1; i < 8; ++i)
        {
            const float x = in.getX() + in.getWidth() * (float) i / 8.0f, y = in.getY() + in.getHeight() * (float) i / 8.0f;
            g.drawVerticalLine ((int) x, in.getY(), in.getBottom());
            g.drawHorizontalLine ((int) y, in.getX(), in.getRight());
        }
        g.setColour (col::edge.brighter (0.4f));
        g.drawRect (in, 1.0f);

        g.setFont (semi (12.0f).withExtraKerningFactor (0.18f));
        g.setColour (col::dim);
        g.drawText ("LOUD", in.withHeight (18.0f).translated (0, 4), juce::Justification::centred);
        g.drawText ("SOFT", in.withTrimmedTop (in.getHeight() - 22.0f), juce::Justification::centred);
        g.drawText ("SIMPLE", in.withWidth (60.0f).translated (6, 0), juce::Justification::centredLeft);
        g.drawText ("COMPLEX", in.withTrimmedLeft (in.getWidth() - 70.0f).translated (-6, 0), juce::Justification::centredRight);

        const auto p = puck();
        g.setColour (col::lime.withAlpha (0.12f));
        g.fillEllipse (juce::Rectangle<float> (64, 64).withCentre (p));
        g.setColour (col::lime.withAlpha (0.25f));
        g.drawLine (in.getX(), p.y, in.getRight(), p.y, 1.0f);
        g.drawLine (p.x, in.getY(), p.x, in.getBottom(), 1.0f);
        g.setColour (col::lime);
        g.fillEllipse (juce::Rectangle<float> (18, 18).withCentre (p));
        g.setColour (col::bg);
        g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre (p));

        g.setFont (mono (12.0f));
        g.setColour (col::bone);
        g.drawText ("LOUD " + juce::String (juce::roundToInt (py.convertTo0to1 (vy) * 100)) + "  CPLX "
                        + juce::String (juce::roundToInt (px.convertTo0to1 (vx) * 100)),
                    in.reduced (8).removeFromBottom (18).withTrimmedLeft (in.getWidth() * 0.5f), juce::Justification::centredRight);
    }

    void mouseDown (const juce::MouseEvent& e) override { proc.apvts.copyState(); ax.beginGesture(); ay.beginGesture(); place (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { place (e.position); }
    void mouseUp (const juce::MouseEvent&) override { ax.endGesture(); ay.endGesture(); proc.apvts.copyState(); }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        proc.apvts.copyState();
        ax.beginGesture(); ay.beginGesture();
        ax.setValueAsPartOfGesture (px.convertFrom0to1 (px.getDefaultValue()));
        ay.setValueAsPartOfGesture (py.convertFrom0to1 (py.getDefaultValue()));
        ax.endGesture(); ay.endGesture();
        proc.apvts.copyState();
    }

    juce::Point<float> puck() const
    {
        auto in = area();
        return { in.getX() + in.getWidth() * px.convertTo0to1 (vx), in.getBottom() - in.getHeight() * py.convertTo0to1 (vy) };
    }

private:
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (10.0f); }
    void place (juce::Point<float> pos)
    {
        auto in = area();
        ax.setValueAsPartOfGesture (px.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, (pos.x - in.getX()) / in.getWidth())));
        ay.setValueAsPartOfGesture (py.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, (in.getBottom() - pos.y) / in.getHeight())));
    }
    juce::RangedAudioParameter &px, &py;
    BruiserProcessor& proc;
    juce::ParameterAttachment ax, ay;
    float vx = 0, vy = 0;
};

class StyleSlider final : public Bound
{
public:
    using Bound::Bound;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto top = r.removeFromTop (18.0f);
        g.setFont (bold (17.0f).withExtraKerningFactor (0.12f));
        g.setColour (col::red.interpolatedWith (col::dim, norm()));
        g.drawText ("SLAM", top, juce::Justification::centredLeft);
        g.setColour (col::pink.interpolatedWith (col::dim, 1.0f - norm()));
        g.drawText ("GALLOP", top, juce::Justification::centredRight);
        g.setColour (col::dim);
        g.setFont (mono (12.0f));
        g.drawText (valueText(), top, juce::Justification::centred);

        auto tr = track();
        g.setColour (juce::Colour (0xff070b0e));
        g.fillRoundedRectangle (tr.expanded (0, 5), 5.0f);
        juce::ColourGradient grad (col::red, tr.getX(), 0, col::pink, tr.getRight(), 0, false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (tr.withHeight (3.0f).withCentre (tr.getCentre()), 1.5f);
        for (int i = 0; i <= 10; ++i)
        {
            const float x = tr.getX() + tr.getWidth() * (float) i / 10.0f;
            g.setColour (col::edge.brighter (0.5f));
            g.drawVerticalLine ((int) x, tr.getBottom() + 4.0f, tr.getBottom() + (i % 5 == 0 ? 10.0f : 7.0f));
        }
        const float x = tr.getX() + tr.getWidth() * norm();
        auto thumb = juce::Rectangle<float> (12.0f, 24.0f).withCentre ({ x, tr.getCentreY() });
        g.setColour (col::bone);
        g.fillRoundedRectangle (thumb, 2.0f);
        g.setColour (col::bg);
        g.fillRect (thumb.withSizeKeepingCentre (2.0f, 14.0f));
    }
    void mouseDown (const juce::MouseEvent& e) override { beginEdit(); place (e.position.x); }
    void mouseDrag (const juce::MouseEvent& e) override { place (e.position.x); }
    void mouseUp (const juce::MouseEvent&) override { endEdit(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { setComplete (param.convertFrom0to1 (param.getDefaultValue())); }

private:
    juce::Rectangle<float> track() const { return getLocalBounds().toFloat().withTrimmedTop (26.0f).withTrimmedBottom (14.0f).reduced (8.0f, 0); }
    void place (float x)
    {
        auto tr = track();
        att.setValueAsPartOfGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, (x - tr.getX()) / tr.getWidth())));
    }
};

// Buttons for a choice (or bool) parameter, one per value.
class Segments final : public Bound
{
public:
    Segments (juce::RangedAudioParameter& p, juce::StringArray l, juce::Colour c, BruiserProcessor& pr, bool big = false)
        : Bound (p, pr), labels (std::move (l)), accent (c), large (big) {}
    std::function<juce::Colour (int)> colourFor;

    int index() const { return juce::roundToInt (value); }
    void paint (juce::Graphics& g) override
    {
        const int n = labels.size();
        auto r = getLocalBounds().toFloat();
        const float gap = 4.0f, w = (r.getWidth() - gap * (float) (n - 1)) / (float) n;
        for (int i = 0; i < n; ++i)
        {
            auto b = juce::Rectangle<float> (r.getX() + (float) i * (w + gap), r.getY(), w, r.getHeight());
            const bool on = i == index();
            const auto c = colourFor ? colourFor (i) : accent;
            g.setColour (on ? c.withAlpha (0.16f) : juce::Colour (0xff0d1418));
            g.fillRoundedRectangle (b, 4.0f);
            g.setColour (on ? c : (i == hover ? col::edge.brighter (0.6f) : col::edge));
            g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, on ? 1.5f : 1.0f);
            if (on && large)
            {
                g.setColour (c);
                g.fillRect (b.getX() + 8.0f, b.getBottom() - 4.0f, b.getWidth() - 16.0f, 2.0f);
            }
            g.setColour (on ? col::bone : col::dim);
            g.setFont ((large ? bold (16.0f) : semi (12.5f)).withExtraKerningFactor (0.1f));
            g.drawText (labels[i], b, juce::Justification::centred);
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { setHover (at (e.position.x)); }
    void mouseExit (const juce::MouseEvent&) override { setHover (-1); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = at (e.position.x);
        if (i >= 0) setComplete ((float) i);
    }

private:
    int at (float x) const { return juce::jlimit (0, labels.size() - 1, (int) (x / (float) getWidth() * (float) labels.size())); }
    void setHover (int i) { if (i != hover) { hover = i; repaint(); } }
    juce::StringArray labels;
    juce::Colour accent;
    bool large;
    int hover = -1;
};

class Toggle final : public Bound
{
public:
    Toggle (juce::RangedAudioParameter& p, juce::String l, juce::Colour c, BruiserProcessor& pr) : Bound (p, pr), text (std::move (l)), accent (c) {}
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const bool on = value > 0.5f;
        g.setColour (on ? accent.withAlpha (0.16f) : juce::Colour (0xff0d1418));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (on ? accent : col::edge);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, on ? 1.5f : 1.0f);
        auto led = juce::Rectangle<float> (8, 8).withCentre ({ r.getX() + 14.0f, r.getCentreY() });
        g.setColour (on ? accent : col::edge.brighter (0.3f));
        g.fillEllipse (led);
        g.setColour (on ? col::bone : col::dim);
        g.setFont (semi (13.0f).withExtraKerningFactor (0.12f));
        g.drawText (text, r.withTrimmedLeft (24.0f), juce::Justification::centredLeft);
    }
    void mouseDown (const juce::MouseEvent&) override { setComplete (value > 0.5f ? 0.0f : 1.0f); }

private:
    juce::String text;
    juce::Colour accent;
};

class Stepper final : public Bound
{
public:
    using Bound::Bound;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::label);
        g.setFont (semi (12.5f).withExtraKerningFactor (0.14f));
        g.drawText ("TAKE", r.removeFromTop (16.0f), juce::Justification::centred);
        auto box = r.withSizeKeepingCentre (r.getWidth(), 40.0f);
        g.setColour (juce::Colour (0xff070b0e));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (col::edge);
        g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (col::brass);
        g.setFont (mono (26.0f));
        g.drawText (juce::String (juce::roundToInt (value)).paddedLeft ('0', 2), box, juce::Justification::centred);
        g.setColour (col::dim);
        g.setFont (bold (18.0f));
        g.drawText ("<", box.withWidth (24.0f), juce::Justification::centred);
        g.drawText (">", box.withTrimmedLeft (box.getWidth() - 24.0f), juce::Justification::centred);
        g.setFont (semi (11.0f));
        g.drawText ("NEW VARIATION", r.removeFromBottom (14.0f), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int v = juce::roundToInt (value);
        const int nv = e.position.x < (float) getWidth() * 0.5f ? (v <= 1 ? 99 : v - 1) : (v >= 99 ? 1 : v + 1);
        setComplete ((float) nv);
    }
};

//==============================================================================
class PatternView final : public juce::Component
{
public:
    explicit PatternView (BruiserProcessor& p) : proc (p) {}

    void update()
    {
        bz::Snapshot s;
        const double pos = proc.player.posInBarQ.load();
        playing = pos >= 0.0 && proc.hostPlaying.load();
        if (playing && proc.player.readSnapshot (s)) { shown = s; live = true; }
        else if (! playing)
        {
            // Stopped: preview a typical bar with the current settings.
            live = false;
            bz::BarContext c;
            c.bar = 1; c.barInSection = 1; proc.effectiveMeter (c.num, c.den); c.bpm = proc.hostBpm.load();
            const auto p = proc.currentParams();
            bz::generate (p, c, shown.bar);
            shown.section = (int) p.section; shown.barInSection = -1; shown.barIndex = -1; shown.fill = false;
        }
        playhead = playing ? pos : -1.0;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff070b0e));
        g.fillRoundedRectangle (r, 5.0f);
        auto lab = r.removeFromLeft (52.0f);
        r = r.reduced (6.0f, 8.0f);
        const auto& bar = shown.bar;
        const double len = juce::jmax (0.25, bar.lengthQ);
        auto xAt = [&] (double q) { return r.getX() + (float) (q / len) * r.getWidth(); };
        const float laneH = r.getHeight() / (float) bz::NumLanes;
        static const char* names[] = { "CYM", "HATS", "SNARE", "TOMS", "KICK" };
        static const juce::Colour lc[] = { col::brass, col::cyan, col::bone, col::red, col::lime };

        if (bar.fillStartQ >= 0.0)
        {
            auto fr = r.withLeft (xAt (bar.fillStartQ));
            g.setColour (col::pink.withAlpha (0.08f));
            g.fillRect (fr);
            g.setColour (col::pink.withAlpha (0.7f));
            g.setFont (semi (11.0f).withExtraKerningFactor (0.14f));
            g.drawText ("FILL", fr.removeFromTop (12.0f).translated (4, -8), juce::Justification::topLeft);
        }
        for (int i = 0; i < bz::NumLanes; ++i)
        {
            auto lr = juce::Rectangle<float> (lab.getX(), r.getY() + laneH * (float) i, lab.getWidth(), laneH);
            g.setColour (col::dim);
            g.setFont (semi (11.5f).withExtraKerningFactor (0.12f));
            g.drawText (names[i], lr.withTrimmedLeft (10.0f), juce::Justification::centredLeft);
            if (i > 0) { g.setColour (col::edge.withAlpha (0.6f)); g.drawHorizontalLine ((int) (r.getY() + laneH * (float) i), r.getX(), r.getRight()); }
        }
        int num = 4, den = 4;
        proc.effectiveMeter (num, den);
        for (double q = 0.0; q < len - 1.0e-6; q += 0.5)
        {
            const bool beat = q < 1.0e-6 || bz::beatIndexAt (num, den, q) != bz::beatIndexAt (num, den, q - 1.0e-6);
            g.setColour (beat ? col::edge.brighter (0.35f) : col::edge.withAlpha (0.45f));
            g.drawVerticalLine ((int) xAt (q), r.getY(), r.getBottom());
        }
        for (int i = 0; i < bar.count; ++i)
        {
            const auto& h = bar.hits[i];
            const float v = (float) h.vel / 127.0f;
            const float x = xAt (juce::jlimit (0.0, len, h.q));
            const float cy = r.getY() + laneH * ((float) h.lane + 0.5f);
            const float hh = laneH * (0.25f + 0.55f * v);
            const bool past = live && playhead >= 0.0 && h.q <= playhead;
            auto c = lc[h.lane];
            if (h.fill) c = c.interpolatedWith (col::pink, 0.5f);
            g.setColour (c.withAlpha (past ? 1.0f : (live ? 0.55f : 0.75f)));
            g.fillRoundedRectangle (juce::Rectangle<float> (5.0f, hh).withCentre ({ x, cy }), 1.5f);
        }
        if (playhead >= 0.0)
        {
            g.setColour (col::lime);
            g.fillRect (xAt (playhead) - 1.0f, r.getY() - 4.0f, 2.0f, r.getHeight() + 8.0f);
        }
        if (! live)
        {
            g.setColour (col::dim.withAlpha (0.8f));
            g.setFont (semi (11.5f).withExtraKerningFactor (0.14f));
            g.drawText ("PREVIEW  -  PRESS PLAY IN LOGIC", getLocalBounds().toFloat().reduced (10, 4).removeFromBottom (14),
                        juce::Justification::centredRight);
        }
    }

    bz::Snapshot shown;
    bool live = false, playing = false;
    double playhead = -1.0;

private:
    BruiserProcessor& proc;
};

// Both clicks and host automation use the same Fill switch, so recorded playback
// follows exactly the state the producer heard while writing automation.
class FillButton final : public Bound
{
public:
    FillButton (juce::RangedAudioParameter& p, BruiserProcessor& pr) : Bound (p, pr) {}
    ~FillButton() override { if (editing) endEdit(); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const bool armed = proc.player.fillPending.load() || value > 0.5f;
        g.setColour (armed ? col::pink.withAlpha (0.22f) : juce::Colour (0xff0d1418));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (armed ? col::pink : col::edge.brighter (0.2f));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, armed ? 1.8f : 1.0f);
        g.setColour (armed ? col::bone : col::label);
        g.setFont (bold (17.0f).withExtraKerningFactor (0.14f));
        g.drawText (value > 0.5f ? "FILL ON" : (armed ? "ARMED" : "FILL!"), r, juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        juce::ignoreUnused (e);
        if (editing) return;
        beginEdit();
        editing = true;
        att.setValueAsPartOfGesture (param.getValue() > 0.5f ? 0.0f : 1.0f);
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (! editing) return;
        editing = false;
        endEdit();
    }
private:
    bool editing = false;
};

class DragTile final : public juce::Component, public juce::SettableTooltipClient
{
public:
    DragTile() { setTooltip ("Choose 4, 8 or 16 bars, then drag this section into Logic as MIDI."); }
    std::function<juce::File (int)> render;
    int bars = 8;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (hover ? col::lime.withAlpha (0.12f) : juce::Colour (0xff0d1418));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (hover ? col::lime : col::edge.brighter (0.2f));
        const float dash[] = { 5.0f, 3.0f };
        juce::Path p; p.addRoundedRectangle (r.reduced (1.0f), 4.0f);
        juce::Path d; juce::PathStrokeType (1.2f).createDashedStroke (d, p, dash, 2);
        g.fillPath (d);

        auto left = r.withTrimmedRight (150.0f);
        g.setColour (col::bone);
        g.setFont (bold (16.0f).withExtraKerningFactor (0.12f));
        g.drawText ("DRAG MIDI TO A TRACK", left.withTrimmedLeft (14.0f).withTrimmedBottom (14.0f), juce::Justification::centredLeft);
        g.setColour (col::dim);
        g.setFont (semi (11.5f));
        g.drawText ("this section, ending in a fill", left.withTrimmedLeft (14.0f).withTrimmedTop (22.0f), juce::Justification::centredLeft);

        const int opts[] = { 4, 8, 16 };
        for (int i = 0; i < 3; ++i)
        {
            auto b = barBox (i);
            const bool on = bars == opts[i];
            g.setColour (on ? col::lime.withAlpha (0.18f) : juce::Colour (0xff070b0e));
            g.fillRoundedRectangle (b, 3.0f);
            g.setColour (on ? col::lime : col::edge);
            g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);
            g.setColour (on ? col::bone : col::dim);
            g.setFont (mono (13.0f));
            g.drawText (juce::String (opts[i]), b, juce::Justification::centred);
        }
        g.setColour (col::dim);
        g.setFont (semi (10.5f));
        g.drawText ("BARS", r.withTrimmedLeft (r.getWidth() - 150.0f).removeFromBottom (12.0f).translated (0, -1), juce::Justification::centred);
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int opts[] = { 4, 8, 16 };
        for (int i = 0; i < 3; ++i)
            if (barBox (i).contains (e.position)) { bars = opts[i]; repaint(); return; }
        dragging = false;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6 || ! render) return;
        for (int i = 0; i < 3; ++i) if (barBox (i).contains (e.mouseDownPosition)) return;
        dragging = true;
        const auto f = render (bars);
        if (f.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
    }

private:
    juce::Rectangle<float> barBox (int i) const
    {
        auto r = getLocalBounds().toFloat();
        return { r.getRight() - 144.0f + (float) i * 46.0f, r.getY() + 7.0f, 40.0f, r.getHeight() - 26.0f };
    }
    bool hover = false, dragging = false;
};
// METER: follow Logic, or play a fixed meter. Turns pink when it disagrees with Logic.
class MeterBox final : public Bound
{
public:
    MeterBox (juce::RangedAudioParameter& p, BruiserProcessor& pr) : Bound (p, pr) {}
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const int m = juce::roundToInt (value);
        const bool clash = m > 0 && (BruiserProcessor::kMeters[m][0] != proc.hostNum.load() || BruiserProcessor::kMeters[m][1] != proc.hostDen.load());
        const auto c = m == 0 ? col::cyan : (clash ? col::pink : col::brass);
        g.setColour (m == 0 ? juce::Colour (0xff0d1418) : c.withAlpha (0.14f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (hover ? c : (m == 0 ? col::edge.brighter (0.2f) : c));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        auto t = r.reduced (10.0f, 3.0f);
        g.setColour (col::dim);
        g.setFont (semi (10.5f).withExtraKerningFactor (0.18f));
        g.drawText (clash ? "METER  /  SET LOGIC TO MATCH" : "METER", t.removeFromTop (12.0f), juce::Justification::centredLeft);
        g.setColour (col::bone);
        g.setFont (m == 0 ? semi (15.0f) : mono (18.0f));
        const juce::String txt = m == 0 ? "FOLLOW LOGIC  " + juce::String (proc.hostNum.load()) + "/" + juce::String (proc.hostDen.load())
                                        : BruiserProcessor::meterNames()[m];
        g.drawText (txt, t, juce::Justification::centredLeft);
        juce::Path tri;
        const float tx = r.getRight() - 14.0f, ty = r.getCentreY();
        tri.addTriangle (tx - 4.0f, ty - 2.0f, tx + 4.0f, ty - 2.0f, tx, ty + 3.0f);
        g.setColour (col::dim);
        g.fillPath (tri);
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override
    {
        juce::PopupMenu menu;
        const auto names = BruiserProcessor::meterNames();
        for (int i = 0; i < names.size(); ++i)
        {
            if (i == 1) menu.addSeparator();
            menu.addItem (i + 1, names[i], true, i == juce::roundToInt (value));
        }
        juce::Component::SafePointer<MeterBox> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int id)
        {
            if (safe != nullptr && id > 0) safe->setComplete ((float) (id - 1));
        });
    }
    bool hover = false;
};

// Header preset browser: arrows step through every preset, the name opens a menu by category.
class PresetBar final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PresetBar (BruiserProcessor& p) : proc (p) { setTitle ("Preset"); setTooltip ("Click the name to browse presets; use the arrows to step through them."); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff070b0e));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (hover ? col::brass.withAlpha (0.8f) : col::edge);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        const auto& pr = bz::kPresets[juce::jlimit (0, bz::kNumPresets - 1, proc.currentPreset)];
        g.setFont (bold (18.0f));
        g.setColour (col::dim);
        g.drawText ("<", r.withWidth (26.0f), juce::Justification::centred);
        g.drawText (">", r.withTrimmedLeft (r.getWidth() - 26.0f), juce::Justification::centred);
        auto mid = r.reduced (30.0f, 3.0f);
        g.setColour (col::brass);
        g.setFont (semi (11.0f).withExtraKerningFactor (0.18f));
        g.drawText (juce::String (pr.category) + (modified ? "  /  EDITED" : ""), mid.removeFromTop (13.0f), juce::Justification::centredLeft);
        g.setColour (modified ? col::label : col::bone);
        g.setFont (bold (18.0f).withExtraKerningFactor (0.04f));
        g.drawText (juce::String (pr.name).toUpperCase(), mid, juce::Justification::centredLeft, true);
        g.setColour (col::dim);
        juce::Path tri;
        const float tx = r.getRight() - 40.0f, ty = r.getCentreY();
        tri.addTriangle (tx - 4.0f, ty - 2.0f, tx + 4.0f, ty - 2.0f, tx, ty + 3.0f);
        g.fillPath (tri);
    }

    void update()
    {
        const bool m = proc.presetModified();
        if (m != modified || shownPreset != proc.currentPreset) { modified = m; shownPreset = proc.currentPreset; repaint(); }
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.position.x < 26.0f)                          step (-1);
        else if (e.position.x > (float) getWidth() - 26.0f) step (1);
        else showMenu();
    }
    void step (int d)
    {
        proc.applyPreset ((proc.currentPreset + d + bz::kNumPresets) % bz::kNumPresets);
        update();
        repaint();
    }

private:
    void showMenu()
    {
        juce::PopupMenu menu;
        juce::String cat;
        juce::PopupMenu sub;
        auto flush = [&]
        {
            if (cat.isNotEmpty()) menu.addSubMenu (cat, sub, true, nullptr, isCurrentIn (cat));
            sub = {};
        };
        for (int i = 0; i < bz::kNumPresets; ++i)
        {
            if (cat != bz::kPresets[i].category) { flush(); cat = bz::kPresets[i].category; }
            sub.addItem (i + 1, proc.presetDisplayName (i), true, i == proc.currentPreset);
        }
        flush();
        menu.addSeparator();
        menu.addItem (10000, "Surprise me");
        juce::Component::SafePointer<PresetBar> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int id)
        {
            if (safe == nullptr || id == 0) return;
            if (id == 10000) safe->proc.surprisePreset();
            else safe->proc.applyPreset (id - 1);
            safe->update();
            safe->repaint();
        });
    }
    bool isCurrentIn (const juce::String& c) const
    {
        return c == bz::kPresets[juce::jlimit (0, bz::kNumPresets - 1, proc.currentPreset)].category;
    }

    BruiserProcessor& proc;
    bool hover = false, modified = false;
    int shownPreset = -1;
};
} // namespace bzui

//==============================================================================
struct bzui::Panel::Impl
{
    explicit Impl (BruiserProcessor& p)
        : proc (p),
          pad (param (pid::complexity), param (pid::loud), p),
          style (param (pid::style), p),
          sections (param (pid::section), { "INTRO", "VERSE", "PRE", "CHORUS", "BRIDGE", "BREAK", "OUTRO" }, col::lime, p, true),
          fills (param (pid::fills), "FILLS", col::pink, p),
          pocket (param (pid::pocket), "POCKET", col::cyan, p),
          swing (param (pid::swing), "SWING", col::cyan, p),
          human (param (pid::human), "HUMAN", col::brass, p),
          take (param (pid::take), p),
          time (param (pid::time), { "HALF", "NORMAL", "DOUBLE" }, col::cyan, p),
          hand (param (pid::hand), { "AUTO", "HATS", "OPEN", "RIDE", "CRASH", "TOMS" }, col::brass, p),
          fillLen (param (pid::fillLen), { "AUTO", "1 BEAT", "2 BEATS", "BAR" }, col::pink, p),
          follow (param (pid::kickFollow), "KICK FOLLOW", col::lime, p),
          active (param (pid::active), "PLAYING", col::lime, p),
          fill (param (pid::fill), p),
          pattern (p),
          presets (p),
          meter (param (pid::meter), p)
    {
        sections.colourFor = [] (int i) { return sectionColour (i); };
    }

    juce::RangedAudioParameter& param (const char* id) { return *proc.apvts.getParameter (id); }

    BruiserProcessor& proc;
    Pad pad;
    StyleSlider style;
    Segments sections;
    Dial fills, pocket, swing, human;
    Stepper take;
    Segments time, hand, fillLen;
    Toggle follow, active;
    FillButton fill;
    PatternView pattern;
    DragTile drag;
    PresetBar presets;
    MeterBox meter;
};

bzui::Panel::Panel (BruiserProcessor& p) : impl (std::make_unique<Impl> (p)), proc (p)
{
    auto& m = *impl;
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("BRUISER");
    const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::days (1);
    for (const auto& file : dir.findChildFiles (juce::File::findFiles, false, "BRUISER *.mid"))
        if (file.getLastModificationTime() < cutoff) file.deleteFile();
    for (juce::Component* c : std::initializer_list<juce::Component*> { &m.pad, &m.style, &m.sections, &m.fills, &m.pocket, &m.swing, &m.human,
                                                                       &m.take, &m.time, &m.hand, &m.fillLen, &m.follow, &m.active,
                                                                       &m.fill, &m.pattern, &m.drag, &m.presets, &m.meter })
        addAndMakeVisible (c);
    m.drag.render = [this] (int bars) { return renderDragFile (bars); };
    setSize (BruiserEditor::kW, BruiserEditor::kH);
}

bzui::Panel::~Panel() = default;

juce::File bzui::Panel::renderDragFile (int bars) const
{
    auto p = proc.currentParams();
    int num = 4, den = 4;
    proc.effectiveMeter (num, den);
    const double bpm = proc.hostBpm.load();
    const auto hits = bz::renderSong (p, { { p.section, bars, true } }, bpm, num, den);
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("BRUISER");
    dir.createDirectory();
    const juce::String name = juce::String ("BRUISER ") + juce::String (bz::sectionName (p.section)).toLowerCase()
                              + " " + juce::String (bars) + " bars " + juce::String (num) + "-" + juce::String (den) + " take " + juce::String (p.take) + ".mid";
    auto f = dir.getChildFile (name);
    bz::writeMidiFile (f, bz::toMidiFile (hits, bpm, num, den));
    return f;
}

juce::StringArray bzui::Panel::boundParameterIDs() const
{
    auto& m = *impl;
    return { pid::complexity, pid::loud, m.style.paramID(), m.sections.paramID(), m.fills.paramID(), m.pocket.paramID(),
             m.swing.paramID(), m.human.paramID(), m.take.paramID(), m.time.paramID(), m.hand.paramID(), m.fillLen.paramID(),
             m.follow.paramID(), m.active.paramID(), m.fill.paramID(), m.meter.paramID() };
}

juce::Component* bzui::Panel::findControl (const juce::String& id) const
{
    auto& m = *impl;
    if (id == pid::complexity || id == pid::loud) return &m.pad;
    for (Bound* b : std::initializer_list<Bound*> { &m.style, &m.sections, &m.fills, &m.pocket, &m.swing, &m.human, &m.take,
                                                    &m.time, &m.hand, &m.fillLen, &m.follow, &m.active, &m.fill, &m.meter })
        if (b->paramID() == id) return b;
    if (id == "pattern") return &m.pattern;
    if (id == "drag") return &m.drag;
    if (id == "preset") return &m.presets;
    return nullptr;
}

void bzui::Panel::resized()
{
    auto& m = *impl;
    m.pad.setBounds (24, 102, 322, 300);
    m.style.setBounds (24, 420, 322, 62);
    m.active.setBounds (758, 22, 178, 34);
    m.presets.setBounds (218, 18, 270, 42);

    m.sections.setBounds (370, 102, 566, 44);
    m.pattern.setBounds (370, 162, 566, 176);

    const int dy = 368, dw = 104;
    m.fills.setBounds (374, dy, dw, 108);
    m.pocket.setBounds (374 + dw, dy, dw, 108);
    m.swing.setBounds (374 + 2 * dw, dy, dw, 108);
    m.human.setBounds (374 + 3 * dw, dy, dw, 108);
    m.take.setBounds (374 + 4 * dw + 18, dy + 4, 116, 100);

    m.time.setBounds (24, 524, 220, 30);
    m.hand.setBounds (260, 524, 380, 30);
    m.fillLen.setBounds (656, 524, 280, 30);

    m.follow.setBounds (24, 568, 160, 36);
    m.fill.setBounds (196, 568, 132, 36);
    m.drag.setBounds (560, 564, 376, 44);
    m.meter.setBounds (340, 564, 206, 44);
}

void bzui::Panel::paint (juce::Graphics& g)
{
    using namespace bzui;
    auto& m = *impl;
    g.fillAll (col::bg);
    {
        juce::ColourGradient glow (col::red.withAlpha (0.10f), 190, 250, juce::Colours::transparentBlack, 700, 600, true);
        g.setGradientFill (glow);
        g.fillRect (getLocalBounds());
    }

    // header
    g.setColour (col::bone);
    g.setFont (bold (46.0f).withExtraKerningFactor (0.06f));
    g.drawText ("BRUISER", 22, 10, 260, 52, juce::Justification::centredLeft);
    g.setColour (col::red);
    g.fillRect (24, 60, 52, 3);

    // transport readout
    auto ro = juce::Rectangle<float> (500, 20, 244, 38);
    g.setColour (juce::Colour (0xff070b0e));
    g.fillRoundedRectangle (ro, 4.0f);
    g.setColour (col::edge);
    g.drawRoundedRectangle (ro.reduced (0.5f), 4.0f, 1.0f);
    const auto& s = m.pattern.shown;
    juce::String left, right;
    if (m.pattern.live)
    {
        const double pos = juce::jmax (0.0, m.pattern.playhead);
        int num = 4, den = 4;
        proc.effectiveMeter (num, den);
        left = "BAR " + juce::String (s.barIndex + 1) + "." + juce::String (bz::beatIndexAt (num, den, pos) + 1);
        right = juce::String (bz::sectionName (s.section)) + " " + juce::String (s.barInSection + 1);
    }
    else
    {
        left = juce::String (juce::roundToInt (proc.hostBpm.load())) + " BPM";
        int n = 4, d = 4;
        proc.effectiveMeter (n, d);
        right = juce::String (n) + "/" + juce::String (d) + "  STOPPED";
    }
    g.setFont (mono (15.0f));
    g.setColour (col::lime);
    g.drawText (left, ro.reduced (10, 0), juce::Justification::centredLeft);
    g.setColour (sectionColour (s.section));
    g.drawText (right, ro.reduced (10, 0), juce::Justification::centredRight);

    // plates
    plate (g, { 14, 78, 342, 418 }, col::lime);
    heading (g, { 26, 84, 300, 16 }, "THE PAD");
    plate (g, { 362, 78, 584, 272 }, col::red);
    heading (g, { 374, 84, 300, 16 }, "SECTION");
    heading (g, { 670, 146, 266, 16 }, "THIS BAR", col::dim);
    plate (g, { 362, 356, 584, 140 }, col::cyan);
    heading (g, { 374, 360, 300, 16 }, "FEEL");
    plate (g, { 14, 502, 932, 112 }, col::brass);
    g.setColour (col::dim);
    g.setFont (semi (11.5f).withExtraKerningFactor (0.16f));
    g.drawText ("TIME", 24, 508, 200, 14, juce::Justification::left);
    g.drawText ("HAND", 260, 508, 200, 14, juce::Justification::left);
    g.drawText ("FILL LENGTH", 656, 508, 200, 14, juce::Justification::left);
}

void bzui::Panel::tick()
{
    impl->pattern.update();
    impl->presets.update();
    impl->fill.repaint();
    impl->meter.repaint();
    repaint (500, 20, 244, 38);
}

//==============================================================================
BruiserEditor::BruiserEditor (BruiserProcessor& p)
    : AudioProcessorEditor (&p), proc (p), panel (std::make_unique<bzui::Panel> (p))
{
    addAndMakeVisible (*panel);
    constrainer.setFixedAspectRatio ((double) kW / (double) kH);
    constrainer.setSizeLimits (kW * 3 / 4, kH * 3 / 4, kW * 2, kH * 2);
    setConstrainer (&constrainer);
    setResizable (true, true);
    const float s = juce::jlimit (0.75f, 2.0f, proc.uiScale);
    setSize (juce::roundToInt (kW * s), juce::roundToInt (kH * s));
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

BruiserEditor::~BruiserEditor() { stopTimer(); }

void BruiserEditor::paint (juce::Graphics& g) { g.fillAll (bzui::col::bg); }

void BruiserEditor::resized()
{
    const float s = (float) getWidth() / (float) kW;
    proc.uiScale = s;
    panel->setTransform (juce::AffineTransform::scale (s));
    panel->setBounds (0, 0, kW, kH);
}

void BruiserEditor::timerCallback() { panel->tick(); }

bool BruiserEditor::keyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'z' || key.getKeyCode() == 'Z'))
    {
        proc.synchroniseMidiControls();
        proc.apvts.copyState();
        if (key.getModifiers().isShiftDown()) proc.undoManager.redo();
        else proc.undoManager.undo();
        panel->tick();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey)
    {
        const int delta = key.getKeyCode() == juce::KeyPress::leftKey ? -1 : 1;
        proc.applyPreset ((proc.currentPreset + delta + bz::kNumPresets) % bz::kNumPresets);
        panel->tick();
        return true;
    }
    return false;
}
