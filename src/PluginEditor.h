#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

class MiserereAudioProcessor;

// A simple, functional v0.1 editor (the suite standard for M1): one rotary
// slider per float parameter, a combo box per choice parameter, and a
// toggle button per bool parameter, grouped into one row strip per bus
// (Global / Direct / Opto / Smash / Slap) in signal-flow order. All controls
// are bound to the APVTS via Slider/ComboBox/ButtonAttachment. A custom
// vector-drawn GUI (needle meters, pointer knobs) is M3; this is
// deliberately plain but fully wired and usable.
//
// Controls are built data-driven from ID/label tables rather than as ~47
// named members - see PluginEditor.cpp.
class MiserereAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit MiserereAudioProcessorEditor (MiserereAudioProcessor& processorToEdit);
    ~MiserereAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboBoxAttachment> attachment;
    };

    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<ButtonAttachment> attachment;
    };

    // A row of controls with a section header (one per bus + one global).
    struct Section
    {
        juce::Label header;
        std::vector<juce::Component*> controlsInOrder; // laid out left-to-right
    };

    Knob& addKnob (Section& section, const char* parameterId, const juce::String& labelText);
    Choice& addChoice (Section& section, const char* parameterId, const juce::String& labelText);
    Toggle& addToggle (Section& section, const char* parameterId, const juce::String& labelText);
    Section& addSection (const juce::String& headerText);

    MiserereAudioProcessor& audioProcessor;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Choice>> choices;
    std::vector<std::unique_ptr<Toggle>> toggles;
    std::vector<std::unique_ptr<Section>> sections;

    int requiredHeight = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MiserereAudioProcessorEditor)
};
