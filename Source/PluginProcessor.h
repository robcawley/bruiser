#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine/Player.h"

namespace pid
{
    inline constexpr const char* active = "active";
    inline constexpr const char* section = "section";
    inline constexpr const char* loud = "loud";
    inline constexpr const char* complexity = "complexity";
    inline constexpr const char* style = "style";
    inline constexpr const char* fills = "fills";
    inline constexpr const char* fillLen = "fill_len";
    inline constexpr const char* fill = "fill";
    inline constexpr const char* pocket = "pocket";
    inline constexpr const char* swing = "swing";
    inline constexpr const char* human = "human";
    inline constexpr const char* take = "take";
    inline constexpr const char* time = "time";
    inline constexpr const char* hand = "hand";
    inline constexpr const char* kickFollow = "kick_follow";
    inline constexpr const char* meter = "meter";
}

class BruiserProcessor final : public juce::AudioProcessor,
                               private juce::Timer,
                               private juce::AudioProcessorValueTreeState::Listener
{
public:
    BruiserProcessor();
    ~BruiserProcessor() override;

    void prepareToPlay (double sampleRate, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "BRUISER"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int index) override { applyPreset (index); }
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // The player settings as they stand now (message or audio thread).
    bz::Params currentParams() const;

    // Message-thread mirror for MIDI switches; audio uses lock-free effective values.
    void synchroniseMidiControls();
    static constexpr int kSwitchChannel = 16, kFirstSwitchNote = 12, kLastSwitchNote = 23;

    // Factory presets: they set the playing, never SECTION, KICK FOLLOW or PLAY.
    void applyPreset (int index);
    void surprisePreset();
    juce::String presetDisplayName (int index) const;
    bool presetModified() const;     // settings moved since the preset was loaded
    int currentPreset = 0;

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    bz::Player player;
    float uiScale = 1.0f;
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<int> hostNum { 4 }, hostDen { 4 };      // Logic's time signature
    std::atomic<int> playNum { 4 }, playDen { 4 };      // what BRUISER plays in (METER, or Logic's)

    // METER choices: 0 follows Logic.
    static constexpr int kNumMeters = 12;
    static constexpr int kMeters[kNumMeters][2] = { { 0, 0 }, { 3, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 7, 4 },
                                                     { 9, 8 }, { 11, 8 }, { 12, 8 }, { 5, 8 }, { 13, 8 }, { 6, 4 } };
    static juce::StringArray meterNames();
    void effectiveMeter (int& num, int& den) const;   // message thread: for preview and drag
    std::atomic<bool> hostPlaying { false };

    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

private:
    struct Raw
    {
        std::atomic<float>* v[16] {};
    } raw;

    struct MidiControl
    {
        // Packed external revision / command serial / value. A single atomic keeps
        // the audio-to-message handoff consistent without allocating or blocking.
        std::atomic<uint64_t> request { 0 };
        std::atomic<uint32_t> revision { 0 };
        uint32_t serial = 0;             // audio thread only
        uint64_t mirrored = 0;           // message thread only
    } midiControls[3];                   // Section, Fill, Time
    static_assert (std::atomic<uint64_t>::is_always_lock_free);
    juce::MidiBuffer switchInput, switchSlice;
    double switchSampleRate = 48000.0;
    float midiControlValue (int control, float base) const;
    void receiveMidiSwitch (int note);
    void timerCallback() override { synchroniseMidiControls(); }
    void parameterChanged (const juce::String&, float) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BruiserProcessor)
};
