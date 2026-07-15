#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "params/ParameterIds.h"

namespace
{
    constexpr int knobSize = 78;
    constexpr int textBoxHeight = 18;
    constexpr int labelHeight = 18;
    constexpr int headerHeight = 22;
    constexpr int margin = 12;
    constexpr int slotWidth = knobSize + margin / 2;
    constexpr int toggleWidth = 70;
    constexpr int rowHeight = headerHeight + labelHeight + knobSize + textBoxHeight + margin;
    constexpr int editorWidth = margin * 2 + 12 * slotWidth; // widest row (Direct) has 12 control slots
}

MiserereAudioProcessorEditor::MiserereAudioProcessorEditor (MiserereAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor (&processorToEdit),
      audioProcessor (processorToEdit)
{
    // Global strip.
    auto& global = addSection ("Global");
    addKnob (global, ParamIDs::inTrim, "In Trim");
    addKnob (global, ParamIDs::outTrim, "Out Trim");
    addToggle (global, ParamIDs::bypass, "Bypass");

    // Bus A - Direct chain, in signal-flow order.
    auto& direct = addSection ("Bus A - Direct");
    addToggle (direct, ParamIDs::busAHpfEnabled, "HPF");
    addKnob (direct, ParamIDs::busAHpfFreq, "HPF Freq");
    addKnob (direct, ParamIDs::busAEqLowGain, "EQ Low");
    addKnob (direct, ParamIDs::busAEqMidFreq, "Mid Freq");
    addKnob (direct, ParamIDs::busAEqMidGain, "Mid Gain");
    addKnob (direct, ParamIDs::busAEqMidQ, "Mid Q");
    addKnob (direct, ParamIDs::busAEqHighGain, "EQ High");
    addChoice (direct, ParamIDs::busACompRatio, "Ratio");
    addKnob (direct, ParamIDs::busACompThreshold, "Threshold");
    addKnob (direct, ParamIDs::busACompAttack, "Attack");
    addKnob (direct, ParamIDs::busACompRelease, "Release");
    addKnob (direct, ParamIDs::busACompMakeup, "Makeup");

    auto& direct2 = addSection ("Bus A - Direct (cont.)");
    addToggle (direct2, ParamIDs::busADeessEnabled, "De-Ess");
    addKnob (direct2, ParamIDs::busADeessFreq, "De-Ess Freq");
    addKnob (direct2, ParamIDs::busADeessThreshold, "De-Ess Thr");
    addKnob (direct2, ParamIDs::busASatDrive, "Sat Drive");
    addKnob (direct2, ParamIDs::busALevel, "Level");
    addToggle (direct2, ParamIDs::busAMute, "Mute");
    addToggle (direct2, ParamIDs::busASolo, "Solo");

    // Bus B - Opto sandwich.
    auto& optoSection = addSection ("Bus B - Opto");
    addChoice (optoSection, ParamIDs::busBLowBoostFreq, "Low Freq");
    addKnob (optoSection, ParamIDs::busBLowBoostGain, "Low Boost");
    addChoice (optoSection, ParamIDs::busBHighBoostFreq, "High Freq");
    addKnob (optoSection, ParamIDs::busBHighBoostGain, "High Boost");
    addKnob (optoSection, ParamIDs::busBOptoReduction, "Peak Red.");
    addKnob (optoSection, ParamIDs::busBOptoMakeup, "Makeup");
    addKnob (optoSection, ParamIDs::busBAirGain, "Air");
    addKnob (optoSection, ParamIDs::busBLevel, "Level");
    addToggle (optoSection, ParamIDs::busBMute, "Mute");
    addToggle (optoSection, ParamIDs::busBSolo, "Solo");

    // Bus C - Smash.
    auto& smashSection = addSection ("Bus C - Smash");
    addKnob (smashSection, ParamIDs::busCAttack, "Attack");
    addKnob (smashSection, ParamIDs::busCRelease, "Release");
    addKnob (smashSection, ParamIDs::busCDrive, "Drive");
    addKnob (smashSection, ParamIDs::busCOutputTrim, "Out Trim");
    addKnob (smashSection, ParamIDs::busCLevel, "Level");
    addToggle (smashSection, ParamIDs::busCMute, "Mute");
    addToggle (smashSection, ParamIDs::busCSolo, "Solo");

    // Bus D - Slap.
    auto& slapSection = addSection ("Bus D - Slap");
    addKnob (slapSection, ParamIDs::busDDelayMs, "Delay");
    addKnob (slapSection, ParamIDs::busDFeedback, "Feedback");
    addKnob (slapSection, ParamIDs::busDHpFreq, "Loop HP");
    addKnob (slapSection, ParamIDs::busDLpFreq, "Loop LP");
    addToggle (slapSection, ParamIDs::busDMono, "Mono");
    addKnob (slapSection, ParamIDs::busDLevel, "Level");
    addToggle (slapSection, ParamIDs::busDMute, "Mute");
    addToggle (slapSection, ParamIDs::busDSolo, "Solo");

    requiredHeight = margin * 2 + static_cast<int> (sections.size()) * rowHeight;

    setResizable (false, false);
    setSize (editorWidth, requiredHeight);
}

MiserereAudioProcessorEditor::~MiserereAudioProcessorEditor() = default;

MiserereAudioProcessorEditor::Section& MiserereAudioProcessorEditor::addSection (const juce::String& headerText)
{
    auto section = std::make_unique<Section>();

    section->header.setText (headerText, juce::dontSendNotification);
    section->header.setJustificationType (juce::Justification::centredLeft);
    section->header.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (section->header);

    sections.push_back (std::move (section));
    return *sections.back();
}

MiserereAudioProcessorEditor::Knob& MiserereAudioProcessorEditor::addKnob (Section& section, const char* parameterId, const juce::String& labelText)
{
    auto knob = std::make_unique<Knob>();

    knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobSize, textBoxHeight);
    addAndMakeVisible (knob->slider);

    knob->label.setText (labelText, juce::dontSendNotification);
    knob->label.setJustificationType (juce::Justification::centred);
    // false => label sits above the slider it tracks; JUCE repositions it
    // automatically whenever the slider's bounds change, so resized() only
    // needs to place the sliders themselves.
    knob->label.attachToComponent (&knob->slider, false);
    addAndMakeVisible (knob->label);

    knob->attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, parameterId, knob->slider);

    section.controlsInOrder.push_back (&knob->slider);
    knobs.push_back (std::move (knob));
    return *knobs.back();
}

MiserereAudioProcessorEditor::Choice& MiserereAudioProcessorEditor::addChoice (Section& section, const char* parameterId, const juce::String& labelText)
{
    auto choice = std::make_unique<Choice>();

    // ComboBoxAttachment does NOT populate the box itself (JUCE 8.0.14 -
    // shipped as an apotheosis v0.1.0 bug in this suite): pull the choice
    // strings straight from the live APVTS parameter
    // (AudioParameterChoice::getAllValueStrings() returns its `choices`
    // array) BEFORE creating the attachment, so the GUI can never drift out
    // of sync with ParameterLayout.cpp. Item IDs are 1-based to match
    // ComboBox's convention; ComboBoxAttachment maps them back to the
    // parameter's 0-based choice index.
    if (auto* parameter = audioProcessor.apvts.getParameter (parameterId))
        choice->box.addItemList (parameter->getAllValueStrings(), 1);

    addAndMakeVisible (choice->box);

    choice->label.setText (labelText, juce::dontSendNotification);
    choice->label.setJustificationType (juce::Justification::centred);
    choice->label.attachToComponent (&choice->box, false);
    addAndMakeVisible (choice->label);

    choice->attachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, parameterId, choice->box);

    section.controlsInOrder.push_back (&choice->box);
    choices.push_back (std::move (choice));
    return *choices.back();
}

MiserereAudioProcessorEditor::Toggle& MiserereAudioProcessorEditor::addToggle (Section& section, const char* parameterId, const juce::String& labelText)
{
    auto toggle = std::make_unique<Toggle>();

    toggle->button.setButtonText (labelText);
    addAndMakeVisible (toggle->button);

    toggle->attachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, parameterId, toggle->button);

    section.controlsInOrder.push_back (&toggle->button);
    toggles.push_back (std::move (toggle));
    return *toggles.back();
}

void MiserereAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MiserereAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (margin);

    for (auto& section : sections)
    {
        auto row = bounds.removeFromTop (rowHeight);

        section->header.setBounds (row.removeFromTop (headerHeight));
        row.removeFromTop (labelHeight); // room for the attached labels above each control

        auto controlsArea = row;

        for (auto* control : section->controlsInOrder)
        {
            if (dynamic_cast<juce::Slider*> (control) != nullptr)
            {
                control->setBounds (controlsArea.removeFromLeft (slotWidth).withTrimmedRight (margin / 2).withHeight (knobSize + textBoxHeight));
            }
            else if (dynamic_cast<juce::ComboBox*> (control) != nullptr)
            {
                auto slot = controlsArea.removeFromLeft (slotWidth).withTrimmedRight (margin / 2);
                control->setBounds (slot.withHeight (textBoxHeight + 6).withY (slot.getY() + knobSize / 2 - textBoxHeight / 2));
            }
            else // toggle button
            {
                auto slot = controlsArea.removeFromLeft (toggleWidth).withTrimmedRight (margin / 2);
                control->setBounds (slot.withHeight (textBoxHeight + 6).withY (slot.getY() + knobSize / 2 - textBoxHeight / 2));
            }
        }
    }
}
