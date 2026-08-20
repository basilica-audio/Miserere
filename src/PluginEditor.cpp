#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "presets/Localisation.h"

#include <BinaryData.h>

#include <algorithm>

namespace
{
    // ----- M3 vector-editor layout metrics (issue #25) --------------------
    // All values are design constants, not measurements of pre-rendered
    // art (there is none): the editor computes its own size from these plus
    // the control tables in the constructor, and tests/gui/EditorLayoutTests.cpp
    // asserts the resulting geometry (containment, no overlap) on the real
    // component tree, so a change here can never silently clip a control.
    constexpr int outerMargin = 10;
    constexpr int presetBarHeight = 30;
    constexpr int bandGap = 8;

    constexpr int panelPadding = 10;
    constexpr int panelBottomPadding = 8;
    constexpr int rowGap = 8;

    // A knob slot: attached label above (JUCE 8.0.14 Label::
    // componentMovedOrResized sizes an above-attached label to
    // borderTopAndBottom + 6 + fontHeight ~ 22 px for the 14 px suite
    // serif, so 24 reserved keeps it clear of the row above), then the
    // rotary area, then the value box baked into the slider's own bounds.
    constexpr int labelHeight = 24;
    constexpr int knobSize = 60;
    constexpr int textBoxHeight = 16;
    constexpr int knobSlotWidth = 80;
    constexpr int toggleSlotWidth = 66;
    constexpr int toggleHeight = 24;
    constexpr int slotGap = 6; // trimmed off the right of every slot
    constexpr int rowHeight = labelHeight + knobSize + textBoxHeight;

    // Right-hand meter bay on the three gain-reducing panels.
    constexpr int meterBayWidth = 150;
    constexpr int meterWidth = 134;
    constexpr int meterHeight = 96;

    // M2 i18n frame: selects German (resources/i18n/de.txt) or falls
    // through to English, once, at editor construction - see
    // Localisation.h's docs. `presetBar` is a member initialised via the
    // constructor's initialiser list, and its own constructor already calls
    // TRANS() on every button label - member initialisers run in
    // declaration order, so this helper (called from presetBar's own
    // initialiser expression below) is what guarantees installLocalisation()
    // runs before presetBar exists, not a call in the constructor *body*,
    // which would run too late.
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
    // Propagates to every child, including the preset bar's stock buttons
    // and any menus/dialogs they open.
    setLookAndFeel (&lookAndFeel);

    // FOCUS ORDER (WCAG 2.4.3): children are created and added in signal-
    // flow/reading order - preset bar, then Global, Direct, Crush,
    // Sandwich, Spread, Slap, left-to-right within each row. JUCE's
    // default traverser follows this creation order; do not reorder.
    addAndMakeVisible (presetBar);

    // --- Global -----------------------------------------------------------
    auto& global = addPanel ("Global");
    globalPanel = &global;
    addKnob (global, ParamIDs::inTrim, "In Trim");
    addKnob (global, ParamIDs::outTrim, "Out Trim");
    addKnob (global, ParamIDs::parallelTrim, "Parallel Trim");
    addToggle (global, ParamIDs::link, "Link");
    addToggle (global, ParamIDs::bypass, "Bypass");

    // --- Direct path (serial), in signal-flow order -----------------------
    auto& direct = addPanel ("Direct Path");
    directPanel = &direct;
    addToggle (direct, ParamIDs::directDeessPreEnabled, "De-Ess Pre");
    addKnob (direct, ParamIDs::directDeessPreFreq, "Pre Freq");
    addKnob (direct, ParamIDs::directDeessPreThreshold, "Pre Thresh");
    addToggle (direct, ParamIDs::directFetEnabled, "FET");
    addKnob (direct, ParamIDs::directFetThreshold, "Threshold");
    addKnob (direct, ParamIDs::directFetAttack, "Attack");
    addKnob (direct, ParamIDs::directFetRelease, "Release");
    addKnob (direct, ParamIDs::directFetMakeup, "Makeup");

    addRow (direct);
    addToggle (direct, ParamIDs::directEqHpfEnabled, "HPF");
    addKnob (direct, ParamIDs::directEqHpfFreq, "HPF Freq");
    addKnob (direct, ParamIDs::directEqLowFreq, "Low Freq");
    addKnob (direct, ParamIDs::directEqLowGain, "Low Gain");
    addKnob (direct, ParamIDs::directEqMidFreq, "Mid Freq");
    addKnob (direct, ParamIDs::directEqMidGain, "Mid Gain");
    addKnob (direct, ParamIDs::directEqHighGain, "High Gain");
    addKnob (direct, ParamIDs::directEqDrive, "EQ Drive");
    addKnob (direct, ParamIDs::directSatDrive, "Sat Drive");
    addToggle (direct, ParamIDs::directDeessPostEnabled, "De-Ess Post");
    addKnob (direct, ParamIDs::directDeessPostFreq, "Post Freq");
    addKnob (direct, ParamIDs::directDeessPostThreshold, "Post Thresh");

    addMeter (direct, "Direct FET gain reduction meter", "FET");

    // --- Bus (1) CRUSH ----------------------------------------------------
    auto& crush = addPanel ("Crush Bus");
    crushPanel = &crush;
    addKnob (crush, ParamIDs::crushInput, "Input");
    addKnob (crush, ParamIDs::crushRatio, "Ratio");
    addKnob (crush, ParamIDs::crushStyle, "Style");
    addKnob (crush, ParamIDs::crushAttack, "Attack");
    addKnob (crush, ParamIDs::crushRelease, "Release");
    addKnob (crush, ParamIDs::crushOutput, "Output");
    addKnob (crush, ParamIDs::crushLevel, "Level");
    addToggle (crush, ParamIDs::crushMute, "Mute");
    addToggle (crush, ParamIDs::crushAudition, "Audition");

    addMeter (crush, "Crush gain reduction meter", "CRUSH");

    // --- Bus (2) SANDWICH -------------------------------------------------
    auto& sandwich = addPanel ("Sandwich Bus");
    sandwichPanel = &sandwich;
    addKnob (sandwich, ParamIDs::sandPreLfFreq, "Pre LF Freq");
    addKnob (sandwich, ParamIDs::sandPreLfBoost, "Pre LF Boost");
    addKnob (sandwich, ParamIDs::sandPreLfCut, "Pre LF Cut");
    addKnob (sandwich, ParamIDs::sandPreHfBellFreq, "Pre Bell Freq");
    addKnob (sandwich, ParamIDs::sandPreHfBellBoost, "Pre Bell Boost");
    addKnob (sandwich, ParamIDs::sandPreHfBellBandwidth, "Pre Bell BW");
    addKnob (sandwich, ParamIDs::sandPreHfShelfFreq, "Pre Shelf Freq");
    addKnob (sandwich, ParamIDs::sandPreHfShelfAtten, "Pre Shelf Cut");

    addRow (sandwich);
    addKnob (sandwich, ParamIDs::sandPeakRed, "Peak Reduction");
    addToggle (sandwich, ParamIDs::sandLimit, "Limit");
    addKnob (sandwich, ParamIDs::sandEmphasis, "Emphasis");
    addToggle (sandwich, ParamIDs::sandResidual, "Residual");

    addRow (sandwich);
    addKnob (sandwich, ParamIDs::sandPostLfFreq, "Post LF Freq");
    addKnob (sandwich, ParamIDs::sandPostLfBoost, "Post LF Boost");
    addKnob (sandwich, ParamIDs::sandPostLfCut, "Post LF Cut");
    addKnob (sandwich, ParamIDs::sandPostHfBellFreq, "Post Bell Freq");
    addKnob (sandwich, ParamIDs::sandPostHfBellBoost, "Post Bell Boost");
    addKnob (sandwich, ParamIDs::sandPostHfBellBandwidth, "Post Bell BW");
    addKnob (sandwich, ParamIDs::sandPostHfShelfFreq, "Post Shelf Freq");
    addKnob (sandwich, ParamIDs::sandPostHfShelfAtten, "Post Shelf Cut");
    addKnob (sandwich, ParamIDs::sandLevel, "Level");
    addToggle (sandwich, ParamIDs::sandMute, "Mute");
    addToggle (sandwich, ParamIDs::sandAudition, "Audition");

    addMeter (sandwich, "Sandwich gain reduction meter", "SANDWICH");

    // --- Bus (3) SPREAD ---------------------------------------------------
    auto& spread = addPanel ("Spread Bus");
    spreadPanel = &spread;
    addKnob (spread, ParamIDs::spreadDetune, "Detune");
    addKnob (spread, ParamIDs::spreadTime, "Time");
    addKnob (spread, ParamIDs::spreadWidth, "Width");
    addKnob (spread, ParamIDs::spreadLevel, "Level");
    addToggle (spread, ParamIDs::spreadMute, "Mute");
    addToggle (spread, ParamIDs::spreadAudition, "Audition");

    // --- Bus (4) SLAP -----------------------------------------------------
    auto& slap = addPanel ("Slap Bus");
    slapPanel = &slap;
    addKnob (slap, ParamIDs::slapTime, "Time");
    addToggle (slap, ParamIDs::slapStereo, "Stereo");
    addKnob (slap, ParamIDs::slapTone, "Tone");
    addKnob (slap, ParamIDs::slapWobble, "Wobble");
    addKnob (slap, ParamIDs::slapAge, "Age");
    addKnob (slap, ParamIDs::slapLevel, "Level");
    addToggle (slap, ParamIDs::slapMute, "Mute");
    addToggle (slap, ParamIDs::slapAudition, "Audition");

    // --- Size: computed from the control tables above ---------------------
    const auto contentWidth = std::max ({ panelRequiredWidth (global),
                                          panelRequiredWidth (direct),
                                          panelRequiredWidth (crush),
                                          panelRequiredWidth (sandwich),
                                          panelRequiredWidth (spread) + bandGap + panelRequiredWidth (slap) });

    const auto contentHeight = presetBarHeight + bandGap
                             + panelRequiredHeight (global) + bandGap
                             + panelRequiredHeight (direct) + bandGap
                             + panelRequiredHeight (crush) + bandGap
                             + panelRequiredHeight (sandwich) + bandGap
                             + std::max (panelRequiredHeight (spread), panelRequiredHeight (slap));

    setResizable (false, false);
    setSize (outerMargin * 2 + contentWidth, outerMargin * 2 + contentHeight);

    // GR meter polling: ~30 Hz GUI-thread timer feeding the ballistic
    // needles; the processor getters are relaxed-atomic loads, so this
    // never touches the audio thread.
    startTimerHz (30);
}

MiserereAudioProcessorEditor::~MiserereAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

MiserereAudioProcessorEditor::Panel& MiserereAudioProcessorEditor::addPanel (const juce::String& busTitle)
{
    auto panel = std::make_unique<Panel>();
    panel->component = std::make_unique<basilica::gui::BusPanel> (busTitle);
    panel->rows.emplace_back();

    addAndMakeVisible (*panel->component);

    panels.push_back (std::move (panel));
    return *panels.back();
}

void MiserereAudioProcessorEditor::addRow (Panel& panel)
{
    panel.rows.emplace_back();
}

MiserereAudioProcessorEditor::Knob& MiserereAudioProcessorEditor::addKnob (Panel& panel, const char* parameterId,
                                                                           const juce::String& labelText)
{
    auto knob = std::make_unique<Knob>();

    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobSlotWidth - slotGap, textBoxHeight);
    knob->slider.setTitle (labelText);
    knob->slider.setName (labelText);
    panel.component->addAndMakeVisible (knob->slider);

    knob->label.setText (labelText, juce::dontSendNotification);
    knob->label.setJustificationType (juce::Justification::centred);
    knob->label.attachToComponent (&knob->slider, false); // above; auto-repositions with the slider
    panel.component->addAndMakeVisible (knob->label);

    // SliderAttachment MUST be constructed before the textFromValueFunction
    // override below, not after: JUCE 8.0.14's SliderParameterAttachment
    // constructor (juce_ParameterAttachments.cpp:128) itself assigns
    // `slider.textFromValueFunction` as part of wiring the attachment -
    // setting our own function BEFORE this point would be silently
    // clobbered the moment the attachment is created.
    knob->attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, parameterId, knob->slider);

    if (auto* param = audioProcessor.apvts.getParameter (parameterId))
    {
        // A-02 pattern: unit-carrying parameters declare their unit via
        // .withLabel() in ParameterLayout.cpp (dB/ms/Hz/%/cents/x) - feed
        // it into both the value box and the accessibility value string.
        // Choice parameters have an empty label and getText() already
        // returns the choice NAME, so this is a no-op suffix for them.
        knob->slider.textFromValueFunction = [param] (double v)
        {
            const auto text = param->getText (param->convertTo0to1 ((float) v), 0);
            const auto unit = param->getLabel();
            return unit.isEmpty() ? text : text + " " + unit;
        };
        knob->slider.updateText();
    }

    panel.rows.back().push_back (&knob->slider);
    knobs.push_back (std::move (knob));
    return *knobs.back();
}

MiserereAudioProcessorEditor::Toggle& MiserereAudioProcessorEditor::addToggle (Panel& panel, const char* parameterId,
                                                                               const juce::String& labelText)
{
    auto toggle = std::make_unique<Toggle>();

    // Real juce::ToggleButton on purpose: focusable and Space/Enter-
    // operable by default, and its createAccessibilityHandler() reports
    // AccessibilityRole::toggleButton (JUCE 8.0.14 juce_ToggleButton.cpp:71)
    // so it lands in the VoiceOver rotor as a toggle, not a plain button.
    toggle->button.setButtonText (labelText);
    toggle->button.setTitle (labelText);
    toggle->button.setName (labelText);
    panel.component->addAndMakeVisible (toggle->button);

    toggle->attachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, parameterId, toggle->button);

    panel.rows.back().push_back (&toggle->button);
    toggles.push_back (std::move (toggle));
    return *toggles.back();
}

basilica::gui::NeedleMeter& MiserereAudioProcessorEditor::addMeter (Panel& panel, const juce::String& accessibleTitle,
                                                                    const juce::String& faceLegend)
{
    auto meter = std::make_unique<basilica::gui::NeedleMeter> (accessibleTitle, faceLegend);
    panel.component->addAndMakeVisible (*meter);
    panel.meter = meter.get();

    meters.push_back (std::move (meter));
    return *meters.back();
}

void MiserereAudioProcessorEditor::timerCallback()
{
    // Positive dB of gain reduction, straight from the engine's per-block
    // metering (relaxed atomic reads - see the DSP headers).
    if (directPanel != nullptr && directPanel->meter != nullptr)
        directPanel->meter->setTargetDb (audioProcessor.getDirectFetGainReductionDb());

    if (crushPanel != nullptr && crushPanel->meter != nullptr)
        crushPanel->meter->setTargetDb (audioProcessor.getCrushGainReductionDb());

    if (sandwichPanel != nullptr && sandwichPanel->meter != nullptr)
        sandwichPanel->meter->setTargetDb (audioProcessor.getSandGainReductionDb());

    constexpr float dtSeconds = 1.0f / 30.0f;

    for (auto& meter : meters)
        meter->tick (dtSeconds);
}

int MiserereAudioProcessorEditor::slotWidthFor (const juce::Component& control) noexcept
{
    return dynamic_cast<const juce::Slider*> (&control) != nullptr ? knobSlotWidth : toggleSlotWidth;
}

int MiserereAudioProcessorEditor::rowWidth (const std::vector<juce::Component*>& row) noexcept
{
    int width = 0;

    for (const auto* control : row)
        width += slotWidthFor (*control);

    return width;
}

int MiserereAudioProcessorEditor::panelRequiredWidth (const Panel& panel) const noexcept
{
    int widest = 0;

    for (const auto& row : panel.rows)
        widest = std::max (widest, rowWidth (row));

    return panelPadding * 2 + widest + (panel.meter != nullptr ? meterBayWidth : 0);
}

int MiserereAudioProcessorEditor::panelRequiredHeight (const Panel& panel) const noexcept
{
    const auto numRows = (int) panel.rows.size();
    return basilica::gui::BusPanel::headerHeight
         + numRows * rowHeight + (numRows - 1) * rowGap
         + panelBottomPadding;
}

void MiserereAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (basilica::gui::BasilicaLookAndFeel::getEditorBackgroundColour());
}

void MiserereAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (outerMargin);

    presetBar.setBounds (bounds.removeFromTop (presetBarHeight));
    bounds.removeFromTop (bandGap);

    const auto layoutPanel = [] (Panel& panel, juce::Rectangle<int> area)
    {
        panel.component->setBounds (area);

        auto content = panel.component->getLocalBounds().reduced (panelPadding, 0);
        content.removeFromTop (basilica::gui::BusPanel::headerHeight);

        if (panel.meter != nullptr)
        {
            auto bay = content.removeFromRight (meterBayWidth);
            panel.meter->setBounds (juce::Rectangle<int> (meterWidth,
                                                          juce::jmin (meterHeight, bay.getHeight()))
                                        .withCentre (bay.getCentre()));
        }

        for (auto& row : panel.rows)
        {
            auto rowArea = content.removeFromTop (rowHeight);
            rowArea.removeFromTop (labelHeight); // attached labels position themselves here

            for (auto* control : row)
            {
                auto slot = rowArea.removeFromLeft (slotWidthFor (*control)).withTrimmedRight (slotGap);

                if (dynamic_cast<juce::Slider*> (control) != nullptr)
                    control->setBounds (slot.withHeight (knobSize + textBoxHeight));
                else
                    control->setBounds (slot.withSizeKeepingCentre (slot.getWidth(), toggleHeight)
                                            .withY (rowArea.getY() + (knobSize - toggleHeight) / 2));
            }

            content.removeFromTop (rowGap);
        }
    };

    layoutPanel (*globalPanel, bounds.removeFromTop (panelRequiredHeight (*globalPanel)));
    bounds.removeFromTop (bandGap);
    layoutPanel (*directPanel, bounds.removeFromTop (panelRequiredHeight (*directPanel)));
    bounds.removeFromTop (bandGap);
    layoutPanel (*crushPanel, bounds.removeFromTop (panelRequiredHeight (*crushPanel)));
    bounds.removeFromTop (bandGap);
    layoutPanel (*sandwichPanel, bounds.removeFromTop (panelRequiredHeight (*sandwichPanel)));
    bounds.removeFromTop (bandGap);

    auto bottomBand = bounds.removeFromTop (std::max (panelRequiredHeight (*spreadPanel),
                                                      panelRequiredHeight (*slapPanel)));
    layoutPanel (*spreadPanel, bottomBand.removeFromLeft (panelRequiredWidth (*spreadPanel)));
    bottomBand.removeFromLeft (bandGap);
    layoutPanel (*slapPanel, bottomBand);
}
