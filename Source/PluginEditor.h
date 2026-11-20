#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

namespace bzui { class Panel; }

class BruiserEditor final : public juce::AudioProcessorEditor,
                            public juce::DragAndDropContainer,
                            private juce::Timer
{
public:
    static constexpr int kW = 960, kH = 620;

    explicit BruiserEditor (BruiserProcessor&);
    ~BruiserEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bzui::Panel& getPanel() { return *panel; }
    void tickForTest() { timerCallback(); }

private:
    void timerCallback() override;

    BruiserProcessor& proc;
    std::unique_ptr<bzui::Panel> panel;
    juce::ComponentBoundsConstrainer constrainer;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BruiserEditor)
};

namespace bzui
{
// Exposed for the offline UI test.
class Panel : public juce::Component
{
public:
    explicit Panel (BruiserProcessor&);
    ~Panel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

    juce::StringArray boundParameterIDs() const;
    juce::Component* findControl (const juce::String& paramID) const;
    juce::File renderDragFile (int bars) const;   // what the DRAG MIDI tile hands to Logic

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    BruiserProcessor& proc;
};
} // namespace bzui
