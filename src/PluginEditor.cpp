#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "presets/Localisation.h"

#include <BinaryData.h>

namespace
{
    constexpr int knobSize = 78;
    constexpr int textBoxHeight = 18;
    constexpr int labelHeight = 18;
    constexpr int headerHeight = 22;
    constexpr int margin = 12;
    constexpr int slotWidth = knobSize + margin / 2;
    constexpr int toggleWidth = 70;
    constexpr int presetBarHeight = 28;
    constexpr int rowHeight = headerHeight + labelHeight + knobSize + textBoxHeight + margin;
    constexpr int editorWidth = margin * 2 + 10 * slotWidth; // widest row has 10 control slots

    // M2 i18n frame (.scaffold/specs/preset-system-m2.md): selects German
    // (resources/i18n/de.txt) or falls through to English, once, at editor
    // construction - see Localisation.h's docs. `presetBar` is a member
    // initialised via the constructor's initialiser list, and its own
    // constructor already calls TRANS() on every button label - member
    // initialisers run in declaration order regardless of the order
    // they're written in, so this helper (called from presetBar's own
    // initialiser expression below) is what actually guarantees
    // installLocalisation() runs before presetBar exists, not an
    // installLocalisation() call in the constructor *body*, which would run
    // too late.
    basilica::presets::PresetManager& initLocalisationThenGetPresetManager (MiserereAudioProcessor& processor)
    {
        basilica::presets::installLocalisation (BinaryData::de_txt, BinaryData::de_txtSize);
        return processor.presetManager;
    }
}

MiserereAudioProcessorEditor::MiserereAudioProcessorEditor (MiserereAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor (&processorToEdit),
      audioProcessor (processorToEdit),
      presetBar (initLocalisationThenGetPresetManager (processorToEdit))
{
    addAndMakeVisible (presetBar);

    // Global strip.
    auto& global = addSection ("Global");
    addKnob (global, ParamIDs::inTrim, "In Trim");
    addKnob (global, ParamIDs::outTrim, "Out Trim");
    addToggle (global, ParamIDs::link, "Link");
    addKnob (global, ParamIDs::parallelTrim, "Parallel");
    addToggle (global, ParamIDs::bypass, "Bypass");

    // Direct path, in signal-flow order.
    auto& direct = addSection ("Direct - De-Ess Pre / FET");
    addToggle (direct, ParamIDs::directDeessPreEnabled, "De-Ess Pre");
    addKnob (direct, ParamIDs::directDeessPreFreq, "Pre Freq");
    addKnob (direct, ParamIDs::directDeessPreThreshold, "Pre Thr");
    addToggle (direct, ParamIDs::directFetEnabled, "FET");
    addKnob (direct, ParamIDs::directFetThreshold, "FET Thr");
    addKnob (direct, ParamIDs::directFetAttack, "FET Atk");
    addKnob (direct, ParamIDs::directFetRelease, "FET Rel");
    addKnob (direct, ParamIDs::directFetMakeup, "FET Makeup");

    auto& directEq = addSection ("Direct - Console EQ / Sat / De-Ess Post");
    addToggle (directEq, ParamIDs::directEqHpfEnabled, "HPF");
    addChoice (directEq, ParamIDs::directEqHpfFreq, "HPF Freq");
    addChoice (directEq, ParamIDs::directEqLowFreq, "Low Freq");
    addKnob (directEq, ParamIDs::directEqLowGain, "Low Gain");
    addChoice (directEq, ParamIDs::directEqMidFreq, "Mid Freq");
    addKnob (directEq, ParamIDs::directEqMidGain, "Mid Gain");
    addKnob (directEq, ParamIDs::directEqHighGain, "High Gain");
    addKnob (directEq, ParamIDs::directEqDrive, "EQ Drive");
    addKnob (directEq, ParamIDs::directSatDrive, "Sat Drive");
    addToggle (directEq, ParamIDs::directDeessPostEnabled, "De-Ess Post");
    addKnob (directEq, ParamIDs::directDeessPostFreq, "Post Freq");
    addKnob (directEq, ParamIDs::directDeessPostThreshold, "Post Thr");

    // Bus (1) CRUSH.
    auto& crush = addSection ("(1) Crush");
    addKnob (crush, ParamIDs::crushInput, "Input");
    addChoice (crush, ParamIDs::crushRatio, "Ratio");
    addChoice (crush, ParamIDs::crushStyle, "Style");
    addKnob (crush, ParamIDs::crushAttack, "Attack");
    addKnob (crush, ParamIDs::crushRelease, "Release");
    addKnob (crush, ParamIDs::crushOutput, "Output");
    addKnob (crush, ParamIDs::crushLevel, "Level");
    addToggle (crush, ParamIDs::crushMute, "Mute");
    addToggle (crush, ParamIDs::crushAudition, "Audition");

    // Bus (2) SANDWICH.
    auto& sandPre = addSection ("(2) Sandwich - Pre EQ");
    addChoice (sandPre, ParamIDs::sandPreLfFreq, "LF Freq");
    addKnob (sandPre, ParamIDs::sandPreLfBoost, "LF Boost");
    addKnob (sandPre, ParamIDs::sandPreLfCut, "LF Cut");
    addChoice (sandPre, ParamIDs::sandPreHfBellFreq, "Bell Freq");
    addKnob (sandPre, ParamIDs::sandPreHfBellBoost, "Bell Boost");
    addKnob (sandPre, ParamIDs::sandPreHfBellBandwidth, "Bell BW");
    addChoice (sandPre, ParamIDs::sandPreHfShelfFreq, "Shelf Freq");
    addKnob (sandPre, ParamIDs::sandPreHfShelfAtten, "Shelf Atten");

    auto& sandOpto = addSection ("(2) Sandwich - Opto");
    addKnob (sandOpto, ParamIDs::sandPeakRed, "Peak Red.");
    addToggle (sandOpto, ParamIDs::sandLimit, "Limit");
    addKnob (sandOpto, ParamIDs::sandEmphasis, "Emphasis");
    addToggle (sandOpto, ParamIDs::sandResidual, "Residual");

    auto& sandPost = addSection ("(2) Sandwich - Post EQ / Return");
    addChoice (sandPost, ParamIDs::sandPostLfFreq, "LF Freq");
    addKnob (sandPost, ParamIDs::sandPostLfBoost, "LF Boost");
    addKnob (sandPost, ParamIDs::sandPostLfCut, "LF Cut");
    addChoice (sandPost, ParamIDs::sandPostHfBellFreq, "Bell Freq");
    addKnob (sandPost, ParamIDs::sandPostHfBellBoost, "Bell Boost");
    addKnob (sandPost, ParamIDs::sandPostHfBellBandwidth, "Bell BW");
    addChoice (sandPost, ParamIDs::sandPostHfShelfFreq, "Shelf Freq");
    addKnob (sandPost, ParamIDs::sandPostHfShelfAtten, "Shelf Atten");
    addKnob (sandPost, ParamIDs::sandLevel, "Level");
    addToggle (sandPost, ParamIDs::sandMute, "Mute");
    addToggle (sandPost, ParamIDs::sandAudition, "Audition");

    // Bus (3) SPREAD.
    auto& spread = addSection ("(3) Spread");
    addKnob (spread, ParamIDs::spreadDetune, "Detune");
    addKnob (spread, ParamIDs::spreadTime, "Time");
    addKnob (spread, ParamIDs::spreadWidth, "Width");
    addKnob (spread, ParamIDs::spreadLevel, "Level");
    addToggle (spread, ParamIDs::spreadMute, "Mute");
    addToggle (spread, ParamIDs::spreadAudition, "Audition");

    // Bus (4) SLAP.
    auto& slap = addSection ("(4) Slap");
    addKnob (slap, ParamIDs::slapTime, "Time");
    addToggle (slap, ParamIDs::slapStereo, "Stereo");
    addKnob (slap, ParamIDs::slapTone, "Tone");
    addKnob (slap, ParamIDs::slapLevel, "Level");
    addToggle (slap, ParamIDs::slapMute, "Mute");
    addToggle (slap, ParamIDs::slapAudition, "Audition");

    requiredHeight = margin * 2 + presetBarHeight + margin + static_cast<int> (sections.size()) * rowHeight;

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

    presetBar.setBounds (bounds.removeFromTop (presetBarHeight));
    bounds.removeFromTop (margin);

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
